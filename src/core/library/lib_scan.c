#include "lib_scan.h"

// --- the block -------------------------------------------------------------
// One reservation for the whole scan, bumped with an atomic add and committed
// 4 MB at a time. The workers therefore allocate paths and batches without a
// lock, and the pages the scan never reaches are never charged.

static void *lib_scan_block_push(LibScan *scan, u64 size, u64 align) {
    u64 padded = size + align - 1;
    u64 at = os_atomic_add_u64(&scan->block_pos, padded);
    u64 aligned = AlignPow2(at, align);
    if (aligned + size > scan->block_size) {
        os_atomic_store_u32(&scan->exhausted, 1);
        os_atomic_store_u32(&scan->cancel, 1);
        return 0;
    }
    if (aligned + size > os_atomic_load_u64(&scan->block_committed)) {
        os_mutex_lock(&scan->commit_mutex);
        u64 committed = scan->block_committed;
        if (aligned + size > committed) {
            u64 target = Min(AlignPow2(aligned + size, LIB_SCAN_COMMIT) + LIB_SCAN_COMMIT,
                             scan->block_size);
            os_memory_commit(scan->block + committed, target - committed);
            os_atomic_store_u64(&scan->block_committed, target);
        }
        os_mutex_unlock(&scan->commit_mutex);
    }
    return scan->block + aligned;
}

md_inline u32 lib_scan_offset_of(LibScan *scan, void *ptr) {
    u64 offset = (u64)((u8 *)ptr - scan->block);
    Assert(offset < scan->block_size);
    return (u32)offset;
}

// Paths are the one thing a job allocates per file: 80 bytes out of a bump
// pointer, no header, no free list.
static String8 lib_scan_push_path(LibScan *scan, String8 dir, String8 name) {
    u64 size = dir.size + 1 + name.size;
    u8 *bytes = (u8 *)lib_scan_block_push(scan, size, 1);
    if (!bytes) { return str8(0, 0); }
    mem_copy(bytes, dir.str, dir.size);
    bytes[dir.size] = '\\';
    mem_copy(bytes + dir.size + 1, name.str, name.size);
    return str8(bytes, size);
}

// --- the published stack ---------------------------------------------------
// Treiber stack of batch offsets. Offsets are never reused inside one scan, so
// a push can never see the ABA a pointer stack would: the CAS is the whole
// synchronisation, and the main thread takes the entire list in one swap.

static void lib_scan_publish(LibScan *scan, LibScanBatch *batch) {
    u32 offset = lib_scan_offset_of(scan, batch) + 1;
    for (;;) {
        u32 head = os_atomic_load_u32(&scan->published);
        batch->next = head;
        if (os_atomic_cas_u32(&scan->published, head, offset) == head) { return; }
    }
}

static LibScanBatch *lib_scan_take_published(LibScan *scan) {
    u32 head = os_atomic_load_u32(&scan->published);
    while (head != 0) {
        u32 found = os_atomic_cas_u32(&scan->published, head, 0);
        if (found == head) { break; }
        head = found;
    }
    return head ? (LibScanBatch *)(scan->block + (head - 1)) : 0;
}

// --- the directory job -----------------------------------------------------

typedef struct LibScanDirJob {
    LibScan *scan;
    String8 path;
} LibScanDirJob;

static void lib_scan_dir_job(void *data, u64 begin, u64 end);

static void lib_scan_push_dir(LibScan *scan, String8 path) {
    LibScanDirJob *job = (LibScanDirJob *)lib_scan_block_push(scan, sizeof(LibScanDirJob), 8);
    if (!job) { return; }
    job->scan = scan;
    job->path = path;
    os_atomic_inc_u32(&scan->dirs_total);
    jobs_push(&scan->counter, lib_scan_dir_job, job);
}

static LibScanBatch *lib_scan_batch_begin(LibScan *scan) {
    LibScanBatch *batch = (LibScanBatch *)lib_scan_block_push(scan, sizeof(LibScanBatch), 8);
    if (batch) {
        batch->next = 0;
        batch->count = 0;
    }
    return batch;
}

// One job, one directory: its files go into batches of 64, each of its
// subdirectories becomes a job of its own. A tree of 30 folders is 30 jobs on
// the ring; a tree of 3 000 is 3 000, and the ring runs the overflow inline.
static void lib_scan_dir_job(void *data, u64 begin, u64 end) {
    Unused(begin);
    Unused(end);
    LibScanDirJob *job = (LibScanDirJob *)data;
    LibScan *scan = job->scan;
    if (os_atomic_load_u32(&scan->cancel)) {
        os_atomic_inc_u32(&scan->dirs_done);
        return;
    }

    OsDirIter iter;
    LibScanBatch *batch = 0;
    if (os_dir_iter_begin(&iter, job->path)) {
        OsFileInfo info;
        while (os_dir_iter_next(&iter, &info)) {
            if (os_atomic_load_u32(&scan->cancel)) { break; }
            if (info.is_dir) {
                String8 path = lib_scan_push_path(scan, job->path, info.name);
                if (path.size) { lib_scan_push_dir(scan, path); }
                continue;
            }
            os_atomic_inc_u32(&scan->files_seen);
            if (lib_codec_from_extension(os_path_extension(info.name)) == LibCodec_Unknown) {
                continue;
            }
            if (!batch) {
                batch = lib_scan_batch_begin(scan);
                if (!batch) { break; }
            }
            LibScanEntry *entry = &batch->entries[batch->count];
            entry->path = lib_scan_push_path(scan, job->path, info.name);
            entry->size = info.size;
            entry->mtime_us = info.mtime_us;
            if (entry->path.size == 0) { break; }
            batch->count += 1;
            if (batch->count == LIB_SCAN_BATCH) {
                lib_scan_publish(scan, batch);
                batch = 0;
            }
        }
        os_dir_iter_end(&iter);
    }
    if (batch && batch->count) { lib_scan_publish(scan, batch); }
    os_atomic_inc_u32(&scan->dirs_done);
}

// --- the merge, on the main thread -----------------------------------------

static void lib_scan_merge(LibScan *scan, LibScanBatch *batch) {
    Library *lib = scan->library;
    u32 added = 0;
    while (batch) {
        for (u32 i = 0; i < batch->count; i += 1) {
            LibScanEntry *entry = &batch->entries[i];
            TrackId id = lib_find_by_path(lib, entry->path);
            if (id == LIB_TRACK_NONE) {
                id = lib_track_add(lib, entry->path, entry->size, entry->mtime_us);
                scan->added += 1;
                added += 1;
            } else if (lib->size[id] != entry->size || lib->mtime_us[id] != entry->mtime_us) {
                // (size, mtime) is the whole change detection: a file whose two
                // values match is not opened again (research/03 s9.4).
                lib->size[id] = entry->size;
                lib->mtime_us[id] = entry->mtime_us;
                lib->flags[id] |= LibTrackFlag_NeedTag;
                scan->updated += 1;
            } else {
                scan->unchanged += 1;
            }
            lib->stamp[id] = scan->generation;
        }
        batch = batch->next ? (LibScanBatch *)(scan->block + (batch->next - 1)) : 0;
    }
    if (added) {
        LibEvent event;
        StructZero(&event);
        event.kind = LibEvent_TracksAdded;
        event.count = added;
        lib_events_push(scan->events, event);
    }
}

// Everything under the root that this scan did not see is gone from the disk.
static void lib_scan_sweep(LibScan *scan) {
    Library *lib = scan->library;
    u32 removed = 0;
    for (TrackId id = 0; id < lib->count; id += 1) {
        if (!(lib->flags[id] & LibTrackFlag_Live)) { continue; }
        if (lib->stamp[id] == scan->generation) { continue; }
        String8 path = lib_track_path(lib, id);
        if (path.size <= scan->root.size) { continue; }
        if (!str8_starts_with(path, scan->root)) { continue; }
        if (!os_path_is_separator(path.str[scan->root.size])) { continue; }
        lib_track_remove(lib, id);
        removed += 1;
    }
    scan->removed = removed;
    if (removed) {
        LibEvent event;
        StructZero(&event);
        event.kind = LibEvent_TracksRemoved;
        event.count = removed;
        lib_events_push(scan->events, event);
    }
}

// --- the public face -------------------------------------------------------

b32 lib_scan_begin(LibScan *scan, Library *library, LibEventQueue *events, String8 root) {
    StructZero(scan);
    OsFileInfo info;
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 normalized = os_path_normalize(scratch.arena, root);
    b32 ok = os_file_stat(normalized, &info) && info.is_dir;
    if (ok) {
        scan->block = (u8 *)os_memory_reserve(LIB_SCAN_RESERVE);
        ok = scan->block != 0;
    }
    if (!ok) {
        scratch_end(scratch);
        return 0;
    }

    scan->block_size = LIB_SCAN_RESERVE;
    os_memory_commit(scan->block, LIB_SCAN_COMMIT);
    scan->block_committed = LIB_SCAN_COMMIT;
    os_mutex_init(&scan->commit_mutex);
    scan->library = library;
    scan->events = events;
    scan->state = LibScanState_Running;
    scan->start_us = os_time_now_us();

    library->generation += 1;
    scan->generation = library->generation;

    u8 *root_bytes = (u8 *)lib_scan_block_push(scan, normalized.size, 1);
    mem_copy(root_bytes, normalized.str, normalized.size);
    scan->root = str8(root_bytes, normalized.size);
    scratch_end(scratch);

    lib_scan_push_dir(scan, scan->root);
    return 1;
}

void lib_scan_cancel(LibScan *scan) { os_atomic_store_u32(&scan->cancel, 1); }

b32 lib_scan_update(LibScan *scan) {
    if (scan->state != LibScanState_Running) { return 0; }
    LibScanBatch *batch = lib_scan_take_published(scan);
    if (batch) { lib_scan_merge(scan, batch); }

    LibEvent event;
    StructZero(&event);
    event.kind = LibEvent_ScanProgress;
    event.files_seen = os_atomic_load_u32(&scan->files_seen);
    event.dirs_done = os_atomic_load_u32(&scan->dirs_done);
    event.dirs_total = os_atomic_load_u32(&scan->dirs_total);
    lib_events_push(scan->events, event);

    // The counter is the whole completion protocol: a job pushes its children
    // before it decrements itself, so zero means the tree is walked and every
    // batch is published - and the batch we just took cannot be one of them.
    if (os_atomic_load_u32(&scan->counter.pending) != 0) { return 1; }
    batch = lib_scan_take_published(scan);
    if (batch) { lib_scan_merge(scan, batch); }

    scan->cancelled = os_atomic_load_u32(&scan->cancel) != 0;
    if (!scan->cancelled) { lib_scan_sweep(scan); }
    scan->end_us = os_time_now_us();
    scan->state = LibScanState_Done;

    StructZero(&event);
    event.kind = LibEvent_ScanDone;
    event.files_seen = os_atomic_load_u32(&scan->files_seen);
    event.dirs_done = os_atomic_load_u32(&scan->dirs_done);
    event.dirs_total = os_atomic_load_u32(&scan->dirs_total);
    event.added = scan->added;
    event.updated = scan->updated;
    event.removed = scan->removed;
    event.cancelled = scan->cancelled;
    lib_events_push(scan->events, event);
    return 0;
}

void lib_scan_end(LibScan *scan) {
    Assert(scan->state != LibScanState_Running);
    if (scan->block) { os_memory_release(scan->block, scan->block_size); }
    scan->block = 0;
    scan->state = LibScanState_Idle;
}

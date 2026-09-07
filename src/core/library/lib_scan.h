// lib_scan.h - the recursive folder scan: one job per subfolder (T-008), the
// results merged into the library by the main thread through a lock free queue.
//
// The UI never waits for it: lib_scan_update is a poll that merges what the
// workers have published so far and returns at once, and lib_scan_cancel is a
// single store the jobs read at the top of every directory.
#ifndef LIB_SCAN_H
#define LIB_SCAN_H

#include "../../base/base.h"
#include "../../base/base_jobs.h"
#include "../../platform/platform.h"
#include "lib_events.h"
#include "lib_model.h"
#include "tags.h"

// Entries travel in batches: one atomic push per 64 files instead of one per
// file, and the main thread drains a whole directory's worth per pop.
#define LIB_SCAN_BATCH     64
#define LIB_SCAN_RESERVE   MB(256)  // virtual, committed 4 MB at a time
#define LIB_SCAN_COMMIT    MB(4)
// Tags are read in a second wave of jobs, over the tracks the merge found new
// or changed: a rescan that changed nothing reads no file at all (T-011).
#define LIB_TAG_BATCH      32

typedef enum LibScanState {
    LibScanState_Idle = 0,
    LibScanState_Running,
    LibScanState_Done,
} LibScanState;

typedef struct LibScanEntry {
    String8 path;  // in the scan's own block, alive until lib_scan_end
    u64 size;
    u64 mtime_us;
} LibScanEntry;

typedef struct LibScanBatch {
    u32 next;  // offset + 1 into the block, 0: end of the list
    u32 count;
    LibScanEntry entries[LIB_SCAN_BATCH];
} LibScanBatch;

// What the main thread hands a tag job: tracks already in the library, whose
// paths live in the scan's block and outlive the job.
typedef struct LibTagRequest {
    u32 next;
    u32 count;
    TrackId ids[LIB_TAG_BATCH];
    String8 paths[LIB_TAG_BATCH];
    u64 sizes[LIB_TAG_BATCH];
} LibTagRequest;

// What comes back. The strings are in the block: interning them into the
// library's table is the main thread's business, and it is the only thread
// that may touch it.
typedef struct LibTagResult {
    TrackId id;
    String8 title, artist, album, album_artist, genre;
    u64 cover_hash;
    u32 duration_ms;
    u32 sample_rate;
    u16 track_no, disc_no, year;
    i16 replaygain_track_db;
    u8 channels;
    u8 codec;
} LibTagResult;

typedef struct LibTagBatch {
    u32 next;   // offset + 1 into the block, 0: end of the list
    u32 count;
    LibTagResult items[LIB_TAG_BATCH];
} LibTagBatch;

typedef struct LibScan {
    // --- the block: a lock free bump allocator over reserved pages ---------
    u8 *block;
    u64 block_size;
    volatile u64 block_pos;
    volatile u64 block_committed;
    OsMutex commit_mutex;

    // --- shared with the workers ------------------------------------------
    volatile u32 published;   // Treiber stack head: batch offset + 1
    volatile u32 cancel;
    volatile u32 files_seen;
    volatile u32 dirs_total;
    volatile u32 dirs_done;
    volatile u32 exhausted;   // the block ran out: the scan stops, and says so
    volatile u32 tags_published;  // Treiber stack of LibTagBatch offsets
    volatile u32 tags_read;
    JobCounter counter;
    JobCounter tag_counter;

    // --- main thread only --------------------------------------------------
    LibScanState state;
    Library *library;
    LibEventQueue *events;
    String8 root;        // in the block, normalised
    u32 generation;
    u32 added, updated, unchanged, removed, tagged;
    LibTagRequest *tag_request;  // the batch being filled by the merge
    u64 start_us, end_us;
    b32 cancelled;
} LibScan;

// `root` is copied into the scan's block. Returns 0 when the root is not a
// readable directory - a domain error, the only one this module has.
b32  lib_scan_begin(LibScan *scan, Library *library, LibEventQueue *events, String8 root);
// Merges what the workers published; returns 1 while the scan is still running.
b32  lib_scan_update(LibScan *scan);
void lib_scan_cancel(LibScan *scan);
void lib_scan_end(LibScan *scan);  // releases the block, after the scan is done

// What a drop from the Explorer means (T-014): a folder is a library folder, a
// file is the folder that holds it. One function, because it is what the tests
// pin down and what the two drop paths (OLE and WM_DROPFILES) share.
String8 lib_drop_folder(String8 path, b32 is_dir);

md_inline b32 lib_scan_running(const LibScan *scan) {
    return scan->state == LibScanState_Running;
}

#endif // LIB_SCAN_H

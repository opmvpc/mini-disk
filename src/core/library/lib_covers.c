// lib_covers.c - see lib_covers.h. One job per cover: read the source bytes,
// decode them twice through the system decoder, write one cache file.
//
// The state table is the whole synchronisation: the main thread moves an entry
// from Missing to Queued and the worker moves it to Ready or Failed, so a plain
// atomic store on one word is all the sharing there is.
#include "lib_covers.h"

// --- the source ------------------------------------------------------------
static const char *lib_cover_names[] = {"cover", "folder", "front"};
static const char *lib_cover_extensions[] = {"jpg", "jpeg", "png", "bmp", "gif"};

md_inline b32 lib_cover_eq_fold(String8 a, const char *b) {
    u64 size = 0;
    while (b[size]) { size += 1; }
    if (a.size != size) { return 0; }
    for (u64 i = 0; i < size; i += 1) {
        u8 c = a.str[i];
        if (c >= 'A' && c <= 'Z') { c = (u8)(c + 32); }
        if (c != (u8)b[i]) { return 0; }
    }
    return 1;
}

u32 lib_cover_folder_rank(String8 file_name) {
    String8 extension = os_path_extension(file_name);
    if (extension.size == 0) { return 0; }
    b32 known = 0;
    for (u32 i = 0; i < ArrayCount(lib_cover_extensions); i += 1) {
        if (lib_cover_eq_fold(extension, lib_cover_extensions[i])) { known = 1; }
    }
    if (!known) { return 0; }
    String8 stem = str8_prefix(file_name, file_name.size - extension.size - 1);
    for (u32 i = 0; i < ArrayCount(lib_cover_names); i += 1) {
        if (lib_cover_eq_fold(stem, lib_cover_names[i])) { return i + 1; }
    }
    return 0;
}

String8 lib_cover_folder_image(Arena *arena, String8 folder) {
    String8 best = str8(0, 0);
    u32 best_rank = 0;
    OsDirIter it;
    if (!os_dir_iter_begin(&it, folder)) { return best; }
    OsFileInfo info;
    ArenaTemp scratch = scratch_begin(&arena, 1);
    String8 best_name = str8(0, 0);
    while (os_dir_iter_next(&it, &info)) {
        if (info.is_dir) { continue; }
        u32 rank = lib_cover_folder_rank(info.name);
        if (rank != 0 && (best_rank == 0 || rank < best_rank)) {
            best_rank = rank;
            best_name = str8_copy(scratch.arena, info.name);  // `name` dies at the next step
        }
    }
    os_dir_iter_end(&it);
    if (best_rank != 0) { best = os_path_join(arena, folder, best_name); }
    scratch_end(scratch);
    return best;
}

LibCoverSource lib_cover_source_of(const Tags *tags, b32 folder_image_exists) {
    if (tags->cover_size != 0) { return LibCoverSource_Embedded; }
    return folder_image_exists ? LibCoverSource_Folder : LibCoverSource_None;
}

// --- the state table -------------------------------------------------------
// Open addressing over a power of two, no deletion: a cover is decoded once for
// the life of the process, and 16 384 entries hold a library of 10 000 albums.
static LibCoverEntry *lib_covers_entry(LibCovers *covers, u64 key, b32 create) {
    u32 index = (u32)(hash64_mix(key) & (LIB_COVER_SLOTS - 1));
    for (u32 probe = 0; probe < LIB_COVER_SLOTS; probe += 1) {
        LibCoverEntry *entry = &covers->entries[index];
        if (entry->hash == key) { return entry; }
        if (entry->hash == 0) {
            if (!create || covers->count >= LIB_COVER_SLOTS / 2) { return 0; }
            entry->hash = key;
            entry->state = LibCoverState_Missing;
            covers->count += 1;
            return entry;
        }
        index = (index + 1) & (LIB_COVER_SLOTS - 1);
    }
    return 0;
}

void lib_covers_init(LibCovers *covers, Arena *arena, String8 cache_dir) {
    StructZero(covers);
    covers->arena = arena;
    covers->entries = push_array_zero(arena, LibCoverEntry, LIB_COVER_SLOTS);
    covers->jobs = push_array_zero(arena, LibCoverJob, LIB_COVER_INFLIGHT);
    covers->dir = os_path_join(arena, cache_dir, str8_lit("covers"));
    os_dir_create(covers->dir);
}

u32 lib_covers_state(LibCovers *covers, u64 key) {
    LibCoverEntry *entry = lib_covers_entry(covers, key, 0);
    return entry ? os_atomic_load_u32(&entry->state) : (u32)LibCoverState_Missing;
}

// <covers>\<key in hex>.raw: fixed length, so no allocation surprises and a
// folder anyone can look at.
static String8 lib_cover_path(Arena *arena, String8 dir, u64 key) {
    u8 name[24];
    static const char digits[] = "0123456789abcdef";
    for (u32 i = 0; i < 16; i += 1) { name[i] = (u8)digits[(key >> ((15 - i) * 4)) & 0xF]; }
    name[16] = '.';
    name[17] = 'r';
    name[18] = 'a';
    name[19] = 'w';
    return os_path_join(arena, dir, str8(name, 20));
}

// --- the job ---------------------------------------------------------------
static b32 lib_cover_write(String8 path, const OsImage *small, const OsImage *large,
                           Arena *arena) {
    u8 *bytes = push_array(arena, u8, LIB_COVER_FILE_BYTES);
    LibCoverHeader header;
    header.magic = LIB_COVER_MAGIC;
    header.version = LIB_COVER_VERSION;
    header.small = LIB_COVER_SMALL;
    header.large = LIB_COVER_LARGE;
    mem_copy(bytes, &header, sizeof(header));
    mem_copy(bytes + sizeof(header), small->pixels, LIB_COVER_SMALL_BYTES);
    mem_copy(bytes + sizeof(header) + LIB_COVER_SMALL_BYTES, large->pixels,
             LIB_COVER_LARGE_BYTES);

    // ".tmp then move": a reader either maps the whole file or none of it
    // (ADR-010 s9.3), which matters here because the reader is our own main
    // thread, one frame later.
    String8 temporary = str8f(arena, "%S.tmp", path);
    if (!os_file_write_all(temporary, str8(bytes, LIB_COVER_FILE_BYTES))) { return 0; }
    return os_file_move_replace(temporary, path);
}

static void lib_cover_job(void *data, u64 begin, u64 end) {
    Unused(begin);
    Unused(end);
    LibCoverJob *job = (LibCoverJob *)data;
    LibCovers *covers = job->covers;
    String8 path = str8(job->path, job->path_size);

    ArenaTemp scratch = scratch_begin(0, 0);
    u8 *head = push_array(scratch.arena, u8, TAGS_BLOCK_SIZE);
    u8 *tail = push_array(scratch.arena, u8, TAGS_BLOCK_SIZE);
    u8 *capture = push_array(scratch.arena, u8, LIB_COVER_MAX_BYTES);

    Tags tags;
    String8 bytes =
        tags_read_cover(&tags, path, job->track_size, head, tail, capture, LIB_COVER_MAX_BYTES);
    if (bytes.size == 0) {
        String8 image = lib_cover_folder_image(scratch.arena, os_path_parent(path));
        if (image.size != 0) { bytes = os_file_read_all(scratch.arena, image); }
    }

    b32 ok = 0;
    if (bytes.size != 0) {
        OsImage small, large;
        // Two scales of the same source, which is what the two sizes want: a
        // 48 px thumbnail downsampled from a 256 px one is visibly softer.
        if (os_image_decode(scratch.arena, bytes, LIB_COVER_LARGE, &large) &&
            os_image_decode(scratch.arena, bytes, LIB_COVER_SMALL, &small)) {
            ok = lib_cover_write(lib_cover_path(scratch.arena, covers->dir, job->hash), &small,
                                 &large, scratch.arena);
        }
    }
    scratch_end(scratch);

    os_atomic_store_u32(&job->entry->state, ok ? LibCoverState_Ready : LibCoverState_Failed);
    os_atomic_inc_u32(ok ? &covers->decoded : &covers->failed);
    os_atomic_dec_u32(&covers->in_flight);
    os_request_redraw();
}

void lib_covers_request(LibCovers *covers, u64 key, String8 track_path, u64 track_size) {
    if (key == 0 || track_path.size == 0 || track_path.size >= OS_PATH_MAX) { return; }
    LibCoverEntry *entry = lib_covers_entry(covers, key, 1);
    if (!entry || entry->state != LibCoverState_Missing) { return; }
    // The cache survives the process: a cover decoded last week is ready now.
    ArenaTemp scratch = scratch_begin(0, 0);
    OsFileInfo info;
    b32 cached = os_file_stat(lib_cover_path(scratch.arena, covers->dir, key), &info) &&
                 info.size == LIB_COVER_FILE_BYTES;
    scratch_end(scratch);
    if (cached) {
        entry->state = LibCoverState_Ready;
        return;
    }
    if (os_atomic_load_u32(&covers->in_flight) >= LIB_COVER_INFLIGHT) { return; }

    u32 slot = covers->job_next % LIB_COVER_INFLIGHT;
    covers->job_next += 1;
    LibCoverJob *job = &covers->jobs[slot];
    job->covers = covers;
    job->entry = entry;
    job->hash = key;
    job->track_size = track_size;
    job->path_size = (u32)track_path.size;
    mem_copy(job->path, track_path.str, track_path.size);

    entry->state = LibCoverState_Queued;
    os_atomic_inc_u32(&covers->in_flight);
    jobs_push(0, lib_cover_job, job);
}

b32 lib_covers_open(LibCovers *covers, u64 key, OsFileMap *out) {
    ArenaTemp scratch = scratch_begin(0, 0);
    b32 mapped = os_file_map(out, lib_cover_path(scratch.arena, covers->dir, key));
    scratch_end(scratch);
    if (!mapped) { return 0; }
    // The boundary: the file was written by us, but a full disk or a crash can
    // still leave a short one, and it is mapped memory we are about to read.
    LibCoverHeader header;
    mem_copy(&header, out->data, sizeof(header));
    if (out->size != LIB_COVER_FILE_BYTES || header.magic != LIB_COVER_MAGIC ||
        header.version != LIB_COVER_VERSION || header.small != LIB_COVER_SMALL ||
        header.large != LIB_COVER_LARGE) {
        os_file_unmap(out);
        return 0;
    }
    return 1;
}

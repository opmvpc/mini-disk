// pipeline_cache.c - see pipeline_cache.h.
#include "pipeline_cache.h"

// --- the key -------------------------------------------------------------------
// Every parameter is folded in as a fixed width word, in a fixed order. Floats
// go in by their bit pattern rather than by value: -0.0 and 0.0 render the same
// audio, but so does every other pair we would have to enumerate to be clever
// about it, and a bit pattern is one instruction.
md_inline u64 pipeline_cache_fold_u32(u64 hash, u32 value) {
    return hash64_combine(hash, (u64)value);
}

md_inline u64 pipeline_cache_fold_f32(u64 hash, f32 value) {
    union {
        f32 f;
        u32 u;
    } bits;
    bits.f = value;
    return pipeline_cache_fold_u32(hash, bits.u);
}

u64 pipeline_cache_params(const PipelineConfig *config) {
    u64 hash = HASH64_SEED;
    hash = pipeline_cache_fold_u32(hash, PIPELINE_CACHE_VERSION);
    hash = pipeline_cache_fold_u32(hash, (u32)config->mono);
    hash = pipeline_cache_fold_u32(hash, (u32)config->normalize);
    hash = pipeline_cache_fold_f32(hash, config->target_lufs);
    hash = pipeline_cache_fold_f32(hash, config->ceiling_dbtp);
    hash = pipeline_cache_fold_f32(hash, config->fixed_gain_db);
    hash = pipeline_cache_fold_u32(hash, (u32)config->trim);
    hash = pipeline_cache_fold_f32(hash, config->fade_in_s);
    hash = pipeline_cache_fold_f32(hash, config->fade_out_s);
    hash = pipeline_cache_fold_f32(hash, config->gap_s);
    hash = pipeline_cache_fold_u32(hash, (u32)config->dither);
    hash = pipeline_cache_fold_u32(hash, (u32)config->noise_shaping);
    hash = pipeline_cache_fold_u32(hash, (u32)config->format);
    return hash;
}

u64 pipeline_cache_key(String8 path, u64 size, u64 mtime_us, const PipelineConfig *config) {
    u64 hash = hash64_seed(path.str, path.size, HASH64_SEED);
    hash = hash64_combine(hash, size);
    hash = hash64_combine(hash, mtime_us);
    hash = hash64_combine(hash, pipeline_cache_params(config));
    // 0 is the "no key" value everywhere above this, so it is the one output
    // the hash may not have.
    return hash ? hash : 1ull;
}

u64 pipeline_cache_key_of(String8 path, const PipelineConfig *config) {
    OsFileInfo info;
    StructZero(&info);
    if (!os_file_stat(path, &info)) { return 0; }
    return pipeline_cache_key(path, info.size, info.mtime_us, config);
}

String8 pipeline_cache_path(const PipelineCache *cache, Arena *arena, u64 key) {
    return cache_lru_name(arena, cache->dir, key, str8_lit("pcm"));
}

// --- the index -------------------------------------------------------------------
// A flat array of CacheLruAccess, magic first. It carries no data that cannot
// be rebuilt, so a version that does not match is dropped rather than migrated.
#define PIPELINE_CACHE_INDEX_MAGIC 0x49504D44u  // 'DMPI'

typedef struct PipelineCacheIndexHeader {
    u32 magic;
    u32 version;
    u32 count;
    u32 reserved;
} PipelineCacheIndexHeader;

void pipeline_cache_index_load(PipelineCache *cache, Arena *scratch) {
    cache->access_count = 0;
    ArenaTemp temp = arena_temp_begin(scratch);
    String8 bytes = os_file_read_all(scratch, cache->index_path);
    if (bytes.size >= sizeof(PipelineCacheIndexHeader)) {
        PipelineCacheIndexHeader header;
        mem_copy(&header, bytes.str, sizeof(header));
        u64 payload = bytes.size - sizeof(header);
        if (header.magic == PIPELINE_CACHE_INDEX_MAGIC &&
            header.version == PIPELINE_CACHE_VERSION &&
            header.count <= PIPELINE_CACHE_ENTRIES &&
            payload >= (u64)header.count * sizeof(CacheLruAccess)) {
            mem_copy(cache->access, bytes.str + sizeof(header),
                     (u64)header.count * sizeof(CacheLruAccess));
            cache->access_count = header.count;
        }
    }
    arena_temp_end(temp);
}

b32 pipeline_cache_index_save(const PipelineCache *cache, Arena *scratch) {
    ArenaTemp temp = arena_temp_begin(scratch);
    u64 size = sizeof(PipelineCacheIndexHeader) +
               (u64)cache->access_count * sizeof(CacheLruAccess);
    u8 *bytes = push_array(scratch, u8, size);
    PipelineCacheIndexHeader header;
    StructZero(&header);
    header.magic = PIPELINE_CACHE_INDEX_MAGIC;
    header.version = PIPELINE_CACHE_VERSION;
    header.count = cache->access_count;
    mem_copy(bytes, &header, sizeof(header));
    mem_copy(bytes + sizeof(header), cache->access,
             (u64)cache->access_count * sizeof(CacheLruAccess));
    b32 ok = os_file_write_all(cache->index_path, str8(bytes, size));
    arena_temp_end(temp);
    return ok;
}

// The access record of `key`, created when there is room. Full means the oldest
// record is reused: losing an access time costs a purge ordering decision.
static void pipeline_cache_touch(PipelineCache *cache, u64 key) {
    u64 now = os_time_now_us();
    for (u32 i = 0; i < cache->access_count; i += 1) {
        if (cache->access[i].key == key) {
            cache->access[i].last_us = now;
            return;
        }
    }
    if (cache->access_count < PIPELINE_CACHE_ENTRIES) {
        cache->access[cache->access_count].key = key;
        cache->access[cache->access_count].last_us = now;
        cache->access_count += 1;
        return;
    }
    u32 oldest = 0;
    for (u32 i = 1; i < cache->access_count; i += 1) {
        if (cache->access[i].last_us < cache->access[oldest].last_us) { oldest = i; }
    }
    cache->access[oldest].key = key;
    cache->access[oldest].last_us = now;
}

// --- the cache -------------------------------------------------------------------

void pipeline_cache_init(PipelineCache *cache, Arena *arena, String8 cache_dir, u64 max_bytes) {
    StructZero(cache);
    cache->arena = arena;
    cache->max_bytes = max_bytes ? max_bytes : PIPELINE_CACHE_DEFAULT_BYTES;
    cache->dir = os_path_join(arena, cache_dir, str8_lit("transcode"));
    cache->index_path = os_path_join(arena, cache->dir, str8_lit("access.idx"));
    cache->ready = os_dir_create(cache->dir);
    if (cache->ready) { pipeline_cache_index_load(cache, arena); }
}

// The boundary of ADR-012: a header off the disk decides how many payload bytes
// the sender will read, so it is checked against the file's real size here, and
// trusted everywhere below.
static b32 pipeline_cache_header_ok(const PipelineCacheHeader *header, u64 key, u64 file_size) {
    return header->magic == PIPELINE_CACHE_MAGIC && header->version == PIPELINE_CACHE_VERSION &&
           header->key == key && header->bytes != 0 &&
           file_size == sizeof(PipelineCacheHeader) + header->bytes;
}

b32 pipeline_cache_lookup(PipelineCache *cache, u64 key, PipelineCacheHeader *out) {
    if (!cache->ready || key == 0) { return 0; }
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 path = pipeline_cache_path(cache, scratch.arena, key);
    OsFileInfo info;
    StructZero(&info);
    b32 found = 0;
    if (os_file_stat(path, &info) && info.size > sizeof(PipelineCacheHeader)) {
        OsFile file = os_file_open(path);
        if (file.v) {
            PipelineCacheHeader header;
            StructZero(&header);
            u64 got = os_file_read_at(file, 0, &header, sizeof(header));
            os_file_close(file);
            if (got == sizeof(header) && pipeline_cache_header_ok(&header, key, info.size)) {
                found = 1;
                if (out) { *out = header; }
            }
        }
    }
    scratch_end(scratch);
    if (found) {
        cache->hits += 1;
        pipeline_cache_touch(cache, key);
    } else {
        cache->misses += 1;
    }
    return found;
}

// --- writing ---------------------------------------------------------------------

b32 pipeline_cache_write_begin(PipelineCache *cache, u64 key, Arena *arena,
                               PipelineCacheWriter *writer) {
    // The buffer is 64 KB and the struct carries it: zeroing it here would be
    // 64 KB of stores per track for bytes that are all about to be overwritten.
    writer->cache = cache;
    writer->key = key;
    writer->file.v = 0;
    writer->bytes = 0;
    writer->failed = 0;
    writer->buffered = 0;
    writer->final_path = pipeline_cache_path(cache, arena, key);
    // The temporary carries the thread id: two burns of two windows would
    // otherwise render the same track into the same file at the same time.
    writer->temp_path = str8f(arena, "%S.%u.tmp", writer->final_path, os_thread_current_id());
    if (!cache->ready) { return 0; }
    writer->file = os_file_create(writer->temp_path);
    if (!writer->file.v) { return 0; }
    // The header is written again at the end with the real numbers; this only
    // reserves its room so the payload starts where the reader expects it.
    PipelineCacheHeader header;
    StructZero(&header);
    if (!os_file_write(writer->file, &header, sizeof(header))) {
        os_file_close(writer->file);
        writer->file.v = 0;
        return 0;
    }
    return 1;
}

static b32 pipeline_cache_flush(PipelineCacheWriter *writer) {
    if (writer->buffered == 0) { return 1; }
    b32 ok = os_file_write(writer->file, writer->buffer, writer->buffered);
    writer->buffered = 0;
    if (!ok) { writer->failed = 1; }
    return ok;
}

b32 pipeline_cache_write(void *user, const u8 *bytes, u64 size) {
    PipelineCacheWriter *writer = (PipelineCacheWriter *)user;
    if (writer->failed || !writer->file.v) { return 0; }
    u64 at = 0;
    while (at < size) {
        u64 room = PIPELINE_CACHE_BUFFER - writer->buffered;
        u64 take = Min(room, size - at);
        mem_copy(writer->buffer + writer->buffered, bytes + at, take);
        writer->buffered += (u32)take;
        at += take;
        if (writer->buffered == PIPELINE_CACHE_BUFFER && !pipeline_cache_flush(writer)) {
            return 0;
        }
    }
    writer->bytes += size;
    return 1;
}

b32 pipeline_cache_write_end(PipelineCacheWriter *writer, const PipelineResult *result, b32 ok) {
    if (!writer->file.v) { return 0; }
    b32 written = ok && !writer->failed && pipeline_cache_flush(writer) && writer->bytes != 0;
    if (written) {
        PipelineCacheHeader header;
        StructZero(&header);
        header.magic = PIPELINE_CACHE_MAGIC;
        header.version = PIPELINE_CACHE_VERSION;
        header.key = writer->key;
        header.bytes = writer->bytes;
        if (result) {
            header.frames = result->out_frames;
            header.sp_frames = result->sp_frames;
            header.gain_db = result->applied_gain_db;
            header.lufs = result->measured_lufs;
            header.dbtp = result->measured_dbtp;
        }
        // The header was left blank when the file was created; now that the
        // payload is on disk, its real numbers go over the placeholder. The
        // rename below is what publishes both at once.
        written = os_file_write_at(writer->file, 0, &header, sizeof(header));
    }
    os_file_close(writer->file);
    writer->file.v = 0;
    if (!written) {
        os_file_delete(writer->temp_path);
        return 0;
    }
    if (!os_file_move_replace(writer->temp_path, writer->final_path)) {
        os_file_delete(writer->temp_path);
        return 0;
    }
    writer->cache->writes += 1;
    pipeline_cache_touch(writer->cache, writer->key);
    return 1;
}

// --- reading ---------------------------------------------------------------------

b32 pipeline_cache_read_begin(PipelineCache *cache, u64 key, Arena *arena,
                              PipelineCacheReader *reader) {
    StructZero(reader);
    if (!cache->ready || key == 0) { return 0; }
    ArenaTemp temp = arena_temp_begin(arena);
    String8 path = pipeline_cache_path(cache, arena, key);
    OsFileInfo info;
    StructZero(&info);
    b32 ok = 0;
    if (os_file_stat(path, &info)) {
        reader->file = os_file_open(path);
        if (reader->file.v) {
            u64 got = os_file_read_at(reader->file, 0, &reader->header, sizeof(reader->header));
            ok = got == sizeof(reader->header) &&
                 pipeline_cache_header_ok(&reader->header, key, info.size);
            if (ok) {
                reader->size = reader->header.bytes;
            } else {
                os_file_close(reader->file);
                reader->file.v = 0;
            }
        }
    }
    arena_temp_end(temp);
    if (ok) { pipeline_cache_touch(cache, key); }
    return ok;
}

u64 pipeline_cache_read(void *user, u8 *dst, u64 size) {
    PipelineCacheReader *reader = (PipelineCacheReader *)user;
    u64 left = reader->size - reader->at;
    u64 take = Min(size, left);
    if (take == 0) { return 0; }
    u64 got = os_file_read_at(reader->file, sizeof(PipelineCacheHeader) + reader->at, dst, take);
    reader->at += got;
    return got;
}

void pipeline_cache_read_end(PipelineCacheReader *reader) {
    if (reader->file.v) { os_file_close(reader->file); }
    reader->file.v = 0;
}

// --- the bound ---------------------------------------------------------------------

void pipeline_cache_purge(PipelineCache *cache, Arena *scratch, CacheLruStats *out) {
    if (!cache->ready) { return; }
    cache_lru_purge(scratch, cache->dir, str8_lit("pcm"), cache->max_bytes, cache->access,
                    cache->access_count, out);
    pipeline_cache_index_save(cache, scratch);
}

void pipeline_cache_purge_covers(Arena *scratch, String8 covers_dir, u64 max_bytes,
                                 CacheLruStats *out) {
    cache_lru_purge(scratch, covers_dir, str8_lit("raw"), max_bytes, 0, 0, out);
}

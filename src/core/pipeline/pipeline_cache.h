// pipeline_cache.h - the transcode cache of ADR-011 D5.
//
// Rendering one track to SP costs a decode, a resample, two loudness passes and
// a dither: about a second of CPU for four minutes of audio, and 42 MB of PCM
// on the way. Burning the same plan onto a second disc pays all of it again for
// bytes that cannot have changed. So the rendered bytes are kept:
//
//   <cache_dir>\transcode\<key>.pcm
//
// where `key` is a 64 bit hash of the *source* (path, size, mtime) and of every
// pipeline parameter that can change a single output byte. A file whose source
// was edited, or whose gain target moved, hashes somewhere else and the old one
// simply ages out - there is no invalidation step, because there is nothing to
// invalidate.
//
// The file is written through a temporary renamed at the end (ADR-010 s9.3): a
// cancelled or crashed render leaves a .tmp behind and never a short .pcm that
// would be read back as a truncated track.
//
// Bounded by total size, not by age (2 GB by default, a preference). The purge
// is cache_lru's, shared with the cover cache, and the access times it works
// from are kept in one small index file beside the data.
//
// core/ only: base + platform.h (ADR-001). One PipelineCache is 100 KB of
// index; the app owns exactly one, on its permanent arena.
#ifndef PIPELINE_CACHE_H
#define PIPELINE_CACHE_H

#include "../cache/cache_lru.h"
#include "pipeline.h"

#define PIPELINE_CACHE_MAGIC   0x43504D44u  // 'DMPC' little endian: minidisk pipeline cache
#define PIPELINE_CACHE_VERSION 1u
#define PIPELINE_CACHE_ENTRIES 4096u          // access records kept in the index
#define PIPELINE_CACHE_BUFFER  KB(64)         // one WriteFile per this much
#define PIPELINE_CACHE_DEFAULT_BYTES GB(2)

// Written at the head of every cache file and checked on every read. This is a
// boundary (ADR-012): the bytes come off a disk anyone can edit, so every field
// that decides how the rest is read is validated here and nowhere else.
typedef struct PipelineCacheHeader {
    u32 magic;
    u32 version;
    u64 key;        // the whole key: source plus parameters
    u64 params;     // the parameter half alone, for the diagnostics
    u64 bytes;      // payload bytes that follow the header
    u64 frames;     // output frames, gap included
    u64 sp_frames;  // 2048 byte frames, SP only
    u32 format;     // PipelineFormat
    f32 gain_db;    // what the render actually applied, for the log
    f32 lufs;
    f32 dbtp;
} PipelineCacheHeader;

StaticAssert(sizeof(PipelineCacheHeader) == 64, pipeline_cache_header_is_64_bytes);

typedef struct PipelineCache {
    Arena *arena;
    String8 dir;
    String8 index_path;
    u64 max_bytes;
    b32 ready;
    // The access times cache_lru_purge works from. A flat array walked linearly:
    // it is touched once per track of a burn, and 4096 u64 pairs are 64 KB that
    // fit in L2 - a hash table here would only add a hash.
    u32 access_count;
    CacheLruAccess access[PIPELINE_CACHE_ENTRIES];
    // Counters the transfer view shows and the tests assert on.
    u32 hits;
    u32 misses;
    u32 writes;
} PipelineCache;

// `cache_dir` is app.cache_dir; the "transcode" subdirectory is created here.
// `max_bytes` 0 means PIPELINE_CACHE_DEFAULT_BYTES.
void pipeline_cache_init(PipelineCache *cache, Arena *arena, String8 cache_dir, u64 max_bytes);

// The parameter half of the key: everything in the config that can change one
// output byte. Fields are hashed one by one, never as a struct, because the
// padding of a struct is not a value and would make the key depend on it.
u64 pipeline_cache_params(const PipelineConfig *config);
// The whole key. `mtime_us` and `size` are the source file's, as os_file_stat
// reports them: a file rewritten with the same size still moves the key.
u64 pipeline_cache_key(String8 path, u64 size, u64 mtime_us, const PipelineConfig *config);
// The same, reading the source's size and mtime off the disk. 0 when the file
// is not there, which is also how the pre-flight finds a missing track.
u64 pipeline_cache_key_of(String8 path, const PipelineConfig *config);

String8 pipeline_cache_path(const PipelineCache *cache, Arena *arena, u64 key);

// Is the rendered file there, whole, and for this key? Fills `out` (may be 0)
// and marks the entry as used, which is what the purge orders by.
b32 pipeline_cache_lookup(PipelineCache *cache, u64 key, PipelineCacheHeader *out);

// --- writing ------------------------------------------------------------------
// The pipeline writes through this: pipeline_cache_write has the DspWriteFunc
// shape exactly, so a PipelineTask binds to it with no adapter.
typedef struct PipelineCacheWriter {
    PipelineCache *cache;
    u64 key;
    OsFile file;
    String8 temp_path;
    String8 final_path;
    u64 bytes;
    b32 failed;
    u32 buffered;
    u8 buffer[PIPELINE_CACHE_BUFFER];
} PipelineCacheWriter;

// `arena` holds the two paths for as long as the writer lives.
b32 pipeline_cache_write_begin(PipelineCache *cache, u64 key, Arena *arena,
                               PipelineCacheWriter *writer);
b32 pipeline_cache_write(void *user, const u8 *bytes, u64 size);  // DspWriteFunc
// `ok` 0 (a cancelled or failed render) deletes the temporary and keeps the
// cache exactly as it was. Otherwise the header is stamped and the file is
// renamed into place in one step.
b32 pipeline_cache_write_end(PipelineCacheWriter *writer, const PipelineResult *result, b32 ok);

// --- reading ------------------------------------------------------------------
// pipeline_cache_read has the NetmdAudioSource shape exactly, so the upload
// binds a cache file as the source of a track with no copy and no adapter.
typedef struct PipelineCacheReader {
    OsFile file;
    u64 at;     // payload bytes already handed out
    u64 size;   // payload bytes in total
    PipelineCacheHeader header;
} PipelineCacheReader;

b32 pipeline_cache_read_begin(PipelineCache *cache, u64 key, Arena *arena,
                              PipelineCacheReader *reader);
u64 pipeline_cache_read(void *user, u8 *dst, u64 size);
void pipeline_cache_read_end(PipelineCacheReader *reader);

// --- the bound ------------------------------------------------------------------
// Reads the index, purges what is over the bound, writes the index back. Called
// at startup and after every burn: never while one is running, because a file
// being read is a file the purge would rather not delete.
void pipeline_cache_purge(PipelineCache *cache, Arena *scratch, CacheLruStats *out);
// The same bound applied to the cover cache of T-014, which keeps no access
// times of its own and is dated by its files (see cache_lru.h).
void pipeline_cache_purge_covers(Arena *scratch, String8 covers_dir, u64 max_bytes,
                                 CacheLruStats *out);

// The index file, written next to the data. Both are best effort: a missing or
// unreadable index costs the access order of one purge and nothing else.
void pipeline_cache_index_load(PipelineCache *cache, Arena *scratch);
b32  pipeline_cache_index_save(const PipelineCache *cache, Arena *scratch);

#endif  // PIPELINE_CACHE_H

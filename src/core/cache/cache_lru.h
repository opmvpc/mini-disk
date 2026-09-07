// cache_lru.h - the one size-bounded purge every on-disk cache of this program
// uses (ADR-011 D5).
//
// Two caches keep files on disk that can always be recomputed: the cover cache
// of T-014 and the transcode cache of T-043. Neither of them has an upper bound
// of its own, and a library of 20 000 tracks burned twice would leave 40 GB of
// PCM behind. So both are bounded the same way, by one function, and the only
// thing that differs between them is where the access times come from.
//
// The victim order is "least recently used first". A cache that keeps its own
// access times hands them over; one that does not (the covers) falls back to
// the file's own mtime, which for a write-once cache is its creation time and
// is therefore the right answer for everything except a re-read.
//
// core/ only: base + platform.h, no allocation beyond the scratch arena the
// caller hands over (ADR-001).
#ifndef CACHE_LRU_H
#define CACHE_LRU_H

#include "../../base/base.h"
#include "../../base/base_arena.h"
#include "../../platform/platform.h"

// A purge walks the directory once and holds one record per file. Past this the
// directory is not a cache any more, it is a leak: the walk stops and the purge
// works on what it saw, which still frees space.
#define CACHE_LRU_MAX_FILES 8192

// What a cache that tracks its own reads hands over. `key` is the file's stem
// read as 16 hexadecimal digits, which is how both caches name their files.
typedef struct CacheLruAccess {
    u64 key;
    u64 last_us;
} CacheLruAccess;

typedef struct CacheLruStats {
    u32 files_before;
    u32 files_deleted;
    u64 bytes_before;
    u64 bytes_deleted;
    u64 bytes_after;
    b32 truncated;  // the directory held more than CACHE_LRU_MAX_FILES files
} CacheLruStats;

// The file name both caches use: 16 hexadecimal digits and an extension. Kept
// here so the purge can read a key back out of a name it did not write.
String8 cache_lru_name(Arena *arena, String8 dir, u64 key, String8 extension);
// The key a name stands for, 0 when the name is not one of ours.
u64 cache_lru_key_from_name(String8 name);

// Deletes least recently used files until the total is at or below `max_bytes`.
// `access` may be 0, in which case every file is dated by its mtime. Files whose
// extension does not match are counted neither in the total nor as victims:
// the index file lives in the same directory. `out` may be 0.
void cache_lru_purge(Arena *scratch, String8 dir, String8 extension, u64 max_bytes,
                     const CacheLruAccess *access, u32 access_count, CacheLruStats *out);

#endif  // CACHE_LRU_H

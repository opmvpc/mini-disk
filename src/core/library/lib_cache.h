// lib_cache.h - library.mdlib: the SoA on disk exactly as it is in memory, so
// loading it is a mapping and a handful of memcpy, not a parser (ADR-010 s9.3).
//
// This is a boundary (ADR-012): the loader validates the header, every offset,
// every size, every string id and the free list before anything downstream is
// allowed to trust a byte of it. Past lib_cache_load, the Library is canonical
// and nobody checks it again. The cache is reconstructible, so a version that
// does not match is not migrated: it is refused and the folder is rescanned.
#ifndef LIB_CACHE_H
#define LIB_CACHE_H

#include "../../base/base.h"
#include "../../platform/platform.h"
#include "lib_model.h"

#define LIB_CACHE_VERSION 1
#define LIB_CACHE_MAX_TRACKS ((u64)4 * 1024 * 1024)

typedef struct LibCacheHeader {
    u8 magic[8];  // "MDSKLIB\0"
    u32 version;
    u32 flags;
    u64 file_size;
    u64 track_count;  // slots, tombstones included: TrackId stays stable
    u64 live_count;
    u64 free_head;
    u64 generation;
    u64 soa_offset, soa_size;
    u64 strings_offset, strings_size;
    u64 string_count;
    u64 slots_offset, slot_count;
    u64 root_offset, root_size;
    u64 created_us;
    u64 reserved[7];
} LibCacheHeader;

StaticAssert(sizeof(LibCacheHeader) == 192, lib_cache_header_layout);

typedef enum LibCacheStatus {
    LibCache_Ok = 0,
    LibCache_Missing,     // no file, or it cannot be mapped
    LibCache_BadVersion,  // written by another build: rescan, do not migrate
    LibCache_Corrupt,     // the file exists and lies about itself
    LibCache_WriteFailed,
} LibCacheStatus;

// Writes to `path`.tmp, flushes, then moves over `path`: a reader ever only
// sees the whole of an old cache or the whole of a new one.
LibCacheStatus lib_cache_save(const Library *lib, String8 path, String8 root);

// Rebuilds `lib` from the file. Both of the library's arenas are cleared, so
// the caller must own them. `root_out` is the scanned folder the cache was
// written for, copied into `root_arena` (which may be one of the library's).
LibCacheStatus lib_cache_load(Library *lib, String8 path, Arena *root_arena, String8 *root_out);

#endif // LIB_CACHE_H

// lib_covers.h - cover art: where it comes from, how it is decoded, and the
// disk cache that means it is decoded once and never again (T-014, ADR-007).
//
// The rule of the pipeline is the rule of the scan: the UI never waits. A row
// that wants a thumbnail states it once per frame; the answer is a state, and
// the pixels arrive through a file the main thread maps. Nothing here talks to
// the atlas - core/ does not know one exists.
#ifndef LIB_COVERS_H
#define LIB_COVERS_H

#include "../../base/base.h"
#include "../../base/base_hash.h"
#include "../../base/base_jobs.h"
#include "../../platform/platform.h"
#include "lib_model.h"
#include "tags.h"

#define LIB_COVER_SMALL     48   // the library rows
#define LIB_COVER_LARGE     256  // the detail panel
#define LIB_COVER_MAGIC     0x5643444Du  // "MDCV"
#define LIB_COVER_VERSION   1
// An embedded picture past this is a booklet scan; the folder image takes over.
#define LIB_COVER_MAX_BYTES MB(4)
#define LIB_COVER_SLOTS     16384  // power of two, open addressed, no deletion
// Covers decoded at the same time. The view re-states what it wants every
// frame, so a request that does not fit is simply made again next frame: no
// queue to size, no backlog to drain, and a scroll always wins over a backlog.
#define LIB_COVER_INFLIGHT  64

typedef enum LibCoverSource {
    LibCoverSource_None = 0,
    LibCoverSource_Embedded,  // APIC / PICTURE / covr, whatever the container
    LibCoverSource_Folder,    // cover.jpg | folder.jpg | front.jpg next to it
} LibCoverSource;

typedef enum LibCoverState {
    LibCoverState_Missing = 0,  // never asked for, or waiting for a free slot
    LibCoverState_Queued,
    LibCoverState_Ready,        // the cache file is written and complete
    LibCoverState_Failed,       // no source, or nothing could decode it
} LibCoverState;

// The cache file: a header and the two thumbnails, RGBA8 premultiplied, so the
// main thread maps it and hands the rows straight to the atlas.
typedef struct LibCoverHeader {
    u32 magic;
    u32 version;
    u32 small;  // edge in pixels, both images are square
    u32 large;
} LibCoverHeader;

#define LIB_COVER_SMALL_BYTES ((u64)LIB_COVER_SMALL * LIB_COVER_SMALL * 4)
#define LIB_COVER_LARGE_BYTES ((u64)LIB_COVER_LARGE * LIB_COVER_LARGE * 4)
#define LIB_COVER_FILE_BYTES \
    (sizeof(LibCoverHeader) + LIB_COVER_SMALL_BYTES + LIB_COVER_LARGE_BYTES)

struct LibCoverEntry {
    u64 hash;
    volatile u32 state;
};

typedef struct LibCoverEntry LibCoverEntry;

typedef struct LibCoverJob {
    struct LibCovers *covers;
    LibCoverEntry *entry;  // handed over by the main thread: the worker never probes
    u64 hash;
    u64 track_size;
    u8 path[OS_PATH_MAX];
    u32 path_size;
} LibCoverJob;

typedef struct LibCovers {
    Arena *arena;
    String8 dir;  // <cache folder>\covers, created at init
    LibCoverEntry *entries;
    u32 count;
    volatile u32 in_flight;
    LibCoverJob *jobs;  // LIB_COVER_INFLIGHT slots, reused in order
    volatile u32 job_next;
    volatile u32 decoded;
    volatile u32 failed;
} LibCovers;

// `cache_dir` is where library.mdlib lives; the covers go in a subfolder of it.
void lib_covers_init(LibCovers *covers, Arena *arena, String8 cache_dir);

// The key a track's thumbnail is filed under: its embedded picture when it has
// one, its folder otherwise, so a whole album shares one decode.
md_inline u64 lib_cover_key(u64 cover_hash, String8 folder) {
    return cover_hash ? cover_hash : hash64(folder.str, folder.size);
}

u32  lib_covers_state(LibCovers *covers, u64 key);
// Idempotent: calling it every frame for every visible row is the design.
void lib_covers_request(LibCovers *covers, u64 key, String8 track_path, u64 track_size);
// Maps the cache file of a ready cover. The mapping is a boundary: a truncated
// or foreign file is refused here, and the caller only ever sees whole images.
b32  lib_covers_open(LibCovers *covers, u64 key, OsFileMap *out);
md_inline const u8 *lib_cover_pixels(const OsFileMap *map, u32 size) {
    return map->data + sizeof(LibCoverHeader) +
           ((size == LIB_COVER_LARGE) ? LIB_COVER_SMALL_BYTES : 0);
}

// --- the source, and the two halves that are worth testing on their own -----
// 0 when the name is not a cover image, else its rank: cover, then folder,
// then front. Case insensitive, and only the formats WIC decodes for us.
u32 lib_cover_folder_rank(String8 file_name);
// The best ranked image sitting in `folder`, empty when there is none.
String8 lib_cover_folder_image(Arena *arena, String8 folder);
// The priority the ticket states: embedded beats the folder.
LibCoverSource lib_cover_source_of(const Tags *tags, b32 folder_image_exists);

#endif // LIB_COVERS_H

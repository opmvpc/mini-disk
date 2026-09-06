// lib_model.h - the library index: parallel arrays (SoA), an interned string
// table, stable track ids and tombstones (ADR-010, research/03 s9.4).
//
// SoA because the dominant query is "filter 100 000 rows by substring while the
// user types": it walks two arrays, not 100 000 structs of 80 bytes. Nothing is
// ever allocated per track - the columns live in one block that doubles, and
// the strings in an arena of their own so they stay contiguous.
//
// This module knows base/ and platform.h only, never an OS header (ADR-001).
#ifndef LIB_MODEL_H
#define LIB_MODEL_H

#include "../../base/base.h"
#include "../../base/base_arena.h"
#include "../../base/base_hash.h"
#include "../../base/base_string.h"
#include "../../platform/platform.h"

// A slot index, stable for the life of the library: removing a track leaves a
// tombstone rather than shifting its neighbours, so a plan may hold ids across
// a rescan (ADR-011). LIB_TRACK_NONE is what the lookups return when they miss.
typedef u32 TrackId;
#define LIB_TRACK_NONE U32_MAX

// An offset into the string table. 0 is the empty string, always present.
typedef u32 StringId;
#define LIB_STRING_NONE 0

typedef enum LibCodec {
    LibCodec_Unknown = 0,
    LibCodec_MP3, LibCodec_FLAC, LibCodec_WAV, LibCodec_AIFF, LibCodec_OGG,
    LibCodec_M4A, LibCodec_AAC, LibCodec_ALAC, LibCodec_WMA, LibCodec_OPUS,
    LibCodec_COUNT
} LibCodec;

typedef enum LibTrackFlag {
    LibTrackFlag_Live    = 1u << 0,  // cleared: tombstone, the slot is reusable
    LibTrackFlag_NeedTag = 1u << 1,  // new or changed on disk, T-011 must read it
} LibTrackFlag;

#define LIB_REPLAYGAIN_NONE ((i16)-32768)  // absent, in 1/256 dB

// --- string table ----------------------------------------------------------
// Interned and deduplicated: 100 000 tracks share a few thousand artist and
// album names, and comparing two of them becomes an integer comparison.
// Layout: a u16 length prefix followed by the UTF-8 bytes, no terminator.
typedef struct StringTable {
    Arena *text;    // the bytes, and nothing else, so they stay contiguous
    u8 *base;       // first byte pushed in `text`: every StringId is relative to it
    Arena *arena;   // the hash slots
    u32 *slots;     // open addressing, power of two; 0 is an empty slot
    u32 slot_count;
    u32 count;
    u64 size;       // bytes used in `text`
} StringTable;

void     lib_strings_init(StringTable *table, Arena *arena, Arena *text);
StringId lib_intern(StringTable *table, String8 s);
String8  lib_string(const StringTable *table, StringId id);

// --- the library -----------------------------------------------------------
#define LIB_MIN_CAPACITY 1024

typedef struct Library {
    Arena *arena;   // columns, hash slots
    Arena *text;    // string bytes
    StringTable strings;

    u32 count;      // slots in use, tombstones included
    u32 capacity;
    u32 live_count;
    u32 free_head;  // head of the tombstone free list, LIB_TRACK_NONE when empty
    u32 generation; // bumped by every scan, stamped on every track it saw

    // --- SoA, `capacity` elements each, one block that doubles --------------
    u64 *size;
    u64 *mtime_us;
    u64 *cover_hash;
    u32 *path_id;   // on a tombstone: the next slot of the free list
    u32 *title_id;
    u32 *artist_id;
    u32 *album_id;
    u32 *album_artist_id;
    u32 *genre_id;
    u32 *duration_ms;
    u32 *sample_rate;
    u32 *flags;
    u32 *stamp;     // the generation of the scan that last saw this track
    u16 *track_no;
    u16 *disc_no;
    u16 *year;
    i16 *replaygain_track_db;  // 1/256 dB, LIB_REPLAYGAIN_NONE when absent
    u8 *channels;
    u8 *codec;

    // path hash -> id + 1, so a rescan finds a track without walking the SoA.
    u32 *index;
    u32 index_slots;
} Library;

#define LIB_BYTES_PER_TRACK (3 * 8 + 10 * 4 + 4 * 2 + 2 * 1)

void lib_init(Library *lib, Arena *arena, Arena *text);
void lib_clear(Library *lib);
// Grows the SoA so `capacity` slots fit without another move. The cache loader
// (T-012) is the only caller: everything else grows one track at a time.
void lib_reserve(Library *lib, u32 capacity);
// Rebuilds the path hash from the live slots. Also the cache loader's, which
// brings back the columns but not a hash table it would have to trust.
void lib_path_index_rebuild(Library *lib);

md_inline b32 lib_track_live(const Library *lib, TrackId id) {
    return id < lib->count && (lib->flags[id] & LibTrackFlag_Live) != 0;
}

// `path` is interned; the caller has already normalised it. Returns the slot,
// reusing a tombstone when there is one.
TrackId lib_track_add(Library *lib, String8 path, u64 size, u64 mtime_us);
void    lib_track_remove(Library *lib, TrackId id);
TrackId lib_find_by_path(const Library *lib, String8 path);
String8 lib_track_path(const Library *lib, TrackId id);

// Extension -> codec, used by the scan filter. LibCodec_Unknown: not audio.
LibCodec lib_codec_from_extension(String8 extension);

#endif // LIB_MODEL_H

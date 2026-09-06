// lib_index.h - sorted views over the library, the normalized text the search
// walks, and the Artist -> Album column browser (T-012, ADR-010).
//
// One pass over the SoA produces, per live track: a 64 bit mask of the letters
// it contains, one normalized blob "title\1artist\1album" the search scans, and
// three nul terminated sort keys (case folded, accents folded, leading article
// optionally dropped). Everything else is derived from those.
//
// The sort keys carry a u64 prefix - their first eight bytes, big endian - so a
// comparison is an integer compare and only ties touch the bytes.
#ifndef LIB_INDEX_H
#define LIB_INDEX_H

#include "../../base/base.h"
#include "../../base/base_arena.h"
#include "../../base/base_string.h"
#include "lib_model.h"

typedef enum LibSortColumn {
    LibSort_Title = 0,
    LibSort_Artist,
    LibSort_Album,
    LibSort_Duration,
    LibSort_Added,
    LibSort_COUNT
} LibSortColumn;

// The three string columns come first, so a column index doubles as the index
// of its sort key. The numeric ones sort on a plain u64 and have no key bytes.
#define LIB_SORT_STRING_COUNT 3

// A field longer than this is truncated: no title, artist or album that a
// human typed reaches it, and the search stays allocation free.
#define LIB_FIELD_MAX 256
#define LIB_SEPARATOR 0x01  // between two fields of the searchable blob

// What the sort actually moves: 16 bytes, the key first, so the whole compare
// is one aligned load and the merge walks memory forward.
typedef struct LibSortItem {
    u64 key;
    u32 id;
    u32 pad;
} LibSortItem;

typedef struct LibIndex {
    Arena *arena;
    const Library *library;
    b32 strip_articles;

    u32 count;        // library slots covered, tombstones included
    u32 live_count;   // entries in every order array

    // --- per track, indexed by TrackId --------------------------------------
    u64 *mask;
    u32 *text_off;    // into `text`: the searchable blob of this track
    u32 *text_len;
    u32 *key_off[LIB_SORT_STRING_COUNT];
    u64 *key_prefix[LIB_SORT_STRING_COUNT];

    u8 *text;
    u64 text_size;

    // --- sorted views, built on demand and cached ---------------------------
    u32 *order[LibSort_COUNT];
    b32 order_built[LibSort_COUNT];
    u32 *identity;    // the live ids in id order: what every sort starts from
    LibSortItem *items;  // the sort works on packed (key, id) pairs, in memory
    LibSortItem *merge;  // order, so a comparison never chases a second array
} LibIndex;

// Rebuilds everything from `lib`. `arena` is the index's own: it is cleared,
// so nothing else may live on it.
void lib_index_build(LibIndex *index, const Library *lib, Arena *arena, b32 strip_articles);
// The live track ids in the order of `column`, ascending. Built on first ask.
const u32 *lib_index_order(LibIndex *index, LibSortColumn column);

// --- normalization, the boundary the rest of the module trusts --------------
// Case folded, accents folded through a Latin-1 / Latin Extended-A table,
// everything else copied through. Returns the bytes written, never more than
// `capacity`. `strip_article` drops a leading "the/le/la/les/l'/der/die/das/el".
u64 lib_normalize(u8 *dst, u64 capacity, String8 text, b32 strip_article);
// The letters of a *normalized* string, one bit each: a-z, 0-9, then the rest
// folded onto the remaining 28 bits.
u64 lib_char_mask(String8 normalized);

// --- Artist -> Album column browser -----------------------------------------
#define LIB_GROUP_ALL U32_MAX

typedef struct LibGroup {
    StringId name;
    u32 count;
} LibGroup;

typedef struct LibBrowser {
    LibGroup *artists;
    u32 artist_count;
    LibGroup *albums;
    u32 album_count;
    u32 selected_artist;  // index into `artists`, LIB_GROUP_ALL: every artist
    u32 selected_album;   // index into `albums`, LIB_GROUP_ALL: every album
} LibBrowser;

// Sized once for `capacity` groups on `arena`; the arena is not the index's.
void lib_browser_init(LibBrowser *browser, Arena *arena, u32 capacity);
// Rebuilds the artist column from the artist order, then the album column of
// whichever artist is selected. Keeps the selection when the name survived.
void lib_browser_build(LibBrowser *browser, LibIndex *index);
void lib_browser_select_artist(LibBrowser *browser, LibIndex *index, u32 group);
void lib_browser_select_album(LibBrowser *browser, u32 group);
// The filter the search must apply, LIB_STRING_NONE meaning "no constraint" is
// not usable (0 is the empty string), so U32_MAX is the "any" value.
#define LIB_FILTER_ANY U32_MAX
StringId lib_browser_artist_filter(const LibBrowser *browser);
StringId lib_browser_album_filter(const LibBrowser *browser);

#endif // LIB_INDEX_H

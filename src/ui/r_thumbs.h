// r_thumbs.h - the second atlas: RGBA8 cover thumbnails, 2048 x 2048, LRU.
//
// Nothing here shares the R8 page of r_atlas.c: glyphs are coverage masks and
// covers are colour, and a thumbnail is evicted while a glyph never is. The
// page is a grid of fixed cells - 48 px rows, 256 px detail - because eviction
// with a skyline packer means repacking, and a grid means overwriting one cell.
#ifndef R_THUMBS_H
#define R_THUMBS_H

#include "../base/base.h"
#include "../base/base_arena.h"
#include "r_atlas.h"

#define R_THUMBS_SIZE       2048
#define R_THUMB_SMALL       48   // the library rows
#define R_THUMB_LARGE       256  // the detail panel
#define R_THUMB_PAD         1    // transparent border: linear filtering never bleeds

// The large cells sit in the first band, the small ones fill the rest.
#define R_THUMB_LARGE_CELL  (R_THUMB_LARGE + 2 * R_THUMB_PAD)
#define R_THUMB_SMALL_CELL  (R_THUMB_SMALL + 2 * R_THUMB_PAD)
#define R_THUMB_LARGE_COLS  (R_THUMBS_SIZE / R_THUMB_LARGE_CELL)
#define R_THUMB_LARGE_COUNT R_THUMB_LARGE_COLS
#define R_THUMB_SMALL_TOP   R_THUMB_LARGE_CELL
#define R_THUMB_SMALL_COLS  (R_THUMBS_SIZE / R_THUMB_SMALL_CELL)
#define R_THUMB_SMALL_ROWS  ((R_THUMBS_SIZE - R_THUMB_SMALL_TOP) / R_THUMB_SMALL_CELL)
#define R_THUMB_SMALL_COUNT (R_THUMB_SMALL_COLS * R_THUMB_SMALL_ROWS)
#define R_THUMB_SLOT_COUNT  (R_THUMB_LARGE_COUNT + R_THUMB_SMALL_COUNT)

void r_thumbs_init(Arena *arena);
void r_thumbs_reset(void);  // DPI change or a new library: the page starts over

// `key` is the cover hash. Both calls touch the LRU, so the entry a frame drew
// is the last one the next eviction will take.
b32 r_thumbs_lookup(u64 key, u32 size, R_AtlasRect *out);
// `pixels` is `size` x `size` RGBA8 premultiplied. Evicts the least recently
// used cell of that size when the band is full.
R_AtlasRect r_thumbs_add(u64 key, u32 size, const u8 *pixels);
void r_thumbs_flush(void);  // one upload of the dirty region, once per frame

u32 r_thumbs_texture(void);
u32 r_thumbs_count(u32 size);      // live entries of that size
u32 r_thumbs_capacity(u32 size);
u64 r_thumbs_evictions(void);

#endif // R_THUMBS_H

// r_atlas.h - the single R8 atlas holding glyph and icon coverage masks.
// Skyline bottom-left packing, deferred upload of the dirty region only.
#ifndef R_ATLAS_H
#define R_ATLAS_H

#include "../base/base.h"
#include "../base/base_math.h"
#include "../base/base_arena.h"

#define R_ATLAS_SIZE_MIN 2048
#define R_ATLAS_SIZE_MAX 8192
#define R_ATLAS_MAX_NODES 4096

// Placement of one entry. `size == 0` means the atlas is full at its maximum
// size: the caller draws nothing rather than drawing garbage.
typedef struct R_AtlasRect {
    u16 x, y, width, height;
    V2 uv0, uv1;  // normalized, ready for R_RectParams
} R_AtlasRect;

// The arena must outlive the atlas: the pixel buffer lives in it and a growth
// pushes a new, bigger one (rare, twice at most).
void r_atlas_init(Arena *arena);
void r_atlas_reset(void);  // DPI change: everything is rasterized again

R_AtlasRect r_atlas_add(u32 width, u32 height, const u8 *pixels);
void        r_atlas_flush(void);  // uploads the dirty region, once per frame

u32 r_atlas_texture(void);
u32 r_atlas_size(void);
u32 r_atlas_used_area(void);  // sum of the entry areas, for the fill ratio test

#endif // R_ATLAS_H

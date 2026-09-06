// r_raster.h - CPU polygon rasterizer with exact area coverage.
// Antialiasing here is not a filter: the value written for a pixel *is* the
// signed area of the polygon inside that pixel, so a rectangle of area 0.5625
// rasterizes to exactly 0.5625 (rounded to 8 bits). Same technique as font-rs.
#ifndef R_RASTER_H
#define R_RASTER_H

#include "../base/base.h"
#include "../base/base_math.h"
#include "../base/base_arena.h"

// Fills `coverage` (width * height, R8) with the polygon made of `contour_count`
// closed contours; contour i owns `contour_sizes[i]` consecutive points.
// Winding is non zero in absolute value: a contour wound the other way punches
// a hole. Points must lie inside [0, width] x [0, height].
void r_raster_fill(Arena *scratch, u8 *coverage, u32 width, u32 height, const V2 *points,
                   const u32 *contour_sizes, u32 contour_count);

#endif // R_RASTER_H

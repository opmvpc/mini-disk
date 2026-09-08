// r_icons.c - eight icons built from polygons in a unit square, rasterized by
// r_raster and packed in the atlas. Strokes are emitted as one quad per segment
// plus a square at each joint; the quads always come out with the same winding,
// so the non zero rule turns overlapping pieces into a clean union.
#include "r_icons.h"

#include "r_raster.h"

#define R_ICON_MAX_POINTS   192
#define R_ICON_MAX_CONTOURS 24

typedef struct R_IconShape {
    V2 points[R_ICON_MAX_POINTS];
    u32 contours[R_ICON_MAX_CONTOURS];
    u32 point_count;
    u32 contour_count;
} R_IconShape;

global R_AtlasRect r_icon_rects[R_Icon_COUNT];

R_AtlasRect r_icon_rect(R_Icon icon) {
    Assert(icon < R_Icon_COUNT);
    return r_icon_rects[icon];
}

static void icon_contour_begin(R_IconShape *shape) {
    Assert(shape->contour_count < R_ICON_MAX_CONTOURS);
    shape->contours[shape->contour_count] = 0;
    shape->contour_count += 1;
}

static void icon_point(R_IconShape *shape, f32 x, f32 y) {
    Assert(shape->point_count < R_ICON_MAX_POINTS);
    shape->points[shape->point_count] = v2(x, y);
    shape->point_count += 1;
    shape->contours[shape->contour_count - 1] += 1;
}

// cos and sin of 2*pi/64: the whole circle is 64 applications of one rotation,
// which is how we draw arcs without a trig function anywhere in base/.
#define R_ICON_ARC_STEPS 64
#define R_ICON_ARC_COS   0.99518472667219693f
#define R_ICON_ARC_SIN   0.09801714032956060f

static void icon_circle(R_IconShape *shape, V2 center, f32 radius, b32 reversed) {
    icon_contour_begin(shape);
    f32 cx = radius;
    f32 cy = 0.0f;
    f32 sin_step = reversed ? -R_ICON_ARC_SIN : R_ICON_ARC_SIN;
    for (u32 i = 0; i < R_ICON_ARC_STEPS; i += 1) {
        icon_point(shape, center.x + cx, center.y + cy);
        f32 nx = cx * R_ICON_ARC_COS - cy * sin_step;
        f32 ny = cx * sin_step + cy * R_ICON_ARC_COS;
        cx = nx;
        cy = ny;
    }
}

static void icon_segment(R_IconShape *shape, V2 a, V2 b, f32 half) {
    V2 d = v2_sub(b, a);
    f32 length = v2_length(d);
    Assert(length > 0.0f);
    V2 n = v2(-d.y / length * half, d.x / length * half);
    icon_contour_begin(shape);
    icon_point(shape, a.x + n.x, a.y + n.y);
    icon_point(shape, b.x + n.x, b.y + n.y);
    icon_point(shape, b.x - n.x, b.y - n.y);
    icon_point(shape, a.x - n.x, a.y - n.y);
}

static void icon_joint(R_IconShape *shape, V2 p, f32 half) {
    icon_contour_begin(shape);
    icon_point(shape, p.x - half, p.y - half);
    icon_point(shape, p.x - half, p.y + half);
    icon_point(shape, p.x + half, p.y + half);
    icon_point(shape, p.x + half, p.y - half);
}

static void icon_polyline(R_IconShape *shape, const V2 *points, u32 count, f32 half) {
    for (u32 i = 0; i + 1 < count; i += 1) { icon_segment(shape, points[i], points[i + 1], half); }
    for (u32 i = 0; i < count; i += 1) { icon_joint(shape, points[i], half); }
}

// Every icon is described in a unit square, then scaled: one description works
// at any DPI.
static void icon_build_shape(R_IconShape *shape, R_Icon icon) {
    StructZero(shape);
    f32 stroke = 0.11f;
    switch (icon) {
        case R_Icon_Circle: {
            icon_circle(shape, v2(0.5f, 0.5f), 0.44f, 0);
        } break;
        case R_Icon_Play: {
            icon_contour_begin(shape);
            icon_point(shape, 0.28f, 0.16f);
            icon_point(shape, 0.84f, 0.50f);
            icon_point(shape, 0.28f, 0.84f);
        } break;
        case R_Icon_Cross: {
            V2 a[2] = {v2(0.22f, 0.22f), v2(0.78f, 0.78f)};
            V2 b[2] = {v2(0.78f, 0.22f), v2(0.22f, 0.78f)};
            icon_polyline(shape, a, 2, stroke * 0.5f);
            icon_polyline(shape, b, 2, stroke * 0.5f);
        } break;
        case R_Icon_Check: {
            V2 p[3] = {v2(0.18f, 0.52f), v2(0.42f, 0.76f), v2(0.84f, 0.24f)};
            icon_polyline(shape, p, 3, stroke * 0.5f);
        } break;
        case R_Icon_ChevronLeft: {
            V2 p[3] = {v2(0.64f, 0.18f), v2(0.34f, 0.50f), v2(0.64f, 0.82f)};
            icon_polyline(shape, p, 3, stroke * 0.5f);
        } break;
        case R_Icon_ChevronRight: {
            V2 p[3] = {v2(0.36f, 0.18f), v2(0.66f, 0.50f), v2(0.36f, 0.82f)};
            icon_polyline(shape, p, 3, stroke * 0.5f);
        } break;
        case R_Icon_ChevronDown: {
            V2 p[3] = {v2(0.18f, 0.36f), v2(0.50f, 0.66f), v2(0.82f, 0.36f)};
            icon_polyline(shape, p, 3, stroke * 0.5f);
        } break;
        case R_Icon_Disc: {
            // The hole is wound backwards: non zero winding cancels it out.
            icon_circle(shape, v2(0.5f, 0.5f), 0.46f, 0);
            icon_circle(shape, v2(0.5f, 0.5f), 0.15f, 1);
        } break;
        case R_Icon_Pause: {
            icon_contour_begin(shape);
            icon_point(shape, 0.28f, 0.18f);
            icon_point(shape, 0.28f, 0.82f);
            icon_point(shape, 0.44f, 0.82f);
            icon_point(shape, 0.44f, 0.18f);
            icon_contour_begin(shape);
            icon_point(shape, 0.56f, 0.18f);
            icon_point(shape, 0.56f, 0.82f);
            icon_point(shape, 0.72f, 0.82f);
            icon_point(shape, 0.72f, 0.18f);
        } break;
        case R_Icon_Stop: {
            icon_contour_begin(shape);
            icon_point(shape, 0.24f, 0.24f);
            icon_point(shape, 0.24f, 0.76f);
            icon_point(shape, 0.76f, 0.76f);
            icon_point(shape, 0.76f, 0.24f);
        } break;
        case R_Icon_Prev: {
            // A triangle pointing left with the bar it stops against.
            icon_contour_begin(shape);
            icon_point(shape, 0.80f, 0.18f);
            icon_point(shape, 0.80f, 0.82f);
            icon_point(shape, 0.34f, 0.50f);
            icon_contour_begin(shape);
            icon_point(shape, 0.20f, 0.18f);
            icon_point(shape, 0.20f, 0.82f);
            icon_point(shape, 0.31f, 0.82f);
            icon_point(shape, 0.31f, 0.18f);
        } break;
        case R_Icon_Next: {
            icon_contour_begin(shape);
            icon_point(shape, 0.20f, 0.18f);
            icon_point(shape, 0.66f, 0.50f);
            icon_point(shape, 0.20f, 0.82f);
            icon_contour_begin(shape);
            icon_point(shape, 0.69f, 0.18f);
            icon_point(shape, 0.69f, 0.82f);
            icon_point(shape, 0.80f, 0.82f);
            icon_point(shape, 0.80f, 0.18f);
        } break;
        default: Assert(0); break;
    }
}

// --- the hatch (research/02 s9.3, T-071) ------------------------------------
// A 45 degree stripe every eight pixels. The shape is (x + y) mod 8, which is
// why this one is not a polygon: r_raster would need a contour per stripe and
// the pattern would still have to tile exactly, whereas the modulo tiles by
// construction. Coverage is a 4 x 4 box sample, which is exact for an edge at
// 45 degrees to within a sixteenth and costs 65 536 comparisons, once.
#define R_HATCH_STRIPE_PX  3  // of the eight, how many carry ink
#define R_HATCH_SUBSAMPLES 4

global R_AtlasRect r_hatch_atlas_rect;

R_AtlasRect r_hatch_rect(void) { return r_hatch_atlas_rect; }

static void r_hatch_build(Arena *scratch) {
    ArenaTemp temp = arena_temp_begin(scratch);
    u32 size = R_HATCH_TILE_PX;
    u8 *coverage = push_array(scratch, u8, (u64)size * size);
    for (u32 y = 0; y < size; y += 1) {
        for (u32 x = 0; x < size; x += 1) {
            // All of it in quarter pixels, so there is not a float in sight:
            // the sample sits in the middle of its sub-cell (two halves of an
            // eighth make the extra quarter), the period is 32 quarters and the
            // stripe is the first 12 of them.
            u32 hits = 0;
            for (u32 sy = 0; sy < R_HATCH_SUBSAMPLES; sy += 1) {
                for (u32 sx = 0; sx < R_HATCH_SUBSAMPLES; sx += 1) {
                    u32 d = (4u * x + sx) + (4u * y + sy) + 1u;
                    if ((d & 31u) < 12u) { hits += 1; }
                }
            }
            u32 total = R_HATCH_SUBSAMPLES * R_HATCH_SUBSAMPLES;
            coverage[(u64)y * size + x] = (u8)((hits * 255u + total / 2u) / total);
        }
    }
    r_hatch_atlas_rect = r_atlas_add(size, size, coverage);
    arena_temp_end(temp);
}

u32 r_hatch_tiles(Rect area, V2 origin, R_HatchTile *out, u32 max) {
    f32 tile = (f32)R_HATCH_TILE_PX;
    f32 width = area.max.x - area.min.x;
    f32 height = area.max.y - area.min.y;
    if (width <= 0.0f || height <= 0.0f || max == 0) { return 0; }
    // The grid the area is cut on is the origin's, not the area's: that is the
    // whole point. An area that moves over a fixed origin shows a different
    // part of the same pattern instead of restarting it.
    f32 first_x = origin.x + floor_f32((area.min.x - origin.x) / tile) * tile;
    f32 first_y = origin.y + floor_f32((area.min.y - origin.y) / tile) * tile;
    u32 count = 0;
    for (f32 ty = first_y; ty < area.max.y && count < max; ty += tile) {
        f32 y0 = max_f32(ty, area.min.y);
        f32 y1 = min_f32(ty + tile, area.max.y);
        for (f32 tx = first_x; tx < area.max.x && count < max; tx += tile) {
            f32 x0 = max_f32(tx, area.min.x);
            f32 x1 = min_f32(tx + tile, area.max.x);
            R_HatchTile *entry = &out[count];
            entry->dst = rect(x0, y0, x1, y1);
            entry->uv0 = v2((x0 - tx) / tile, (y0 - ty) / tile);
            entry->uv1 = v2((x1 - tx) / tile, (y1 - ty) / tile);
            count += 1;
        }
    }
    return count;
}

void r_icons_build(Arena *scratch, u32 size) {
    ArenaTemp temp = arena_temp_begin(scratch);
    u8 *coverage = push_array(scratch, u8, (u64)size * size);
    R_IconShape *shape = push_struct(scratch, R_IconShape);

    for (u32 i = 0; i < R_Icon_COUNT; i += 1) {
        icon_build_shape(shape, (R_Icon)i);
        for (u32 p = 0; p < shape->point_count; p += 1) {
            shape->points[p] = v2_scale(shape->points[p], (f32)size);
        }
        mem_zero(coverage, (u64)size * size);
        r_raster_fill(scratch, coverage, size, size, shape->points, shape->contours,
                      shape->contour_count);
        r_icon_rects[i] = r_atlas_add(size, size, coverage);
    }
    arena_temp_end(temp);
    // After the icons, so a DPI change rebuilds it too: the atlas was reset and
    // the tile went with it.
    r_hatch_build(scratch);
}

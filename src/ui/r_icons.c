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
        default: Assert(0); break;
    }
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
}

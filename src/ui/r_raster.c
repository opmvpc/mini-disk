// r_raster.c - signed area accumulation, one pass, no sorting, no active edge
// list. Each edge deposits into a f32 grid the *derivative* along x of the area
// it covers; a prefix sum over the whole grid then turns that back into the
// area itself. Cost is O(edge length), and the result is exact.
#include "r_raster.h"

// Deposits one edge. `accum` has width + 2 columns per row so the rightmost
// pixel can spill into a guard column instead of into the next row.
static void r_raster_edge(f32 *accum, u32 pitch, u32 height, V2 p0, V2 p1) {
    if (p0.y == p1.y) { return; }  // horizontal edges contribute no area
    f32 dir = 1.0f;
    if (p0.y > p1.y) {
        dir = -1.0f;
        V2 swap = p0;
        p0 = p1;
        p1 = swap;
    }
    f32 dxdy = (p1.x - p0.x) / (p1.y - p0.y);
    f32 x = p0.x;
    if (p0.y < 0.0f) { x -= p0.y * dxdy; }

    u32 y_first = (u32)max_f32(p0.y, 0.0f);
    u32 y_last = (u32)min_f32(ceil_f32(p1.y), (f32)height);
    for (u32 y = y_first; y < y_last; y += 1) {
        f32 *row = accum + (u64)y * pitch;
        // Vertical extent of the edge inside this scanline, and the x it spans.
        f32 dy = min_f32((f32)(y + 1), p1.y) - max_f32((f32)y, p0.y);
        f32 x_next = x + dxdy * dy;
        f32 d = dy * dir;
        f32 x0 = min_f32(x, x_next);
        f32 x1 = max_f32(x, x_next);
        f32 x0_floor = floor_f32(x0);
        f32 x1_ceil = ceil_f32(x1);
        u32 i0 = (u32)x0_floor;
        u32 i1 = (u32)x1_ceil;

        if (i1 <= i0 + 1) {
            // The edge stays inside one column: split d by the mean x.
            f32 xmf = 0.5f * (x + x_next) - x0_floor;
            row[i0] += d - d * xmf;
            row[i0 + 1] += d * xmf;
        } else {
            // The edge crosses several columns: the first and last get a
            // triangle, the ones in between get an equal slice.
            f32 s = 1.0f / (x1 - x0);
            f32 x0f = x0 - x0_floor;
            f32 a0 = 0.5f * s * (1.0f - x0f) * (1.0f - x0f);
            f32 x1f = x1 - x1_ceil + 1.0f;
            f32 am = 0.5f * s * x1f * x1f;
            row[i0] += d * a0;
            if (i1 == i0 + 2) {
                row[i0 + 1] += d * (1.0f - a0 - am);
            } else {
                f32 a1 = s * (1.5f - x0f);
                row[i0 + 1] += d * (a1 - a0);
                for (u32 i = i0 + 2; i + 1 < i1; i += 1) { row[i] += d * s; }
                f32 a2 = a1 + (f32)(i1 - i0 - 3) * s;
                row[i1 - 1] += d * (1.0f - a2 - am);
            }
            row[i1] += d * am;
        }
        x = x_next;
    }
}

void r_raster_fill(Arena *scratch, u8 *coverage, u32 width, u32 height, const V2 *points,
                   const u32 *contour_sizes, u32 contour_count) {
    ArenaTemp temp = arena_temp_begin(scratch);
    u32 pitch = width + 2;
    f32 *accum = push_array_zero(scratch, f32, (u64)pitch * height);

    const V2 *contour = points;
    for (u32 c = 0; c < contour_count; c += 1) {
        u32 count = contour_sizes[c];
        for (u32 i = 0; i < count; i += 1) {
            V2 a = contour[i];
            V2 b = contour[(i + 1 == count) ? 0 : i + 1];
            Assert(a.x >= 0.0f && a.x <= (f32)width && a.y >= 0.0f && a.y <= (f32)height);
            r_raster_edge(accum, pitch, height, a, b);
        }
        contour += count;
    }

    for (u32 y = 0; y < height; y += 1) {
        const f32 *row = accum + (u64)y * pitch;
        u8 *out = coverage + (u64)y * width;
        f32 sum = 0.0f;
        for (u32 x = 0; x < width; x += 1) {
            sum += row[x];
            f32 a = min_f32(abs_f32(sum), 1.0f);
            out[x] = (u8)(a * 255.0f + 0.5f);
        }
    }
    arena_temp_end(temp);
}

// r_atlas.c - skyline bottom-left packing over a single R8 page (research/03 s4.8).
// The skyline is the staircase of the occupied space: a sorted list of steps,
// step i covering [nodes[i].x, nodes[i+1].x) at height nodes[i].y. Placing a
// rect means finding the step run where it sits lowest, then rewriting that run.
#include "r_atlas.h"

#include "r_backend.h"

typedef struct R_SkylineNode {
    u16 x, y;
} R_SkylineNode;

typedef struct R_Atlas {
    Arena *arena;
    u8 *pixels;  // size * size, R8 coverage
    u32 size;
    u32 texture;
    u32 used_area;

    R_SkylineNode *nodes;  // nodes[count-1] is the sentinel at x == size
    u32 node_count;

    // Dirty region in texels, empty when x1 == 0.
    u32 dirty_x0, dirty_y0, dirty_x1, dirty_y1;
} R_Atlas;

global R_Atlas r_atlas;

// One transparent texel around every entry: linear filtering must never pick up
// the neighbour (research/03 s4.8).
#define R_ATLAS_PAD 1

static void r_atlas_skyline_reset(void) {
    r_atlas.node_count = 2;
    r_atlas.nodes[0].x = 0;
    r_atlas.nodes[0].y = 0;
    r_atlas.nodes[1].x = (u16)r_atlas.size;
    r_atlas.nodes[1].y = 0;
    r_atlas.used_area = 0;
}

static void r_atlas_dirty_add(u32 x0, u32 y0, u32 x1, u32 y1) {
    if (r_atlas.dirty_x1 == 0) {
        r_atlas.dirty_x0 = x0;
        r_atlas.dirty_y0 = y0;
        r_atlas.dirty_x1 = x1;
        r_atlas.dirty_y1 = y1;
        return;
    }
    r_atlas.dirty_x0 = Min(r_atlas.dirty_x0, x0);
    r_atlas.dirty_y0 = Min(r_atlas.dirty_y0, y0);
    r_atlas.dirty_x1 = Max(r_atlas.dirty_x1, x1);
    r_atlas.dirty_y1 = Max(r_atlas.dirty_y1, y1);
}

void r_atlas_init(Arena *arena) {
    r_atlas.arena = arena;
    r_atlas.size = R_ATLAS_SIZE_MIN;
    r_atlas.pixels = push_array_zero(arena, u8, (u64)r_atlas.size * r_atlas.size);
    r_atlas.nodes = push_array(arena, R_SkylineNode, R_ATLAS_MAX_NODES);
    r_atlas.texture = r_backend_texture_r8(r_atlas.size);
    r_atlas.dirty_x1 = 0;
    r_atlas_skyline_reset();
}

void r_atlas_reset(void) {
    mem_zero(r_atlas.pixels, (u64)r_atlas.size * r_atlas.size);
    r_atlas_skyline_reset();
    r_atlas_dirty_add(0, 0, r_atlas.size, r_atlas.size);
}

// Doubles the page, keeping every existing placement: the origin is top left,
// so growing only adds space to the right and below.
static b32 r_atlas_grow(void) {
    if (r_atlas.size >= R_ATLAS_SIZE_MAX) { return 0; }
    u32 old_size = r_atlas.size;
    u32 new_size = old_size * 2;
    u8 *pixels = push_array_zero(r_atlas.arena, u8, (u64)new_size * new_size);
    for (u32 row = 0; row < old_size; row += 1) {
        mem_copy(pixels + (u64)row * new_size, r_atlas.pixels + (u64)row * old_size, old_size);
    }
    r_atlas.pixels = pixels;
    r_atlas.size = new_size;

    // The sentinel becomes a real step at height 0 covering the new columns.
    r_atlas.nodes[r_atlas.node_count - 1].y = 0;
    Assert(r_atlas.node_count < R_ATLAS_MAX_NODES);
    r_atlas.nodes[r_atlas.node_count].x = (u16)new_size;
    r_atlas.nodes[r_atlas.node_count].y = 0;
    r_atlas.node_count += 1;

    r_atlas.texture = r_backend_texture_r8(new_size);
    r_atlas_dirty_add(0, 0, new_size, new_size);
    return 1;
}

// Lowest y at which `width` fits starting exactly at nodes[index].x, or the
// page height when it does not fit at all.
static u32 r_atlas_fit(u32 index, u32 width, u32 height) {
    u32 x = r_atlas.nodes[index].x;
    if (x + width > r_atlas.size) { return r_atlas.size; }
    u32 y = 0;
    for (u32 i = index; i < r_atlas.node_count - 1 && r_atlas.nodes[i].x < x + width; i += 1) {
        y = Max(y, (u32)r_atlas.nodes[i].y);
    }
    if (y + height > r_atlas.size) { return r_atlas.size; }
    return y;
}

static void r_atlas_skyline_insert(u32 index, u32 x, u32 y, u32 width) {
    u32 x1 = x + width;
    u32 last = index;
    while (r_atlas.nodes[last].x < x1) { last += 1; }
    u32 tail_y = r_atlas.nodes[last - 1].y;

    R_SkylineNode inserted[2];
    u32 inserted_count = 1;
    inserted[0].x = (u16)x;
    inserted[0].y = (u16)y;
    if (r_atlas.nodes[last].x > x1) {
        inserted[1].x = (u16)x1;
        inserted[1].y = (u16)tail_y;
        inserted_count = 2;
    }

    u32 removed = last - index;
    Assert(r_atlas.node_count + inserted_count - removed <= R_ATLAS_MAX_NODES);
    u32 tail_count = r_atlas.node_count - last;
    mem_move(&r_atlas.nodes[index + inserted_count], &r_atlas.nodes[last],
             (u64)tail_count * sizeof(R_SkylineNode));
    // Written out, not looped: MSVC turns a two iteration copy loop into a call
    // to memcpy, which /GL then refuses to link against without the CRT.
    r_atlas.nodes[index] = inserted[0];
    if (inserted_count == 2) { r_atlas.nodes[index + 1] = inserted[1]; }
    r_atlas.node_count += inserted_count - removed;

    // Merge steps that ended up at the same height: without this the node list
    // grows without bound and every placement gets slower. The sentinel is
    // never merged, it is what terminates every scan of the skyline.
    u32 write = 1;
    for (u32 read = 1; read + 1 < r_atlas.node_count; read += 1) {
        if (r_atlas.nodes[read].y != r_atlas.nodes[write - 1].y) {
            r_atlas.nodes[write] = r_atlas.nodes[read];
            write += 1;
        }
    }
    r_atlas.nodes[write] = r_atlas.nodes[r_atlas.node_count - 1];
    r_atlas.node_count = write + 1;
}

R_AtlasRect r_atlas_add(u32 width, u32 height, const u8 *pixels) {
    Assert(width > 0 && height > 0);
    u32 padded_w = width + 2 * R_ATLAS_PAD;
    u32 padded_h = height + 2 * R_ATLAS_PAD;

    R_AtlasRect result;
    StructZero(&result);

    u32 best_index = 0;
    u32 best_x = 0;
    u32 best_y = 0;
    for (;;) {
        best_y = r_atlas.size;  // the page height stands for "does not fit"
        for (u32 i = 0; i + 1 < r_atlas.node_count; i += 1) {
            u32 y = r_atlas_fit(i, padded_w, padded_h);
            u32 x = r_atlas.nodes[i].x;
            // Bottom left: lowest step wins, leftmost breaks the tie.
            if (y < best_y || (y == best_y && x < best_x)) {
                best_y = y;
                best_x = x;
                best_index = i;
            }
        }
        if (best_y < r_atlas.size) { break; }
        if (!r_atlas_grow()) { return result; }  // full: width stays 0
    }

    r_atlas_skyline_insert(best_index, best_x, best_y + padded_h, padded_w);
    r_atlas.used_area += width * height;

    u32 x = best_x + R_ATLAS_PAD;
    u32 y = best_y + R_ATLAS_PAD;
    for (u32 row = 0; row < height; row += 1) {
        mem_copy(r_atlas.pixels + (u64)(y + row) * r_atlas.size + x, pixels + (u64)row * width,
                 width);
    }
    r_atlas_dirty_add(best_x, best_y, best_x + padded_w, best_y + padded_h);

    f32 inv = 1.0f / (f32)r_atlas.size;
    result.x = (u16)x;
    result.y = (u16)y;
    result.width = (u16)width;
    result.height = (u16)height;
    result.uv0 = v2((f32)x * inv, (f32)y * inv);
    result.uv1 = v2((f32)(x + width) * inv, (f32)(y + height) * inv);
    return result;
}

void r_atlas_flush(void) {
    if (r_atlas.dirty_x1 == 0) { return; }
    r_backend_texture_upload_r8(r_atlas.texture, r_atlas.size, r_atlas.pixels, r_atlas.dirty_x0,
                                r_atlas.dirty_y0, r_atlas.dirty_x1 - r_atlas.dirty_x0,
                                r_atlas.dirty_y1 - r_atlas.dirty_y0);
    r_atlas.dirty_x1 = 0;
}

u32 r_atlas_texture(void) { return r_atlas.texture; }
u32 r_atlas_size(void) { return r_atlas.size; }
u32 r_atlas_used_area(void) { return r_atlas.used_area; }

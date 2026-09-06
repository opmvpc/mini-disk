// r_core.c - vertex generation and batching. Knows nothing about OpenGL: the
// GL side is r_gl.c, reached through the three r_gl_* calls below.
#include "r_core.h"

static b32  r_gl_init(void);
static void r_gl_shutdown(void);
static void r_gl_draw(const R_Frame *frame);

global R_Frame r_frame;

const R_Frame *r_frame_state(void) { return &r_frame; }

b32 r_init(void) { return r_gl_init(); }

void r_shutdown(void) { r_gl_shutdown(); }

void r_begin_frame(f32 width, f32 height, f32 dpi_scale) {
    r_frame.quad_count = 0;
    r_frame.batch_count = 0;
    r_frame.viewport = v2(width, height);
    r_frame.dpi_scale = dpi_scale;
    r_frame.clear_color = 0;
    r_frame.clip = rect(0.0f, 0.0f, width, height);
    r_frame.texture = 0;
}

void r_clear(u32 color) { r_frame.clear_color = color; }

void r_set_clip(Rect clip) { r_frame.clip = clip; }

// One draw call per (texture, clip) run: the batch grows as long as both hold.
static R_Batch *r_batch_for_current_state(void) {
    R_Batch *batch = r_frame.batch_count ? &r_frame.batches[r_frame.batch_count - 1] : 0;
    if (batch && batch->texture == r_frame.texture && batch->clip.min.x == r_frame.clip.min.x &&
        batch->clip.min.y == r_frame.clip.min.y && batch->clip.max.x == r_frame.clip.max.x &&
        batch->clip.max.y == r_frame.clip.max.y) {
        return batch;
    }
    Assert(r_frame.batch_count < R_MAX_BATCHES);
    batch = &r_frame.batches[r_frame.batch_count];
    r_frame.batch_count += 1;
    batch->index_first = r_frame.quad_count * 6;
    batch->index_count = 0;
    batch->texture = r_frame.texture;
    batch->clip = r_frame.clip;
    return batch;
}

void r_rect(R_RectParams params) {
    Assert(r_frame.quad_count < R_MAX_QUADS);

    // Pixel alignment (ADR-005): the rect edges land on whole physical pixels
    // and the border is a whole number of them, so a 1 px line stays crisp at
    // any DPI instead of being smeared over two rows by the SDF.
    f32 x0 = round_f32(params.dst.min.x);
    f32 y0 = round_f32(params.dst.min.y);
    f32 x1 = round_f32(params.dst.max.x);
    f32 y1 = round_f32(params.dst.max.y);
    f32 border = params.border > 0.0f ? max_f32(1.0f, round_f32(params.border)) : 0.0f;
    f32 softness = max_f32(params.softness, 0.0f);

    V2 half = v2((x1 - x0) * 0.5f, (y1 - y0) * 0.5f);
    V2 center = v2(x0 + half.x, y0 + half.y);
    // A radius bigger than the half size folds the distance field inside out.
    f32 radius = clamp_f32(params.corner_radius, 0.0f, min_f32(half.x, half.y));
    // The quad is grown so the antialiasing ramp and the blur have room.
    f32 pad = softness + 1.0f;

    u8 flags = 0;
    if (softness > 0.0f) { flags |= R_VertFlag_Shadow; }
    if (params.texture) { flags |= R_VertFlag_Texture; }
    // The shadow blur reuses the border field: a shadow never has a border.
    f32 packed_border = softness > 0.0f ? softness : border;

    R_Batch *batch = r_batch_for_current_state();
    R_Vertex *vertex = &r_frame.vertices[(u64)r_frame.quad_count * 4];
    r_frame.quad_count += 1;
    batch->index_count += 6;

    // Everything but the corner position and its UV is shared: write the top
    // left vertex once, copy it three times, patch the two fields that differ.
    // Corner order is 0,1,2,3 = TL,TR,BL,BR, matching the index pattern.
    vertex[0].dst_pos[0] = x0 - pad;
    vertex[0].dst_pos[1] = y0 - pad;
    vertex[0].dst_center[0] = center.x;
    vertex[0].dst_center[1] = center.y;
    vertex[0].dst_half[0] = half.x;
    vertex[0].dst_half[1] = half.y;
    vertex[0].src_uv[0] = params.uv0.x;
    vertex[0].src_uv[1] = params.uv0.y;
    vertex[0].color[0] = (u8)(params.color);
    vertex[0].color[1] = (u8)(params.color >> 8);
    vertex[0].color[2] = (u8)(params.color >> 16);
    vertex[0].color[3] = (u8)(params.color >> 24);
    vertex[0].corner_radius = (u16)(radius * 16.0f + 0.5f);
    vertex[0].border = (u8)(min_f32(packed_border, 127.0f) * 2.0f + 0.5f);
    vertex[0].flags = flags;
    vertex[1] = vertex[0];
    vertex[2] = vertex[0];
    vertex[3] = vertex[0];
    vertex[1].dst_pos[0] = vertex[3].dst_pos[0] = x1 + pad;
    vertex[1].src_uv[0] = vertex[3].src_uv[0] = params.uv1.x;
    vertex[2].dst_pos[1] = vertex[3].dst_pos[1] = y1 + pad;
    vertex[2].src_uv[1] = vertex[3].src_uv[1] = params.uv1.y;
}

void r_end_frame(void) { r_gl_draw(&r_frame); }

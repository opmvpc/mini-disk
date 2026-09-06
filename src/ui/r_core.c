// r_core.c - command queue, clip stack, layer sort, batching and vertex
// generation. Knows nothing about OpenGL: the back end is reached through
// r_backend.h.
#include "r_core.h"

#include "r_atlas.h"
#include "r_backend.h"

// One command is one future quad. They are stored in chunks so the frame arena
// can be used by the caller between two r_rect calls without splitting the list.
#define R_CMD_CHUNK 256

struct R_Cmd {
    Rect dst;
    Rect clip;
    V2 uv0, uv1;
    u32 color;
    u32 texture;
    f32 corner_radius;
    f32 border;
    f32 softness;
    u8 flags;
    u8 layer;
};

struct R_CmdChunk {
    R_CmdChunk *next;
    u32 count;
    R_Cmd cmds[R_CMD_CHUNK];
};

global R_Frame r_frame;

const R_Frame *r_frame_state(void) { return &r_frame; }
u32 r_draw_call_count(void) { return r_frame.batch_count; }

b32 r_init(Arena *persistent) {
    if (!r_backend_init()) { return 0; }
    r_atlas_init(persistent);
    return 1;
}

void r_shutdown(void) { r_backend_shutdown(); }

void r_begin_frame(Arena *frame_arena, f32 width, f32 height, f32 dpi_scale) {
    StructZero(&r_frame);
    r_frame.arena = frame_arena;
    r_frame.viewport = v2(width, height);
    r_frame.dpi_scale = dpi_scale;
    r_frame.clips[0] = rect(0.0f, 0.0f, width, height);
    r_frame.clip_depth = 1;
}

void r_clear(u32 color) { r_frame.clear_color = color; }

void r_push_clip(Rect clip) {
    Assert(r_frame.clip_depth < R_MAX_CLIP_DEPTH);
    // Intersection, always: a child never draws outside its parent.
    r_frame.clips[r_frame.clip_depth] = rect_intersect(clip, r_frame.clips[r_frame.clip_depth - 1]);
    r_frame.clip_depth += 1;
}

void r_pop_clip(void) {
    Assert(r_frame.clip_depth > 1);
    r_frame.clip_depth -= 1;
}

Rect r_clip(void) { return r_frame.clips[r_frame.clip_depth - 1]; }

void r_set_layer(R_Layer layer) {
    Assert(layer < R_Layer_COUNT);
    r_frame.layer = layer;
}

static R_Cmd *r_cmd_push(void) {
    R_CmdChunk *chunk = r_frame.cmd_last;
    if (chunk == 0 || chunk->count == R_CMD_CHUNK) {
        R_CmdChunk *next = push_struct(r_frame.arena, R_CmdChunk);
        next->next = 0;
        next->count = 0;
        if (chunk) {
            chunk->next = next;
        } else {
            r_frame.cmd_first = next;
        }
        r_frame.cmd_last = next;
        chunk = next;
    }
    Assert(r_frame.cmd_count < R_MAX_QUADS);
    r_frame.cmd_count += 1;
    R_Cmd *cmd = &chunk->cmds[chunk->count];
    chunk->count += 1;
    return cmd;
}

void r_rect(R_RectParams params) {
    R_Cmd *cmd = r_cmd_push();
    cmd->dst = params.dst;
    cmd->clip = r_clip();
    cmd->uv0 = params.uv0;
    cmd->uv1 = params.uv1;
    cmd->color = params.color;
    cmd->texture = params.texture;
    cmd->corner_radius = params.corner_radius;
    cmd->border = params.border;
    cmd->softness = params.softness;
    cmd->flags = params.flags;
    cmd->layer = (u8)r_frame.layer;
}

void r_rect_textured(Rect dst, u32 texture, V2 uv0, V2 uv1, u32 color, u32 mask) {
    R_RectParams params;
    StructZero(&params);
    params.dst = dst;
    params.color = color;
    params.texture = texture;
    params.uv0 = uv0;
    params.uv1 = uv1;
    // A coverage mask multiplies the colour; a full colour image replaces it.
    // Either way the quad is raw: the SDF would clip the sampled pixels.
    params.flags = (u8)(R_VertFlag_NoSdf | (mask ? R_VertFlag_R8 : 0));
    r_rect(params);
}

void r_line_1px(V2 from, V2 to, u32 color) {
    // Axis aligned only: a diagonal 1 px line is a rotated rect, and we have no
    // rotation. The half pixel snap is what keeps it from smearing over two rows.
    Assert(from.x == to.x || from.y == to.y);
    R_RectParams params;
    StructZero(&params);
    params.color = color;
    params.flags = R_VertFlag_NoSdf;
    if (from.y == to.y) {
        f32 y = floor_f32(from.y);
        params.dst = rect(min_f32(from.x, to.x), y, max_f32(from.x, to.x), y + 1.0f);
    } else {
        f32 x = floor_f32(from.x);
        params.dst = rect(x, min_f32(from.y, to.y), x + 1.0f, max_f32(from.y, to.y));
    }
    r_rect(params);
}

void r_shadow(Rect dst, u32 color, f32 corner_radius, f32 softness, V2 offset) {
    R_RectParams params;
    StructZero(&params);
    params.dst = rect(dst.min.x + offset.x, dst.min.y + offset.y, dst.max.x + offset.x,
                      dst.max.y + offset.y);
    params.color = color;
    params.corner_radius = corner_radius;
    params.softness = softness;
    r_rect(params);
}

// --- end of frame ----------------------------------------------------------

static void r_write_quad(R_Vertex *out, const R_Cmd *cmd) {
    // Pixel alignment (ADR-005): the rect edges land on whole physical pixels
    // and the border is a whole number of them, so a 1 px line stays crisp at
    // any DPI instead of being smeared over two rows by the SDF.
    f32 x0 = round_f32(cmd->dst.min.x);
    f32 y0 = round_f32(cmd->dst.min.y);
    f32 x1 = round_f32(cmd->dst.max.x);
    f32 y1 = round_f32(cmd->dst.max.y);
    f32 border = cmd->border > 0.0f ? max_f32(1.0f, round_f32(cmd->border)) : 0.0f;
    f32 softness = max_f32(cmd->softness, 0.0f);

    V2 half = v2((x1 - x0) * 0.5f, (y1 - y0) * 0.5f);
    V2 center = v2(x0 + half.x, y0 + half.y);
    // A radius bigger than the half size folds the distance field inside out.
    f32 radius = clamp_f32(cmd->corner_radius, 0.0f, min_f32(half.x, half.y));

    u8 flags = cmd->flags;
    if (softness > 0.0f) { flags |= R_VertFlag_Shadow; }
    if (cmd->texture) { flags |= R_VertFlag_Texture; }
    // The shadow blur reuses the border field: a shadow never has a border.
    f32 packed_border = softness > 0.0f ? softness : border;
    // The quad is grown so the antialiasing ramp and the blur have room; a raw
    // quad is exactly its rect, growing it would blur a texture lookup.
    f32 pad = (flags & R_VertFlag_NoSdf) ? 0.0f : softness + 1.0f;

    // Built on the stack, then written out four times: the destination may be
    // write combined GPU memory, where reading back a vertex costs a stall.
    R_Vertex v;
    v.dst_pos[0] = x0 - pad;
    v.dst_pos[1] = y0 - pad;
    v.dst_center[0] = center.x;
    v.dst_center[1] = center.y;
    v.dst_half[0] = half.x;
    v.dst_half[1] = half.y;
    v.src_uv[0] = cmd->uv0.x;
    v.src_uv[1] = cmd->uv0.y;
    v.color[0] = (u8)(cmd->color);
    v.color[1] = (u8)(cmd->color >> 8);
    v.color[2] = (u8)(cmd->color >> 16);
    v.color[3] = (u8)(cmd->color >> 24);
    v.corner_radius = (u16)(radius * 16.0f + 0.5f);
    v.border = (u8)(min_f32(packed_border, 127.0f) * 2.0f + 0.5f);
    v.flags = flags;

    // Corner order 0,1,2,3 = TL,TR,BL,BR, matching the index pattern.
    out[0] = v;
    v.dst_pos[0] = x1 + pad;
    v.src_uv[0] = cmd->uv1.x;
    out[1] = v;
    v.dst_pos[0] = x0 - pad;
    v.src_uv[0] = cmd->uv0.x;
    v.dst_pos[1] = y1 + pad;
    v.src_uv[1] = cmd->uv1.y;
    out[2] = v;
    v.dst_pos[0] = x1 + pad;
    v.src_uv[0] = cmd->uv1.x;
    out[3] = v;
}

static b32 r_same_clip(Rect a, Rect b) {
    return a.min.x == b.min.x && a.min.y == b.min.y && a.max.x == b.max.x && a.max.y == b.max.y;
}

// Counting sort on three buckets: stable, one pass to count and one to place,
// which is what keeps "the order inside a layer is the order of the calls" true.
static R_Cmd **r_sort_commands(void) {
    u32 first[R_Layer_COUNT + 1];
    mem_zero(first, sizeof(first));
    for (R_CmdChunk *chunk = r_frame.cmd_first; chunk; chunk = chunk->next) {
        for (u32 i = 0; i < chunk->count; i += 1) { first[chunk->cmds[i].layer + 1] += 1; }
    }
    for (u32 i = 1; i <= R_Layer_COUNT; i += 1) { first[i] += first[i - 1]; }

    R_Cmd **order = push_array(r_frame.arena, R_Cmd *, r_frame.cmd_count);
    for (R_CmdChunk *chunk = r_frame.cmd_first; chunk; chunk = chunk->next) {
        for (u32 i = 0; i < chunk->count; i += 1) {
            R_Cmd *cmd = &chunk->cmds[i];
            order[first[cmd->layer]] = cmd;
            first[cmd->layer] += 1;
        }
    }
    return order;
}

void r_end_frame(void) {
    r_atlas_flush();
    if (r_frame.cmd_count == 0) {
        r_backend_draw(&r_frame);
        return;
    }

    R_Cmd **order = r_sort_commands();
    r_frame.vertices = r_backend_map_vertices(r_frame.cmd_count);
    r_frame.vertices_mapped = r_frame.vertices != 0;
    if (!r_frame.vertices_mapped) {
        r_frame.vertices = push_array(r_frame.arena, R_Vertex, (u64)r_frame.cmd_count * 4);
    }
    // Worst case one batch per command; the arena makes the bound free.
    r_frame.batches = push_array(r_frame.arena, R_Batch, r_frame.cmd_count);

    R_Batch *batch = 0;
    for (u32 i = 0; i < r_frame.cmd_count; i += 1) {
        const R_Cmd *cmd = order[i];
        if (batch == 0 || batch->texture != cmd->texture || !r_same_clip(batch->clip, cmd->clip) ||
            batch->quad_count == R_MAX_BATCH_QUADS) {
            batch = &r_frame.batches[r_frame.batch_count];
            r_frame.batch_count += 1;
            batch->quad_first = r_frame.quad_count;
            batch->quad_count = 0;
            batch->texture = cmd->texture;
            batch->clip = cmd->clip;
        }
        r_write_quad(r_frame.vertices + (u64)r_frame.quad_count * 4, cmd);
        r_frame.quad_count += 1;
        batch->quad_count += 1;
    }

    r_backend_draw(&r_frame);
}

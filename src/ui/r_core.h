// r_core.h - the whole renderer API: one primitive, the SDF rect (ADR-005).
// Everything is in physical pixels; the caller applies the DPI scale.
//
// A frame is a command queue built in a frame arena, then sorted by layer and
// cut into batches at each change of (texture, clip). Nothing is drawn before
// r_end_frame, so a popup can be emitted while its parent panel is still being
// built and still land on top.
#ifndef R_CORE_H
#define R_CORE_H

#include "../base/base.h"
#include "../base/base_arena.h"
#include "../base/base_math.h"

// 40 bytes, alignment 4 (research/03 s4.3).
typedef struct R_Vertex {
    f32 dst_pos[2];      //  0 : quad corner, screen pixels, softness padding included
    f32 dst_center[2];   //  8 : centre of the logical rect (SDF)
    f32 dst_half[2];     // 16 : half size of the logical rect (SDF)
    f32 src_uv[2];       // 24 : atlas coordinates, normalized
    u8 color[4];         // 32 : RGBA8 premultiplied
    u16 corner_radius;   // 36 : 1/16 px
    u8 border;           // 38 : 1/2 px, doubles as the shadow softness
    u8 flags;            // 39 : R_VertFlag_*
} R_Vertex;
StaticAssert(sizeof(R_Vertex) == 40, r_vertex_is_40_bytes);

typedef enum R_VertFlag {
    R_VertFlag_R8 = 1,       // the texture is a single channel coverage mask
    R_VertFlag_NoSdf = 2,    // raw quad, no distance field
    R_VertFlag_Shadow = 4,   // `border` carries a blur radius instead
    R_VertFlag_Texture = 8,  // sample the atlas at src_uv
} R_VertFlag;

// Drawing order. Inside a layer the order is the order of the calls; the sort
// between layers is stable, so nothing else moves.
typedef enum R_Layer {
    R_Layer_Content = 0,
    R_Layer_Popup,
    R_Layer_Tooltip,
    R_Layer_COUNT
} R_Layer;

// A batch is one draw call: it ends when the texture or the clip rect changes,
// or when it would need more than 65 536 vertices (u16 indices).
typedef struct R_Batch {
    u32 quad_first;  // in the frame's vertex buffer
    u32 quad_count;
    u32 texture;
    Rect clip;  // screen pixels, top left origin
} R_Batch;

// Coverage exponent applied to the R8 atlas in the fragment shader. 1/1.2 is
// the value that matches the system rendering of light text on dark panels.
#define R_TEXT_GAMMA (1.0f / 1.2f)

#define R_MAX_QUADS       32768  // per frame, sized with the VBO regions
#define R_MAX_BATCH_QUADS 16384  // 65 536 vertices: the u16 index ceiling
#define R_MAX_CLIP_DEPTH  32

typedef struct R_Cmd R_Cmd;
typedef struct R_CmdChunk R_CmdChunk;

typedef struct R_Frame {
    Arena *arena;  // frame arena, cleared by the caller after r_end_frame

    R_CmdChunk *cmd_first, *cmd_last;
    u32 cmd_count;

    R_Vertex *vertices;  // mapped GPU memory or staged in the frame arena
    u32 quad_count;
    R_Batch *batches;
    u32 batch_count;
    b32 vertices_mapped;

    V2 viewport;      // physical pixels
    f32 dpi_scale;    // 1.0 = 96 dpi
    u32 clear_color;  // RGBA8 premultiplied

    Rect clips[R_MAX_CLIP_DEPTH];
    u32 clip_depth;  // clips[0] is the viewport, never popped
    R_Layer layer;
} R_Frame;

typedef struct R_RectParams {
    Rect dst;
    u32 color;  // RGBA8 premultiplied, see r_rgba
    f32 corner_radius;
    f32 border;    // > 0 : ring instead of fill, rounded to whole pixels
    f32 softness;  // > 0 : blurred shadow, mutually exclusive with border
    u32 texture;   // 0 : no texture
    V2 uv0, uv1;
    u8 flags;  // extra R_VertFlag_*, for R8 masks and raw quads
} R_RectParams;

// Premultiplied RGBA8 from straight 8 bit components.
md_inline u32 r_rgba(u8 red, u8 green, u8 blue, u8 alpha) {
    u32 a = alpha;
    u32 r = ((u32)red * a + 127) / 255;
    u32 g = ((u32)green * a + 127) / 255;
    u32 b = ((u32)blue * a + 127) / 255;
    return r | (g << 8) | (b << 16) | (a << 24);
}
#define r_rgb(hex)                                                                   \
    r_rgba((u8)(((hex) >> 16) & 0xFF), (u8)(((hex) >> 8) & 0xFF), (u8)((hex) & 0xFF), \
           255)

b32  r_init(Arena *persistent);  // 0 when the GL objects cannot be created
void r_shutdown(void);

void r_begin_frame(Arena *frame_arena, f32 width, f32 height, f32 dpi_scale);
void r_clear(u32 color);
void r_end_frame(void);  // sort, batch, upload, draw calls, swap

void r_push_clip(Rect clip);  // intersected with the clip already in place
void r_pop_clip(void);
Rect r_clip(void);
void r_set_layer(R_Layer layer);

void r_rect(R_RectParams params);
void r_rect_textured(Rect dst, u32 texture, V2 uv0, V2 uv1, u32 color, u32 mask);
void r_line_1px(V2 from, V2 to, u32 color);  // axis aligned, whole physical pixel
void r_shadow(Rect dst, u32 color, f32 corner_radius, f32 softness, V2 offset);

const R_Frame *r_frame_state(void);  // tests and the debug overlay
u32 r_draw_call_count(void);

#endif // R_CORE_H

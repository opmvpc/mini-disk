// r_core.h - the whole renderer API: one primitive, the SDF rect (ADR-005).
// Everything is in physical pixels; the caller applies the DPI scale.
#ifndef R_CORE_H
#define R_CORE_H

#include "../base/base.h"
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

// A batch is one draw call: it ends when the texture or the clip rect changes.
typedef struct R_Batch {
    u32 index_first;
    u32 index_count;
    u32 texture;
    Rect clip;  // screen pixels, top left origin
} R_Batch;

#define R_MAX_QUADS   4096
#define R_MAX_BATCHES 64

typedef struct R_Frame {
    R_Vertex vertices[R_MAX_QUADS * 4];
    u32 quad_count;
    R_Batch batches[R_MAX_BATCHES];
    u32 batch_count;
    V2 viewport;      // physical pixels
    f32 dpi_scale;    // 1.0 = 96 dpi
    u32 clear_color;  // RGBA8 premultiplied
    Rect clip;        // current clip rect, applied to the batches that follow
    u32 texture;      // current atlas
} R_Frame;

typedef struct R_RectParams {
    Rect dst;
    u32 color;  // RGBA8 premultiplied, see r_rgba
    f32 corner_radius;
    f32 border;     // > 0 : ring instead of fill, rounded to whole pixels
    f32 softness;   // > 0 : blurred shadow, mutually exclusive with border
    u32 texture;    // 0 : no texture
    V2 uv0, uv1;
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

b32  r_init(void);   // 0 when the GL objects cannot be created
void r_shutdown(void);
void r_begin_frame(f32 width, f32 height, f32 dpi_scale);
void r_clear(u32 color);
void r_set_clip(Rect clip);
void r_rect(R_RectParams params);
void r_end_frame(void);   // upload, draw calls, swap

const R_Frame *r_frame_state(void);   // tests and the debug overlay

#endif // R_CORE_H

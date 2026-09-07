// r_backend.h - the seam between the renderer and the graphics API. r_core.c,
// r_atlas.c and the tests all speak this; only r_gl.c (or the test stub)
// implements it. Keeps ui/ free of any GL type outside r_gl.c.
#ifndef R_BACKEND_H
#define R_BACKEND_H

#include "../base/base.h"

typedef struct R_Frame R_Frame;
typedef struct R_Vertex R_Vertex;

b32  r_backend_init(void);
void r_backend_shutdown(void);

// Write destination for the frame's vertices. Returns the mapped GPU memory
// when persistent mapping is available (write combined: write forward, never
// read back), 0 when the caller has to stage the vertices itself.
R_Vertex *r_backend_map_vertices(u32 quad_count);
void      r_backend_draw(const R_Frame *frame);

// R8 texture of `size` x `size`, contents undefined. Replaces the previous one.
u32  r_backend_texture_r8(u32 size);
void r_backend_texture_upload_r8(u32 texture, u32 atlas_size, const u8 *pixels, u32 x, u32 y,
                                 u32 width, u32 height);

// RGBA8 texture of `size` x `size` for the cover thumbnails (T-014). It lives
// beside the R8 atlas, not instead of it: one atlas per pixel format.
u32  r_backend_texture_rgba8(u32 size);
void r_backend_texture_upload_rgba8(u32 texture, u32 atlas_size, const u8 *pixels, u32 x, u32 y,
                                    u32 width, u32 height);

#endif // R_BACKEND_H

// r_gl.c - the OpenGL 3.3 back end of r_core: one program, one VAO, one
// triple buffered vertex buffer, a static index buffer and the R8 atlas.
#include "gl_loader.h"
#include "r_backend.h"
#include "r_core.h"

#include "../platform/platform.h"

static const GLchar *r_gl_vertex_source =
        "#version 330 core\n"
        "layout(location=0) in vec2  a_dst_pos;\n"
        "layout(location=1) in vec2  a_dst_center;\n"
        "layout(location=2) in vec2  a_dst_half;\n"
        "layout(location=3) in vec2  a_src_uv;\n"
        "layout(location=4) in vec4  a_color;\n"
        "layout(location=5) in float a_corner_radius;\n"  // 1/16 px
        "layout(location=6) in float a_border;\n"         // 1/2 px
        "layout(location=7) in uint  a_flags;\n"
        "uniform vec2 u_viewport;\n"
        "out vec2      v_pos;\n"
        "out vec2      v_center;\n"
        "out vec2      v_half;\n"
        "out vec2      v_uv;\n"
        "out vec4      v_color;\n"
        "out float     v_radius;\n"
        "out float     v_border;\n"
        "flat out uint v_flags;\n"
        "void main() {\n"
        "    vec2 ndc = vec2(2.0 * a_dst_pos.x / u_viewport.x - 1.0,\n"
        "                    1.0 - 2.0 * a_dst_pos.y / u_viewport.y);\n"
        "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
        "    v_pos    = a_dst_pos;\n"
        "    v_center = a_dst_center;\n"
        "    v_half   = a_dst_half;\n"
        "    v_uv     = a_src_uv;\n"
        "    v_color  = a_color;\n"
        "    v_radius = a_corner_radius * (1.0/16.0);\n"
        "    v_border = a_border * 0.5;\n"
        "    v_flags  = a_flags;\n"
        "}\n";

static const GLchar *r_gl_fragment_source =
        "#version 330 core\n"
        "in vec2      v_pos;\n"
        "in vec2      v_center;\n"
        "in vec2      v_half;\n"
        "in vec2      v_uv;\n"
        "in vec4      v_color;\n"
        "in float     v_radius;\n"
        "in float     v_border;\n"
        "flat in uint v_flags;\n"
        "uniform sampler2D u_atlas;\n"
        "out vec4 frag;\n"
        "#define FLAG_R8      1u\n"
        "#define FLAG_NO_SDF  2u\n"
        "#define FLAG_SHADOW  4u\n"
        "#define FLAG_TEXTURE 8u\n"
        // < 0 inside, 0 on the edge, > 0 outside.
        "float sdf_round_rect(vec2 p, vec2 b, float r) {\n"
        "    vec2 q = abs(p) - b + vec2(r);\n"
        "    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;\n"
        "}\n"
        "void main() {\n"
        "    vec4 c = v_color;\n"
        "    if ((v_flags & FLAG_R8) != 0u) {\n"
        "        c *= texture(u_atlas, v_uv).r;\n"
        "    } else if ((v_flags & FLAG_TEXTURE) != 0u) {\n"
        "        c *= texture(u_atlas, v_uv);\n"
        "    }\n"
        "    if ((v_flags & FLAG_NO_SDF) == 0u) {\n"
        "        float d = sdf_round_rect(v_pos - v_center, v_half, v_radius);\n"
        "        float alpha;\n"
        "        if ((v_flags & FLAG_SHADOW) != 0u) {\n"
        // v_border carries the blur radius for shadows.
        "            float s = max(v_border, 0.5);\n"
        "            alpha = 1.0 - smoothstep(-s, s, d);\n"
        "        } else {\n"
        // fwidth, not 1.0: still exact if a global zoom shows up one day.
        "            float aa = fwidth(d);\n"
        "            alpha = 1.0 - smoothstep(-aa * 0.5, aa * 0.5, d);\n"
        "            if (v_border > 0.0) {\n"
        "                alpha -= 1.0 - smoothstep(-aa * 0.5, aa * 0.5, d + v_border);\n"
        "            }\n"
        "        }\n"
        "        c *= alpha;\n"
        "    }\n"
        "    frag = c;\n"  // premultiplied: blending is (ONE, ONE_MINUS_SRC_ALPHA)
        "}\n";

// Triple buffering: the CPU writes region N % 3 while the GPU may still be
// reading the two others (research/03 s4.7 option A).
#define R_GL_REGIONS       3
#define R_GL_REGION_QUADS  R_MAX_QUADS
#define R_GL_REGION_BYTES  ((u64)R_GL_REGION_QUADS * 4 * sizeof(R_Vertex))

typedef struct R_GlState {
    GLuint program;
    GLuint vao;
    GLuint vbo;
    GLuint ibo;
    GLuint atlas;
    GLint u_viewport;
    GLint u_atlas;
    GLint u_text_gamma;

    b32 persistent;             // ARB_buffer_storage available
    R_Vertex *mapped;           // the whole buffer, R_GL_REGIONS regions
    GLsync fences[R_GL_REGIONS];
    u32 region;                 // region the current frame writes into
} R_GlState;

global R_GlState r_gl;

#if BUILD_DEBUG
static void GL_CALL r_gl_debug_message(GLenum source, GLenum type, GLuint id, GLenum severity,
                                       GLsizei length, const GLchar *message, const void *user) {
    Unused(source);
    Unused(type);
    Unused(id);
    Unused(user);
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena, "gl: %S\n", str8((u8 *)message, (u64)length)));
    scratch_end(scratch);
    // Only a real error stops the world; performance chatter stays a log line.
    if (severity == GL_DEBUG_SEVERITY_HIGH) { __debugbreak(); }
}
#endif

static GLuint r_gl_compile(GLenum type, const GLchar *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, 0);
    glCompileShader(shader);
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        ArenaTemp scratch = scratch_begin(0, 0);
        GLchar *log = push_array(scratch.arena, GLchar, 2048);
        glGetShaderInfoLog(shader, 2048, 0, log);
        os_debug_print(str8f(scratch.arena, "opengl: shader compile failed: %s\n", log));
        scratch_end(scratch);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static b32 r_gl_build_program(void) {
    GLuint vertex = r_gl_compile(GL_VERTEX_SHADER, r_gl_vertex_source);
    GLuint fragment = r_gl_compile(GL_FRAGMENT_SHADER, r_gl_fragment_source);
    if (vertex == 0 || fragment == 0) { return 0; }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        ArenaTemp scratch = scratch_begin(0, 0);
        GLchar *log = push_array(scratch.arena, GLchar, 2048);
        glGetProgramInfoLog(program, 2048, 0, log);
        os_debug_print(str8f(scratch.arena, "opengl: program link failed: %s\n", log));
        scratch_end(scratch);
        glDeleteProgram(program);
        return 0;
    }
    r_gl.program = program;
    r_gl.u_viewport = glGetUniformLocation(program, "u_viewport");
    r_gl.u_atlas = glGetUniformLocation(program, "u_atlas");
    r_gl.u_text_gamma = glGetUniformLocation(program, "u_text_gamma");
    return 1;
}

// Quad pattern 0,1,2, 2,1,3 for one batch worth of quads, written once and
// never touched again. Batches are drawn with a base vertex, so the same
// 65 536 indices serve every batch of the frame whatever its offset.
static void r_gl_build_index_buffer(void) {
    ArenaTemp scratch = scratch_begin(0, 0);
    u16 *indices = push_array(scratch.arena, u16, R_MAX_BATCH_QUADS * 6);
    for (u32 quad = 0; quad < R_MAX_BATCH_QUADS; quad += 1) {
        u16 base = (u16)(quad * 4);
        u16 *out = indices + (u64)quad * 6;
        out[0] = base;
        out[1] = (u16)(base + 1);
        out[2] = (u16)(base + 2);
        out[3] = (u16)(base + 2);
        out[4] = (u16)(base + 1);
        out[5] = (u16)(base + 3);
    }
    glGenBuffers(1, &r_gl.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r_gl.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((u64)R_MAX_BATCH_QUADS * 6 * sizeof(u16)),
                 indices, GL_STATIC_DRAW);
    scratch_end(scratch);
}

// NOLINTBEGIN(performance-no-int-to-ptr) glVertexAttribPointer takes offsets
// as pointers: that is the GL API, not a pointer we ever dereference.
static void r_gl_setup_vertex_layout(void) {
    glBindBuffer(GL_ARRAY_BUFFER, r_gl.vbo);
#define R_ATTR(index, count, type, normalized, field)                                 \
    glEnableVertexAttribArray(index);                                                 \
    glVertexAttribPointer(index, count, type, normalized, (GLsizei)sizeof(R_Vertex),  \
                          (const void *)OffsetOf(R_Vertex, field))
    R_ATTR(0, 2, GL_FLOAT, GL_FALSE, dst_pos);
    R_ATTR(1, 2, GL_FLOAT, GL_FALSE, dst_center);
    R_ATTR(2, 2, GL_FLOAT, GL_FALSE, dst_half);
    R_ATTR(3, 2, GL_FLOAT, GL_FALSE, src_uv);
    R_ATTR(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, color);
    R_ATTR(5, 1, GL_UNSIGNED_SHORT, GL_FALSE, corner_radius);
    R_ATTR(6, 1, GL_UNSIGNED_BYTE, GL_FALSE, border);
#undef R_ATTR
    // Pure integer, read as `flat uint`: bit tests must not go through a float.
    glEnableVertexAttribArray(7);
    glVertexAttribIPointer(7, 1, GL_UNSIGNED_BYTE, (GLsizei)sizeof(R_Vertex),
                           (const void *)OffsetOf(R_Vertex, flags));
}
// NOLINTEND(performance-no-int-to-ptr)

// The extension strings are the one place we get C strings from the outside;
// comparing them in place beats building two String8 per candidate.
static b32 r_gl_name_eq(const char *a, const char *b) {
    while (*a && *a == *b) {
        a += 1;
        b += 1;
    }
    return *a == *b;
}

static b32 r_gl_has_extension(const char *name) {
    if (glGetStringi == 0) { return 0; }
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; i += 1) {
        const GLubyte *found = glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (found && r_gl_name_eq((const char *)found, name)) { return 1; }
    }
    return 0;
}

// Persistent mapping when the driver has ARB_buffer_storage, orphaning
// otherwise. Both paths hand r_core the same R_Vertex array to fill.
static void r_gl_create_vertex_buffer(void) {
    glGenBuffers(1, &r_gl.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r_gl.vbo);
    r_gl.persistent = glBufferStorage != 0 && r_gl_has_extension("GL_ARB_buffer_storage");
    if (r_gl.persistent) {
        GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        GLsizeiptr size = (GLsizeiptr)(R_GL_REGION_BYTES * R_GL_REGIONS);
        glBufferStorage(GL_ARRAY_BUFFER, size, 0, flags);
        r_gl.mapped = (R_Vertex *)glMapBufferRange(GL_ARRAY_BUFFER, 0, size, flags);
        r_gl.persistent = r_gl.mapped != 0;
    }
    if (!r_gl.persistent) {
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)R_GL_REGION_BYTES, 0, GL_STREAM_DRAW);
    }
    r_gl.region = R_GL_REGIONS - 1;
}

b32 r_backend_init(void) {
    u32 missing = gl_loader_init();
    if (missing) {
        ArenaTemp scratch = scratch_begin(0, 0);
        os_debug_print(str8f(scratch.arena, "opengl: %u entry point(s) missing from the driver\n",
                             missing));
        scratch_end(scratch);
        return 0;
    }

#if BUILD_DEBUG
    if (glDebugMessageCallback) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(r_gl_debug_message, 0);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, 0,
                              GL_FALSE);
    }
    {
        ArenaTemp scratch = scratch_begin(0, 0);
        os_debug_print(str8f(scratch.arena, "opengl: %s, %s\n", glGetString(GL_RENDERER),
                             glGetString(GL_VERSION)));
        scratch_end(scratch);
    }
#endif

    if (!r_gl_build_program()) { return 0; }

    glGenVertexArrays(1, &r_gl.vao);
    glBindVertexArray(r_gl.vao);
    r_gl_create_vertex_buffer();
    r_gl_setup_vertex_layout();
    r_gl_build_index_buffer();

#if BUILD_DEBUG
    {
        ArenaTemp scratch = scratch_begin(0, 0);
        os_debug_print(str8f(scratch.arena, "opengl: vertex upload = %s\n",
                             r_gl.persistent ? "persistent map x3" : "orphaning"));
        scratch_end(scratch);
    }
#endif

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    return 1;
}

void r_backend_shutdown(void) {
    for (u32 i = 0; i < R_GL_REGIONS; i += 1) {
        if (r_gl.fences[i]) { glDeleteSync(r_gl.fences[i]); }
    }
    if (r_gl.persistent) {
        glBindBuffer(GL_ARRAY_BUFFER, r_gl.vbo);
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }
    if (r_gl.atlas) { glDeleteTextures(1, &r_gl.atlas); }
    glDeleteBuffers(1, &r_gl.ibo);
    glDeleteBuffers(1, &r_gl.vbo);
    glDeleteVertexArrays(1, &r_gl.vao);
    glDeleteProgram(r_gl.program);
}

u32 r_backend_texture_r8(u32 size) {
    if (r_gl.atlas) { glDeleteTextures(1, &r_gl.atlas); }
    glGenTextures(1, &r_gl.atlas);
    glBindTexture(GL_TEXTURE_2D, r_gl.atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, (GLsizei)size, (GLsizei)size, 0, GL_RED,
                 GL_UNSIGNED_BYTE, 0);
    // Linear so subpixel positioning works; the 1 texel padding of the atlas is
    // what keeps neighbours from bleeding in (research/03 s4.8).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return r_gl.atlas;
}

void r_backend_texture_upload_r8(u32 texture, u32 atlas_size, const u8 *pixels, u32 x, u32 y,
                                 u32 width, u32 height) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // GL_UNPACK_ROW_LENGTH lets one call read a sub rectangle straight out of
    // the atlas buffer: no staging copy, no upload per row.
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)atlas_size);
    glTexSubImage2D(GL_TEXTURE_2D, 0, (GLint)x, (GLint)y, (GLsizei)width, (GLsizei)height, GL_RED,
                    GL_UNSIGNED_BYTE, pixels + (u64)y * atlas_size + x);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

R_Vertex *r_backend_map_vertices(u32 quad_count) {
    AssertAlways(quad_count <= R_GL_REGION_QUADS);
    if (!r_gl.persistent) { return 0; }
    r_gl.region = (r_gl.region + 1) % R_GL_REGIONS;
    GLsync fence = r_gl.fences[r_gl.region];
    if (fence) {
        // The region was last used two frames ago: in practice the fence is
        // already signalled and this costs nothing, but it is what makes
        // writing into mapped memory safe.
        glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        glDeleteSync(fence);
        r_gl.fences[r_gl.region] = 0;
    }
    return r_gl.mapped + (u64)r_gl.region * R_GL_REGION_QUADS * 4;
}

void r_backend_draw(const R_Frame *frame) {
    GLsizei width = (GLsizei)frame->viewport.x;
    GLsizei height = (GLsizei)frame->viewport.y;
    glViewport(0, 0, width, height);
    glScissor(0, 0, width, height);
    glClearColor((f32)(frame->clear_color & 0xFF) / 255.0f,
                 (f32)((frame->clear_color >> 8) & 0xFF) / 255.0f,
                 (f32)((frame->clear_color >> 16) & 0xFF) / 255.0f,
                 (f32)((frame->clear_color >> 24) & 0xFF) / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (frame->quad_count) {
        glUseProgram(r_gl.program);
        glUniform2f(r_gl.u_viewport, frame->viewport.x, frame->viewport.y);
        glUniform1i(r_gl.u_atlas, 0);
        glUniform1f(r_gl.u_text_gamma, R_TEXT_GAMMA);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(r_gl.vao);
        glBindBuffer(GL_ARRAY_BUFFER, r_gl.vbo);

        GLint region_base = 0;
        if (r_gl.persistent) {
            // The vertices are already in the buffer: r_core wrote them there.
            region_base = (GLint)(r_gl.region * R_GL_REGION_QUADS * 4);
        } else {
            // Orphaning: the driver hands us fresh storage instead of stalling
            // on the frame the GPU may still be reading (s4.7 option B).
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)R_GL_REGION_BYTES, 0, GL_STREAM_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            (GLsizeiptr)((u64)frame->quad_count * 4 * sizeof(R_Vertex)),
                            frame->vertices);
        }

        for (u32 i = 0; i < frame->batch_count; i += 1) {
            const R_Batch *batch = &frame->batches[i];
            glBindTexture(GL_TEXTURE_2D, batch->texture ? batch->texture : r_gl.atlas);
            // GL scissor counts from the bottom left, our rects from the top left.
            GLint x = (GLint)batch->clip.min.x;
            GLint y = (GLint)(frame->viewport.y - batch->clip.max.y);
            glScissor(x, y, (GLsizei)rect_width(batch->clip), (GLsizei)rect_height(batch->clip));
            glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)(batch->quad_count * 6),
                                     GL_UNSIGNED_SHORT, 0,
                                     region_base + (GLint)(batch->quad_first * 4));
        }

        if (r_gl.persistent) {
            r_gl.fences[r_gl.region] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        }
    }
    os_gl_swap();
}

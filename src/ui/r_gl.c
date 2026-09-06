// r_gl.c - the OpenGL 3.3 back end of r_core: one program, one VAO, one
// dynamic VBO, a static index buffer, one white texel standing in for the atlas.
#include "gl_loader.h"
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

typedef struct R_GlState {
    GLuint program;
    GLuint vao;
    GLuint vbo;
    GLuint ibo;
    GLuint white;
    GLint u_viewport;
    GLint u_atlas;
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
    return 1;
}

// Quad pattern 0,1,2, 2,1,3 written once for the whole VBO and never touched again.
static void r_gl_build_index_buffer(void) {
    ArenaTemp scratch = scratch_begin(0, 0);
    u16 *indices = push_array(scratch.arena, u16, R_MAX_QUADS * 6);
    for (u32 quad = 0; quad < R_MAX_QUADS; quad += 1) {
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
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((u64)R_MAX_QUADS * 6 * sizeof(u16)), indices,
                 GL_STATIC_DRAW);
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

static b32 r_gl_init(void) {
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
    glGenBuffers(1, &r_gl.vbo);
    r_gl_setup_vertex_layout();
    r_gl_build_index_buffer();

    // One opaque white texel: the sampler is always complete, even though no
    // atlas exists yet, so the shader keeps a single code path.
    u32 white_pixel = 0xFFFFFFFFu;
    glGenTextures(1, &r_gl.white);
    glBindTexture(GL_TEXTURE_2D, r_gl.white);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white_pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    return 1;
}

static void r_gl_shutdown(void) {
    glDeleteTextures(1, &r_gl.white);
    glDeleteBuffers(1, &r_gl.ibo);
    glDeleteBuffers(1, &r_gl.vbo);
    glDeleteVertexArrays(1, &r_gl.vao);
    glDeleteProgram(r_gl.program);
}

static void r_gl_draw(const R_Frame *frame) {
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
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(r_gl.vao);

        // Orphaning: the driver hands us fresh storage instead of stalling on
        // the frame the GPU may still be reading (research/03 s4.7 option B).
        GLsizeiptr used = (GLsizeiptr)((u64)frame->quad_count * 4 * sizeof(R_Vertex));
        glBindBuffer(GL_ARRAY_BUFFER, r_gl.vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(frame->vertices), 0, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, used, frame->vertices);

        // NOLINTBEGIN(performance-no-int-to-ptr) the index offset is a byte
        // count that GL insists on receiving as a pointer.
        for (u32 i = 0; i < frame->batch_count; i += 1) {
            const R_Batch *batch = &frame->batches[i];
            if (batch->index_count == 0) { continue; }
            glBindTexture(GL_TEXTURE_2D, batch->texture ? batch->texture : r_gl.white);
            // GL scissor counts from the bottom left, our rects from the top left.
            GLint x = (GLint)batch->clip.min.x;
            GLint y = (GLint)(frame->viewport.y - batch->clip.max.y);
            glScissor(x, y, (GLsizei)rect_width(batch->clip), (GLsizei)rect_height(batch->clip));
            glDrawElements(GL_TRIANGLES, (GLsizei)batch->index_count, GL_UNSIGNED_SHORT,
                           (const void *)((u64)batch->index_first * sizeof(u16)));
        }
        // NOLINTEND(performance-no-int-to-ptr)
    }
    os_gl_swap();
}

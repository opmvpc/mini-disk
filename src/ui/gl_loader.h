// gl_loader.h - the ~50 GL 3.3 core entry points we use, loaded by hand.
// No OS header, no GL/gl.h: ui/ only talks to platform.h (ADR-001). The types
// and enums below are the ABI of GL itself, they are not going to change.
#ifndef GL_LOADER_H
#define GL_LOADER_H

#include "../base/base.h"

typedef u32  GLenum;
typedef u32  GLbitfield;
typedef u32  GLuint;
typedef i32  GLint;
typedef i32  GLsizei;
typedef u8   GLboolean;
typedef u8   GLubyte;
typedef char GLchar;
typedef f32  GLfloat;
typedef i64  GLintptr;
typedef i64  GLsizeiptr;
typedef u64  GLuint64;
typedef void *GLsync;

#define GL_CALL __stdcall

typedef void(GL_CALL *GLDEBUGPROC)(GLenum source, GLenum type, GLuint id, GLenum severity,
                                   GLsizei length, const GLchar *message, const void *user);

// --- constants -------------------------------------------------------------
#define GL_FALSE                          0
#define GL_TRUE                           1
#define GL_TRIANGLES                      0x0004
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_FLOAT                          0x1406
#define GL_RGBA                           0x1908
#define GL_RGBA8                          0x8058
#define GL_TEXTURE_2D                     0x0DE1
#define GL_TEXTURE0                       0x84C0
#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_MIN_FILTER             0x2801
#define GL_TEXTURE_WRAP_S                 0x2802
#define GL_TEXTURE_WRAP_T                 0x2803
#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_UNPACK_ALIGNMENT               0x0CF5
#define GL_UNPACK_ROW_LENGTH              0x0CF2
#define GL_COLOR_BUFFER_BIT               0x00004000
#define GL_BLEND                          0x0BE2
#define GL_SCISSOR_TEST                   0x0C11
#define GL_ONE                            1
#define GL_ONE_MINUS_SRC_ALPHA            0x0303
#define GL_ARRAY_BUFFER                   0x8892
#define GL_ELEMENT_ARRAY_BUFFER           0x8893
#define GL_STATIC_DRAW                    0x88E4
#define GL_STREAM_DRAW                    0x88E0
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_VERTEX_SHADER                  0x8B31
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84
#define GL_VENDOR                         0x1F00
#define GL_RENDERER                       0x1F01
#define GL_VERSION                        0x1F02
#define GL_NO_ERROR                       0
#define GL_DEBUG_OUTPUT                   0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS       0x8242
#define GL_DEBUG_SEVERITY_NOTIFICATION    0x826B
#define GL_DEBUG_SEVERITY_HIGH            0x9146
#define GL_DONT_CARE                      0x1100
#define GL_R8                             0x8229
#define GL_RED                            0x1903
#define GL_MAX_TEXTURE_SIZE               0x0D33
#define GL_NUM_EXTENSIONS                 0x821D
#define GL_EXTENSIONS                     0x1F03
#define GL_MAP_READ_BIT                   0x0001
#define GL_MAP_WRITE_BIT                  0x0002
#define GL_MAP_PERSISTENT_BIT             0x0040
#define GL_MAP_COHERENT_BIT               0x0080
#define GL_DYNAMIC_STORAGE_BIT            0x0100
#define GL_SYNC_GPU_COMMANDS_COMPLETE     0x9117
#define GL_SYNC_FLUSH_COMMANDS_BIT        0x00000001
#define GL_ALREADY_SIGNALED               0x911A
#define GL_TIMEOUT_EXPIRED                0x911B
#define GL_CONDITION_SATISFIED            0x911C
#define GL_WAIT_FAILED                    0x911D

// --- entry points ----------------------------------------------------------
// GL 1.1 lives in opengl32.dll, the rest comes from the driver; os_gl_get_proc
// hides the difference. GL_FUNCS_OPTIONAL may legitimately be absent (KHR_debug).
#define GL_FUNCS                                                                                  \
    X(void, glClear, (GLbitfield mask))                                                           \
    X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                           \
    X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))                                 \
    X(void, glScissor, (GLint x, GLint y, GLsizei w, GLsizei h))                                  \
    X(void, glEnable, (GLenum cap))                                                               \
    X(void, glDisable, (GLenum cap))                                                              \
    X(GLenum, glGetError, (void))                                                                 \
    X(const GLubyte *, glGetString, (GLenum name))                                                \
    X(void, glPixelStorei, (GLenum name, GLint param))                                            \
    X(void, glGenTextures, (GLsizei n, GLuint * textures))                                        \
    X(void, glDeleteTextures, (GLsizei n, const GLuint *textures))                                \
    X(void, glBindTexture, (GLenum target, GLuint texture))                                       \
    X(void, glTexParameteri, (GLenum target, GLenum name, GLint param))                           \
    X(void, glTexImage2D,                                                                         \
      (GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,            \
       GLenum format, GLenum type, const void *pixels))                                           \
    X(void, glBlendFuncSeparate, (GLenum sc, GLenum dc, GLenum sa, GLenum da))                    \
    X(void, glActiveTexture, (GLenum unit))                                                       \
    X(void, glGenBuffers, (GLsizei n, GLuint * buffers))                                          \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint *buffers))                                  \
    X(void, glBindBuffer, (GLenum target, GLuint buffer))                                         \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage))       \
    X(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void *data)) \
    X(void, glGenVertexArrays, (GLsizei n, GLuint * arrays))                                      \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint *arrays))                              \
    X(void, glBindVertexArray, (GLuint array))                                                    \
    X(void, glEnableVertexAttribArray, (GLuint index))                                            \
    X(void, glVertexAttribPointer,                                                                \
      (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,               \
       const void *pointer))                                                                      \
    X(void, glVertexAttribIPointer,                                                               \
      (GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer))               \
    X(GLuint, glCreateShader, (GLenum type))                                                      \
    X(void, glShaderSource,                                                                       \
      (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length))           \
    X(void, glCompileShader, (GLuint shader))                                                     \
    X(void, glGetShaderiv, (GLuint shader, GLenum name, GLint * params))                          \
    X(void, glGetShaderInfoLog, (GLuint shader, GLsizei max, GLsizei * length, GLchar * log))     \
    X(void, glDeleteShader, (GLuint shader))                                                      \
    X(GLuint, glCreateProgram, (void))                                                            \
    X(void, glAttachShader, (GLuint program, GLuint shader))                                      \
    X(void, glLinkProgram, (GLuint program))                                                      \
    X(void, glGetProgramiv, (GLuint program, GLenum name, GLint * params))                        \
    X(void, glGetProgramInfoLog, (GLuint program, GLsizei max, GLsizei * length, GLchar * log))   \
    X(void, glUseProgram, (GLuint program))                                                       \
    X(void, glDeleteProgram, (GLuint program))                                                    \
    X(GLint, glGetUniformLocation, (GLuint program, const GLchar *name))                          \
    X(void, glUniform1i, (GLint location, GLint v0))                                              \
    X(void, glUniform2f, (GLint location, GLfloat v0, GLfloat v1))                                \
    X(void, glGetIntegerv, (GLenum name, GLint * params))                                         \
    X(const GLubyte *, glGetStringi, (GLenum name, GLuint index))                                 \
    X(void, glTexSubImage2D,                                                                      \
      (GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,         \
       GLenum type, const void *pixels))                                                          \
    X(void, glDrawElementsBaseVertex,                                                             \
      (GLenum mode, GLsizei count, GLenum type, const void *indices, GLint base_vertex))          \
    X(void *, glMapBufferRange,                                                                   \
      (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access))                     \
    X(GLboolean, glUnmapBuffer, (GLenum target))                                                  \
    X(GLsync, glFenceSync, (GLenum condition, GLbitfield flags))                                  \
    X(GLenum, glClientWaitSync, (GLsync sync, GLbitfield flags, GLuint64 timeout))                \
    X(void, glDeleteSync, (GLsync sync))

#define GL_FUNCS_OPTIONAL                                                                    \
    X(void, glDebugMessageCallback, (GLDEBUGPROC callback, const void *user))                \
    X(void, glDebugMessageControl, (GLenum source, GLenum type, GLenum severity, GLsizei n,  \
                                    const GLuint *ids, GLboolean enabled))                   \
    X(void, glBufferStorage,                                                                 \
      (GLenum target, GLsizeiptr size, const void *data, GLbitfield flags))

// NOLINTNEXTLINE(bugprone-macro-parentheses) `args` is a parameter list, not an expression
#define X(ret, name, args) typedef ret(GL_CALL *name##_fn) args; extern name##_fn name;
GL_FUNCS
GL_FUNCS_OPTIONAL
#undef X

// Loads everything through os_gl_get_proc. Returns the number of *required*
// entry points the driver did not provide: 0 means the renderer can run.
u32 gl_loader_init(void);

#endif // GL_LOADER_H

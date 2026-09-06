// gl_loader.c - the X-macro made flesh: one pointer per entry point.
#include "gl_loader.h"

#include "../platform/platform.h"

#define X(ret, name, args) name##_fn name;
GL_FUNCS
GL_FUNCS_OPTIONAL
#undef X

// Slots and names in the same order: loading is a loop over two tables rather
// than fifty inlined copies of the same three instructions (2.5 KB of exe).
static void **const gl_slots[] = {
#define X(ret, name, args) (void **)&name,  // NOLINT(bugprone-macro-parentheses)
    GL_FUNCS GL_FUNCS_OPTIONAL
#undef X
};

static const char gl_names[] =
#define X(ret, name, args) #name "\0"
        GL_FUNCS GL_FUNCS_OPTIONAL
#undef X
        ;

#define X(ret, name, args) +1  // NOLINT(bugprone-macro-parentheses)
static const u32 gl_required_count = 0 GL_FUNCS;
#undef X

u32 gl_loader_init(void) {
    const char *name = gl_names;
    u32 missing = 0;
    for (u32 i = 0; i < ArrayCount(gl_slots); i += 1) {
        void *proc = os_gl_get_proc(name);
        *gl_slots[i] = proc;
        if (proc == 0 && i < gl_required_count) { missing += 1; }
        while (*name) { name += 1; }
        name += 1;
    }
    return missing;
}

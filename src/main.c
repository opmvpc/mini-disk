// main.c - unity build: the single translation unit holding all of our code (ADR-002).
#include "base/base.h"
#include "base/base_arena.h"
#include "base/base_string.h"
#include "base/base_math.h"
#include "base/base_hash.h"
#include "platform/platform.h"
#include "ui/gl_loader.h"
#include "ui/r_core.h"

#if BUILD_NO_CRT
#include "base/base_crt_stubs.c"
#endif

#include "base/base_arena.c"
#include "base/base_string.c"
#include "base/base_math.c"
#include "base/base_hash.c"

#include "platform/win32/win32_platform.c"
#include "platform/win32/win32_window.c"
#include "platform/win32/win32_gl.c"

#include "ui/gl_loader.c"
#include "ui/r_core.c"
#include "ui/r_gl.c"

#include "app/app.c"

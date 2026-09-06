// win32_gl.c - WGL: dummy window, 3.3 core context, vsync, proc loading.
// opengl32.dll and gdi32.dll are loaded by hand: the import table stays
// kernel32 + user32 (ADR-002). Nothing here knows about the renderer.
#include <windows.h>

#include "../platform.h"

// --- WGL / pixel format constants (we do not include GL/gl.h) --------------
#define WGL_DRAW_TO_WINDOW_ARB              0x2001
#define WGL_ACCELERATION_ARB                0x2003
#define WGL_SUPPORT_OPENGL_ARB              0x2010
#define WGL_DOUBLE_BUFFER_ARB               0x2011
#define WGL_PIXEL_TYPE_ARB                  0x2013
#define WGL_COLOR_BITS_ARB                  0x2014
#define WGL_ALPHA_BITS_ARB                  0x201B
#define WGL_DEPTH_BITS_ARB                  0x2022
#define WGL_STENCIL_BITS_ARB                0x2023
#define WGL_FULL_ACCELERATION_ARB           0x2027
#define WGL_TYPE_RGBA_ARB                   0x202B
#define WGL_SAMPLE_BUFFERS_ARB              0x2041
#define WGL_CONTEXT_MAJOR_VERSION_ARB       0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB       0x2092
#define WGL_CONTEXT_FLAGS_ARB               0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB        0x9126
#define WGL_CONTEXT_DEBUG_BIT_ARB           0x0001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x0002
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB    0x0001

typedef HGLRC(WINAPI *Win32wglCreateContext)(HDC);
typedef BOOL(WINAPI *Win32wglDeleteContext)(HGLRC);
typedef BOOL(WINAPI *Win32wglMakeCurrent)(HDC, HGLRC);
typedef PROC(WINAPI *Win32wglGetProcAddress)(LPCSTR);
typedef HGLRC(WINAPI *Win32wglCreateContextAttribsARB)(HDC, HGLRC, const int *);
typedef BOOL(WINAPI *Win32wglChoosePixelFormatARB)(HDC, const int *, const FLOAT *, UINT, int *,
                                                   UINT *);
typedef BOOL(WINAPI *Win32wglSwapIntervalEXT)(int);

typedef int(WINAPI *Win32ChoosePixelFormat)(HDC, const PIXELFORMATDESCRIPTOR *);
typedef int(WINAPI *Win32DescribePixelFormat)(HDC, int, UINT, PIXELFORMATDESCRIPTOR *);
typedef BOOL(WINAPI *Win32SetPixelFormat)(HDC, int, const PIXELFORMATDESCRIPTOR *);
typedef BOOL(WINAPI *Win32SwapBuffers)(HDC);

typedef struct Win32GlState {
    HMODULE opengl32;
    HMODULE gdi32;
    HDC dc;              // the real window's DC, owned for good (CS_OWNDC)
    HGLRC rc;

    Win32wglCreateContext wglCreateContext_;
    Win32wglDeleteContext wglDeleteContext_;
    Win32wglMakeCurrent wglMakeCurrent_;
    Win32wglGetProcAddress wglGetProcAddress_;
    Win32wglCreateContextAttribsARB wglCreateContextAttribsARB_;
    Win32wglChoosePixelFormatARB wglChoosePixelFormatARB_;
    Win32wglSwapIntervalEXT wglSwapIntervalEXT_;

    Win32ChoosePixelFormat ChoosePixelFormat_;
    Win32DescribePixelFormat DescribePixelFormat_;
    Win32SetPixelFormat SetPixelFormat_;
    Win32SwapBuffers SwapBuffers_;
} Win32GlState;

global Win32GlState win32_gl;

// Some drivers answer 1, 2, 3 or -1 instead of 0 for "not there".
static void *win32_gl_wgl_proc(const char *name) {
    void *proc = (void *)win32_gl.wglGetProcAddress_(name);
    i64 value = (i64)proc;
    return (value >= -1 && value <= 3) ? 0 : proc;
}

// Step one of the WGL dance: a throwaway window and a legacy context, only to
// reach the ARB entry points. Torn down completely before the real one.
static b32 win32_gl_load_wgl_extensions(void) {
    WNDCLASSEXW window_class;
    StructZero(&window_class);
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = win32_state.instance;
    window_class.lpszClassName = L"minidisk_gl_dummy";
    RegisterClassExW(&window_class);

    HWND window = CreateWindowExW(0, window_class.lpszClassName, L"", 0, CW_USEDEFAULT,
                                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
                                  win32_state.instance, 0);
    HDC dc = GetDC(window);

    PIXELFORMATDESCRIPTOR pfd;
    StructZero(&pfd);
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;

    b32 ok = 0;
    int format = win32_gl.ChoosePixelFormat_(dc, &pfd);
    if (format != 0) {
        win32_gl.DescribePixelFormat_(dc, format, sizeof(pfd), &pfd);
        win32_gl.SetPixelFormat_(dc, format, &pfd);
        HGLRC rc = win32_gl.wglCreateContext_(dc);
        if (rc != 0) {
            win32_gl.wglMakeCurrent_(dc, rc);
            win32_gl.wglCreateContextAttribsARB_ =
                    (Win32wglCreateContextAttribsARB)win32_gl_wgl_proc("wglCreateContextAttribsARB");
            win32_gl.wglChoosePixelFormatARB_ =
                    (Win32wglChoosePixelFormatARB)win32_gl_wgl_proc("wglChoosePixelFormatARB");
            win32_gl.wglSwapIntervalEXT_ =
                    (Win32wglSwapIntervalEXT)win32_gl_wgl_proc("wglSwapIntervalEXT");
            ok = win32_gl.wglCreateContextAttribsARB_ != 0 && win32_gl.wglChoosePixelFormatARB_ != 0;
            win32_gl.wglMakeCurrent_(0, 0);
            win32_gl.wglDeleteContext_(rc);
        }
    }
    ReleaseDC(window, dc);
    DestroyWindow(window);
    UnregisterClassW(window_class.lpszClassName, win32_state.instance);
    return ok;
}

static b32 win32_gl_load_modules(void) {
    win32_gl.opengl32 = LoadLibraryW(L"opengl32.dll");
    win32_gl.gdi32 = LoadLibraryW(L"gdi32.dll");
    if (win32_gl.opengl32 == 0 || win32_gl.gdi32 == 0) { return 0; }

    win32_gl.wglCreateContext_ =
            (Win32wglCreateContext)GetProcAddress(win32_gl.opengl32, "wglCreateContext");
    win32_gl.wglDeleteContext_ =
            (Win32wglDeleteContext)GetProcAddress(win32_gl.opengl32, "wglDeleteContext");
    win32_gl.wglMakeCurrent_ =
            (Win32wglMakeCurrent)GetProcAddress(win32_gl.opengl32, "wglMakeCurrent");
    win32_gl.wglGetProcAddress_ =
            (Win32wglGetProcAddress)GetProcAddress(win32_gl.opengl32, "wglGetProcAddress");
    win32_gl.ChoosePixelFormat_ =
            (Win32ChoosePixelFormat)GetProcAddress(win32_gl.gdi32, "ChoosePixelFormat");
    win32_gl.DescribePixelFormat_ =
            (Win32DescribePixelFormat)GetProcAddress(win32_gl.gdi32, "DescribePixelFormat");
    win32_gl.SetPixelFormat_ = (Win32SetPixelFormat)GetProcAddress(win32_gl.gdi32, "SetPixelFormat");
    win32_gl.SwapBuffers_ = (Win32SwapBuffers)GetProcAddress(win32_gl.gdi32, "SwapBuffers");
    return win32_gl.wglCreateContext_ != 0 && win32_gl.wglGetProcAddress_ != 0 &&
           win32_gl.SwapBuffers_ != 0;
}

b32 os_gl_init(OsWindow window) {
    if (!win32_gl_load_modules()) {
        os_debug_print(str8_lit("opengl: opengl32.dll or gdi32.dll unavailable\n"));
        return 0;
    }
    if (!win32_gl_load_wgl_extensions()) {
        os_debug_print(str8_lit("opengl: WGL_ARB_create_context missing, driver too old\n"));
        return 0;
    }

    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    HWND handle = (HWND)window.v;
    HDC dc = GetDC(handle);  // CS_OWNDC: this DC is ours until the window dies
    win32_gl.dc = dc;

    const int format_attribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, 1,
        WGL_SUPPORT_OPENGL_ARB, 1,
        WGL_DOUBLE_BUFFER_ARB,  1,
        WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
        WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB,     24,
        WGL_ALPHA_BITS_ARB,     8,
        WGL_DEPTH_BITS_ARB,     0,  // 2D only: no depth
        WGL_STENCIL_BITS_ARB,   0,  // clipping is done with scissor
        WGL_SAMPLE_BUFFERS_ARB, 0,  // the SDF antialiases analytically
        0,
    };
    int format = 0;
    UINT format_count = 0;
    win32_gl.wglChoosePixelFormatARB_(dc, format_attribs, 0, 1, &format, &format_count);
    if (format_count == 0) {
        os_debug_print(str8_lit("opengl: no hardware accelerated pixel format\n"));
        return 0;
    }
    PIXELFORMATDESCRIPTOR pfd;
    win32_gl.DescribePixelFormat_(dc, format, sizeof(pfd), &pfd);
    win32_gl.SetPixelFormat_(dc, format, &pfd);

    const int context_attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        WGL_CONTEXT_FLAGS_ARB,
#if BUILD_DEBUG
        WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB | WGL_CONTEXT_DEBUG_BIT_ARB,
#else
        WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
#endif
        0,
    };
    HGLRC rc = win32_gl.wglCreateContextAttribsARB_(dc, 0, context_attribs);
    if (rc == 0) {
        os_debug_print(str8_lit("opengl: no 3.3 core context, driver too old\n"));
        return 0;
    }
    win32_gl.rc = rc;
    win32_gl.wglMakeCurrent_(dc, rc);
    if (win32_gl.wglSwapIntervalEXT_) { win32_gl.wglSwapIntervalEXT_(1); }
    return 1;
}

void os_gl_shutdown(void) {
    win32_gl.wglMakeCurrent_(0, 0);
    win32_gl.wglDeleteContext_(win32_gl.rc);
    win32_gl.rc = 0;
    FreeLibrary(win32_gl.opengl32);
    FreeLibrary(win32_gl.gdi32);
}

void os_gl_swap(void) { win32_gl.SwapBuffers_(win32_gl.dc); }

// opengl32.dll only exports GL 1.1; everything newer comes from the driver
// through wglGetProcAddress, which in turn returns nothing for GL 1.1.
void *os_gl_get_proc(const char *name) {
    void *proc = win32_gl_wgl_proc(name);
    if (proc == 0) { proc = (void *)GetProcAddress(win32_gl.opengl32, name); }
    return proc;
}

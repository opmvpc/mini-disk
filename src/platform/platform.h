// platform.h - the only contract towards the OS (research/03 s10.2).
// T-001 implements the subset below; the rest of the sketch lands with later tickets.
#ifndef PLATFORM_H
#define PLATFORM_H

#include "../base/base.h"
#include "../base/base_math.h"
#include "../base/base_string.h"

// --- lifecycle -------------------------------------------------------------
void os_init(void);
no_return void os_exit(i32 code);

// --- virtual memory --------------------------------------------------------
void *os_memory_reserve(u64 size);
b32   os_memory_commit(void *ptr, u64 size);
void  os_memory_release(void *ptr, u64 size);
u64   os_page_size(void);

// --- files -----------------------------------------------------------------
String8 os_file_read_all(Arena *arena, String8 path);   // size 0 when unreadable
b32     os_file_write_all(String8 path, String8 data);

// --- time and threads ------------------------------------------------------
u64 os_time_now_us(void);      // monotonic, microseconds
void os_sleep_us(u64 us);
u32 os_thread_current_id(void);

// --- keys ------------------------------------------------------------------
// Positional: derived from the hardware scancode, so OsKey_Q is the key at the
// physical Q position whatever the layout says. OsEvent also carries the raw
// virtual key, which is what layout dependent shortcuts (Ctrl+F...) must use.
typedef enum OsKey {
    OsKey_None = 0,
    OsKey_A, OsKey_B, OsKey_C, OsKey_D, OsKey_E, OsKey_F, OsKey_G, OsKey_H, OsKey_I,
    OsKey_J, OsKey_K, OsKey_L, OsKey_M, OsKey_N, OsKey_O, OsKey_P, OsKey_Q, OsKey_R,
    OsKey_S, OsKey_T, OsKey_U, OsKey_V, OsKey_W, OsKey_X, OsKey_Y, OsKey_Z,
    OsKey_0, OsKey_1, OsKey_2, OsKey_3, OsKey_4,
    OsKey_5, OsKey_6, OsKey_7, OsKey_8, OsKey_9,
    OsKey_Escape, OsKey_Enter, OsKey_Tab, OsKey_Backspace, OsKey_Space,
    OsKey_Minus, OsKey_Equal, OsKey_LeftBracket, OsKey_RightBracket, OsKey_Backslash,
    OsKey_Semicolon, OsKey_Quote, OsKey_Grave, OsKey_Comma, OsKey_Period, OsKey_Slash,
    OsKey_CapsLock,
    OsKey_F1, OsKey_F2, OsKey_F3, OsKey_F4, OsKey_F5, OsKey_F6,
    OsKey_F7, OsKey_F8, OsKey_F9, OsKey_F10, OsKey_F11, OsKey_F12,
    OsKey_ScrollLock, OsKey_Pause,
    OsKey_Insert, OsKey_Delete, OsKey_Home, OsKey_End, OsKey_PageUp, OsKey_PageDown,
    OsKey_Left, OsKey_Right, OsKey_Up, OsKey_Down,
    OsKey_NumLock, OsKey_NumpadDivide, OsKey_NumpadMultiply, OsKey_NumpadMinus,
    OsKey_NumpadPlus, OsKey_NumpadEnter, OsKey_NumpadDecimal,
    OsKey_Numpad0, OsKey_Numpad1, OsKey_Numpad2, OsKey_Numpad3, OsKey_Numpad4,
    OsKey_Numpad5, OsKey_Numpad6, OsKey_Numpad7, OsKey_Numpad8, OsKey_Numpad9,
    OsKey_LeftShift, OsKey_RightShift, OsKey_LeftCtrl, OsKey_RightCtrl,
    OsKey_LeftAlt, OsKey_RightAlt, OsKey_LeftSuper, OsKey_RightSuper, OsKey_Menu,
    OsKey_COUNT
} OsKey;

typedef enum OsMod {
    OsMod_Ctrl  = 1 << 0,
    OsMod_Shift = 1 << 1,
    OsMod_Alt   = 1 << 2,
    OsMod_Super = 1 << 3,
} OsMod;

typedef enum OsMouseButton {
    OsMouseButton_Left = 0,
    OsMouseButton_Right,
    OsMouseButton_Middle,
    OsMouseButton_X1,
    OsMouseButton_X2,
    OsMouseButton_COUNT
} OsMouseButton;

typedef enum OsCursor {
    OsCursor_Arrow = 0,
    OsCursor_IBeam,
    OsCursor_Hand,
    OsCursor_ResizeH,
    OsCursor_ResizeV,
    OsCursor_Forbidden,
    OsCursor_Wait,
    OsCursor_COUNT
} OsCursor;

// --- events ----------------------------------------------------------------
typedef enum OsEventKind {
    OsEvent_None = 0,
    OsEvent_Close,
    OsEvent_KeyDown,
    OsEvent_KeyUp,
    OsEvent_Char,
    OsEvent_MouseMove,
    OsEvent_MouseDown,
    OsEvent_MouseUp,
    OsEvent_Wheel,
    OsEvent_Resize,
    OsEvent_DpiChanged,
    OsEvent_FocusGain,
    OsEvent_FocusLose,
    OsEvent_DropFiles,
    OsEvent_DeviceChange,
    OsEvent_COUNT
} OsEventKind;

// Tagged union in the loose sense: one flat struct, `kind` says which fields
// carry meaning. Fixed size so the queue is a plain ring buffer.
typedef struct OsEvent {
    OsEventKind kind;
    u32 key;          // OsKey, positional (KeyDown/KeyUp)
    u32 vk;           // Win32 virtual key, layout dependent (KeyDown/KeyUp)
    u32 scancode;     // set 1 scancode, extended keys have bit 8 set
    u32 modifiers;    // OsMod mask
    u32 repeat;       // auto repeat count reported by the OS (KeyDown)
    u32 codepoint;    // Char: one full UTF-32 codepoint, surrogates recombined
    u32 button;       // OsMouseButton (MouseDown/MouseUp)
    u32 click_count;  // 1, 2 or 3 (MouseDown)
    V2 pos;           // physical pixels, client top left (mouse events)
    V2 wheel_lines;   // Wheel: (horizontal, vertical) in lines
    V2 wheel_pixels;  // Wheel: same delta expressed in physical pixels
    V2 size;          // Resize/DpiChanged: client size in physical pixels
    f32 dpi_scale;    // DpiChanged: new scale (1.0 = 96 dpi)
    String8 *paths;   // DropFiles: UTF-8 paths, allocated in the frame arena
    u64 path_count;
    u64 timestamp_us;
} OsEvent;

// --- window ----------------------------------------------------------------
typedef struct OsWindow { u64 v; } OsWindow;

#define OS_TIMEOUT_INFINITE U64_MAX

OsWindow os_window_create(String8 title, u32 width, u32 height);
void     os_window_destroy(OsWindow window);
V2       os_window_get_size(OsWindow window);      // client size, physical pixels
f32      os_window_dpi_scale(OsWindow window);
void     os_window_set_title(OsWindow window, String8 title);

// --- opengl ----------------------------------------------------------------
// opengl32.dll and gdi32.dll are loaded by hand so the import table stays
// kernel32 + user32. os_gl_init returns 0 when no 3.3 core context can be made
// (old driver, generic rasterizer): the caller reports and exits, no crash.
b32   os_gl_init(OsWindow window);
void  os_gl_shutdown(void);
void  os_gl_swap(void);              // vsync'd: wglSwapIntervalEXT(1)
void *os_gl_get_proc(const char *name);

// DropFiles paths are pushed here; the caller clears the arena once per frame,
// after it has consumed the events of that frame.
void os_events_set_frame_arena(Arena *arena);
// On demand loop: blocking waits on messages, on os_request_redraw from another
// thread, and on timeout_us (OS_TIMEOUT_INFINITE to wait forever). Never spins.
void os_events_pump(b32 blocking, u64 timeout_us);
b32  os_event_next(OsEvent *out);   // 0 when the queue is empty
void os_request_redraw(void);       // thread safe
b32  os_redraw_requested(void);

// --- clipboard and cursor --------------------------------------------------
String8 os_clipboard_get(Arena *arena);   // size 0 when there is no text
b32     os_clipboard_set(String8 text);
void    os_cursor_set(OsCursor cursor);

// --- diagnostics -----------------------------------------------------------
void os_debug_print(String8 s);

#endif // PLATFORM_H

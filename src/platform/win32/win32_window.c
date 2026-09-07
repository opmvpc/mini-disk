// win32_window.c - window class, WndProc, on demand event loop, DPI v2, clipboard.
// The WndProc only translates messages into OsEvent, never any application logic.
#include <windows.h>

#include "../platform.h"

// --- dynamically loaded entry points ---------------------------------------
// dwmapi and shell32 stay out of the import table: they are optional polish and
// loading them by hand keeps the exe's imports down to kernel32 + user32.
typedef HRESULT(WINAPI *Win32DwmSetWindowAttribute)(HWND, DWORD, const void *, DWORD);
typedef void(WINAPI *Win32DragAcceptFiles)(HWND, BOOL);
typedef UINT(WINAPI *Win32DragQueryFileW)(HANDLE, UINT, WCHAR *, UINT);
typedef void(WINAPI *Win32DragFinish)(HANDLE);
typedef BOOL(WINAPI *Win32DragQueryPoint)(HANDLE, POINT *);
// ole32, for the drop target of T-014: the shell only tells us where the
// cursor is *during* a drag through IDropTarget, and WM_DROPFILES only ever
// reports the drop itself.
typedef HRESULT(WINAPI *Win32OleInitialize)(void *reserved);
typedef HRESULT(WINAPI *Win32RegisterDragDrop)(HWND, void *drop_target);
typedef HRESULT(WINAPI *Win32RevokeDragDrop)(HWND);
typedef void(WINAPI *Win32ReleaseStgMedium)(void *medium);

// A dropped selection past this is not a music folder, it is a mistake: the
// paths land in the frame arena, so the boundary caps them here.
#define WIN32_DROP_MAX_PATHS 4096

#define WIN32_DWMWA_USE_IMMERSIVE_DARK_MODE     20
#define WIN32_DWMWA_USE_IMMERSIVE_DARK_MODE_OLD 19  // Win10 before 20H1
#define WIN32_DWMWA_WINDOW_CORNER_PREFERENCE    33
#define WIN32_DWMWA_BORDER_COLOR                34
#define WIN32_DWMWCP_ROUND                      2

#define WIN32_TIMER_RESIZE 1
#define WIN32_EVENT_RING_CAPACITY 512
#define WIN32_MIN_CLIENT_WIDTH  800
#define WIN32_MIN_CLIENT_HEIGHT 520

// --- event ring ------------------------------------------------------------
// Fixed capacity, power of two, allocated once in an arena. A full ring means
// the frame loop stopped draining: drop the newest event and assert in debug.
typedef struct Win32EventRing {
    OsEvent *events;
    u64 capacity;  // power of two
    u64 read;
    u64 write;
    u64 dropped;
} Win32EventRing;

static void win32_event_ring_init(Win32EventRing *ring, Arena *arena, u64 capacity) {
    Assert(IsPow2(capacity));
    ring->events = push_array_zero(arena, OsEvent, capacity);
    ring->capacity = capacity;
    ring->read = 0;
    ring->write = 0;
    ring->dropped = 0;
}

static b32 win32_event_ring_push(Win32EventRing *ring, const OsEvent *event) {
    if (ring->write - ring->read >= ring->capacity) {
        ring->dropped += 1;
        return 0;
    }
    ring->events[ring->write & (ring->capacity - 1)] = *event;
    ring->write += 1;
    return 1;
}

static b32 win32_event_ring_pop(Win32EventRing *ring, OsEvent *out) {
    if (ring->read == ring->write) { return 0; }
    *out = ring->events[ring->read & (ring->capacity - 1)];
    ring->read += 1;
    return 1;
}

// --- state -----------------------------------------------------------------
typedef struct Win32WindowState {
    HWND window;
    Arena *arena;         // permanent: the ring lives here
    Arena *frame_arena;   // per frame: DropFiles paths
    Win32EventRing ring;
    HANDLE wake_event;    // auto reset, signalled by os_request_redraw
    volatile LONG redraw_requested;

    f32 dpi_scale;
    u32 client_width;
    u32 client_height;
    u32 wheel_scroll_lines;
    u16 pending_high_surrogate;
    u32 mouse_capture_count;

    // Self counted multi click: WM_LBUTTONDBLCLK cannot report a triple click.
    u64 last_click_time_ms;
    POINT last_click_pos;
    u32 last_click_button;
    u32 click_count;

    HCURSOR cursors[OsCursor_COUNT];
    OsCursor cursor;

    HMODULE dwmapi;
    HMODULE shell32;
    HMODULE ole32;
    Win32OleInitialize OleInitialize_;
    Win32RegisterDragDrop RegisterDragDrop_;
    Win32RevokeDragDrop RevokeDragDrop_;
    Win32ReleaseStgMedium ReleaseStgMedium_;
    b32 drop_target_registered;  // 0: WM_DROPFILES is doing the work instead
    Win32DwmSetWindowAttribute DwmSetWindowAttribute_;
    Win32DragAcceptFiles DragAcceptFiles_;
    Win32DragQueryFileW DragQueryFileW_;
    Win32DragFinish DragFinish_;
    Win32DragQueryPoint DragQueryPoint_;
} Win32WindowState;

global Win32WindowState win32_window_state;

// --- scancode to key -------------------------------------------------------
// Set 1 scancodes as they arrive in bits 16..23 of lParam. Extended keys (the
// E0 prefix, bit 24) go through win32_key_from_extended_scancode instead.
global const u8 win32_scancode_to_key[128] = {
    /* 00 */ OsKey_None,   OsKey_Escape, OsKey_1,      OsKey_2,
    /* 04 */ OsKey_3,      OsKey_4,      OsKey_5,      OsKey_6,
    /* 08 */ OsKey_7,      OsKey_8,      OsKey_9,      OsKey_0,
    /* 0C */ OsKey_Minus,  OsKey_Equal,  OsKey_Backspace, OsKey_Tab,
    /* 10 */ OsKey_Q,      OsKey_W,      OsKey_E,      OsKey_R,
    /* 14 */ OsKey_T,      OsKey_Y,      OsKey_U,      OsKey_I,
    /* 18 */ OsKey_O,      OsKey_P,      OsKey_LeftBracket, OsKey_RightBracket,
    /* 1C */ OsKey_Enter,  OsKey_LeftCtrl, OsKey_A,    OsKey_S,
    /* 20 */ OsKey_D,      OsKey_F,      OsKey_G,      OsKey_H,
    /* 24 */ OsKey_J,      OsKey_K,      OsKey_L,      OsKey_Semicolon,
    /* 28 */ OsKey_Quote,  OsKey_Grave,  OsKey_LeftShift, OsKey_Backslash,
    /* 2C */ OsKey_Z,      OsKey_X,      OsKey_C,      OsKey_V,
    /* 30 */ OsKey_B,      OsKey_N,      OsKey_M,      OsKey_Comma,
    /* 34 */ OsKey_Period, OsKey_Slash,  OsKey_RightShift, OsKey_NumpadMultiply,
    /* 38 */ OsKey_LeftAlt, OsKey_Space, OsKey_CapsLock, OsKey_F1,
    /* 3C */ OsKey_F2,     OsKey_F3,     OsKey_F4,     OsKey_F5,
    /* 40 */ OsKey_F6,     OsKey_F7,     OsKey_F8,     OsKey_F9,
    /* 44 */ OsKey_F10,    OsKey_NumLock, OsKey_ScrollLock, OsKey_Numpad7,
    /* 48 */ OsKey_Numpad8, OsKey_Numpad9, OsKey_NumpadMinus, OsKey_Numpad4,
    /* 4C */ OsKey_Numpad5, OsKey_Numpad6, OsKey_NumpadPlus, OsKey_Numpad1,
    /* 50 */ OsKey_Numpad2, OsKey_Numpad3, OsKey_Numpad0, OsKey_NumpadDecimal,
    /* 54 */ OsKey_None,   OsKey_None,   OsKey_Backslash, OsKey_F11,
    /* 58 */ OsKey_F12,    OsKey_None,   OsKey_None,   OsKey_None,
    /* 5C */ OsKey_None,   OsKey_None,   OsKey_None,   OsKey_None,
    /* 60 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 70 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static OsKey win32_key_from_extended_scancode(u32 scancode) {
    switch (scancode) {
        case 0x1C: return OsKey_NumpadEnter;
        case 0x1D: return OsKey_RightCtrl;
        case 0x35: return OsKey_NumpadDivide;
        case 0x37: return OsKey_Pause;  // PrintScreen reports as E0 37
        case 0x38: return OsKey_RightAlt;
        case 0x45: return OsKey_Pause;
        case 0x47: return OsKey_Home;
        case 0x48: return OsKey_Up;
        case 0x49: return OsKey_PageUp;
        case 0x4B: return OsKey_Left;
        case 0x4D: return OsKey_Right;
        case 0x4F: return OsKey_End;
        case 0x50: return OsKey_Down;
        case 0x51: return OsKey_PageDown;
        case 0x52: return OsKey_Insert;
        case 0x53: return OsKey_Delete;
        case 0x5B: return OsKey_LeftSuper;
        case 0x5C: return OsKey_RightSuper;
        case 0x5D: return OsKey_Menu;
        default: return OsKey_None;
    }
}

// scancode carries the extended flag in bit 8, like our OsEvent.scancode.
static OsKey win32_key_from_scancode(u32 scancode) {
    if (scancode & 0x100) { return win32_key_from_extended_scancode(scancode & 0xFF); }
    return (OsKey)win32_scancode_to_key[scancode & 0x7F];
}

static u32 win32_scancode_from_lparam(LPARAM lparam) {
    u32 scancode = (u32)((lparam >> 16) & 0xFF);
    if (lparam & (1ll << 24)) { scancode |= 0x100; }
    return scancode;
}

static u32 win32_modifiers_now(void) {
    u32 modifiers = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) { modifiers |= OsMod_Ctrl; }
    if (GetKeyState(VK_SHIFT) & 0x8000) { modifiers |= OsMod_Shift; }
    if (GetKeyState(VK_MENU) & 0x8000) { modifiers |= OsMod_Alt; }
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) { modifiers |= OsMod_Super; }
    return modifiers;
}

// --- UTF-8 conversion for dropped paths ------------------------------------
// Boundary: the shell hands us UTF-16 that may contain unpaired surrogates.
// str8_from_str16 substitutes U+FFFD for those, which is what we want here.
static String8 win32_drop_path_to_utf8(Arena *arena, const u16 *path, u64 length) {
    String16 wide;
    wide.str = (u16 *)path;
    wide.size = length;
    return str8_from_str16(arena, wide);
}

// --- event helpers ---------------------------------------------------------
static OsEvent win32_event_make(OsEventKind kind) {
    OsEvent event;
    StructZero(&event);
    event.kind = kind;
    event.modifiers = win32_modifiers_now();
    event.timestamp_us = os_time_now_us();
    return event;
}

static void win32_event_push(const OsEvent *event) {
    // A full ring means the frame loop stopped draining: that is our bug, and
    // the ring itself stays consistent (the newest event is dropped, counted).
    b32 pushed = win32_event_ring_push(&win32_window_state.ring, event);
    Assert(pushed);
    Unused(pushed);
}

static V2 win32_mouse_pos_from_lparam(LPARAM lparam) {
    // The short cast is mandatory: coordinates go negative on multi monitor.
    return v2((f32)(i16)LOWORD(lparam), (f32)(i16)HIWORD(lparam));
}

static void win32_push_mouse_button(UINT message, WPARAM wparam, LPARAM lparam,
                                    OsMouseButton button, b32 down) {
    Unused(message);
    Win32WindowState *state = &win32_window_state;
    OsEvent event = win32_event_make(down ? OsEvent_MouseDown : OsEvent_MouseUp);
    event.button = button;
    event.pos = win32_mouse_pos_from_lparam(lparam);
    if (down) {
        POINT point;
        point.x = (LONG)event.pos.x;
        point.y = (LONG)event.pos.y;
        u64 now_ms = GetTickCount64();
        i32 slop_x = GetSystemMetrics(SM_CXDOUBLECLK);
        i32 slop_y = GetSystemMetrics(SM_CYDOUBLECLK);
        b32 chained = state->last_click_button == (u32)button &&
                      now_ms - state->last_click_time_ms <= GetDoubleClickTime() &&
                      Max(point.x, state->last_click_pos.x) -
                              Min(point.x, state->last_click_pos.x) <= slop_x &&
                      Max(point.y, state->last_click_pos.y) -
                              Min(point.y, state->last_click_pos.y) <= slop_y;
        state->click_count = chained ? Min(state->click_count + 1, 3u) : 1;
        state->last_click_time_ms = now_ms;
        state->last_click_pos = point;
        state->last_click_button = (u32)button;
        event.click_count = state->click_count;

        state->mouse_capture_count += 1;
        if (state->mouse_capture_count == 1) { SetCapture(state->window); }
    } else if (state->mouse_capture_count > 0) {
        state->mouse_capture_count -= 1;
        if (state->mouse_capture_count == 0) { ReleaseCapture(); }
    }
    Unused(wparam);
    win32_event_push(&event);
    os_request_redraw();
}

static void win32_push_wheel(WPARAM wparam, LPARAM lparam, b32 horizontal) {
    Win32WindowState *state = &win32_window_state;
    OsEvent event = win32_event_make(OsEvent_Wheel);
    // Wheel coordinates are in screen space, unlike every other mouse message.
    POINT point;
    point.x = (i16)LOWORD(lparam);
    point.y = (i16)HIWORD(lparam);
    ScreenToClient(state->window, &point);
    event.pos = v2((f32)point.x, (f32)point.y);

    f32 notches = (f32)(i16)HIWORD(wparam) / (f32)WHEEL_DELTA;
    f32 lines = notches * (f32)state->wheel_scroll_lines;
    // 20 physical pixels per line at 96 dpi, the value Explorer and browsers use.
    f32 pixels = lines * 20.0f * state->dpi_scale;
    if (horizontal) {
        event.wheel_lines = v2(lines, 0.0f);
        event.wheel_pixels = v2(pixels, 0.0f);
    } else {
        event.wheel_lines = v2(0.0f, lines);
        event.wheel_pixels = v2(0.0f, pixels);
    }
    win32_event_push(&event);
    os_request_redraw();
}

// `drop` is an HDROP, whether it came from WM_DROPFILES or out of the data
// object of a real OLE drop. Only the first owns it, so only the first finishes
// it. Both are boundaries: the count and every length come from another process.
static void win32_push_drop_files(HANDLE drop, b32 finish, const POINT *client_point) {
    Win32WindowState *state = &win32_window_state;
    OsEvent event = win32_event_make(OsEvent_DropFiles);
    POINT point;
    if (client_point) {
        event.pos = v2((f32)client_point->x, (f32)client_point->y);
    } else if (state->DragQueryPoint_ && state->DragQueryPoint_(drop, &point)) {
        event.pos = v2((f32)point.x, (f32)point.y);
    }
    u32 count = state->DragQueryFileW_(drop, 0xFFFFFFFFu, 0, 0);
    if (count > WIN32_DROP_MAX_PATHS) { count = WIN32_DROP_MAX_PATHS; }
    event.paths = push_array(state->frame_arena, String8, count);
    event.path_count = count;

    ArenaTemp scratch = scratch_begin(&state->frame_arena, 1);
    for (u32 i = 0; i < count; i += 1) {
        u32 length = state->DragQueryFileW_(drop, i, 0, 0);
        u16 *wide = push_array(scratch.arena, u16, (u64)length + 1);
        state->DragQueryFileW_(drop, i, (WCHAR *)wide, length + 1);
        event.paths[i] = win32_drop_path_to_utf8(state->frame_arena, wide, length);
    }
    scratch_end(scratch);

    if (finish) { state->DragFinish_(drop); }
    win32_event_push(&event);
    os_request_redraw();
}

// --- the drop target (T-014) -----------------------------------------------
// IDropTarget is four methods over IUnknown, so it is written out here rather
// than pulled in from ole2.h: the object is a global that never dies, its
// reference count is a formality, and the shell only ever calls it from the
// thread that owns the window.
typedef struct Win32DropTarget Win32DropTarget;
typedef struct Win32DropTargetVtbl {
    HRESULT(WINAPI *QueryInterface)(Win32DropTarget *self, const GUID *iid, void **out);
    ULONG(WINAPI *AddRef)(Win32DropTarget *self);
    ULONG(WINAPI *Release)(Win32DropTarget *self);
    HRESULT(WINAPI *DragEnter)(Win32DropTarget *self, void *data, DWORD keys, POINTL point,
                               DWORD *effect);
    HRESULT(WINAPI *DragOver)(Win32DropTarget *self, DWORD keys, POINTL point, DWORD *effect);
    HRESULT(WINAPI *DragLeave)(Win32DropTarget *self);
    HRESULT(WINAPI *Drop)(Win32DropTarget *self, void *data, DWORD keys, POINTL point,
                          DWORD *effect);
} Win32DropTargetVtbl;
struct Win32DropTarget { const Win32DropTargetVtbl *lpVtbl; };

// FORMATETC and STGMEDIUM live in objidl.h, which WIN32_LEAN_AND_MEAN keeps
// out: the two are plain structs, and the layout is the ABI.
typedef struct Win32FormatEtc {
    u16 cfFormat;
    void *ptd;
    DWORD dwAspect;
    LONG lindex;
    DWORD tymed;
} Win32FormatEtc;

typedef struct Win32StgMedium {
    DWORD tymed;
    void *handle;  // the union: hGlobal for TYMED_HGLOBAL, which is all we ask
    void *unknown_for_release;
} Win32StgMedium;

#define WIN32_DVASPECT_CONTENT 1u
#define WIN32_TYMED_HGLOBAL    1u

// IDataObject, down to the one method we call.
typedef struct Win32DataObject Win32DataObject;
typedef struct Win32DataObjectVtbl {
    void *slots_unknown[3];
    HRESULT(WINAPI *GetData)(Win32DataObject *self, const Win32FormatEtc *format,
                             Win32StgMedium *medium);
} Win32DataObjectVtbl;
struct Win32DataObject { Win32DataObjectVtbl *lpVtbl; };

#define WIN32_DROPEFFECT_NONE 0u
#define WIN32_DROPEFFECT_COPY 1u

static const GUID win32_iid_drop_target = {
    0x00000122, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID win32_iid_unknown = {
    0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// The HDROP inside the data object, 0 when the payload is not a file list
// (a text selection dragged out of a browser, say).
static HANDLE win32_drop_hdrop(void *data, Win32StgMedium *medium) {
    Win32DataObject *object = (Win32DataObject *)data;
    Win32FormatEtc format;
    format.cfFormat = (u16)CF_HDROP;
    format.ptd = 0;
    format.dwAspect = WIN32_DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = WIN32_TYMED_HGLOBAL;
    StructZero(medium);
    if (object->lpVtbl->GetData(object, &format, medium) != S_OK) { return 0; }
    return medium->handle;
}

static void win32_push_drag(OsEventKind kind, POINTL point) {
    OsEvent event = win32_event_make(kind);
    if (kind != OsEvent_DragLeave) {
        POINT client;
        client.x = point.x;
        client.y = point.y;
        ScreenToClient(win32_window_state.window, &client);
        event.pos = v2((f32)client.x, (f32)client.y);
    }
    win32_event_push(&event);
    os_request_redraw();
}

md_inline b32 win32_guid_eq(const GUID *a, const GUID *b) {
    const u64 *left = (const u64 *)a;
    const u64 *right = (const u64 *)b;
    return left[0] == right[0] && left[1] == right[1];
}

static HRESULT WINAPI win32_drop_query_interface(Win32DropTarget *self, const GUID *iid,
                                                 void **out) {
    if (win32_guid_eq(iid, &win32_iid_drop_target) || win32_guid_eq(iid, &win32_iid_unknown)) {
        *out = self;
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI win32_drop_add_ref(Win32DropTarget *self) {
    Unused(self);
    return 1;
}

static ULONG WINAPI win32_drop_release(Win32DropTarget *self) {
    Unused(self);
    return 1;
}

static HRESULT WINAPI win32_drop_drag_enter(Win32DropTarget *self, void *data, DWORD keys,
                                            POINTL point, DWORD *effect) {
    Unused(self);
    Unused(keys);
    Win32StgMedium medium;
    HANDLE drop = win32_drop_hdrop(data, &medium);
    if (!drop) {
        *effect = WIN32_DROPEFFECT_NONE;
        return S_OK;
    }
    win32_window_state.ReleaseStgMedium_(&medium);
    *effect = WIN32_DROPEFFECT_COPY;
    win32_push_drag(OsEvent_DragEnter, point);
    return S_OK;
}

static HRESULT WINAPI win32_drop_drag_over(Win32DropTarget *self, DWORD keys, POINTL point,
                                           DWORD *effect) {
    Unused(self);
    Unused(keys);
    *effect = WIN32_DROPEFFECT_COPY;
    win32_push_drag(OsEvent_DragOver, point);
    return S_OK;
}

static HRESULT WINAPI win32_drop_drag_leave(Win32DropTarget *self) {
    Unused(self);
    POINTL zero;
    zero.x = 0;
    zero.y = 0;
    win32_push_drag(OsEvent_DragLeave, zero);
    return S_OK;
}

static HRESULT WINAPI win32_drop_drop(Win32DropTarget *self, void *data, DWORD keys, POINTL point,
                                      DWORD *effect) {
    Unused(self);
    Unused(keys);
    *effect = WIN32_DROPEFFECT_NONE;
    Win32StgMedium medium;
    HANDLE drop = win32_drop_hdrop(data, &medium);
    // The drag ends whatever the payload was: the highlight must go away even
    // when the drop brought nothing we can use.
    POINTL zero;
    zero.x = 0;
    zero.y = 0;
    win32_push_drag(OsEvent_DragLeave, zero);
    if (!drop) { return S_OK; }
    POINT client;
    client.x = point.x;
    client.y = point.y;
    ScreenToClient(win32_window_state.window, &client);
    win32_push_drop_files(drop, 0, &client);
    win32_window_state.ReleaseStgMedium_(&medium);
    *effect = WIN32_DROPEFFECT_COPY;
    return S_OK;
}

static const Win32DropTargetVtbl win32_drop_target_vtbl = {
    win32_drop_query_interface, win32_drop_add_ref,  win32_drop_release, win32_drop_drag_enter,
    win32_drop_drag_over,       win32_drop_drag_leave, win32_drop_drop,
};
global Win32DropTarget win32_drop_target = {&win32_drop_target_vtbl};

static void win32_push_char(WPARAM wparam) {
    Win32WindowState *state = &win32_window_state;
    u16 unit = (u16)wparam;
    u32 codepoint = unit;
    if (unit >= 0xD800 && unit < 0xDC00) {
        state->pending_high_surrogate = unit;
        return;
    }
    if (unit >= 0xDC00 && unit < 0xE000) {
        if (state->pending_high_surrogate == 0) { return; }
        codepoint = 0x10000u + (((u32)state->pending_high_surrogate - 0xD800u) << 10) +
                    ((u32)unit - 0xDC00u);
        state->pending_high_surrogate = 0;
    } else {
        state->pending_high_surrogate = 0;
    }
    // Control codes are key events, not text. Tab is text in a multiline field.
    if (codepoint < 32 && codepoint != '\t') { return; }
    if (codepoint == 127) { return; }

    OsEvent event = win32_event_make(OsEvent_Char);
    event.codepoint = codepoint;
    win32_event_push(&event);
    os_request_redraw();
}

// --- diagnostics counters (P-005) ------------------------------------------
// Plain increments, no atomics: a window proc only ever runs on the thread that
// created the window, whether the message was posted or sent from elsewhere.
// That is exactly the point - `messages` minus `dispatched` is what other
// threads send us behind the back of the message queue.
#define WIN32_MESSAGE_TABLE_SIZE 1024

typedef struct Win32Counters {
    u64 pump_calls;
    u64 wakeups;
    u64 messages;
    u64 dispatched;
    u64 by_message[WIN32_MESSAGE_TABLE_SIZE];
    u64 above_table;
} Win32Counters;

global Win32Counters win32_counters;

static void win32_count_message(UINT message) {
    win32_counters.messages += 1;
    if (message < WIN32_MESSAGE_TABLE_SIZE) {
        win32_counters.by_message[message] += 1;
    } else {
        win32_counters.above_table += 1;
    }
}

void os_event_counters(OsEventCounters *out) {
    StructZero(out);
    out->pump_calls = win32_counters.pump_calls;
    out->wakeups = win32_counters.wakeups;
    out->messages = win32_counters.messages;
    out->dispatched = win32_counters.dispatched;
    // Selection sort of five out of a thousand: cheaper than any structure we
    // would have to keep up to date on every single message.
    for (u32 rank = 0; rank < OS_MESSAGE_TOP_COUNT; rank += 1) {
        u64 best_count = 0;
        u32 best_id = 0;
        for (u32 id = 0; id < WIN32_MESSAGE_TABLE_SIZE; id += 1) {
            u64 count = win32_counters.by_message[id];
            b32 taken = 0;
            for (u32 i = 0; i < rank; i += 1) {
                if (out->top_message[i] == id) { taken = 1; }
            }
            if (!taken && count > best_count) {
                best_count = count;
                best_id = id;
            }
        }
        out->top_message[rank] = best_id;
        out->top_count[rank] = best_count;
    }
}

// --- window proc -----------------------------------------------------------

static LRESULT CALLBACK win32_window_proc(HWND window, UINT message, WPARAM wparam,
                                          LPARAM lparam) {
    Win32WindowState *state = &win32_window_state;
    win32_count_message(message);
    switch (message) {
        case WM_CLOSE: {
            OsEvent event = win32_event_make(OsEvent_Close);
            win32_event_push(&event);
        } return 0;

        case WM_DESTROY: PostQuitMessage(0); return 0;

        case WM_PAINT: {
            // Validate even though we do not draw here: an unvalidated WM_PAINT
            // is re-posted forever and the loop never sleeps again.
            PAINTSTRUCT paint;
            BeginPaint(window, &paint);
            EndPaint(window, &paint);
            os_request_redraw();
        } return 0;

        case WM_SIZE: {
            state->client_width = LOWORD(lparam);
            state->client_height = HIWORD(lparam);
            OsEvent event = win32_event_make(OsEvent_Resize);
            event.size = v2((f32)state->client_width, (f32)state->client_height);
            win32_event_push(&event);
            os_request_redraw();
        } return 0;

        case WM_GETMINMAXINFO: {
            // lparam is a pointer by contract of the message, hence the cast.
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            MINMAXINFO *info = (MINMAXINFO *)lparam;
            info->ptMinTrackSize.x = (LONG)(WIN32_MIN_CLIENT_WIDTH * state->dpi_scale);
            info->ptMinTrackSize.y = (LONG)(WIN32_MIN_CLIENT_HEIGHT * state->dpi_scale);
        } return 0;

        // A modal resize/move loop runs inside user32 and starves our wait, so
        // we drive redraws from a timer for as long as it lasts.
        case WM_ENTERSIZEMOVE: SetTimer(window, WIN32_TIMER_RESIZE, USER_TIMER_MINIMUM, 0); return 0;
        case WM_EXITSIZEMOVE: KillTimer(window, WIN32_TIMER_RESIZE); return 0;
        case WM_TIMER: {
            if (wparam == WIN32_TIMER_RESIZE) { os_request_redraw(); }
        } return 0;

        case WM_DPICHANGED: {
            state->dpi_scale = (f32)HIWORD(wparam) / 96.0f;
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            RECT *suggested = (RECT *)lparam;
            SetWindowPos(window, 0, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            OsEvent event = win32_event_make(OsEvent_DpiChanged);
            event.dpi_scale = state->dpi_scale;
            event.size = v2((f32)state->client_width, (f32)state->client_height);
            win32_event_push(&event);
            os_request_redraw();
        } return 0;

        case WM_SETFOCUS:
        case WM_KILLFOCUS: {
            OsEvent event =
                    win32_event_make(message == WM_SETFOCUS ? OsEvent_FocusGain : OsEvent_FocusLose);
            if (message == WM_KILLFOCUS) {
                // Alt+Tab eats the key up messages: forget the modifier state.
                event.modifiers = 0;
                state->pending_high_surrogate = 0;
            }
            win32_event_push(&event);
            os_request_redraw();
        } return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            b32 down = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
            OsEvent event = win32_event_make(down ? OsEvent_KeyDown : OsEvent_KeyUp);
            event.scancode = win32_scancode_from_lparam(lparam);
            event.key = win32_key_from_scancode(event.scancode);
            event.vk = (u32)wparam;
            event.repeat = down ? (u32)(lparam & 0xFFFF) : 0;
            win32_event_push(&event);
            os_request_redraw();
            // Alt+F4 and the system menu must keep working.
            if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) { break; }
        } return 0;

        case WM_CHAR:
        case WM_SYSCHAR: win32_push_char(wparam); return 0;

        case WM_MOUSEMOVE: {
            OsEvent event = win32_event_make(OsEvent_MouseMove);
            event.pos = win32_mouse_pos_from_lparam(lparam);
            win32_event_push(&event);
            os_request_redraw();
        } return 0;

        case WM_LBUTTONDOWN: win32_push_mouse_button(message, wparam, lparam, OsMouseButton_Left, 1); return 0;
        case WM_LBUTTONUP: win32_push_mouse_button(message, wparam, lparam, OsMouseButton_Left, 0); return 0;
        case WM_RBUTTONDOWN: win32_push_mouse_button(message, wparam, lparam, OsMouseButton_Right, 1); return 0;
        case WM_RBUTTONUP: win32_push_mouse_button(message, wparam, lparam, OsMouseButton_Right, 0); return 0;
        case WM_MBUTTONDOWN: win32_push_mouse_button(message, wparam, lparam, OsMouseButton_Middle, 1); return 0;
        case WM_MBUTTONUP: win32_push_mouse_button(message, wparam, lparam, OsMouseButton_Middle, 0); return 0;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: {
            OsMouseButton button = (HIWORD(wparam) == XBUTTON1) ? OsMouseButton_X1 : OsMouseButton_X2;
            win32_push_mouse_button(message, wparam, lparam, button, message == WM_XBUTTONDOWN);
        } return TRUE;

        case WM_MOUSEWHEEL: win32_push_wheel(wparam, lparam, 0); return 0;
        case WM_MOUSEHWHEEL: win32_push_wheel(wparam, lparam, 1); return 0;

        case WM_SETCURSOR: {
            if (LOWORD(lparam) == HTCLIENT) {
                SetCursor(state->cursors[state->cursor]);
                return TRUE;
            }
        } break;

        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        case WM_DROPFILES: win32_push_drop_files((HANDLE)wparam, 1, 0); return 0;

        case WM_DEVICECHANGE: {
            OsEvent event = win32_event_make(OsEvent_DeviceChange);
            win32_event_push(&event);
            os_request_redraw();
        } return TRUE;

        case WM_SETTINGCHANGE: {
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &state->wheel_scroll_lines, 0);
        } break;

        default: break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

// --- dynamic modules -------------------------------------------------------

static void win32_window_load_optional_modules(Win32WindowState *state) {
    state->dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (state->dwmapi) {
        state->DwmSetWindowAttribute_ = (Win32DwmSetWindowAttribute)GetProcAddress(
                state->dwmapi, "DwmSetWindowAttribute");
    }
    state->ole32 = LoadLibraryW(L"ole32.dll");
    if (state->ole32) {
        state->OleInitialize_ = (Win32OleInitialize)GetProcAddress(state->ole32, "OleInitialize");
        state->RegisterDragDrop_ =
                (Win32RegisterDragDrop)GetProcAddress(state->ole32, "RegisterDragDrop");
        state->RevokeDragDrop_ =
                (Win32RevokeDragDrop)GetProcAddress(state->ole32, "RevokeDragDrop");
        state->ReleaseStgMedium_ =
                (Win32ReleaseStgMedium)GetProcAddress(state->ole32, "ReleaseStgMedium");
    }
    state->shell32 = LoadLibraryW(L"shell32.dll");
    if (state->shell32) {
        state->DragAcceptFiles_ =
                (Win32DragAcceptFiles)GetProcAddress(state->shell32, "DragAcceptFiles");
        state->DragQueryFileW_ =
                (Win32DragQueryFileW)GetProcAddress(state->shell32, "DragQueryFileW");
        state->DragFinish_ = (Win32DragFinish)GetProcAddress(state->shell32, "DragFinish");
        state->DragQueryPoint_ =
                (Win32DragQueryPoint)GetProcAddress(state->shell32, "DragQueryPoint");
    }
}

// The real drop target when ole32 gives us one, WM_DROPFILES otherwise: the
// fallback still delivers the drop, it just cannot say where the cursor is
// while the drag is in flight.
static void win32_window_register_drop_target(Win32WindowState *state, HWND window) {
    if (state->OleInitialize_ && state->RegisterDragDrop_) {
        // S_FALSE: the thread was already an apartment, which is fine.
        state->OleInitialize_(0);
        if (state->RegisterDragDrop_(window, &win32_drop_target) == S_OK) {
            state->drop_target_registered = 1;
            return;
        }
    }
    if (state->DragAcceptFiles_) { state->DragAcceptFiles_(window, TRUE); }
}

// Dark title bar, rounded corners, matching border. Every failure is ignored:
// on an older Windows we simply get the light title bar.
static void win32_window_apply_dark_frame(Win32WindowState *state, HWND window) {
    if (!state->DwmSetWindowAttribute_) { return; }
    BOOL dark = TRUE;
    if (state->DwmSetWindowAttribute_(window, WIN32_DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                                      sizeof(dark)) != S_OK) {
        state->DwmSetWindowAttribute_(window, WIN32_DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &dark,
                                      sizeof(dark));
    }
    DWORD corner = WIN32_DWMWCP_ROUND;
    state->DwmSetWindowAttribute_(window, WIN32_DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                                  sizeof(corner));
    COLORREF border = 0x002B2B2B;  // 0x00BBGGRR
    state->DwmSetWindowAttribute_(window, WIN32_DWMWA_BORDER_COLOR, &border, sizeof(border));
}

static void win32_window_load_cursors(Win32WindowState *state) {
    // NOLINTBEGIN(performance-no-int-to-ptr) IDC_* are small integers by design.
    state->cursors[OsCursor_Arrow] = LoadCursorW(0, (LPCWSTR)IDC_ARROW);
    state->cursors[OsCursor_IBeam] = LoadCursorW(0, (LPCWSTR)IDC_IBEAM);
    state->cursors[OsCursor_Hand] = LoadCursorW(0, (LPCWSTR)IDC_HAND);
    state->cursors[OsCursor_ResizeH] = LoadCursorW(0, (LPCWSTR)IDC_SIZEWE);
    state->cursors[OsCursor_ResizeV] = LoadCursorW(0, (LPCWSTR)IDC_SIZENS);
    state->cursors[OsCursor_Forbidden] = LoadCursorW(0, (LPCWSTR)IDC_NO);
    state->cursors[OsCursor_Wait] = LoadCursorW(0, (LPCWSTR)IDC_WAIT);
    // NOLINTEND(performance-no-int-to-ptr)
}

// --- public API ------------------------------------------------------------

void os_events_set_frame_arena(Arena *arena) { win32_window_state.frame_arena = arena; }

OsWindow os_window_create(String8 title, u32 width, u32 height) {
    Win32WindowState *state = &win32_window_state;
    Assert(state->window == 0);
    Assert(state->frame_arena != 0);  // DropFiles has nowhere to go otherwise

    state->arena = arena_alloc(MB(1));
    win32_event_ring_init(&state->ring, state->arena, WIN32_EVENT_RING_CAPACITY);
    state->wake_event = CreateEventW(0, FALSE, FALSE, 0);
    state->wheel_scroll_lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &state->wheel_scroll_lines, 0);
    win32_window_load_optional_modules(state);
    win32_window_load_cursors(state);
    state->cursor = OsCursor_Arrow;

    WNDCLASSEXW window_class;
    StructZero(&window_class);
    window_class.cbSize = sizeof(window_class);
    // CS_OWNDC: the GL context of T-003 needs a HDC that stays valid for good.
    window_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = win32_window_proc;
    window_class.hInstance = win32_state.instance;
    window_class.hCursor = state->cursors[OsCursor_Arrow];
    // System colour brush passed as a small integer: black background without
    // pulling gdi32 in for CreateSolidBrush, and no white flash at startup.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOWTEXT + 1);
    window_class.lpszClassName = L"minidisk_window";
    RegisterClassExW(&window_class);

    // The manifest already made us per monitor v2 aware, so this is the real dpi.
    u32 dpi = GetDpiForSystem();
    state->dpi_scale = (f32)dpi / 96.0f;

    RECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = (LONG)((f32)width * state->dpi_scale);
    rect.bottom = (LONG)((f32)height * state->dpi_scale);
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);

    ArenaTemp scratch = scratch_begin(0, 0);
    String16 title16 = str16_from_str8(scratch.arena, title);
    // Created hidden: the first frame is drawn before the window appears.
    HWND window = CreateWindowExW(0, window_class.lpszClassName, (LPCWSTR)title16.str,
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  rect.right - rect.left, rect.bottom - rect.top, 0, 0,
                                  win32_state.instance, 0);
    scratch_end(scratch);
    AssertAlways(window != 0);
    state->window = window;

    state->dpi_scale = (f32)GetDpiForWindow(window) / 96.0f;
    win32_window_apply_dark_frame(state, window);
    win32_window_register_drop_target(state, window);

    RECT client;
    GetClientRect(window, &client);
    state->client_width = (u32)(client.right - client.left);
    state->client_height = (u32)(client.bottom - client.top);

    os_request_redraw();

    OsWindow result;
    result.v = (u64)window;
    return result;
}

// NOLINTBEGIN(performance-no-int-to-ptr) OsWindow carries the HWND as a u64,
// which is the whole point of the opaque handle (platform.h).
void os_window_show(OsWindow window, b32 maximized) {
    ShowWindow((HWND)window.v, maximized ? SW_SHOWMAXIMIZED : SW_SHOW);
    os_request_redraw();
}

void os_window_get_placement(OsWindow window, OsWindowPlacement *out) {
    WINDOWPLACEMENT placement;
    StructZero(&placement);
    placement.length = sizeof(placement);
    GetWindowPlacement((HWND)window.v, &placement);
    RECT rect = placement.rcNormalPosition;
    out->x = rect.left;
    out->y = rect.top;
    out->width = (u32)(rect.right - rect.left);
    out->height = (u32)(rect.bottom - rect.top);
    out->maximized = (placement.showCmd == SW_SHOWMAXIMIZED) ||
                     (IsZoomed((HWND)window.v) ? 1 : 0);
}

void os_window_set_placement(OsWindow window, const OsWindowPlacement *placement) {
    WINDOWPLACEMENT wp;
    StructZero(&wp);
    wp.length = sizeof(wp);
    GetWindowPlacement((HWND)window.v, &wp);
    RECT rect;
    rect.left = placement->x;
    rect.top = placement->y;
    rect.right = placement->x + (LONG)placement->width;
    rect.bottom = placement->y + (LONG)placement->height;
    // A window larger than the screen it opens on is a window whose right hand
    // panel nobody ever sees: the size is clamped to the work area, the
    // position is left to Win32 (rcNormalPosition is pulled back on its own).
    HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info;
    StructZero(&monitor_info);
    monitor_info.cbSize = sizeof(monitor_info);
    if (GetMonitorInfoW(monitor, &monitor_info)) {
        LONG max_width = monitor_info.rcWork.right - monitor_info.rcWork.left;
        LONG max_height = monitor_info.rcWork.bottom - monitor_info.rcWork.top;
        if (rect.right - rect.left > max_width) { rect.right = rect.left + max_width; }
        if (rect.bottom - rect.top > max_height) { rect.bottom = rect.top + max_height; }
    }
    wp.rcNormalPosition = rect;
    // Recording a rectangle must not show a window that is still hidden, nor
    // hide one that is already up: the show state is left exactly as it is.
    if (!IsWindowVisible((HWND)window.v)) { wp.showCmd = SW_HIDE; }
    // Win32 clamps rcNormalPosition to the nearest monitor's work area for us,
    // so a window saved on a screen that is gone comes back on a screen we have.
    SetWindowPlacement((HWND)window.v, &wp);
    RECT client;
    GetClientRect((HWND)window.v, &client);
    win32_window_state.client_width = (u32)(client.right - client.left);
    win32_window_state.client_height = (u32)(client.bottom - client.top);
}

// NOLINTEND(performance-no-int-to-ptr)

void os_window_destroy(OsWindow window) {
    Win32WindowState *state = &win32_window_state;
    Assert((HWND)window.v == state->window);
    DestroyWindow(state->window);
    state->window = 0;
    CloseHandle(state->wake_event);
    state->wake_event = 0;
    if (state->drop_target_registered && state->RevokeDragDrop_) {
        state->RevokeDragDrop_(state->window);
    }
    if (state->dwmapi) { FreeLibrary(state->dwmapi); }
    if (state->ole32) { FreeLibrary(state->ole32); }
    if (state->shell32) { FreeLibrary(state->shell32); }
    arena_release(state->arena);
    state->arena = 0;
}

V2 os_window_get_size(OsWindow window) {
    Unused(window);
    return v2((f32)win32_window_state.client_width, (f32)win32_window_state.client_height);
}

f32 os_window_dpi_scale(OsWindow window) {
    Unused(window);
    return win32_window_state.dpi_scale;
}

void os_window_set_title(OsWindow window, String8 title) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String16 title16 = str16_from_str8(scratch.arena, title);
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    SetWindowTextW((HWND)window.v, (LPCWSTR)title16.str);
    scratch_end(scratch);
}

void os_events_pump(b32 blocking, u64 timeout_us) {
    Win32WindowState *state = &win32_window_state;
    win32_counters.pump_calls += 1;
    if (blocking) {
        DWORD timeout = INFINITE;
        if (timeout_us != OS_TIMEOUT_INFINITE) {
            u64 milliseconds = (timeout_us + 999) / 1000;
            timeout = (DWORD)Min(milliseconds, (u64)(INFINITE - 1));
        }
        HANDLE handles[1];
        handles[0] = state->wake_event;
        MsgWaitForMultipleObjectsEx(1, handles, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        win32_counters.wakeups += 1;
    }
    MSG message;
    while (PeekMessageW(&message, 0, 0, 0, PM_REMOVE)) {
        win32_counters.dispatched += 1;
        if (message.message == WM_QUIT) {
            OsEvent event = win32_event_make(OsEvent_Close);
            win32_event_push(&event);
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

b32 os_event_next(OsEvent *out) { return win32_event_ring_pop(&win32_window_state.ring, out); }

void os_request_redraw(void) {
    Win32WindowState *state = &win32_window_state;
    InterlockedExchange(&state->redraw_requested, 1);
    if (state->wake_event) { SetEvent(state->wake_event); }
}

b32 os_redraw_requested(void) {
    return InterlockedExchange(&win32_window_state.redraw_requested, 0) != 0;
}

// --- clipboard and cursor --------------------------------------------------

String8 os_clipboard_get(Arena *arena) {
    String8 result;
    result.str = 0;
    result.size = 0;
    if (!OpenClipboard(win32_window_state.window)) { return result; }
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle) {
        const u16 *text = (const u16 *)GlobalLock(handle);
        if (text) {
            result = str8_from_cstr16(arena, text);
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return result;
}

b32 os_clipboard_set(String8 text) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String16 wide = str16_from_str8(scratch.arena, text);
    u64 bytes = (wide.size + 1) * sizeof(u16);
    b32 ok = 0;
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (block) {
        void *destination = GlobalLock(block);
        mem_copy(destination, wide.str, bytes);
        GlobalUnlock(block);
        if (OpenClipboard(win32_window_state.window)) {
            EmptyClipboard();
            ok = SetClipboardData(CF_UNICODETEXT, block) != 0;
            CloseClipboard();
        }
        if (!ok) { GlobalFree(block); }
    }
    scratch_end(scratch);
    return ok;
}

void os_cursor_set(OsCursor cursor) {
    Win32WindowState *state = &win32_window_state;
    Assert(cursor < OsCursor_COUNT);
    if (state->cursor == cursor) { return; }
    state->cursor = cursor;
    SetCursor(state->cursors[cursor]);
}

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
b32     os_file_write_all(String8 path, String8 data);  // flushed before it returns
// Rename over an existing file in one step: what makes a ".tmp then move"
// write atomic for a reader (ADR-010 s9.3).
b32     os_file_move_replace(String8 from, String8 to);

// A read only view of a whole file, paged in by the OS on demand. This is how
// the library cache is loaded: three system calls and no copy (research/03 s9.4).
typedef struct OsFileMap {
    u8 *data;    // 0 when the mapping failed
    u64 size;
    void *file;  // opaque: the OS handles
    void *mapping;
} OsFileMap;

b32  os_file_map(OsFileMap *map, String8 path);  // 0: unreadable or empty
void os_file_unmap(OsFileMap *map);

// --- file system -----------------------------------------------------------
// A directory entry, or what os_file_stat found. `name` points into the
// iterator and is valid until the next os_dir_iter_next: walking a tree of
// 50 000 files therefore allocates nothing at all.
#define OS_NAME_MAX 780   // 260 UTF-16 units, worst case in UTF-8
#define OS_PATH_MAX 1024

typedef struct OsFileInfo {
    String8 name;
    u64 size;
    u64 mtime_us;  // unix epoch, microseconds
    b32 is_dir;
} OsFileInfo;

// Opaque tail: the OS search handle plus the buffer it fills (WIN32_FIND_DATAW
// is 592 bytes; a static assert in the backend keeps this honest).
typedef struct OsDirIter {
    void *handle;
    b32 pending;  // the entry the search handed us when it opened
    u8 name[OS_NAME_MAX];
    u8 opaque[640];
} OsDirIter;

b32  os_dir_iter_begin(OsDirIter *it, String8 dir_path);  // 0: unreadable
b32  os_dir_iter_next(OsDirIter *it, OsFileInfo *out);    // 0: exhausted
void os_dir_iter_end(OsDirIter *it);

b32 os_file_stat(String8 path, OsFileInfo *out);  // `name` stays empty
b32 os_dir_create(String8 path);                  // 1 when it exists afterwards
b32 os_file_delete(String8 path);
b32 os_dir_delete(String8 path);                  // the directory must be empty

// Random access reads, for the tag parsers of T-011 and the codecs later on.
typedef struct OsFile { void *v; } OsFile;  // v == 0: not open
OsFile os_file_open(String8 path);        // read only, shared read
u64    os_file_read_at(OsFile file, u64 offset, void *dst, u64 size);  // bytes read
void   os_file_close(OsFile file);

// --- paths -----------------------------------------------------------------
// Slices into `path` where they can be, so walking a tree copies nothing.
b32     os_path_is_separator(u8 c);
String8 os_path_join(Arena *arena, String8 a, String8 b);
String8 os_path_parent(String8 path);     // without the trailing separator
String8 os_path_filename(String8 path);
String8 os_path_extension(String8 path);  // without the dot, empty when none
String8 os_path_normalize(Arena *arena, String8 path);  // '/' -> '\', no trailing sep

typedef enum OsKnownFolder {
    OsKnownFolder_Music = 0,
    OsKnownFolder_LocalAppData,
    OsKnownFolder_Temp,
    OsKnownFolder_COUNT
} OsKnownFolder;

String8 os_known_folder(Arena *arena, OsKnownFolder folder);  // size 0 when unknown

// The process command line, arguments only (the exe path is dropped). The app
// parses it; nothing below app/ ever looks at it.
String8 os_command_line(Arena *arena);

// The folder holding the exe, no trailing separator. Portable mode (ADR-010)
// looks for its marker there and writes its files next to it.
String8 os_exe_dir(Arena *arena);

// --- time and threads ------------------------------------------------------
u64 os_time_now_us(void);      // monotonic, microseconds
// Local civil time. Only for naming something a human will read back (the TOC
// backups of T-022); every measurement uses the monotonic clock above.
typedef struct OsWallClock {
    u32 year, month, day, hour, minute, second;
} OsWallClock;
void os_time_local(OsWallClock *out);
void os_sleep_us(u64 us);
// T-042: hold off the idle sleep timer for the length of a transfer. Per
// thread, so the thread that turns it on is the thread that turns it off.
void os_power_keep_awake(b32 keep_awake);
// Unpredictable bytes for the NetMD nonces and packet keys (research/01 s4.6,
// s4.10). BCryptGenRandom when bcrypt.dll is there, a QPC/counter mix when it
// is not - documented at the implementation, never used for anything at rest.
void os_random_bytes(void *dst, u64 size);
u32 os_thread_current_id(void);

// --- atomics ---------------------------------------------------------------
// Macros over the MSVC interlocked intrinsics, which are all full barriers -
// which is what every one of our uses wants. `inc`/`dec` return the new value,
// `add` the previous one, `cas` the value that was found (== expected: swapped).
#define os_atomic_load_u32(p)     ((u32)_InterlockedOr((volatile long *)(p), 0))
#define os_atomic_store_u32(p, v) ((void)_InterlockedExchange((volatile long *)(p), (long)(v)))
#define os_atomic_add_u32(p, v)   ((u32)_InterlockedExchangeAdd((volatile long *)(p), (long)(v)))
#define os_atomic_inc_u32(p)      ((u32)_InterlockedIncrement((volatile long *)(p)))
#define os_atomic_dec_u32(p)      ((u32)_InterlockedDecrement((volatile long *)(p)))
#define os_atomic_cas_u32(p, expected, desired)                          \
    ((u32)_InterlockedCompareExchange((volatile long *)(p), (long)(desired), \
                                      (long)(expected)))
#define os_atomic_load_u64(p)     ((u64)_InterlockedOr64((volatile __int64 *)(p), 0))
#define os_atomic_store_u64(p, v) ((void)_InterlockedExchange64((volatile __int64 *)(p), (__int64)(v)))
#define os_atomic_add_u64(p, v)                                          \
    ((u64)_InterlockedExchangeAdd64((volatile __int64 *)(p), (__int64)(v)))
#define os_atomic_inc_u64(p)      ((u64)_InterlockedIncrement64((volatile __int64 *)(p)))
// The hint that tells the core we are in a spin loop: shortens the memory order
// violation penalty and lets a hyperthread sibling have the pipeline.
#define os_cpu_pause()            _mm_pause()

// --- threads, semaphores, mutexes ------------------------------------------
typedef struct OsThread { u64 v; } OsThread;
typedef struct OsSemaphore { u64 v; } OsSemaphore;
typedef struct OsMutex { void *v; } OsMutex;  // an SRWLOCK is one pointer

typedef void OsThreadProc(void *data);

u32      os_cpu_count(void);   // logical cores
// The thread starts immediately; `name` is what the debugger and the profiler
// show. A thread that used a scratch arena calls scratch_thread_release itself.
OsThread os_thread_create(OsThreadProc *proc, void *data, String8 name);
void     os_thread_join(OsThread thread);
void     os_thread_set_name(String8 name);  // names the calling thread
void     os_thread_yield(void);

OsSemaphore os_semaphore_create(u32 initial_count, u32 max_count);
void        os_semaphore_destroy(OsSemaphore semaphore);
void        os_semaphore_wait(OsSemaphore semaphore);  // blocks, 0 % cpu
// 1 when the semaphore was taken, 0 on timeout. What lets the device thread
// watch for a disc change without spinning (T-021).
b32         os_semaphore_wait_for(OsSemaphore semaphore, u64 timeout_us);
void        os_semaphore_signal(OsSemaphore semaphore, u32 count);

void os_mutex_init(OsMutex *mutex);
void os_mutex_lock(OsMutex *mutex);
void os_mutex_unlock(OsMutex *mutex);

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
    // The drag itself, so a panel can highlight while the cursor is over it
    // (T-014). Only `pos` carries meaning; the paths arrive with DropFiles.
    OsEvent_DragEnter,
    OsEvent_DragOver,
    OsEvent_DragLeave,
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

// Created hidden: the caller restores the saved placement, draws its first
// frame, then calls os_window_show. `width` and `height` are logical (dp).
OsWindow os_window_create(String8 title, u32 width, u32 height);
void     os_window_show(OsWindow window, b32 maximized);
void     os_window_destroy(OsWindow window);

// Restore rectangle and maximized state, the pair the preferences store: the
// rectangle is the *restored* one even while the window is maximized, which is
// what makes un-maximizing land back where the user left it.
typedef struct OsWindowPlacement {
    i32 x, y;
    u32 width, height;  // physical pixels, outer rectangle
    b32 maximized;
} OsWindowPlacement;

void os_window_get_placement(OsWindow window, OsWindowPlacement *out);
// Moves the restored rectangle; does not show the window. Off screen positions
// are pulled back onto the nearest monitor's work area.
void os_window_set_placement(OsWindow window, const OsWindowPlacement *placement);
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

// --- fonts -----------------------------------------------------------------
// The rasterizer is the system's (DirectWrite on Windows): no font is shipped
// with the exe, and Unicode fallback is free (ADR-006). ui/ only sees this.
typedef struct OsFont { u64 v; } OsFont;  // v == 0 : no such font

typedef struct OsFontMetrics {
    f32 ascent, descent;   // both positive, physical pixels
    f32 line_gap;
    f32 x_height, cap_height;
    f32 digit_advance;     // widest of '0'..'9': the tabular figure cell
} OsFontMetrics;

// Placement of one rasterized glyph relative to the pen: the destination rect
// is (floor(pen_x) + offset_x, baseline_y + offset_y, + width, + height).
typedef struct OsGlyphMetrics {
    f32 advance;
    i32 offset_x, offset_y;
    u32 width, height;
} OsGlyphMetrics;

// Coverage buffers handed to os_font_rasterize are this big, which no UI glyph
// comes close to (a 32 px kanji is 34 x 34).
#define OS_GLYPH_MAX_DIM 256

b32  os_font_init(void);      // 0 when the system has no rasterizer at all
void os_font_shutdown(void);

// `size_px` is already scaled for the DPI and rounded by the caller.
OsFont os_font_open(String8 family, f32 size_px, u32 weight);  // weight 100..900
// Drops every open font, fallbacks included. A DPI change rebuilds them all,
// so closing them one by one would leave the fallback faces behind.
void os_font_close_all(void);
u32    os_font_id(OsFont font);  // small dense id, meant for cache keys
OsFontMetrics os_font_metrics(OsFont font);

// Glyph for `codepoint`, resolved through the system fallback: `*out_font` is
// the font that actually carries it, `font` itself for the common case. Returns
// 0 (the tofu glyph) when nothing on the machine has it.
u32 os_font_glyph_index(OsFont font, u32 codepoint, OsFont *out_font);
f32 os_font_advance(OsFont font, u32 glyph);
f32 os_font_kern(OsFont font, u32 left_glyph, u32 right_glyph);

// Grayscale coverage, one byte per pixel, tightly packed (stride == width).
// `subpixel_x` is the fractional pen position, in [0, 1). Returns 0 when the
// glyph has no ink (space): the metrics are still filled in.
b32 os_font_rasterize(OsFont font, u32 glyph, f32 subpixel_x, u8 *out, u64 out_capacity,
                      OsGlyphMetrics *out_metrics);

// --- images ----------------------------------------------------------------
// The decoder is the system's (WIC on Windows), so not one byte of JPEG or PNG
// decoding ships in the exe (ADR-007). `bytes` is a boundary: it comes from a
// file somebody else wrote, and every failure is a plain 0 return.
//
// `size` 0 keeps the natural dimensions; anything else scales the image to
// `size` x `size`, which is what the two thumbnail formats (48 and 256) want.
typedef struct OsImage {
    u8 *pixels;  // RGBA8 premultiplied, width * height * 4 bytes, in the arena
    u32 width, height;
} OsImage;

b32 os_image_decode(Arena *arena, String8 bytes, u32 size, OsImage *out);

// --- usb -------------------------------------------------------------------
// WinUSB on Windows (ADR-008). The layer stays generic: it knows nothing about
// NetMD, it only reports what the bus has and hands out raw transfers. The
// VID/PID filter and the model table live in core/netmd (netmd_models.h).
typedef enum OsUsbState {
    OsUsbState_Ready = 0,  // a usable device interface is bound: it can be opened
    OsUsbState_NoDriver,   // present but nothing user mode can talk to (P-001, code 28)
    OsUsbState_InUse,      // bound, but another process holds it
    OsUsbState_COUNT
} OsUsbState;

// Every transfer returns the byte count, or one of these. Negative on purpose:
// a caller tests `< 0` once and reads the reason only when it cares.
typedef enum OsUsbError {
    OsUsbError_Failed = -1,
    OsUsbError_Timeout = -2,
    OsUsbError_Disconnected = -3,
    OsUsbError_NotOpen = -4,
    OsUsbError_Divergence = -5,  // the replay transport: the request left the script
} OsUsbError;

// A present USB device node. `path` is what os_usb_open takes and is empty for
// anything but Ready; `bus_name` is the name the bus shows ("Net MD Walkman"),
// which is all we can show of a device that has no driver.
typedef struct OsUsbDeviceInfo {
    u16 vid;
    u16 pid;
    u32 state;         // OsUsbState
    u32 problem_code;  // CM_PROB_*, 28 when no driver is installed; 0 when fine
    String8 path;
    String8 bus_name;
} OsUsbDeviceInfo;

typedef struct OsUsbDeviceList {
    OsUsbDeviceInfo *items;
    u64 count;
} OsUsbDeviceList;

// More USB device nodes than this on one machine is a hub farm, not a desktop.
#define OS_USB_DEVICE_MAX 96

// Everything present on the USB enumerator, driverless nodes included: seeing
// those is the whole point of the guided driver screen.
OsUsbDeviceList os_usb_enumerate(Arena *arena);

typedef struct OsUsb { void *v; } OsUsb;  // v == 0: not open

OsUsb os_usb_open(String8 path);
void  os_usb_close(OsUsb usb);
md_inline b32 os_usb_is_open(OsUsb usb) { return usb.v != 0; }

// Control transfer on EP0. This is the NetMD command channel: the whole
// protocol goes through it, the bulk pipes only carry audio (research/01 s2.4).
i32 os_usb_control(OsUsb usb, u8 request_type, u8 request, u16 value, u16 index,
                   void *buffer, u32 length, u32 timeout_ms);
i32 os_usb_bulk_write(OsUsb usb, u8 endpoint, const void *data, u32 length, u32 timeout_ms);
i32 os_usb_bulk_read(OsUsb usb, u8 endpoint, void *data, u32 length, u32 timeout_ms);
b32 os_usb_reset(OsUsb usb);

// Opens `url` in the user's browser (the Zadig page of the driver screen).
// shell32 is loaded by hand, so the import table stays kernel32 + user32.
void os_open_url(String8 url);
// --- system audio decoding -------------------------------------------------
// AAC/M4A, ALAC and WMA are Media Foundation's job (ADR-007): patent encumbered
// formats Windows already decodes, for zero bytes of exe. mfplat/mfreadwrite are
// loaded on the first open, so the import table stays kernel32 + user32.
//
// A boundary like every other file input: an unreadable or unsupported stream
// is a 0 return, never a crash. Samples come out interleaved f32, which is what
// the reader is configured to produce whatever the file actually holds.
typedef struct OsMediaDecoder { void *v; } OsMediaDecoder;  // v == 0: not open

typedef struct OsMediaInfo {
    u32 sample_rate;
    u32 channels;
    u64 total_frames;  // 0 when the container does not carry a duration
} OsMediaInfo;

b32  os_media_decoder_open(OsMediaDecoder *out, String8 path, OsMediaInfo *info);
u64  os_media_decoder_read(OsMediaDecoder decoder, f32 *dst, u64 frames);  // frames written
// Seeks to the sample the container can start on, which for a compressed audio
// stream is the frame asked for or a hair before it.
b32  os_media_decoder_seek(OsMediaDecoder decoder, u64 frame);
void os_media_decoder_close(OsMediaDecoder decoder);

// --- clipboard and cursor --------------------------------------------------
// The system folder picker (IFileDialog): modal on the active window, returns
// size 0 when the user cancelled. ole32/shell32 are loaded on the first call
// and never unloaded, so the import table stays kernel32 + user32.
String8 os_dialog_pick_folder(Arena *arena, String8 title);
// The same picker, for one file. `filter_name` labels the file type in the
// dialog and `filter_spec` is its pattern ("*.mdplan"); `suggested` pre-fills
// the name of a save. Size 0 when the user cancelled.
String8 os_dialog_open_file(Arena *arena, String8 title, String8 filter_name,
                            String8 filter_spec);
String8 os_dialog_save_file(Arena *arena, String8 title, String8 filter_name,
                            String8 filter_spec, String8 suggested);

String8 os_clipboard_get(Arena *arena);   // size 0 when there is no text
b32     os_clipboard_set(String8 text);
void    os_cursor_set(OsCursor cursor);

// --- diagnostics -----------------------------------------------------------
void os_debug_print(String8 s);

// Counters the debug overlay reads to answer P-005: how often the loop really
// wakes up at rest, and which messages the window proc is handed meanwhile.
// `messages` counts every call into the window proc, `dispatched` only the ones
// that came off the queue: the difference is what other threads send us.
#define OS_MESSAGE_TOP_COUNT 5
typedef struct OsEventCounters {
    u64 pump_calls;
    u64 wakeups;
    u64 messages;
    u64 dispatched;
    u32 top_message[OS_MESSAGE_TOP_COUNT];  // most seen message ids, descending
    u64 top_count[OS_MESSAGE_TOP_COUNT];
} OsEventCounters;

void os_event_counters(OsEventCounters *out);

#endif // PLATFORM_H

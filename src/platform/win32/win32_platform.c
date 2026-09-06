// win32_platform.c - Win32 implementation of platform.h, entry point and black window.
#include <windows.h>

#include "../platform.h"
#include "../../base/base_hash.h"
#include "../../base/base_math.h"

typedef struct Win32State {
    u64 page_size;
    u64 qpc_frequency;
    u64 qpc_origin;
    HINSTANCE instance;
} Win32State;

global Win32State win32_state;

// --- lifecycle -------------------------------------------------------------

void os_init(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    win32_state.page_size = info.dwPageSize;
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    win32_state.qpc_frequency = (u64)frequency.QuadPart;
    win32_state.qpc_origin = (u64)counter.QuadPart;
    win32_state.instance = GetModuleHandleW(0);
}

no_return void os_exit(i32 code) { ExitProcess((UINT)code); }

// --- virtual memory --------------------------------------------------------

void *os_memory_reserve(u64 size) { return VirtualAlloc(0, size, MEM_RESERVE, PAGE_READWRITE); }

b32 os_memory_commit(void *ptr, u64 size) {
    return VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) != 0;
}

void os_memory_release(void *ptr, u64 size) {
    Unused(size);
    VirtualFree(ptr, 0, MEM_RELEASE);
}

u64 os_page_size(void) { return win32_state.page_size; }

// --- files -----------------------------------------------------------------

String8 os_file_read_all(Arena *arena, String8 path) {
    String8 result;
    result.str = 0;
    result.size = 0;
    ArenaTemp scratch = scratch_begin(&arena, 1);
    String16 path16 = str16_from_str8(scratch.arena, path);
    HANDLE file = CreateFileW((LPCWSTR)path16.str, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, 0);
    if (file != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size;
        if (GetFileSizeEx(file, &size) && size.QuadPart >= 0) {
            u64 total = (u64)size.QuadPart;
            u8 *bytes = push_array(arena, u8, total + 1);
            u64 read_total = 0;
            b32 ok = 1;
            while (ok && read_total < total) {
                DWORD chunk = (DWORD)Min(total - read_total, (u64)0x40000000u);
                DWORD got = 0;
                ok = ReadFile(file, bytes + read_total, chunk, &got, 0) && got != 0;
                read_total += got;
            }
            bytes[read_total] = 0;
            result = str8(bytes, read_total);
        }
        CloseHandle(file);
    }
    scratch_end(scratch);
    return result;
}

b32 os_file_write_all(String8 path, String8 data) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String16 path16 = str16_from_str8(scratch.arena, path);
    HANDLE file = CreateFileW((LPCWSTR)path16.str, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, 0);
    b32 ok = 0;
    if (file != INVALID_HANDLE_VALUE) {
        ok = 1;
        u64 written_total = 0;
        while (ok && written_total < data.size) {
            DWORD chunk = (DWORD)Min(data.size - written_total, (u64)0x40000000u);
            DWORD written = 0;
            ok = WriteFile(file, data.str + written_total, chunk, &written, 0) && written != 0;
            written_total += written;
        }
        CloseHandle(file);
    }
    scratch_end(scratch);
    return ok;
}

// --- time and threads ------------------------------------------------------

u64 os_time_now_us(void) {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    u64 ticks = (u64)counter.QuadPart - win32_state.qpc_origin;
    // Split to avoid overflowing ticks * 1e6 (QPC at 10 MHz overflows after ~21 days).
    u64 frequency = win32_state.qpc_frequency;
    return (ticks / frequency) * 1000000ull + ((ticks % frequency) * 1000000ull) / frequency;
}

u32 os_thread_current_id(void) { return GetCurrentThreadId(); }

// --- diagnostics -----------------------------------------------------------

void os_debug_print(String8 s) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 text = str8_copy(scratch.arena, s);  // guarantees the null terminator
    OutputDebugStringA((LPCSTR)text.str);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != 0 && out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(out, text.str, (DWORD)text.size, &written, 0);
    }
    scratch_end(scratch);
}

#if !BUILD_TEST && !BUILD_BENCH

// --- black window ----------------------------------------------------------

static LRESULT CALLBACK win32_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_CLOSE: DestroyWindow(window); return 0;
        case WM_KEYDOWN: {
            if (wparam == VK_ESCAPE) {
                DestroyWindow(window);
                return 0;
            }
        } break;
        default: break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void win32_window_run(void) {
    WNDCLASSW window_class;
    StructZero(&window_class);
    window_class.lpfnWndProc = win32_window_proc;
    window_class.hInstance = win32_state.instance;
    window_class.hCursor = LoadCursorW(0, (LPCWSTR)IDC_ARROW);
    // System colour brush passed as a small integer: black background without
    // pulling gdi32 in for CreateSolidBrush. NOLINTNEXTLINE(performance-no-int-to-ptr)
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOWTEXT + 1);
    window_class.lpszClassName = L"minidisk_window";
    RegisterClassW(&window_class);
    HWND window = CreateWindowExW(0, window_class.lpszClassName, L"minidisk",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                                  1024, 640, 0, 0, win32_state.instance, 0);
    Unused(window);
    MSG message;
    while (GetMessageW(&message, 0, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

// --- self test (--selftest): boot the base layer without opening a window ---

static i32 win32_selftest(void) {
    Arena *arena = arena_alloc(MB(64));
    i32 failures = 0;

    u8 *block = push_array(arena, u8, 4096);
    mem_set(block, 0xAB, 4096);
    if (block[0] != 0xAB || block[4095] != 0xAB) { failures += 1; }

    ArenaTemp temp = arena_temp_begin(arena);
    String8 text = str8_cat(arena, str8_lit("mini"), str8_lit("disk"));
    if (!str8_eq(text, str8_lit("minidisk"))) { failures += 1; }
    arena_temp_end(temp);
    if (arena_pos(arena) != temp.pos) { failures += 1; }

    if (hash64_mix(0) == 0) { failures += 1; }
    if (sqrt_f32(16.0f) != 4.0f) { failures += 1; }
    if (os_time_now_us() == 0 && os_thread_current_id() == 0) { failures += 1; }

    // Literal messages on purpose: str8f is exercised by the tests, and keeping it
    // out of this path keeps the release exe free of code nothing else calls yet.
    os_debug_print(failures == 0 ? str8_lit("selftest: ok\n") : str8_lit("selftest: FAILED\n"));
    arena_release(arena);
    return failures;
}

static b32 win32_command_line_has(const u16 *command_line, String8 flag) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 text = str8_from_cstr16(scratch.arena, command_line);
    b32 found = str8_find(text, flag, 0) != text.size;
    scratch_end(scratch);
    return found;
}

static i32 minidisk_main(void) {
    os_init();
    if (win32_command_line_has((const u16 *)GetCommandLineW(), str8_lit("--selftest"))) {
        return win32_selftest();
    }
    win32_window_run();
    return 0;
}

#if BUILD_NO_CRT
void __stdcall entry_point(void) { os_exit(minidisk_main()); }
#else
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show) {
    Unused(instance);
    Unused(previous);
    Unused(command_line);
    Unused(show);
    return minidisk_main();
}
#endif

#endif // !BUILD_TEST && !BUILD_BENCH

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
        // The bytes must be on the platter before the rename that publishes
        // them, or a power cut leaves an empty file under the real name.
        if (ok) { ok = FlushFileBuffers(file) != 0; }
        CloseHandle(file);
    }
    scratch_end(scratch);
    return ok;
}

b32 os_file_move_replace(String8 from, String8 to) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String16 from16 = str16_from_str8(scratch.arena, from);
    String16 to16 = str16_from_str8(scratch.arena, to);
    b32 ok = MoveFileExW((LPCWSTR)from16.str, (LPCWSTR)to16.str,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    scratch_end(scratch);
    return ok;
}

b32 os_file_map(OsFileMap *map, String8 path) {
    StructZero(map);
    ArenaTemp scratch = scratch_begin(0, 0);
    String16 path16 = str16_from_str8(scratch.arena, path);
    HANDLE file = CreateFileW((LPCWSTR)path16.str, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, 0);
    scratch_end(scratch);
    if (file == INVALID_HANDLE_VALUE) { return 0; }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
        CloseHandle(file);
        return 0;
    }
    HANDLE mapping = CreateFileMappingW(file, 0, PAGE_READONLY, 0, 0, 0);
    if (!mapping) {
        CloseHandle(file);
        return 0;
    }
    void *base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!base) {
        CloseHandle(mapping);
        CloseHandle(file);
        return 0;
    }
    map->data = (u8 *)base;
    map->size = (u64)size.QuadPart;
    map->file = file;
    map->mapping = mapping;
    return 1;
}

void os_file_unmap(OsFileMap *map) {
    if (map->data) { UnmapViewOfFile(map->data); }
    if (map->mapping) { CloseHandle(map->mapping); }
    if (map->file) { CloseHandle(map->file); }
    StructZero(map);
}

String8 os_command_line(Arena *arena) {
    String8 text = str8_from_cstr16(arena, (const u16 *)GetCommandLineW());
    // Past the exe path, quoted or not: everything the user typed after it.
    u64 at = 0;
    if (text.size && text.str[0] == '"') {
        at = str8_find(text, str8_lit("\""), 1);
        at = (at < text.size) ? at + 1 : text.size;
    } else {
        at = str8_find(text, str8_lit(" "), 0);
    }
    return str8_trim(str8_skip(text, Min(at, text.size)));
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

void os_sleep_us(u64 us) { Sleep((DWORD)((us + 999) / 1000)); }

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

// The window lives in win32_window.c and the demo loop in app/app.c; both are
// included after this file by the unity build (src/main.c).
static void app_run(void);

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
    app_run();
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

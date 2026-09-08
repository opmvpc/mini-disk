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

// The wall clock, which os_time_now_us deliberately is not: a monotonic counter
// cannot name a file after the moment it was written (T-022, the TOC backups).
void os_time_local(OsWallClock *out) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    out->year = now.wYear;
    out->month = now.wMonth;
    out->day = now.wDay;
    out->hour = now.wHour;
    out->minute = now.wMinute;
    out->second = now.wSecond;
}

void os_sleep_us(u64 us) { Sleep((DWORD)((us + 999) / 1000)); }

// --- power (T-042) ---------------------------------------------------------
// A SP transfer runs at 1x real time: an album is 45 minutes of the machine
// doing nothing the user can see, and the default idle timer suspends it in the
// middle of a track. ES_SYSTEM_REQUIRED without ES_DISPLAY_REQUIRED keeps the
// machine awake and lets the screen go dark, which is what a long copy wants.
// The flag is per thread, so this is called on the device thread and nowhere
// else, and cleared there too - a process that forgets it never sleeps again.
void os_power_keep_awake(b32 keep_awake) {
    SetThreadExecutionState(keep_awake ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED)
                                       : ES_CONTINUOUS);
}

// --- entropy (T-042) -------------------------------------------------------
// The host nonce and the packet key have to be unpredictable, and bcrypt.dll is
// loaded by name rather than linked so the import table stays kernel32 +
// user32 (the KPI of every milestone). When it is not there - Wine, a stripped
// image - the fallback mixes QPC, the thread id and a counter through the
// project's hash: not cryptographic, and the comment says so, but a transfer
// that refuses to start because an optional DLL is missing helps nobody.
typedef LONG(WINAPI *Win32BCryptGenRandom)(void *, unsigned char *, ULONG, ULONG);
global Win32BCryptGenRandom win32_bcrypt_gen_random;
global b32 win32_bcrypt_tried;

void os_random_bytes(void *dst, u64 size) {
    if (!win32_bcrypt_tried) {
        win32_bcrypt_tried = 1;
        HMODULE module = LoadLibraryW(L"bcrypt.dll");
        if (module) {
            win32_bcrypt_gen_random =
                (Win32BCryptGenRandom)GetProcAddress(module, "BCryptGenRandom");
        }
    }
    if (win32_bcrypt_gen_random &&
        win32_bcrypt_gen_random(0, (unsigned char *)dst, (ULONG)size, 0x00000002u) >= 0) {
        return;  // BCRYPT_USE_SYSTEM_PREFERRED_RNG
    }
    global u64 fallback_counter;
    u8 *out = (u8 *)dst;
    for (u64 i = 0; i < size; i += 1) {
        fallback_counter += 1;
        u64 mixed = hash64_mix(os_time_now_us() ^ (fallback_counter * 0x9E3779B97F4A7C15ull) ^
                             ((u64)os_thread_current_id() << 32));
        out[i] = (u8)(mixed >> 24);
    }
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

// The window lives in win32_window.c and the demo loop in app/app.c; both are
// included after this file by the unity build (src/main.c).
static void app_run(void);

// --- self test (--selftest): boot the base layer without opening a window ---

// Eight frames of 44,1 kHz mono PCM 16 in a canonical RIFF/WAVE, 60 bytes.
// It is here rather than in a fixture file because the point is to prove
// something about the *shipped exe*: the link line no longer carries
// /INCLUDE:codec_open (T-070), so nothing but a real call anchors the decoder
// table. This one opens a file through codec_open and reads it back.
static const u8 win32_selftest_wav[60] = {
    0x52, 0x49, 0x46, 0x46, 0x34, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45,
    0x66, 0x6D, 0x74, 0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x44, 0xAC, 0x00, 0x00, 0x88, 0x58, 0x01, 0x00, 0x02, 0x00, 0x10, 0x00,
    0x64, 0x61, 0x74, 0x61, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x10, 0x00, 0x20, 0x00, 0x30,
    0x00, 0x40, 0x00, 0x50, 0x00, 0x60, 0x00, 0x70,
};

static i32 win32_selftest_decoder(Arena *arena) {
    u16 temp_dir[OS_PATH_MAX];
    if (GetTempPathW(OS_PATH_MAX, (LPWSTR)temp_dir) == 0) { return 1; }

    String8 path = os_path_join(arena, str8_from_cstr16(arena, temp_dir),
                                str8_lit("minidisk_selftest.wav"));
    String8 bytes = {(u8 *)win32_selftest_wav, sizeof(win32_selftest_wav)};
    if (!os_file_write_all(path, bytes)) { return 1; }

    i32 failures = 0;
    Decoder *decoder = 0;
    if (codec_open(&decoder, path) != CODEC_OK) {
        failures += 1;
    } else {
        if (decoder->info.sample_rate != 44100 || decoder->info.channels != 1) { failures += 1; }
        f32 samples[8] = {0};
        f32 *planes[1] = {samples};
        if (codec_read_f32_planar(decoder, planes, 8) != 8) { failures += 1; }
        codec_close(decoder);
    }
    os_file_delete(path);
    return failures;
}

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

    failures += win32_selftest_decoder(arena);

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

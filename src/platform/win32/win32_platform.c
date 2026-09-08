// win32_platform.c - Win32 implementation of platform.h, entry point and black window.
#include <windows.h>

#include "../platform.h"
#include "../../base/base_hash.h"
#include "../../base/base_math.h"
#include "../../base/base_jobs.h"

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

// Every way out of the process goes through here, an AssertAlways included
// (base.h): emptying the log ring on the way is what puts the last line an
// assertion printed on disk, without a single byte of code at the assert site.
no_return void os_exit(i32 code) {
    os_log_flush();
    ExitProcess((UINT)code);
}

// --- virtual memory --------------------------------------------------------

void *os_memory_reserve(u64 size) { return VirtualAlloc(0, size, MEM_RESERVE, PAGE_READWRITE); }

b32 os_memory_commit(void *ptr, u64 size) {
    return VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) != 0;
}

void os_memory_release(void *ptr, u64 size) {
    Unused(size);
    VirtualFree(ptr, 0, MEM_RELEASE);
}

u32 os_last_error(void) { return (u32)GetLastError(); }

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

// --- the error log ---------------------------------------------------------
// The ring is written by every thread and drained by one job. Its lock is held
// for a mem_copy and nothing else: no thread ever waits on a disk behind it,
// which is the whole point (P-009's lesson applied to the log).

#define WIN32_LOG_PATH_MAX 512
#define WIN32_LOG_STAGING  KB(8)

typedef struct Win32Log {
    OsMutex mutex;
    b32 ready;
    u8 ring[OS_LOG_RING_SIZE];
    u64 write, read;  // monotonic, masked into the ring: write - read is pending
    u64 dropped, dropped_written;
    u64 last_flush_us;
    volatile u32 flushing;  // one flush at a time, whoever asked for it
    u16 path[WIN32_LOG_PATH_MAX];
    u64 path_size;
} Win32Log;

global Win32Log win32_log;

md_inline u64 win32_log_pending(void) { return win32_log.write - win32_log.read; }

// Append, or refuse and count. Refusing the newest line rather than dropping
// the oldest keeps the ring in file order: a log whose middle is missing is a
// log nobody can read, and the count says exactly how much went missing.
static void win32_log_append(String8 s) {
    os_mutex_lock(&win32_log.mutex);
    if (s.size > OS_LOG_RING_SIZE - win32_log_pending()) {
        win32_log.dropped += s.size;
    } else {
        u64 at = win32_log.write & (OS_LOG_RING_SIZE - 1);
        u64 first = Min(s.size, OS_LOG_RING_SIZE - at);
        mem_copy(win32_log.ring + at, s.str, first);
        if (first < s.size) { mem_copy(win32_log.ring, s.str + first, s.size - first); }
        win32_log.write += s.size;
    }
    os_mutex_unlock(&win32_log.mutex);
}

// Opened and closed per flush: at most one pair of syscalls a second, and no
// handle left dangling when the process dies inside an assertion.
static HANDLE win32_log_open(void) {
    if (win32_log.path_size == 0) { return INVALID_HANDLE_VALUE; }
    HANDLE file = CreateFileW((LPCWSTR)win32_log.path, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, 0);
    // This is the boundary, so the two shapes of "no file" become one here:
    // CreateFileW answers INVALID_HANDLE_VALUE, but /analyze only knows that a
    // HANDLE may be null, and one comparison is cheaper than arguing with it.
    return (file == 0) ? INVALID_HANDLE_VALUE : file;
}

void os_log_flush(void) {
    if (!win32_log.ready) { return; }
    // A flush already running drains everything this one would have: leaving
    // is not losing a line, and it is what keeps os_exit from waiting on a job.
    if (os_atomic_cas_u32(&win32_log.flushing, 0, 1) != 0) { return; }
    global u8 staging[WIN32_LOG_STAGING];  // guarded by `flushing`
    HANDLE file = INVALID_HANDLE_VALUE;
    for (;;) {
        os_mutex_lock(&win32_log.mutex);
        u64 size = Min(win32_log_pending(), (u64)sizeof(staging));
        u64 at = win32_log.read & (OS_LOG_RING_SIZE - 1);
        u64 first = Min(size, OS_LOG_RING_SIZE - at);
        mem_copy(staging, win32_log.ring + at, first);
        if (first < size) { mem_copy(staging + first, win32_log.ring, size - first); }
        win32_log.read += size;
        os_mutex_unlock(&win32_log.mutex);
        if (size == 0) { break; }
        if (file == INVALID_HANDLE_VALUE) { file = win32_log_open(); }
        if (file == INVALID_HANDLE_VALUE || file == 0) { break; }
        DWORD written = 0;
        WriteFile(file, staging, (DWORD)size, &written, 0);
    }
    if (file != INVALID_HANDLE_VALUE && file != 0) {
        if (win32_log.dropped != win32_log.dropped_written) {
            u8 line[128];
            String8 note = str8f_buf(line, sizeof(line),
                                       "[log] ring plein: %llu octet(s) perdu(s)\n",
                                       win32_log.dropped);
            DWORD written = 0;
            WriteFile(file, note.str, (DWORD)note.size, &written, 0);
            win32_log.dropped_written = win32_log.dropped;
        }
        CloseHandle(file);
    }
    os_atomic_store_u32(&win32_log.flushing, 0);
}

static void win32_log_flush_job(void *data, u64 begin, u64 end) {
    Unused(data);
    Unused(begin);
    Unused(end);
    os_log_flush();
}

void os_log_tick(u64 now_us) {
    if (!win32_log.ready) { return; }
    u64 pending = win32_log_pending();
    if (pending == 0) { return; }
    // Half a ring is the point past which waiting for the second would start
    // costing lines, so it goes out now whatever the clock says.
    if (pending < OS_LOG_RING_SIZE / 2 && now_us - win32_log.last_flush_us < OS_LOG_FLUSH_US) {
        return;
    }
    win32_log.last_flush_us = now_us;
    // With no pool - the tests, the selftest - there is no other thread to hand
    // it to and the caller writes it itself.
    if (jobs_worker_count() != 0) {
        jobs_push(0, win32_log_flush_job, 0);
    } else {
        os_log_flush();
    }
}

// Rotation: the files are named after the day, so byte order is age order.
// Everything past the four oldest goes, which leaves room for today's.
static void win32_log_rotate(Arena *arena, String8 dir) {
    String8List files;
    StructZero(&files);
    OsDirIter it;
    if (os_dir_iter_begin(&it, dir)) {
        OsFileInfo info;
        while (os_dir_iter_next(&it, &info)) {
            if (info.is_dir) { continue; }
            if (!str8_starts_with(info.name, str8_lit("minidisk-"))) { continue; }
            if (!str8_ends_with(info.name, str8_lit(".txt"))) { continue; }
            str8_list_push(arena, &files, str8_copy(arena, info.name));
        }
        os_dir_iter_end(&it);
    }
    while (files.count >= OS_LOG_FILE_MAX && files.first != 0) {
        String8Node *oldest = files.first;
        String8Node *previous = 0;
        String8Node *before_oldest = 0;
        for (String8Node *node = files.first; node != 0; node = node->next) {
            if (str8_cmp(node->str, oldest->str) < 0) {
                oldest = node;
                before_oldest = previous;
            }
            previous = node;
        }
        os_file_delete(os_path_join(arena, dir, oldest->str));
        if (before_oldest) {
            before_oldest->next = oldest->next;
        } else {
            files.first = oldest->next;
        }
        files.count -= 1;
    }
}

void os_log_init(String8 base_dir) {
    if (win32_log.ready) { return; }
    os_mutex_init(&win32_log.mutex);
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 dir = os_path_join(scratch.arena, base_dir, str8_lit("logs"));
    if (os_dir_create(dir)) {
        win32_log_rotate(scratch.arena, dir);
        OsWallClock now;
        os_time_local(&now);
        String8 name = str8f(scratch.arena, "minidisk-%04u-%02u-%02u.txt", now.year, now.month,
                             now.day);
        String16 path = str16_from_str8(scratch.arena, os_path_join(scratch.arena, dir, name));
        if (path.size < WIN32_LOG_PATH_MAX) {
            for (u64 i = 0; i <= path.size; i += 1) { win32_log.path[i] = path.str[i]; }
            win32_log.path_size = path.size;
            win32_log.last_flush_us = os_time_now_us();
            win32_log.ready = 1;
        }
    }
    scratch_end(scratch);
}

void os_log_shutdown(void) {
    if (!win32_log.ready) { return; }
    os_log_flush();
    win32_log.ready = 0;
    // The path stays: os_log_path still names the file that was written, which
    // is what the shutdown test looks at once the log is closed.
}

u64 os_log_dropped(void) { return win32_log.dropped; }

String8 os_log_path(Arena *arena) {
    if (win32_log.path_size == 0) { return str8(0, 0); }
    String16 path;
    path.str = win32_log.path;
    path.size = win32_log.path_size;
    return str8_from_str16(arena, path);
}

void os_debug_print(String8 s) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 text = str8_copy(scratch.arena, s);  // guarantees the null terminator
    if (win32_log.ready) { win32_log_append(text); }
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

// T-073: everything the frame loop starts, started and stopped without a
// window, in the order app_run stops it. What it proves is that the order is
// executable, not just written down in a comment: a device thread that is not
// joined, a save job left in the ring or a log file that is never opened all
// show up here as a failure or as a hang.
static i32 win32_selftest_lifecycle(Arena *arena) {
    i32 failures = 0;
    String8 dir = os_path_join(arena, os_known_folder(arena, OsKnownFolder_Temp),
                               str8_lit("minidisk-selftest"));
    os_dir_create(dir);
    os_log_init(dir);

    jobs_init(0);
    if (jobs_worker_count() == 0 && os_cpu_count() > 1) { failures += 1; }

    // The device thread: started and joined, nothing posted to it, so no byte
    // ever reaches a real recorder from here.
    NetmdDevice *device = push_struct_zero(arena, NetmdDevice);
    netmd_device_start(device, arena_alloc(MB(4)));
    netmd_device_stop(device);

    // A plan through the save job, and the file it leaves behind.
    Plan *plan = push_struct_zero(arena, Plan);
    plan_init(plan, arena_alloc(MB(4)), arena_alloc(MB(4)));
    PlanSaver saver;
    plan_saver_init(&saver, arena_alloc(MB(8)));
    String8 plan_path = os_path_join(arena, dir, str8_lit("selftest.mdplan"));
    if (!plan_save_async(&saver, plan, plan_path)) { failures += 1; }
    plan_save_wait(&saver);
    if (plan_save_state(&saver) != PlanSave_Done) { failures += 1; }
    OsFileInfo info;
    StructZero(&info);
    if (!os_file_stat(plan_path, &info) || info.size == 0) { failures += 1; }
    os_file_delete(plan_path);

    os_debug_print(str8_lit("selftest: journal\n"));
    jobs_shutdown();
    os_log_flush();  // step 5: this thread empties the ring, no worker is left
    StructZero(&info);
    String8 log_path = os_log_path(arena);
    if (log_path.size == 0 || !os_file_stat(log_path, &info) || info.size == 0) { failures += 1; }
    if (os_log_dropped() != 0) { failures += 1; }
    // The log stays open: the "selftest: ok" line printed by the caller has to
    // reach the file too, and os_exit flushes what is left of the ring.
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

    failures += win32_selftest_lifecycle(arena);

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

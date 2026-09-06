// win32_file.c - the file system half of platform.h: directory iteration,
// stat, random access reads, path helpers and known folders (T-010).
//
// Nothing here allocates per entry: the iterator carries its own find buffer
// and hands out slices into it, which is what lets a 50 000 file scan run
// without touching an allocator on the hot path.
#include <windows.h>

#include "../platform.h"

StaticAssert(sizeof(WIN32_FIND_DATAW) <= 640, find_data_fits_in_os_dir_iter);

// FILETIME counts 100 ns ticks from 1601-01-01; the epoch is 11644473600 s away.
#define WIN32_UNIX_EPOCH_TICKS 116444736000000000ull

static u64 win32_file_time_us(FILETIME time) {
    u64 ticks = ((u64)time.dwHighDateTime << 32) | (u64)time.dwLowDateTime;
    if (ticks < WIN32_UNIX_EPOCH_TICKS) { return 0; }
    return (ticks - WIN32_UNIX_EPOCH_TICKS) / 10ull;
}

// The find data hands us UTF-16 names; they become UTF-8 in the iterator's own
// buffer, which the caller reads until it asks for the next entry.
static void win32_dir_iter_fill(OsDirIter *it, OsFileInfo *out) {
    WIN32_FIND_DATAW *find = (WIN32_FIND_DATAW *)it->opaque;
    u64 size = 0;
    for (u64 i = 0; find->cFileName[i] != 0 && size + 4 < OS_NAME_MAX; i += 1) {
        UnicodeDecode decode = utf16_decode((const u16 *)find->cFileName + i, 2);
        i += decode.advance - 1;
        size += utf8_encode(it->name + size, decode.codepoint);
    }
    out->name = str8(it->name, size);
    out->size = ((u64)find->nFileSizeHigh << 32) | (u64)find->nFileSizeLow;
    out->mtime_us = win32_file_time_us(find->ftLastWriteTime);
    out->is_dir = (find->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// "." and ".." are the OS talking about itself, never entries of the tree.
static b32 win32_dir_entry_is_dot(const WCHAR *name) {
    return name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0));
}

b32 os_dir_iter_begin(OsDirIter *it, String8 dir_path) {
    StructZero(it);
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 pattern = str8_cat(scratch.arena, dir_path, str8_lit("\\*"));
    String16 pattern16 = str16_from_str8(scratch.arena, pattern);
    HANDLE find = FindFirstFileExW((LPCWSTR)pattern16.str, FindExInfoBasic,
                                   (WIN32_FIND_DATAW *)it->opaque, FindExSearchNameMatch, 0,
                                   FIND_FIRST_EX_LARGE_FETCH);
    scratch_end(scratch);
    if (find == INVALID_HANDLE_VALUE) { return 0; }
    it->handle = find;
    it->pending = 1;
    return 1;
}

b32 os_dir_iter_next(OsDirIter *it, OsFileInfo *out) {
    WIN32_FIND_DATAW *find = (WIN32_FIND_DATAW *)it->opaque;
    for (;;) {
        if (it->pending) {
            it->pending = 0;
        } else if (!FindNextFileW(it->handle, find)) {
            return 0;
        }
        // Reparse points would let a junction loop the walk back onto itself.
        if (win32_dir_entry_is_dot(find->cFileName)) { continue; }
        if (find->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { continue; }
        win32_dir_iter_fill(it, out);
        return 1;
    }
}

void os_dir_iter_end(OsDirIter *it) {
    if (it->handle) { FindClose(it->handle); }
    it->handle = 0;
}

b32 os_file_stat(String8 path, OsFileInfo *out) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String16 path16 = str16_from_str8(scratch.arena, path);
    WIN32_FILE_ATTRIBUTE_DATA data;
    b32 ok = GetFileAttributesExW((LPCWSTR)path16.str, GetFileExInfoStandard, &data) != 0;
    scratch_end(scratch);
    if (!ok) { return 0; }
    StructZero(out);
    out->size = ((u64)data.nFileSizeHigh << 32) | (u64)data.nFileSizeLow;
    out->mtime_us = win32_file_time_us(data.ftLastWriteTime);
    out->is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return 1;
}

b32 os_dir_create(String8 path) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String16 path16 = str16_from_str8(scratch.arena, path);
    b32 ok = CreateDirectoryW((LPCWSTR)path16.str, 0) != 0 ||
             GetLastError() == ERROR_ALREADY_EXISTS;
    scratch_end(scratch);
    return ok;
}

b32 os_file_delete(String8 path) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String16 path16 = str16_from_str8(scratch.arena, path);
    b32 ok = DeleteFileW((LPCWSTR)path16.str) != 0;
    scratch_end(scratch);
    return ok;
}

b32 os_dir_delete(String8 path) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String16 path16 = str16_from_str8(scratch.arena, path);
    b32 ok = RemoveDirectoryW((LPCWSTR)path16.str) != 0;
    scratch_end(scratch);
    return ok;
}

OsFile os_file_open(String8 path) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String16 path16 = str16_from_str8(scratch.arena, path);
    HANDLE file = CreateFileW((LPCWSTR)path16.str, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, 0);
    scratch_end(scratch);
    OsFile result;
    result.v = (file == INVALID_HANDLE_VALUE) ? 0 : file;
    return result;
}

u64 os_file_read_at(OsFile file, u64 offset, void *dst, u64 size) {
    Assert(file.v != 0);
    u64 total = 0;
    while (total < size) {
        // OVERLAPPED carries the offset, so the handle keeps no file pointer
        // and several threads may read the same file at once.
        OVERLAPPED overlapped;
        StructZero(&overlapped);
        u64 at = offset + total;
        overlapped.Offset = (DWORD)(at & 0xFFFFFFFFull);
        overlapped.OffsetHigh = (DWORD)(at >> 32);
        DWORD chunk = (DWORD)Min(size - total, (u64)0x40000000u);
        DWORD got = 0;
        if (!ReadFile(file.v, (u8 *)dst + total, chunk, &got, &overlapped) || got == 0) {
            break;
        }
        total += got;
    }
    return total;
}

void os_file_close(OsFile file) {
    if (file.v) { CloseHandle(file.v); }
}

// --- paths -----------------------------------------------------------------

b32 os_path_is_separator(u8 c) { return c == '\\' || c == '/'; }

String8 os_path_join(Arena *arena, String8 a, String8 b) {
    while (a.size > 0 && os_path_is_separator(a.str[a.size - 1])) { a.size -= 1; }
    while (b.size > 0 && os_path_is_separator(b.str[0])) { b = str8_skip(b, 1); }
    if (a.size == 0) { return str8_copy(arena, b); }
    if (b.size == 0) { return str8_copy(arena, a); }
    u8 *bytes = push_array(arena, u8, a.size + 1 + b.size + 1);
    mem_copy(bytes, a.str, a.size);
    bytes[a.size] = '\\';
    mem_copy(bytes + a.size + 1, b.str, b.size);
    bytes[a.size + 1 + b.size] = 0;
    return str8(bytes, a.size + 1 + b.size);
}

static u64 win32_path_last_separator(String8 path) {
    for (u64 i = path.size; i > 0; i -= 1) {
        if (os_path_is_separator(path.str[i - 1])) { return i - 1; }
    }
    return path.size;
}

String8 os_path_parent(String8 path) {
    while (path.size > 0 && os_path_is_separator(path.str[path.size - 1])) { path.size -= 1; }
    u64 at = win32_path_last_separator(path);
    if (at == path.size) { return str8(path.str, 0); }
    return str8_prefix(path, at);
}

String8 os_path_filename(String8 path) {
    while (path.size > 0 && os_path_is_separator(path.str[path.size - 1])) { path.size -= 1; }
    u64 at = win32_path_last_separator(path);
    if (at == path.size) { return path; }
    return str8_skip(path, at + 1);
}

String8 os_path_extension(String8 path) {
    String8 name = os_path_filename(path);
    for (u64 i = name.size; i > 0; i -= 1) {
        if (name.str[i - 1] == '.') { return str8_skip(name, i); }
    }
    return str8(name.str, 0);
}

String8 os_path_normalize(Arena *arena, String8 path) {
    while (path.size > 0 && os_path_is_separator(path.str[path.size - 1])) { path.size -= 1; }
    String8 result = str8_copy(arena, path);
    for (u64 i = 0; i < result.size; i += 1) {
        if (result.str[i] == '/') { result.str[i] = '\\'; }
    }
    return result;
}

// --- known folders ---------------------------------------------------------
// shell32 and ole32 are loaded by hand: linking them would put two more DLLs in
// the import table for three calls (ADR-002).

typedef HRESULT(WINAPI *Win32SHGetKnownFolderPath)(const GUID *id, DWORD flags, HANDLE token,
                                                   PWSTR *out);
typedef void(WINAPI *Win32CoTaskMemFree)(void *ptr);

typedef struct Win32ShellApi {
    b32 loaded;
    Win32SHGetKnownFolderPath get_known_folder_path;
    Win32CoTaskMemFree task_mem_release;
} Win32ShellApi;

global Win32ShellApi win32_shell;

static b32 win32_shell_load(void) {
    if (win32_shell.loaded) { return win32_shell.get_known_folder_path != 0; }
    win32_shell.loaded = 1;
    HMODULE shell = LoadLibraryW(L"shell32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    if (shell && ole) {
        win32_shell.get_known_folder_path =
            (Win32SHGetKnownFolderPath)GetProcAddress(shell, "SHGetKnownFolderPath");
        win32_shell.task_mem_release =
            (Win32CoTaskMemFree)GetProcAddress(ole, "CoTaskMemFree");
    }
    return win32_shell.get_known_folder_path != 0 && win32_shell.task_mem_release != 0;
}

static const GUID win32_folderid_music = {
    0x4BD8D571, 0x6D19, 0x48D3, {0xBE, 0x97, 0x42, 0x22, 0x20, 0x08, 0x0E, 0x43}};
static const GUID win32_folderid_local_appdata = {
    0xF1B32785, 0x6FBA, 0x4FCF, {0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91}};

String8 os_known_folder(Arena *arena, OsKnownFolder folder) {
    String8 result = str8(0, 0);
    if (folder == OsKnownFolder_Temp) {
        WCHAR buffer[MAX_PATH + 2];
        DWORD size = GetTempPathW(MAX_PATH + 1, buffer);
        if (size == 0) { return result; }
        String16 text;
        text.str = (u16 *)buffer;
        text.size = size;
        result = os_path_normalize(arena, str8_from_str16(arena, text));
        return result;
    }
    if (!win32_shell_load()) { return result; }
    const GUID *id = (folder == OsKnownFolder_Music) ? &win32_folderid_music
                                                     : &win32_folderid_local_appdata;
    PWSTR path = 0;
    if (win32_shell.get_known_folder_path(id, 0, 0, &path) == S_OK && path) {
        result = str8_from_cstr16(arena, (const u16 *)path);
        win32_shell.task_mem_release(path);
    }
    return result;
}

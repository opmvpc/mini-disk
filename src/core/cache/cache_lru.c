// cache_lru.c - see cache_lru.h.
#include "cache_lru.h"

static const char cache_lru_digits[] = "0123456789abcdef";

String8 cache_lru_name(Arena *arena, String8 dir, u64 key, String8 extension) {
    u8 stem[16];
    for (u32 i = 0; i < 16; i += 1) {
        stem[i] = (u8)cache_lru_digits[(key >> ((15 - i) * 4)) & 0xFu];
    }
    ArenaTemp scratch = scratch_begin(&arena, 1);
    String8 file = str8f(scratch.arena, "%S.%S", str8(stem, 16), extension);
    String8 result = os_path_join(arena, dir, file);
    scratch_end(scratch);
    return result;
}

md_inline u32 cache_lru_hex_value(u8 c) {
    if (c >= '0' && c <= '9') { return (u32)(c - '0'); }
    if (c >= 'a' && c <= 'f') { return (u32)(c - 'a') + 10u; }
    if (c >= 'A' && c <= 'F') { return (u32)(c - 'A') + 10u; }
    return 16u;  // not a hexadecimal digit
}

u64 cache_lru_key_from_name(String8 name) {
    if (name.size < 16) { return 0; }
    u64 key = 0;
    for (u32 i = 0; i < 16; i += 1) {
        u32 value = cache_lru_hex_value(name.str[i]);
        if (value == 16u) { return 0; }
        key = (key << 4) | value;
    }
    return key;
}

// The extension test, without the dot and case insensitively: a directory a
// human has looked at can hold anything, and only our own files are candidates.
static b32 cache_lru_extension_is(String8 name, String8 extension) {
    String8 found = os_path_extension(name);
    if (found.size != extension.size) { return 0; }
    for (u64 i = 0; i < found.size; i += 1) {
        u8 a = found.str[i];
        u8 b = extension.str[i];
        if (a >= 'A' && a <= 'Z') { a = (u8)(a + 32); }
        if (b >= 'A' && b <= 'Z') { b = (u8)(b + 32); }
        if (a != b) { return 0; }
    }
    return 1;
}

typedef struct CacheLruFile {
    u64 when_us;
    u64 size;
    u32 name_size;
    u8 name[OS_NAME_MAX];
} CacheLruFile;

void cache_lru_purge(Arena *scratch_arena, String8 dir, String8 extension, u64 max_bytes,
                     const CacheLruAccess *access, u32 access_count, CacheLruStats *out) {
    CacheLruStats stats;
    StructZero(&stats);
    ArenaTemp scratch = arena_temp_begin(scratch_arena);
    CacheLruFile *files = push_array(scratch_arena, CacheLruFile, CACHE_LRU_MAX_FILES);

    OsDirIter it;
    if (os_dir_iter_begin(&it, dir)) {
        OsFileInfo info;
        while (os_dir_iter_next(&it, &info)) {
            if (info.is_dir || !cache_lru_extension_is(info.name, extension)) { continue; }
            if (stats.files_before >= CACHE_LRU_MAX_FILES) {
                stats.truncated = 1;
                break;
            }
            CacheLruFile *file = &files[stats.files_before];
            file->size = info.size;
            file->when_us = info.mtime_us;
            // A cache that remembers its own reads overrides the mtime: a file
            // read yesterday and written last month is a recent file.
            u64 key = cache_lru_key_from_name(info.name);
            for (u32 i = 0; i < access_count && key != 0; i += 1) {
                if (access[i].key == key) {
                    file->when_us = access[i].last_us;
                    break;
                }
            }
            file->name_size = (u32)Min(info.name.size, (u64)(OS_NAME_MAX - 1));
            mem_copy(file->name, info.name.str, file->name_size);
            stats.bytes_before += info.size;
            stats.files_before += 1;
        }
        os_dir_iter_end(&it);
    }

    stats.bytes_after = stats.bytes_before;
    if (stats.bytes_before > max_bytes && stats.files_before != 0) {
        // Sorted by index rather than by record: a CacheLruFile is 800 bytes,
        // and shifting those around is what turns an insertion sort into a
        // memory bandwidth problem. The array is a cache directory, so this
        // runs once at startup over a few thousand entries.
        u32 *order = push_array(scratch_arena, u32, stats.files_before);
        for (u32 i = 0; i < stats.files_before; i += 1) { order[i] = i; }
        for (u32 i = 1; i < stats.files_before; i += 1) {
            u32 value = order[i];
            u64 when = files[value].when_us;
            u32 j = i;
            while (j > 0 && files[order[j - 1]].when_us > when) {
                order[j] = order[j - 1];
                j -= 1;
            }
            order[j] = value;
        }
        for (u32 i = 0; i < stats.files_before && stats.bytes_after > max_bytes; i += 1) {
            CacheLruFile *file = &files[order[i]];
            String8 path = os_path_join(scratch_arena, dir, str8(file->name, file->name_size));
            if (!os_file_delete(path)) { continue; }  // held open by a reader: next time
            stats.bytes_after -= file->size;
            stats.bytes_deleted += file->size;
            stats.files_deleted += 1;
        }
    }

    arena_temp_end(scratch);
    if (out) { *out = stats; }
}

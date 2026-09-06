#include "lib_cache.h"

// The columns, in the order lib_grow carves them: descending alignment, so the
// on disk block needs no padding and no per field decoding. One list, used by
// both directions - a column added to the model and forgotten here would not
// compile.
#define LIB_CACHE_COLUMNS(X)                                                   \
    X(u64, size) X(u64, mtime_us) X(u64, cover_hash)                           \
    X(u32, path_id) X(u32, title_id) X(u32, artist_id) X(u32, album_id)        \
    X(u32, album_artist_id) X(u32, genre_id) X(u32, duration_ms)               \
    X(u32, sample_rate) X(u32, flags) X(u32, stamp)                            \
    X(u16, track_no) X(u16, disc_no) X(u16, year) X(i16, replaygain_track_db)  \
    X(u8, channels) X(u8, codec)

static const u8 lib_cache_magic[8] = {'M', 'D', 'S', 'K', 'L', 'I', 'B', 0};

md_inline u64 lib_cache_align(u64 value) { return AlignPow2(value, 8ull); }

// --- writing ----------------------------------------------------------------

LibCacheStatus lib_cache_save(const Library *lib, String8 path, String8 root) {
    ArenaTemp scratch = scratch_begin(0, 0);
    u64 count = lib->count;
    u64 soa_size = count * LIB_BYTES_PER_TRACK;
    u64 strings_size = lib->strings.size;
    u64 slot_bytes = (u64)lib->strings.slot_count * sizeof(u32);

    LibCacheHeader header;
    StructZero(&header);
    mem_copy(header.magic, lib_cache_magic, sizeof(header.magic));
    header.version = LIB_CACHE_VERSION;
    header.track_count = count;
    header.live_count = lib->live_count;
    header.free_head = lib->free_head;
    header.generation = lib->generation;
    header.soa_offset = lib_cache_align(sizeof(LibCacheHeader));
    header.soa_size = soa_size;
    header.strings_offset = lib_cache_align(header.soa_offset + soa_size);
    header.strings_size = strings_size;
    header.string_count = lib->strings.count;
    header.slots_offset = lib_cache_align(header.strings_offset + strings_size);
    header.slot_count = lib->strings.slot_count;
    header.root_offset = lib_cache_align(header.slots_offset + slot_bytes);
    header.root_size = root.size;
    header.created_us = os_time_now_us();
    header.file_size = lib_cache_align(header.root_offset + root.size);

    u8 *buffer = push_array_zero(scratch.arena, u8, header.file_size);
    mem_copy(buffer, &header, sizeof(header));
    u8 *at = buffer + header.soa_offset;
#define LIB_CACHE_WRITE(T, field)                          \
    mem_copy(at, lib->field, sizeof(T) * count);           \
    at += sizeof(T) * count;
    LIB_CACHE_COLUMNS(LIB_CACHE_WRITE)
#undef LIB_CACHE_WRITE
    AssertAlways((u64)(at - buffer) == header.soa_offset + soa_size);
    mem_copy(buffer + header.strings_offset, lib->strings.base, strings_size);
    mem_copy(buffer + header.slots_offset, lib->strings.slots, slot_bytes);
    mem_copy(buffer + header.root_offset, root.str, root.size);

    // Atomic: the bytes reach the disk under a temporary name, and only a
    // rename publishes them (ADR-010 s9.3).
    String8 tmp = str8_cat(scratch.arena, path, str8_lit(".tmp"));
    LibCacheStatus status = LibCache_Ok;
    if (!os_file_write_all(tmp, str8(buffer, header.file_size)) ||
        !os_file_move_replace(tmp, path)) {
        os_file_delete(tmp);
        status = LibCache_WriteFailed;
    }
    scratch_end(scratch);
    return status;
}

// --- reading: the boundary --------------------------------------------------

md_inline b32 lib_cache_section_ok(u64 offset, u64 size, u64 file_size) {
    return (offset % 8) == 0 && offset <= file_size && size <= file_size - offset;
}

md_inline b32 lib_cache_string_ok(const u8 *strings, u64 size, u32 id) {
    if ((u64)id + 2 > size) { return 0; }
    u64 length = (u64)strings[id] | ((u64)strings[id + 1] << 8);
    return (u64)id + 2 + length <= size;
}

static b32 lib_cache_validate(const LibCacheHeader *header, const u8 *base, u64 mapped_size) {
    if (header->file_size != mapped_size) { return 0; }
    if (header->track_count > LIB_CACHE_MAX_TRACKS) { return 0; }
    if (header->live_count > header->track_count) { return 0; }
    if (header->strings_size < 2 || header->strings_size > U32_MAX) { return 0; }
    if (!IsPow2(header->slot_count) || header->slot_count > LIB_CACHE_MAX_TRACKS * 4) {
        return 0;
    }
    if (header->soa_size != header->track_count * LIB_BYTES_PER_TRACK) { return 0; }
    if (!lib_cache_section_ok(header->soa_offset, header->soa_size, mapped_size)) { return 0; }
    if (!lib_cache_section_ok(header->strings_offset, header->strings_size, mapped_size)) {
        return 0;
    }
    if (!lib_cache_section_ok(header->slots_offset, header->slot_count * sizeof(u32),
                              mapped_size)) {
        return 0;
    }
    if (!lib_cache_section_ok(header->root_offset, header->root_size, mapped_size)) { return 0; }
    if (header->root_size > OS_PATH_MAX) { return 0; }
    if (header->soa_offset < sizeof(LibCacheHeader)) { return 0; }

    const u8 *strings = base + header->strings_offset;
    u64 strings_size = header->strings_size;
    if (strings[0] != 0 || strings[1] != 0) { return 0; }  // offset 0: the empty string

    // The intern table: every slot points at a whole string, the occupancy
    // matches the count, and the load factor is the one lib_intern assumes -
    // without which a probe could walk a full table forever.
    const u32 *slots = (const u32 *)(base + header->slots_offset);
    u64 occupied = 0;
    for (u64 i = 0; i < header->slot_count; i += 1) {
        u32 entry = slots[i];
        if (entry == 0) { continue; }
        if (!lib_cache_string_ok(strings, strings_size, entry - 1)) { return 0; }
        occupied += 1;
    }
    if (occupied != header->string_count) { return 0; }
    if ((header->string_count + 1) * 4 >= header->slot_count * 3) { return 0; }

    // The SoA: the string ids of every live track, the flag bits, the tombstone
    // free list. After this, lib_string and lib_track_live cannot be lied to.
    u64 count = header->track_count;
    const u8 *soa = base + header->soa_offset;
    const u32 *path_id = (const u32 *)(soa + count * 24);
    const u32 *title_id = path_id + count;
    const u32 *artist_id = title_id + count;
    const u32 *album_id = artist_id + count;
    const u32 *album_artist_id = album_id + count;
    const u32 *genre_id = album_artist_id + count;
    const u32 *flags = genre_id + count * 3;  // duration_ms, sample_rate, then flags
    u64 live = 0;
    for (u64 id = 0; id < count; id += 1) {
        u32 track_flags = flags[id];
        if (track_flags & ~(u32)(LibTrackFlag_Live | LibTrackFlag_NeedTag)) { return 0; }
        if (!(track_flags & LibTrackFlag_Live)) { continue; }
        live += 1;
        if (!lib_cache_string_ok(strings, strings_size, path_id[id]) ||
            !lib_cache_string_ok(strings, strings_size, title_id[id]) ||
            !lib_cache_string_ok(strings, strings_size, artist_id[id]) ||
            !lib_cache_string_ok(strings, strings_size, album_id[id]) ||
            !lib_cache_string_ok(strings, strings_size, album_artist_id[id]) ||
            !lib_cache_string_ok(strings, strings_size, genre_id[id])) {
            return 0;
        }
    }
    if (live != header->live_count) { return 0; }

    u64 dead = count - live;
    u64 walked = 0;
    u64 slot = header->free_head;
    while (slot != LIB_TRACK_NONE) {
        if (slot >= count || (flags[slot] & LibTrackFlag_Live)) { return 0; }
        if (walked > dead) { return 0; }  // the list loops
        walked += 1;
        slot = path_id[slot];
    }
    if (walked != dead) { return 0; }
    return 1;
}

LibCacheStatus lib_cache_load(Library *lib, String8 path, Arena *root_arena, String8 *root_out) {
    OsFileMap map;
    if (!os_file_map(&map, path)) { return LibCache_Missing; }
    if (map.size < sizeof(LibCacheHeader)) {
        os_file_unmap(&map);
        return LibCache_Corrupt;
    }
    LibCacheHeader header;
    mem_copy(&header, map.data, sizeof(header));
    if (mem_cmp(header.magic, lib_cache_magic, sizeof(lib_cache_magic)) != 0) {
        os_file_unmap(&map);
        return LibCache_Corrupt;
    }
    if (header.version != LIB_CACHE_VERSION) {
        os_file_unmap(&map);
        return LibCache_BadVersion;
    }
    if (!lib_cache_validate(&header, map.data, map.size)) {
        os_file_unmap(&map);
        return LibCache_Corrupt;
    }

    // Canonical from here on: the columns are copied out of the mapping into
    // the library's own arenas, which is what makes the library writable for
    // the rescan that follows. No field is decoded, only moved.
    Arena *arena = lib->arena;
    Arena *text = lib->text;
    arena_clear(arena);
    if (text != arena) { arena_clear(text); }
    lib_init(lib, arena, text);
    u32 count = (u32)header.track_count;
    lib_reserve(lib, count);

    u8 *blob = push_array(text, u8, header.strings_size);
    mem_copy(blob, map.data + header.strings_offset, header.strings_size);
    lib->strings.base = blob;
    lib->strings.size = header.strings_size;
    lib->strings.count = (u32)header.string_count;
    lib->strings.slot_count = (u32)header.slot_count;
    lib->strings.slots = push_array(arena, u32, header.slot_count);
    mem_copy(lib->strings.slots, map.data + header.slots_offset,
             header.slot_count * sizeof(u32));

    const u8 *at = map.data + header.soa_offset;
#define LIB_CACHE_READ(T, field)                             \
    mem_copy(lib->field, at, sizeof(T) * (u64)count);        \
    at += sizeof(T) * (u64)count;
    LIB_CACHE_COLUMNS(LIB_CACHE_READ)
#undef LIB_CACHE_READ

    lib->count = count;
    lib->live_count = (u32)header.live_count;
    lib->free_head = (u32)header.free_head;
    lib->generation = (u32)header.generation;
    lib_path_index_rebuild(lib);

    if (root_out) {
        *root_out = str8_copy(root_arena, str8(map.data + header.root_offset, header.root_size));
    }
    os_file_unmap(&map);
    return LibCache_Ok;
}

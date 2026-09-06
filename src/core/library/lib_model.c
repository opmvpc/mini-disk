#include "lib_model.h"

// --- string table ----------------------------------------------------------

void lib_strings_init(StringTable *table, Arena *arena, Arena *text) {
    StructZero(table);
    table->arena = arena;
    table->text = text;
    table->slot_count = 1024;
    table->slots = push_array_zero(arena, u32, table->slot_count);
    // Offset 0 is the empty string: one entry that every "no value" shares.
    table->base = push_array_zero(text, u8, 2);
    table->size = 2;
}

md_inline String8 lib_strings_at(const StringTable *table, u64 offset) {
    u8 *base = table->base + offset;
    u64 size = (u64)base[0] | ((u64)base[1] << 8);
    return str8(base + 2, size);
}

String8 lib_string(const StringTable *table, StringId id) {
    Assert((u64)id + 2 <= table->size);
    return lib_strings_at(table, id);
}

static void lib_strings_grow(StringTable *table) {
    u32 slot_count = table->slot_count * 2;
    u32 *slots = push_array_zero(table->arena, u32, slot_count);
    u32 mask = slot_count - 1;
    for (u32 i = 0; i < table->slot_count; i += 1) {
        u32 entry = table->slots[i];
        if (entry == 0) { continue; }
        String8 s = lib_strings_at(table, entry - 1);
        u32 slot = (u32)(hash64(s.str, s.size) & mask);
        while (slots[slot] != 0) { slot = (slot + 1) & mask; }
        slots[slot] = entry;
    }
    table->slots = slots;
    table->slot_count = slot_count;
}

StringId lib_intern(StringTable *table, String8 s) {
    if (s.size == 0) { return LIB_STRING_NONE; }
    AssertAlways(s.size <= U16_MAX);  // a path or a tag, never a file
    if ((table->count + 1) * 4 >= table->slot_count * 3) { lib_strings_grow(table); }
    u32 mask = table->slot_count - 1;
    u32 slot = (u32)(hash64(s.str, s.size) & mask);
    for (;;) {
        u32 entry = table->slots[slot];
        if (entry == 0) { break; }
        String8 found = lib_strings_at(table, entry - 1);
        if (str8_eq(found, s)) { return (StringId)(entry - 1); }
        slot = (slot + 1) & mask;
    }
    u64 offset = table->size;
    u8 *bytes = push_array(table->text, u8, s.size + 2);
    bytes[0] = (u8)(s.size & 0xFF);
    bytes[1] = (u8)(s.size >> 8);
    mem_copy(bytes + 2, s.str, s.size);
    table->size = offset + s.size + 2;
    table->slots[slot] = (u32)(offset + 1);
    table->count += 1;
    return (StringId)offset;
}

// --- the SoA ---------------------------------------------------------------

md_inline u64 lib_next_pow2(u64 x) {
    u64 result = 1;
    while (result < x) { result <<= 1; }
    return result;
}

static void lib_index_rebuild(Library *lib) {
    u32 mask = lib->index_slots - 1;
    mem_zero(lib->index, sizeof(u32) * lib->index_slots);
    for (u32 id = 0; id < lib->count; id += 1) {
        if (!(lib->flags[id] & LibTrackFlag_Live)) { continue; }
        String8 path = lib_string(&lib->strings, lib->path_id[id]);
        u32 slot = (u32)(hash64(path.str, path.size) & mask);
        while (lib->index[slot] != 0) { slot = (slot + 1) & mask; }
        lib->index[slot] = id + 1;
    }
}

static void lib_grow(Library *lib, u32 capacity) {
    Assert(capacity > lib->capacity);
    u8 *block = (u8 *)arena_push(lib->arena, (u64)capacity * LIB_BYTES_PER_TRACK, 8);
    u32 count = lib->count;

    // Carved by descending alignment, so no column needs padding.
#define LIB_TAKE(T, n) (T *)block; block += sizeof(T) * (u64)(n)
    u64 *size = LIB_TAKE(u64, capacity);
    u64 *mtime_us = LIB_TAKE(u64, capacity);
    u64 *cover_hash = LIB_TAKE(u64, capacity);
    u32 *path_id = LIB_TAKE(u32, capacity);
    u32 *title_id = LIB_TAKE(u32, capacity);
    u32 *artist_id = LIB_TAKE(u32, capacity);
    u32 *album_id = LIB_TAKE(u32, capacity);
    u32 *album_artist_id = LIB_TAKE(u32, capacity);
    u32 *genre_id = LIB_TAKE(u32, capacity);
    u32 *duration_ms = LIB_TAKE(u32, capacity);
    u32 *sample_rate = LIB_TAKE(u32, capacity);
    u32 *flags = LIB_TAKE(u32, capacity);
    u32 *stamp = LIB_TAKE(u32, capacity);
    u16 *track_no = LIB_TAKE(u16, capacity);
    u16 *disc_no = LIB_TAKE(u16, capacity);
    u16 *year = LIB_TAKE(u16, capacity);
    i16 *replaygain = LIB_TAKE(i16, capacity);
    u8 *channels = LIB_TAKE(u8, capacity);
    u8 *codec = LIB_TAKE(u8, capacity);
#undef LIB_TAKE

#define LIB_MOVE(field, T, dst) \
    if (count) { mem_copy(dst, lib->field, sizeof(T) * (u64)count); } lib->field = dst
    LIB_MOVE(size, u64, size);
    LIB_MOVE(mtime_us, u64, mtime_us);
    LIB_MOVE(cover_hash, u64, cover_hash);
    LIB_MOVE(path_id, u32, path_id);
    LIB_MOVE(title_id, u32, title_id);
    LIB_MOVE(artist_id, u32, artist_id);
    LIB_MOVE(album_id, u32, album_id);
    LIB_MOVE(album_artist_id, u32, album_artist_id);
    LIB_MOVE(genre_id, u32, genre_id);
    LIB_MOVE(duration_ms, u32, duration_ms);
    LIB_MOVE(sample_rate, u32, sample_rate);
    LIB_MOVE(flags, u32, flags);
    LIB_MOVE(stamp, u32, stamp);
    LIB_MOVE(track_no, u16, track_no);
    LIB_MOVE(disc_no, u16, disc_no);
    LIB_MOVE(year, u16, year);
    LIB_MOVE(replaygain_track_db, i16, replaygain);
    LIB_MOVE(channels, u8, channels);
    LIB_MOVE(codec, u8, codec);
#undef LIB_MOVE

    lib->capacity = capacity;
    lib->index_slots = (u32)lib_next_pow2((u64)capacity * 2);
    lib->index = push_array_zero(lib->arena, u32, lib->index_slots);
    lib_index_rebuild(lib);
}

void lib_init(Library *lib, Arena *arena, Arena *text) {
    StructZero(lib);
    lib->arena = arena;
    lib->text = text;
    lib->free_head = LIB_TRACK_NONE;
    lib_strings_init(&lib->strings, arena, text);
    lib_grow(lib, LIB_MIN_CAPACITY);
}

void lib_clear(Library *lib) {
    lib->count = 0;
    lib->live_count = 0;
    lib->free_head = LIB_TRACK_NONE;
    mem_zero(lib->index, sizeof(u32) * lib->index_slots);
}

TrackId lib_find_by_path(const Library *lib, String8 path) {
    u32 mask = lib->index_slots - 1;
    u32 slot = (u32)(hash64(path.str, path.size) & mask);
    for (;;) {
        u32 entry = lib->index[slot];
        if (entry == 0) { return LIB_TRACK_NONE; }
        TrackId id = entry - 1;
        if (str8_eq(lib_string(&lib->strings, lib->path_id[id]), path)) { return id; }
        slot = (slot + 1) & mask;
    }
}

String8 lib_track_path(const Library *lib, TrackId id) {
    Assert(lib_track_live(lib, id));
    return lib_string(&lib->strings, lib->path_id[id]);
}

TrackId lib_track_add(Library *lib, String8 path, u64 size, u64 mtime_us) {
    StringId path_id = lib_intern(&lib->strings, path);
    TrackId id;
    if (lib->free_head != LIB_TRACK_NONE) {
        id = lib->free_head;
        lib->free_head = lib->path_id[id];  // the free list threads through path_id
    } else {
        if (lib->count == lib->capacity) { lib_grow(lib, lib->capacity * 2); }
        id = lib->count;
        lib->count += 1;
    }

    lib->path_id[id] = path_id;
    lib->size[id] = size;
    lib->mtime_us[id] = mtime_us;
    lib->cover_hash[id] = 0;
    lib->title_id[id] = LIB_STRING_NONE;
    lib->artist_id[id] = LIB_STRING_NONE;
    lib->album_id[id] = LIB_STRING_NONE;
    lib->album_artist_id[id] = LIB_STRING_NONE;
    lib->genre_id[id] = LIB_STRING_NONE;
    lib->duration_ms[id] = 0;
    lib->sample_rate[id] = 0;
    lib->stamp[id] = lib->generation;
    lib->track_no[id] = 0;
    lib->disc_no[id] = 0;
    lib->year[id] = 0;
    lib->replaygain_track_db[id] = LIB_REPLAYGAIN_NONE;
    lib->channels[id] = 0;
    lib->codec[id] = (u8)lib_codec_from_extension(os_path_extension(path));
    lib->flags[id] = LibTrackFlag_Live | LibTrackFlag_NeedTag;
    lib->live_count += 1;

    u32 mask = lib->index_slots - 1;
    u32 slot = (u32)(hash64(path.str, path.size) & mask);
    while (lib->index[slot] != 0) { slot = (slot + 1) & mask; }
    lib->index[slot] = id + 1;
    return id;
}

void lib_track_remove(Library *lib, TrackId id) {
    Assert(lib_track_live(lib, id));
    // Open addressing: rebuilding the run behind the hole is what keeps the
    // probe sequences of the survivors intact, and it costs a handful of slots.
    u32 mask = lib->index_slots - 1;
    String8 path = lib_string(&lib->strings, lib->path_id[id]);
    u32 slot = (u32)(hash64(path.str, path.size) & mask);
    while (lib->index[slot] != id + 1) { slot = (slot + 1) & mask; }
    lib->index[slot] = 0;
    for (u32 next = (slot + 1) & mask; lib->index[next] != 0; next = (next + 1) & mask) {
        u32 entry = lib->index[next];
        lib->index[next] = 0;
        String8 other = lib_string(&lib->strings, lib->path_id[entry - 1]);
        u32 home = (u32)(hash64(other.str, other.size) & mask);
        while (lib->index[home] != 0) { home = (home + 1) & mask; }
        lib->index[home] = entry;
    }

    lib->flags[id] = 0;
    lib->path_id[id] = lib->free_head;
    lib->free_head = id;
    lib->live_count -= 1;
}

// --- extensions ------------------------------------------------------------

LibCodec lib_codec_from_extension(String8 extension) {
    // Lowercased into a small buffer: an extension longer than that is not one
    // of ours anyway.
    u8 lower[8];
    if (extension.size == 0 || extension.size > sizeof(lower)) { return LibCodec_Unknown; }
    for (u64 i = 0; i < extension.size; i += 1) {
        u8 c = extension.str[i];
        lower[i] = (c >= 'A' && c <= 'Z') ? (u8)(c + 32) : c;
    }
    String8 ext = str8(lower, extension.size);
    if (str8_eq(ext, str8_lit("mp3"))) { return LibCodec_MP3; }
    if (str8_eq(ext, str8_lit("flac"))) { return LibCodec_FLAC; }
    if (str8_eq(ext, str8_lit("wav"))) { return LibCodec_WAV; }
    if (str8_eq(ext, str8_lit("aif")) || str8_eq(ext, str8_lit("aiff"))) { return LibCodec_AIFF; }
    if (str8_eq(ext, str8_lit("ogg"))) { return LibCodec_OGG; }
    if (str8_eq(ext, str8_lit("m4a"))) { return LibCodec_M4A; }
    if (str8_eq(ext, str8_lit("aac"))) { return LibCodec_AAC; }
    if (str8_eq(ext, str8_lit("alac"))) { return LibCodec_ALAC; }
    if (str8_eq(ext, str8_lit("wma"))) { return LibCodec_WMA; }
    if (str8_eq(ext, str8_lit("opus"))) { return LibCodec_OPUS; }
    return LibCodec_Unknown;
}

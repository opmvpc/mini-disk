#include "lib_index.h"

// --- accent folding ---------------------------------------------------------
// Two flat tables, 384 bytes of read only data, covering Latin-1 Supplement and
// Latin Extended-A: everything a European tag can hold. A pair of zero bytes
// means "not a letter, copy the codepoint through".
#define F1(a) {(u8)(a), 0}
#define F2(a, b) {(u8)(a), (u8)(b)}
#define FK {0, 0}

// U+00C0 .. U+00FF
static const u8 lib_fold_latin1[64][2] = {
    F1('a'), F1('a'), F1('a'), F1('a'), F1('a'), F1('a'), F2('a', 'e'), F1('c'),
    F1('e'), F1('e'), F1('e'), F1('e'), F1('i'), F1('i'), F1('i'), F1('i'),
    F1('d'), F1('n'), F1('o'), F1('o'), F1('o'), F1('o'), F1('o'), FK,
    F1('o'), F1('u'), F1('u'), F1('u'), F1('u'), F1('y'), F2('t', 'h'), F2('s', 's'),
    F1('a'), F1('a'), F1('a'), F1('a'), F1('a'), F1('a'), F2('a', 'e'), F1('c'),
    F1('e'), F1('e'), F1('e'), F1('e'), F1('i'), F1('i'), F1('i'), F1('i'),
    F1('d'), F1('n'), F1('o'), F1('o'), F1('o'), F1('o'), F1('o'), FK,
    F1('o'), F1('u'), F1('u'), F1('u'), F1('u'), F1('y'), F2('t', 'h'), F1('y'),
};

// U+0100 .. U+017F
static const u8 lib_fold_latin_a[128][2] = {
    F1('a'), F1('a'), F1('a'), F1('a'), F1('a'), F1('a'), F1('c'), F1('c'),
    F1('c'), F1('c'), F1('c'), F1('c'), F1('c'), F1('c'), F1('d'), F1('d'),
    F1('d'), F1('d'), F1('e'), F1('e'), F1('e'), F1('e'), F1('e'), F1('e'),
    F1('e'), F1('e'), F1('e'), F1('e'), F1('g'), F1('g'), F1('g'), F1('g'),
    F1('g'), F1('g'), F1('g'), F1('g'), F1('h'), F1('h'), F1('h'), F1('h'),
    F1('i'), F1('i'), F1('i'), F1('i'), F1('i'), F1('i'), F1('i'), F1('i'),
    F1('i'), F1('i'), F2('i', 'j'), F2('i', 'j'), F1('j'), F1('j'), F1('k'), F1('k'),
    F1('k'), F1('l'), F1('l'), F1('l'), F1('l'), F1('l'), F1('l'), F1('l'),
    F1('l'), F1('l'), F1('l'), F1('n'), F1('n'), F1('n'), F1('n'), F1('n'),
    F1('n'), F1('n'), F1('n'), F1('n'), F1('o'), F1('o'), F1('o'), F1('o'),
    F1('o'), F1('o'), F2('o', 'e'), F2('o', 'e'), F1('r'), F1('r'), F1('r'), F1('r'),
    F1('r'), F1('r'), F1('s'), F1('s'), F1('s'), F1('s'), F1('s'), F1('s'),
    F1('s'), F1('s'), F1('t'), F1('t'), F1('t'), F1('t'), F1('t'), F1('t'),
    F1('u'), F1('u'), F1('u'), F1('u'), F1('u'), F1('u'), F1('u'), F1('u'),
    F1('u'), F1('u'), F1('u'), F1('u'), F1('w'), F1('w'), F1('y'), F1('y'),
    F1('y'), F1('z'), F1('z'), F1('z'), F1('z'), F1('z'), F1('z'), F1('s'),
};
#undef F1
#undef F2
#undef FK

// Dropped from the head of a sort key so "The Beatles" files under B. The list
// is the one a music library actually needs: English, French, German, Spanish.
static const char *lib_articles[] = {"the ", "a ",   "an ",  "le ",  "la ", "les ",
                                     "l'",   "un ",  "une ", "der ", "die ", "das ",
                                     "el ",  "los ", "las "};

u64 lib_normalize(u8 *dst, u64 capacity, String8 text, b32 strip_article) {
    u64 out = 0;
    u64 at = 0;
    while (at < text.size && out < capacity) {
        u8 c = text.str[at];
        if (c < 0x80) {
            at += 1;
            dst[out] = (c >= 'A' && c <= 'Z') ? (u8)(c + 32) : c;
            out += 1;
            continue;
        }
        UnicodeDecode decode = utf8_decode(text.str + at, text.size - at);
        at += decode.advance;
        u32 codepoint = decode.codepoint;
        const u8 *fold = 0;
        if (codepoint >= 0xC0 && codepoint <= 0xFF) {
            fold = lib_fold_latin1[codepoint - 0xC0];
        } else if (codepoint >= 0x100 && codepoint <= 0x17F) {
            fold = lib_fold_latin_a[codepoint - 0x100];
        }
        if (fold && fold[0]) {
            dst[out] = fold[0];
            out += 1;
            if (fold[1] && out < capacity) {
                dst[out] = fold[1];
                out += 1;
            }
            continue;
        }
        // Not a Latin letter: kept as it came, so Japanese titles still match.
        if (out + 4 > capacity) { break; }
        out += utf8_encode(dst + out, codepoint);
    }

    u64 start = 0;
    if (strip_article) {
        for (u32 i = 0; i < ArrayCount(lib_articles); i += 1) {
            String8 article = str8_cstr(lib_articles[i]);
            if (out > article.size &&
                mem_cmp(dst, article.str, article.size) == 0) {
                start = article.size;
                break;
            }
        }
    }
    while (start < out && dst[start] == ' ') { start += 1; }
    if (start != 0) {
        out -= start;
        mem_move(dst, dst + start, out);
    }
    return out;
}

md_inline u32 lib_mask_bit(u8 c) {
    if (c >= 'a' && c <= 'z') { return (u32)(c - 'a'); }
    if (c >= '0' && c <= '9') { return 26u + (u32)(c - '0'); }
    return 36u + (u32)(c & 27);
}

u64 lib_char_mask(String8 normalized) {
    u64 mask = 0;
    for (u64 i = 0; i < normalized.size; i += 1) {
        mask |= 1ull << lib_mask_bit(normalized.str[i]);
    }
    return mask;
}

// --- the build --------------------------------------------------------------

md_inline u64 lib_key_prefix(const u8 *key, u64 size) {
    u64 prefix = 0;
    for (u32 i = 0; i < 8; i += 1) {
        prefix = (prefix << 8) | ((i < size) ? (u64)key[i] : 0ull);
    }
    return prefix;
}

md_inline String8 lib_index_field(const Library *lib, StringId id) {
    return lib_string(&lib->strings, id);
}

void lib_index_build(LibIndex *index, const Library *lib, Arena *arena, b32 strip_articles) {
    arena_clear(arena);
    StructZero(index);
    index->arena = arena;
    index->library = lib;
    index->strip_articles = strip_articles;
    index->count = lib->count;
    index->live_count = lib->live_count;

    u32 count = index->count ? index->count : 1;
    u32 live = index->live_count ? index->live_count : 1;
    index->mask = push_array(arena, u64, count);
    for (u32 c = 0; c < LIB_SORT_STRING_COUNT; c += 1) {
        index->key_prefix[c] = push_array(arena, u64, count);
    }
    index->text_off = push_array(arena, u32, count);
    index->text_len = push_array(arena, u32, count);
    for (u32 c = 0; c < LIB_SORT_STRING_COUNT; c += 1) {
        index->key_off[c] = push_array(arena, u32, count);
    }
    for (u32 c = 0; c < LibSort_COUNT; c += 1) {
        index->order[c] = push_array(arena, u32, live);
    }
    index->identity = push_array(arena, u32, live);
    index->items = push_array(arena, LibSortItem, live);
    index->merge = push_array(arena, LibSortItem, live);

    // Everything below this point is one contiguous run of bytes on the arena:
    // the blob is addressed by offset, and offset 0 is never a real string.
    index->text = (u8 *)arena_push(arena, 1, 8);
    index->text[0] = 0;
    index->text_size = 1;

    u8 scratch[LIB_FIELD_MAX];
    u32 *identity = index->identity;
    u32 live_seen = 0;
    for (TrackId id = 0; id < index->count; id += 1) {
        index->mask[id] = 0;
        index->text_off[id] = 0;
        index->text_len[id] = 0;
        for (u32 c = 0; c < LIB_SORT_STRING_COUNT; c += 1) {
            index->key_off[c][id] = 0;
            index->key_prefix[c][id] = 0;
        }
        if (!(lib->flags[id] & LibTrackFlag_Live)) { continue; }
        identity[live_seen] = id;
        live_seen += 1;

        StringId fields[LIB_SORT_STRING_COUNT];
        fields[LibSort_Title] = lib->title_id[id];
        fields[LibSort_Artist] = lib->artist_id[id];
        fields[LibSort_Album] = lib->album_id[id];

        // The searchable blob: the three fields separated by a byte no
        // normalized text can hold, so a token never straddles two of them.
        u32 offset = (u32)index->text_size;
        u64 mask = 0;
        u32 length = 0;
        for (u32 c = 0; c < LIB_SORT_STRING_COUNT; c += 1) {
            u64 size = lib_normalize(scratch, sizeof(scratch), lib_index_field(lib, fields[c]), 0);
            u8 *bytes = push_array(arena, u8, size + 1);
            mem_copy(bytes, scratch, size);
            bytes[size] = LIB_SEPARATOR;
            mask |= lib_char_mask(str8(scratch, size));
            length += (u32)size + 1;
        }
        index->text_size += length;
        index->text_off[id] = offset;
        index->text_len[id] = length;
        index->mask[id] = mask;

        // The sort keys: the same folding, plus the article, nul terminated.
        for (u32 c = 0; c < LIB_SORT_STRING_COUNT; c += 1) {
            u64 size = lib_normalize(scratch, sizeof(scratch), lib_index_field(lib, fields[c]),
                                     strip_articles);
            u8 *bytes = push_array(arena, u8, size + 1);
            mem_copy(bytes, scratch, size);
            bytes[size] = 0;
            index->key_off[c][id] = (u32)index->text_size;
            index->key_prefix[c][id] = lib_key_prefix(scratch, size);
            index->text_size += size + 1;
        }
    }
    Assert(live_seen == index->live_count);
}

// --- the sort ---------------------------------------------------------------

// Two keys that differ inside their first eight bytes are settled by the u64
// alone; only a real tie walks the bytes, and only when the key is long enough
// to have any left (a zero in the low byte means it ended before byte eight).
md_inline i32 lib_index_tie(const LibIndex *index, LibSortColumn column, u32 a, u32 b) {
    if (column >= LIB_SORT_STRING_COUNT) { return 0; }
    const u8 *sa = index->text + index->key_off[column][a];
    const u8 *sb = index->text + index->key_off[column][b];
    u64 i = 8;
    while (sa[i] == sb[i] && sa[i] != 0) { i += 1; }
    if (sa[i] == sb[i]) { return 0; }
    return (sa[i] < sb[i]) ? -1 : 1;
}

md_inline u64 lib_index_key(const LibIndex *index, LibSortColumn column, u32 id) {
    if (column == LibSort_Duration) { return index->library->duration_ms[id]; }
    if (column == LibSort_Added) { return index->library->mtime_us[id]; }
    return index->key_prefix[column][id];
}

md_inline i32 lib_index_cmp(const LibIndex *index, LibSortColumn column, u32 a, u32 b) {
    u64 ka = lib_index_key(index, column, a);
    u64 kb = lib_index_key(index, column, b);
    if (ka != kb) { return (ka < kb) ? -1 : 1; }
    if ((ka & 0xFFull) == 0) { return 0; }
    return lib_index_tie(index, column, a, b);
}

md_inline b32 lib_sort_before(const LibIndex *index, LibSortColumn column, LibSortItem right,
                              LibSortItem left) {
    if (right.key != left.key) { return right.key < left.key; }
    if ((right.key & 0xFFull) == 0) { return 0; }
    return lib_index_tie(index, column, right.id, left.id) < 0;
}

// Bottom up merge sort over packed (key, id) pairs: stable by construction -
// which is what makes two tracks of the same album keep the order the previous
// column gave them - and every comparison reads memory it is already walking.
static void lib_index_sort(LibIndex *index, LibSortColumn column, u32 count) {
    LibSortItem *items = index->items;
    LibSortItem *merge = index->merge;
    for (u32 width = 1; width < count; width *= 2) {
        for (u32 i = 0; i < count; i += 2 * width) {
            u32 mid = Min(i + width, count);
            u32 end = Min(i + 2 * width, count);
            u32 l = i, r = mid, o = i;
            while (l < mid && r < end) {
                merge[o] = lib_sort_before(index, column, items[r], items[l]) ? items[r++]
                                                                             : items[l++];
                o += 1;
            }
            while (l < mid) { merge[o++] = items[l++]; }
            while (r < end) { merge[o++] = items[r++]; }
        }
        LibSortItem *swap = items;
        items = merge;
        merge = swap;
    }
    index->items = items;
    index->merge = merge;
}

const u32 *lib_index_order(LibIndex *index, LibSortColumn column) {
    Assert(column < LibSort_COUNT);
    u32 *order = index->order[column];
    if (index->order_built[column]) { return order; }
    u32 count = index->live_count;
    for (u32 i = 0; i < count; i += 1) {
        // Every column starts from the id order, so a sort never depends on
        // which column the user clicked before: ties break on the id.
        u32 id = index->identity[i];
        index->items[i].key = lib_index_key(index, column, id);
        index->items[i].id = id;
    }
    lib_index_sort(index, column, count);
    for (u32 i = 0; i < count; i += 1) { order[i] = index->items[i].id; }
    index->order_built[column] = 1;
    return order;
}

// --- the column browser -----------------------------------------------------

void lib_browser_init(LibBrowser *browser, Arena *arena, u32 capacity) {
    StructZero(browser);
    browser->artists = push_array(arena, LibGroup, capacity ? capacity : 1);
    browser->albums = push_array(arena, LibGroup, capacity ? capacity : 1);
    browser->selected_artist = LIB_GROUP_ALL;
    browser->selected_album = LIB_GROUP_ALL;
}

static void lib_browser_albums_build(LibBrowser *browser, LibIndex *index) {
    const u32 *order = lib_index_order(index, LibSort_Album);
    const Library *lib = index->library;
    StringId artist = lib_browser_artist_filter(browser);
    browser->album_count = 0;
    u32 previous = LIB_TRACK_NONE;
    for (u32 i = 0; i < index->live_count; i += 1) {
        u32 id = order[i];
        if (artist != LIB_FILTER_ANY && lib->artist_id[id] != artist) { continue; }
        if (previous != LIB_TRACK_NONE &&
            lib_index_cmp(index, LibSort_Album, previous, id) == 0) {
            browser->albums[browser->album_count - 1].count += 1;
            continue;
        }
        browser->albums[browser->album_count].name = lib->album_id[id];
        browser->albums[browser->album_count].count = 1;
        browser->album_count += 1;
        previous = id;
    }
    if (browser->selected_album != LIB_GROUP_ALL &&
        browser->selected_album >= browser->album_count) {
        browser->selected_album = LIB_GROUP_ALL;
    }
}

void lib_browser_build(LibBrowser *browser, LibIndex *index) {
    const u32 *order = lib_index_order(index, LibSort_Artist);
    const Library *lib = index->library;
    StringId selected = LIB_FILTER_ANY;
    if (browser->selected_artist != LIB_GROUP_ALL &&
        browser->selected_artist < browser->artist_count) {
        selected = browser->artists[browser->selected_artist].name;
    }

    browser->artist_count = 0;
    u32 previous = LIB_TRACK_NONE;
    for (u32 i = 0; i < index->live_count; i += 1) {
        u32 id = order[i];
        if (previous != LIB_TRACK_NONE &&
            lib_index_cmp(index, LibSort_Artist, previous, id) == 0) {
            browser->artists[browser->artist_count - 1].count += 1;
            continue;
        }
        browser->artists[browser->artist_count].name = lib->artist_id[id];
        browser->artists[browser->artist_count].count = 1;
        browser->artist_count += 1;
        previous = id;
    }

    // The selection follows the name, not the row: a rescan that inserts an
    // artist above the selected one must not move the selection.
    browser->selected_artist = LIB_GROUP_ALL;
    if (selected != LIB_FILTER_ANY) {
        for (u32 i = 0; i < browser->artist_count; i += 1) {
            if (browser->artists[i].name == selected) {
                browser->selected_artist = i;
                break;
            }
        }
    }
    lib_browser_albums_build(browser, index);
}

void lib_browser_select_artist(LibBrowser *browser, LibIndex *index, u32 group) {
    browser->selected_artist = (group < browser->artist_count) ? group : LIB_GROUP_ALL;
    browser->selected_album = LIB_GROUP_ALL;
    lib_browser_albums_build(browser, index);
}

void lib_browser_select_album(LibBrowser *browser, u32 group) {
    browser->selected_album = (group < browser->album_count) ? group : LIB_GROUP_ALL;
}

StringId lib_browser_artist_filter(const LibBrowser *browser) {
    if (browser->selected_artist >= browser->artist_count) { return LIB_FILTER_ANY; }
    return browser->artists[browser->selected_artist].name;
}

StringId lib_browser_album_filter(const LibBrowser *browser) {
    if (browser->selected_album >= browser->album_count) { return LIB_FILTER_ANY; }
    return browser->albums[browser->selected_album].name;
}

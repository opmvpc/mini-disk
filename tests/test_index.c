// test_index.c - T-012: normalization and accent folding, stable sort per
// column, the column browser, the incremental search (tokens, accents,
// refinement) and the binary cache (round trip, version, corruption).

#define TEST_INDEX_CAPACITY 20000

typedef struct TestIndexLib {
    Library lib;
    Arena *arena;
    Arena *text;
    Arena *index_arena;
} TestIndexLib;

static void test_index_open(TestIndexLib *db) {
    db->arena = arena_alloc(MB(256));
    db->text = arena_alloc(MB(256));
    db->index_arena = arena_alloc(MB(512));
    lib_init(&db->lib, db->arena, db->text);
}

static void test_index_close(TestIndexLib *db) {
    arena_release(db->index_arena);
    arena_release(db->text);
    arena_release(db->arena);
}

static TrackId test_index_add(TestIndexLib *db, const char *path, const char *title,
                              const char *artist, const char *album, u32 duration_ms,
                              u64 mtime_us) {
    Library *lib = &db->lib;
    TrackId id = lib_track_add(lib, str8_cstr(path), 4096, mtime_us);
    lib->title_id[id] = lib_intern(&lib->strings, str8_cstr(title));
    lib->artist_id[id] = lib_intern(&lib->strings, str8_cstr(artist));
    lib->album_id[id] = lib_intern(&lib->strings, str8_cstr(album));
    lib->duration_ms[id] = duration_ms;
    return id;
}

static String8 test_index_artist(TestIndexLib *db, u32 id) {
    return lib_string(&db->lib.strings, db->lib.artist_id[id]);
}

TEST(index_normalize) {
    u8 buffer[64];
    u64 size = lib_normalize(buffer, sizeof(buffer), str8_lit("\xC3\x89milie"), 0);
    EXPECT(str8_eq(str8(buffer, size), str8_lit("emilie")));
    size = lib_normalize(buffer, sizeof(buffer), str8_lit("Bj\xC3\xB6rk"), 0);
    EXPECT(str8_eq(str8(buffer, size), str8_lit("bjork")));
    // Two byte foldings: ae, ss, oe, th.
    size = lib_normalize(buffer, sizeof(buffer), str8_lit("\xC3\x86ther Stra\xC3\x9F""e"), 0);
    EXPECT(str8_eq(str8(buffer, size), str8_lit("aether strasse")));
    // Latin Extended-A: Dvorak, Lodz.
    size = lib_normalize(buffer, sizeof(buffer), str8_lit("Dvo\xC5\x99\xC3\xA1k \xC5\x81\xC3\xB3""d\xC5\xBA"), 0);
    EXPECT(str8_eq(str8(buffer, size), str8_lit("dvorak lodz")));
    // Not latin: copied through, so a Japanese title still matches itself.
    size = lib_normalize(buffer, sizeof(buffer), str8_lit("\xE6\xB0\xB4\xE9\x9F\xB3"), 0);
    EXPECT(str8_eq(str8(buffer, size), str8_lit("\xE6\xB0\xB4\xE9\x9F\xB3")));
    // Articles, only when the caller asks.
    size = lib_normalize(buffer, sizeof(buffer), str8_lit("The Beatles"), 0);
    EXPECT(str8_eq(str8(buffer, size), str8_lit("the beatles")));
    size = lib_normalize(buffer, sizeof(buffer), str8_lit("The Beatles"), 1);
    EXPECT(str8_eq(str8(buffer, size), str8_lit("beatles")));
    size = lib_normalize(buffer, sizeof(buffer), str8_lit("Les Rita Mitsouko"), 1);
    EXPECT(str8_eq(str8(buffer, size), str8_lit("rita mitsouko")));
    size = lib_normalize(buffer, sizeof(buffer), str8_lit("Thelonious"), 1);
    EXPECT(str8_eq(str8(buffer, size), str8_lit("thelonious")));  // not an article
    // Truncation never splits the buffer open.
    size = lib_normalize(buffer, 4, str8_lit("abcdefgh"), 0);
    EXPECT(size == 4);

    u64 mask = lib_char_mask(str8_lit("abc"));
    EXPECT((mask & lib_char_mask(str8_lit("ab"))) == lib_char_mask(str8_lit("ab")));
    EXPECT((mask & lib_char_mask(str8_lit("z"))) == 0);
    Unused(arena);
}

TEST(index_sort_order) {
    TestIndexLib db;
    test_index_open(&db);
    // The ticket's ordering: "Emilie" folds between "Elias" and "Fanny".
    test_index_add(&db, "a.mp3", "Un", "Fanny", "Album", 100, 3);
    test_index_add(&db, "b.mp3", "Deux", "\xC3\x89milie", "Album", 200, 2);
    test_index_add(&db, "c.mp3", "Trois", "Elias", "Album", 300, 1);
    // Three tracks sharing an artist: the stable sort must keep their id order.
    test_index_add(&db, "d.mp3", "Quatre", "Elias", "Album", 400, 4);
    test_index_add(&db, "e.mp3", "Cinq", "Elias", "Album", 500, 5);

    LibIndex index;
    lib_index_build(&index, &db.lib, db.index_arena, 0);
    const u32 *order = lib_index_order(&index, LibSort_Artist);
    EXPECT(index.live_count == 5);
    EXPECT(str8_eq(test_index_artist(&db, order[0]), str8_lit("Elias")));
    EXPECT(str8_eq(test_index_artist(&db, order[3]), str8_lit("\xC3\x89milie")));
    EXPECT(str8_eq(test_index_artist(&db, order[4]), str8_lit("Fanny")));
    EXPECT(order[0] == 2 && order[1] == 3 && order[2] == 4);  // stable over ids

    const u32 *by_duration = lib_index_order(&index, LibSort_Duration);
    EXPECT(by_duration[0] == 0 && by_duration[4] == 4);
    const u32 *by_added = lib_index_order(&index, LibSort_Added);
    EXPECT(by_added[0] == 2 && by_added[4] == 4);

    // Articles: "The Beatles" files under B, and only when the flag says so.
    test_index_add(&db, "f.mp3", "Six", "The Beatles", "Album", 600, 6);
    lib_index_build(&index, &db.lib, db.index_arena, 0);
    order = lib_index_order(&index, LibSort_Artist);
    EXPECT(str8_eq(test_index_artist(&db, order[5]), str8_lit("The Beatles")));
    lib_index_build(&index, &db.lib, db.index_arena, 1);
    order = lib_index_order(&index, LibSort_Artist);
    EXPECT(str8_eq(test_index_artist(&db, order[0]), str8_lit("The Beatles")));
    test_index_close(&db);
    Unused(arena);
}

TEST(index_sort_stability_and_size) {
    TestIndexLib db;
    test_index_open(&db);
    // 4 000 tracks over 8 artists: every artist group must come out in id order.
    static const char *artists[] = {"Aa", "Bb", "Cc", "Dd", "Ee", "Ff", "Gg", "Hh"};
    for (u32 i = 0; i < 4000; i += 1) {
        String8 path = str8f(arena, "t%04u.mp3", i);
        u32 pick = (i * 7 + 3) % ArrayCount(artists);
        Library *lib = &db.lib;
        TrackId id = lib_track_add(lib, path, 4096, i);
        lib->artist_id[id] = lib_intern(&lib->strings, str8_cstr(artists[pick]));
        lib->title_id[id] = lib_intern(&lib->strings, str8_lit("same title"));
        lib->album_id[id] = lib_intern(&lib->strings, str8_lit("same album"));
        lib->duration_ms[id] = 1000;
    }
    LibIndex index;
    lib_index_build(&index, &db.lib, db.index_arena, 0);
    const u32 *order = lib_index_order(&index, LibSort_Artist);
    b32 sorted = 1;
    b32 stable = 1;
    for (u32 i = 1; i < index.live_count; i += 1) {
        String8 previous = test_index_artist(&db, order[i - 1]);
        String8 current = test_index_artist(&db, order[i]);
        i32 cmp = str8_cmp(previous, current);
        if (cmp > 0) { sorted = 0; }
        if (cmp == 0 && order[i - 1] > order[i]) { stable = 0; }
    }
    EXPECT(sorted);
    EXPECT(stable);
    // Equal titles everywhere: the whole column is one tie, and the order is
    // exactly the id order.
    const u32 *by_title = lib_index_order(&index, LibSort_Title);
    b32 identity = 1;
    for (u32 i = 0; i < index.live_count; i += 1) {
        if (by_title[i] != i) { identity = 0; }
    }
    EXPECT(identity);
    test_index_close(&db);
}

TEST(index_browser) {
    TestIndexLib db;
    test_index_open(&db);
    test_index_add(&db, "1.mp3", "A", "Autechre", "Amber", 100, 1);
    test_index_add(&db, "2.mp3", "B", "Autechre", "Amber", 100, 2);
    test_index_add(&db, "3.mp3", "C", "Autechre", "Tri Repetae", 100, 3);
    test_index_add(&db, "4.mp3", "D", "Oval", "94diskont", 100, 4);

    LibIndex index;
    lib_index_build(&index, &db.lib, db.index_arena, 0);
    LibBrowser browser;
    lib_browser_init(&browser, db.index_arena, 64);
    lib_browser_build(&browser, &index);
    EXPECT(browser.artist_count == 2);
    EXPECT(browser.artists[0].count == 3 && browser.artists[1].count == 1);
    EXPECT(browser.album_count == 3);  // every album while no artist is picked

    lib_browser_select_artist(&browser, &index, 0);
    EXPECT(browser.album_count == 2);
    EXPECT(browser.albums[0].count == 2);  // Amber
    EXPECT(browser.albums[1].count == 1);  // Tri Repetae
    EXPECT(lib_browser_artist_filter(&browser) != LIB_FILTER_ANY);
    lib_browser_select_artist(&browser, &index, LIB_GROUP_ALL);
    EXPECT(lib_browser_artist_filter(&browser) == LIB_FILTER_ANY);
    test_index_close(&db);
    Unused(arena);
}

TEST(index_search) {
    TestIndexLib db;
    test_index_open(&db);
    test_index_add(&db, "1.mp3", "The Bends", "Radiohead", "The Bends", 100, 1);
    test_index_add(&db, "2.mp3", "Black Star", "Radiohead", "The Bends", 200, 2);
    test_index_add(&db, "3.mp3", "\xC3\x89tude", "\xC3\x89milie Simon", "V\xC3\xA9gonia", 300, 3);
    test_index_add(&db, "4.mp3", "Theme", "Boards of Canada", "Geogaddi", 400, 4);

    LibIndex index;
    lib_index_build(&index, &db.lib, db.index_arena, 0);
    LibSearch search;
    lib_search_init(&search, db.index_arena, TEST_INDEX_CAPACITY);
    lib_search_set_order(&search, LibSort_Title, 0);

    lib_search_run(&search, &index, str8_lit(""));
    EXPECT(search.result_count == 4);

    lib_search_run(&search, &index, str8_lit("the"));
    EXPECT(search.result_count == 3);  // two "The Bends" and "Theme"

    // Two tokens are ANDed and may land in different fields.
    lib_search_invalidate(&search);
    lib_search_run(&search, &index, str8_lit("the radio"));
    EXPECT(search.result_count == 2);

    // Accents: the query has none, the tags do, and it still matches.
    lib_search_invalidate(&search);
    lib_search_run(&search, &index, str8_lit("etude"));
    EXPECT(search.result_count == 1);
    lib_search_invalidate(&search);
    lib_search_run(&search, &index, str8_lit("\xC3\xA9tude"));
    EXPECT(search.result_count == 1);
    lib_search_invalidate(&search);
    lib_search_run(&search, &index, str8_lit("VEGONIA"));
    EXPECT(search.result_count == 1);

    // A token that no track carries: rejected by the mask alone.
    lib_search_invalidate(&search);
    lib_search_run(&search, &index, str8_lit("xylophone"));
    EXPECT(search.result_count == 0);

    // Results come back in the current sort order, and follow it when it flips.
    lib_search_invalidate(&search);
    lib_search_run(&search, &index, str8_lit("the"));
    const u32 *by_title = lib_index_order(&index, LibSort_Title);
    u32 first = search.results[0];
    EXPECT(first == by_title[0] || first == by_title[1] || first == by_title[2]);
    lib_search_set_order(&search, LibSort_Title, 1);
    lib_search_run(&search, &index, str8_lit("the"));
    EXPECT(search.result_count == 3);
    EXPECT(search.results[0] != first);
    test_index_close(&db);
    Unused(arena);
}

TEST(index_search_refinement) {
    TestIndexLib db;
    test_index_open(&db);
    static const char *words[] = {"the", "beat", "blue", "night", "bell", "bath", "ocean"};
    for (u32 i = 0; i < 3000; i += 1) {
        String8 path = str8f(arena, "r%04u.mp3", i);
        Library *lib = &db.lib;
        TrackId id = lib_track_add(lib, path, 4096, i);
        lib->title_id[id] = lib_intern(
            &lib->strings, str8f(arena, "%s %s %u", words[i % ArrayCount(words)],
                                 words[(i / 7) % ArrayCount(words)], i));
        lib->artist_id[id] = lib_intern(&lib->strings,
                                        str8f(arena, "artist %u", i % 40));
        lib->album_id[id] = lib_intern(&lib->strings, str8f(arena, "album %u", i % 17));
        lib->duration_ms[id] = 60000 + i;
    }
    LibIndex index;
    lib_index_build(&index, &db.lib, db.index_arena, 0);
    LibSearch refined;
    LibSearch fresh;
    lib_search_init(&refined, db.index_arena, TEST_INDEX_CAPACITY);
    lib_search_init(&fresh, db.index_arena, TEST_INDEX_CAPACITY);

    // Typing "the b" one character at a time must land on exactly the result a
    // cold search for "the b" gives, in the same order.
    static const char *steps[] = {"t", "th", "the", "the ", "the b"};
    for (u32 i = 0; i < ArrayCount(steps); i += 1) {
        lib_search_run(&refined, &index, str8_cstr(steps[i]));
    }
    EXPECT(refined.refined);
    lib_search_run(&fresh, &index, str8_lit("the b"));
    EXPECT(!fresh.refined);
    EXPECT(refined.result_count == fresh.result_count);
    EXPECT(refined.result_count > 0);
    b32 same = 1;
    for (u32 i = 0; i < refined.result_count; i += 1) {
        if (refined.results[i] != fresh.results[i]) { same = 0; }
    }
    EXPECT(same);
    // The refinement really did walk fewer candidates than the library holds.
    EXPECT(refined.scanned < index.live_count);

    // Backspacing shrinks the query: no refinement, and the count grows back.
    lib_search_run(&refined, &index, str8_lit("the"));
    EXPECT(!refined.refined);
    EXPECT(refined.result_count >= fresh.result_count);

    // The same, sorted by another column: refinement must not reorder.
    lib_search_set_order(&refined, LibSort_Duration, 1);
    lib_search_set_order(&fresh, LibSort_Duration, 1);
    lib_search_run(&refined, &index, str8_lit("the"));
    lib_search_run(&refined, &index, str8_lit("the b"));
    lib_search_run(&fresh, &index, str8_lit("the b"));
    same = (refined.result_count == fresh.result_count);
    for (u32 i = 0; same && i < refined.result_count; i += 1) {
        if (refined.results[i] != fresh.results[i]) { same = 0; }
    }
    EXPECT(same);
    test_index_close(&db);
}

// --- the cache --------------------------------------------------------------

#define TEST_CACHE_TRACKS 10000

static String8 test_cache_path(Arena *arena, const char *name) {
    String8 temp = os_known_folder(arena, OsKnownFolder_Temp);
    return os_path_join(arena, temp, str8_cstr(name));
}

static void test_cache_fill(TestIndexLib *db, Arena *arena, u32 count) {
    Library *lib = &db->lib;
    for (u32 i = 0; i < count; i += 1) {
        TrackId id = lib_track_add(lib, str8f(arena, "C:\\music\\%03u\\track%05u.mp3", i % 64, i),
                                   (u64)i * 1024 + 7, (u64)i * 1000000 + 5);
        lib->title_id[id] = lib_intern(&lib->strings, str8f(arena, "Title %u", i));
        lib->artist_id[id] = lib_intern(&lib->strings, str8f(arena, "Artist %u", i % 500));
        lib->album_id[id] = lib_intern(&lib->strings, str8f(arena, "Album %u", i % 1200));
        lib->album_artist_id[id] = lib->artist_id[id];
        lib->genre_id[id] = lib_intern(&lib->strings, str8_lit("Ambient"));
        lib->duration_ms[id] = 60000 + i;
        lib->sample_rate[id] = 44100;
        lib->cover_hash[id] = (u64)i * 0x9E3779B97F4A7C15ull;
        lib->track_no[id] = (u16)(i % 20 + 1);
        lib->disc_no[id] = (u16)(i % 3 + 1);
        lib->year[id] = (u16)(1970 + i % 50);
        lib->replaygain_track_db[id] = (i16)(-2000 + (i32)i);
        lib->channels[id] = 2;
        lib->codec[id] = (u8)(i % LibCodec_COUNT);
    }
    // A hole: the cache must bring tombstones and the free list back untouched.
    lib_track_remove(lib, 7);
    lib_track_remove(lib, 11);
}

TEST(index_cache_round_trip) {
    TestIndexLib source;
    test_index_open(&source);
    test_cache_fill(&source, arena, TEST_CACHE_TRACKS);
    String8 path = test_cache_path(arena, "minidisk_test_library.mdlib");
    String8 root = str8_lit("C:\\music");
    EXPECT(lib_cache_save(&source.lib, path, root) == LibCache_Ok);

    TestIndexLib loaded;
    test_index_open(&loaded);
    String8 loaded_root = str8(0, 0);
    u64 start_us = os_time_now_us();
    LibCacheStatus status = lib_cache_load(&loaded.lib, path, arena, &loaded_root);
    u64 elapsed_us = os_time_now_us() - start_us;
    EXPECT(status == LibCache_Ok);
    EXPECT(str8_eq(loaded_root, root));
    EXPECT(elapsed_us < 50000);

    Library *a = &source.lib;
    Library *b = &loaded.lib;
    EXPECT(a->count == b->count);
    EXPECT(a->live_count == b->live_count);
    EXPECT(a->free_head == b->free_head);
    b32 same = 1;
    for (TrackId id = 0; id < a->count; id += 1) {
        if (lib_track_live(a, id) != lib_track_live(b, id)) { same = 0; break; }
        if (!lib_track_live(a, id)) { continue; }
        same &= a->size[id] == b->size[id];
        same &= a->mtime_us[id] == b->mtime_us[id];
        same &= a->cover_hash[id] == b->cover_hash[id];
        same &= a->duration_ms[id] == b->duration_ms[id];
        same &= a->sample_rate[id] == b->sample_rate[id];
        same &= a->flags[id] == b->flags[id];
        same &= a->track_no[id] == b->track_no[id];
        same &= a->disc_no[id] == b->disc_no[id];
        same &= a->year[id] == b->year[id];
        same &= a->replaygain_track_db[id] == b->replaygain_track_db[id];
        same &= a->channels[id] == b->channels[id];
        same &= a->codec[id] == b->codec[id];
        same &= str8_eq(lib_track_path(a, id), lib_track_path(b, id));
        same &= str8_eq(lib_string(&a->strings, a->title_id[id]),
                        lib_string(&b->strings, b->title_id[id]));
        same &= str8_eq(lib_string(&a->strings, a->artist_id[id]),
                        lib_string(&b->strings, b->artist_id[id]));
        same &= str8_eq(lib_string(&a->strings, a->album_id[id]),
                        lib_string(&b->strings, b->album_id[id]));
        same &= str8_eq(lib_string(&a->strings, a->genre_id[id]),
                        lib_string(&b->strings, b->genre_id[id]));
        if (!same) { break; }
    }
    EXPECT(same);

    // The rebuilt hash finds every path, and interning an old string returns
    // the id it already had: the loaded table is a working table, not a dump.
    EXPECT(lib_find_by_path(b, lib_track_path(a, 3)) == 3);
    EXPECT(lib_find_by_path(b, str8_lit("C:\\music\\nope.mp3")) == LIB_TRACK_NONE);
    StringId again = lib_intern(&b->strings, str8_lit("Album 5"));
    EXPECT(str8_eq(lib_string(&b->strings, again), str8_lit("Album 5")));
    EXPECT(b->strings.count == a->strings.count);
    // And it still grows: a new track lands on a free slot of the free list.
    TrackId reused = lib_track_add(b, str8_lit("C:\\music\\new.mp3"), 10, 20);
    EXPECT(reused == 11);
    EXPECT(lib_find_by_path(b, str8_lit("C:\\music\\new.mp3")) == 11);

    test_index_close(&loaded);
    test_index_close(&source);
    os_file_delete(path);
}

// Rewrites the cache with one header field patched, and expects the loader to
// refuse it without touching the target library.
static LibCacheStatus test_cache_tamper(Arena *arena, String8 source, u64 offset, u64 value,
                                        u32 width) {
    String8 bytes = os_file_read_all(arena, source);
    if (bytes.size == 0) { return LibCache_Missing; }
    if (width == 4) {
        u32 patched = (u32)value;
        mem_copy(bytes.str + offset, &patched, 4);
    } else {
        mem_copy(bytes.str + offset, &value, 8);
    }
    String8 target = test_cache_path(arena, "minidisk_test_library_bad.mdlib");
    os_file_write_all(target, bytes);
    TestIndexLib db;
    test_index_open(&db);
    LibCacheStatus status = lib_cache_load(&db.lib, target, arena, 0);
    test_index_close(&db);
    os_file_delete(target);
    return status;
}

TEST(index_cache_rejects) {
    TestIndexLib source;
    test_index_open(&source);
    test_cache_fill(&source, arena, 64);
    String8 path = test_cache_path(arena, "minidisk_test_library_v.mdlib");
    EXPECT(lib_cache_save(&source.lib, path, str8_lit("C:\\music")) == LibCache_Ok);
    test_index_close(&source);

    // Field offsets in LibCacheHeader, spelled out so a layout change is caught.
    EXPECT(OffsetOf(LibCacheHeader, version) == 8);
    EXPECT(OffsetOf(LibCacheHeader, soa_offset) == 56);
    EXPECT(OffsetOf(LibCacheHeader, strings_size) == 80);

    EXPECT(test_cache_tamper(arena, path, OffsetOf(LibCacheHeader, version),
                             LIB_CACHE_VERSION + 1, 4) == LibCache_BadVersion);
    EXPECT(test_cache_tamper(arena, path, 0, 0x4141414141414141ull, 8) == LibCache_Corrupt);
    // Offsets and sizes that walk out of the file, or lie about each other.
    EXPECT(test_cache_tamper(arena, path, OffsetOf(LibCacheHeader, soa_offset),
                             0xFFFFFFFFull, 8) == LibCache_Corrupt);
    EXPECT(test_cache_tamper(arena, path, OffsetOf(LibCacheHeader, soa_offset), 193, 8) ==
           LibCache_Corrupt);  // not aligned
    EXPECT(test_cache_tamper(arena, path, OffsetOf(LibCacheHeader, soa_size), 8, 8) ==
           LibCache_Corrupt);
    EXPECT(test_cache_tamper(arena, path, OffsetOf(LibCacheHeader, strings_size),
                             0xFFFFFFFFFFull, 8) == LibCache_Corrupt);
    EXPECT(test_cache_tamper(arena, path, OffsetOf(LibCacheHeader, strings_offset),
                             0xFFFFFFFFull, 8) == LibCache_Corrupt);
    EXPECT(test_cache_tamper(arena, path, OffsetOf(LibCacheHeader, slot_count), 1000, 8) ==
           LibCache_Corrupt);  // not a power of two
    EXPECT(test_cache_tamper(arena, path, OffsetOf(LibCacheHeader, live_count), 63, 8) ==
           LibCache_Corrupt);
    EXPECT(test_cache_tamper(arena, path, OffsetOf(LibCacheHeader, free_head), 3, 8) ==
           LibCache_Corrupt);  // a live slot cannot head the free list
    EXPECT(test_cache_tamper(arena, path, OffsetOf(LibCacheHeader, track_count),
                             LIB_CACHE_MAX_TRACKS + 1, 8) == LibCache_Corrupt);
    EXPECT(test_cache_tamper(arena, path, OffsetOf(LibCacheHeader, file_size), 1024, 8) ==
           LibCache_Corrupt);
    EXPECT(test_cache_tamper(arena, path, OffsetOf(LibCacheHeader, string_count), 3, 8) ==
           LibCache_Corrupt);

    TestIndexLib db;
    test_index_open(&db);
    EXPECT(lib_cache_load(&db.lib, test_cache_path(arena, "minidisk_no_such.mdlib"), arena, 0) ==
           LibCache_Missing);
    test_index_close(&db);
    os_file_delete(path);
}

static void test_index_run_all(void) {
    test_report("index / search / cache\n");
    RUN(index_normalize);
    RUN(index_sort_order);
    RUN(index_sort_stability_and_size);
    RUN(index_browser);
    RUN(index_search);
    RUN(index_search_refinement);
    RUN(index_cache_round_trip);
    RUN(index_cache_rejects);
}

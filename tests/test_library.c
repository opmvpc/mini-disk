// test_library.c - T-010: string table, SoA with tombstones, path helpers, and
// the scan itself against a real tree generated in %TEMP% (500 files in 30
// folders): full scan, incremental rescan, removal, cancellation.

// --- the fake tree ---------------------------------------------------------
#define TEST_LIB_FOLDERS 30
#define TEST_LIB_FILES 500

static String8 test_lib_root(Arena *arena) {
    String8 temp = os_known_folder(arena, OsKnownFolder_Temp);
    return os_path_join(arena, temp, str8_lit("minidisk_test_scan"));
}

// Folder 0 is the root; the others hang under it, every third one nested one
// level deeper, so the walk really has to recurse.
static String8 test_lib_folder(Arena *arena, String8 root, u32 index) {
    if (index == 0) { return root; }
    String8 parent = root;
    if ((index % 3) == 0) {
        parent = os_path_join(arena, root, str8f(arena, "d%02u", index - 1));
    }
    return os_path_join(arena, parent, str8f(arena, "d%02u", index));
}

// Deleting while the search handle is still open makes FindNextFileW skip
// entries: the listing is taken first, the deletions come after.
static void test_lib_tree_remove(Arena *arena, String8 dir) {
    OsDirIter iter;
    if (!os_dir_iter_begin(&iter, dir)) { return; }
    String8List files;
    String8List folders;
    StructZero(&files);
    StructZero(&folders);
    OsFileInfo info;
    while (os_dir_iter_next(&iter, &info)) {
        String8 path = os_path_join(arena, dir, info.name);
        str8_list_push(arena, info.is_dir ? &folders : &files, path);
    }
    os_dir_iter_end(&iter);
    for (String8Node *node = files.first; node; node = node->next) { os_file_delete(node->str); }
    for (String8Node *node = folders.first; node; node = node->next) {
        test_lib_tree_remove(arena, node->str);
    }
    os_dir_delete(dir);
}

// 500 audio files plus a few files the extension filter must drop.
static String8 test_lib_tree_build(Arena *arena) {
    String8 root = test_lib_root(arena);
    test_lib_tree_remove(arena, root);
    os_dir_create(root);
    for (u32 i = 1; i < TEST_LIB_FOLDERS; i += 1) {
        if ((i % 3) == 0) {
            os_dir_create(os_path_join(arena, root, str8f(arena, "d%02u", i - 1)));
        }
        os_dir_create(test_lib_folder(arena, root, i));
    }
    static const char *extensions[] = {"mp3", "flac", "wav", "m4a", "opus", "txt", "jpg"};
    for (u32 i = 0; i < TEST_LIB_FILES; i += 1) {
        String8 folder = test_lib_folder(arena, root, i % TEST_LIB_FOLDERS);
        // One in ten is not audio: the filter must not count it.
        const char *extension = ((i % 10) == 9) ? extensions[5 + (i % 2)] : extensions[i % 5];
        String8 path = os_path_join(arena, folder, str8f(arena, "t%03u.%s", i, extension));
        os_file_write_all(path, str8_lit("minidisk"));
    }
    return root;
}

static u32 test_lib_audio_count(void) {
    u32 count = 0;
    for (u32 i = 0; i < TEST_LIB_FILES; i += 1) {
        if ((i % 10) != 9) { count += 1; }
    }
    return count;
}

// The scan as the app drives it: poll, never block.
static void test_lib_scan(LibScan *scan, Library *lib, LibEventQueue *events, String8 root) {
    lib_scan_begin(scan, lib, events, root);
    while (lib_scan_update(scan)) { os_sleep_us(100); }
    lib_scan_end(scan);
}

// --- string table ----------------------------------------------------------

TEST(library_string_table) {
    Arena *text = arena_alloc(MB(16));
    StringTable table;
    lib_strings_init(&table, arena, text);

    EXPECT(lib_string(&table, LIB_STRING_NONE).size == 0);
    StringId a = lib_intern(&table, str8_lit("Boards of Canada"));
    StringId b = lib_intern(&table, str8_lit("Autechre"));
    StringId again = lib_intern(&table, str8_lit("Boards of Canada"));
    EXPECT(a == again);
    EXPECT(a != b);
    EXPECT(table.count == 2);
    EXPECT(str8_eq(lib_string(&table, a), str8_lit("Boards of Canada")));
    EXPECT(str8_eq(lib_string(&table, b), str8_lit("Autechre")));
    EXPECT(lib_intern(&table, str8_lit("")) == LIB_STRING_NONE);

    // Enough entries to grow the slot table several times: every one of them
    // must still resolve, and re-interning must never add a duplicate.
    u32 count = 4000;
    u32 *ids = push_array(arena, u32, count);
    for (u32 i = 0; i < count; i += 1) {
        ids[i] = lib_intern(&table, str8f(arena, "artist %u", i));
    }
    EXPECT(table.count == count + 2);
    b32 stable = 1;
    b32 exact = 1;
    for (u32 i = 0; i < count; i += 1) {
        if (lib_intern(&table, str8f(arena, "artist %u", i)) != ids[i]) { stable = 0; }
        if (!str8_eq(lib_string(&table, ids[i]), str8f(arena, "artist %u", i))) { exact = 0; }
    }
    EXPECT(stable);
    EXPECT(exact);
    EXPECT(table.count == count + 2);
    arena_release(text);
}

// --- the SoA ---------------------------------------------------------------

TEST(library_soa_add_remove) {
    Arena *text = arena_alloc(MB(16));
    Library lib;
    lib_init(&lib, arena, text);
    EXPECT(lib.count == 0 && lib.live_count == 0);

    TrackId first = lib_track_add(&lib, str8_lit("C:\\m\\a.mp3"), 1024, 111);
    TrackId second = lib_track_add(&lib, str8_lit("C:\\m\\b.flac"), 2048, 222);
    EXPECT(first == 0 && second == 1);
    EXPECT(lib.live_count == 2);
    EXPECT(lib.codec[first] == LibCodec_MP3);
    EXPECT(lib.codec[second] == LibCodec_FLAC);
    EXPECT(lib.size[second] == 2048 && lib.mtime_us[second] == 222);
    EXPECT((lib.flags[first] & LibTrackFlag_NeedTag) != 0);
    EXPECT(lib.replaygain_track_db[first] == LIB_REPLAYGAIN_NONE);
    EXPECT(lib_find_by_path(&lib, str8_lit("C:\\m\\b.flac")) == second);
    EXPECT(lib_find_by_path(&lib, str8_lit("C:\\m\\c.wav")) == LIB_TRACK_NONE);

    // A tombstone: the id stays valid to look at, the slot leaves the index.
    lib_track_remove(&lib, first);
    EXPECT(lib.live_count == 1);
    EXPECT(!lib_track_live(&lib, first));
    EXPECT(lib_find_by_path(&lib, str8_lit("C:\\m\\a.mp3")) == LIB_TRACK_NONE);
    EXPECT(lib_find_by_path(&lib, str8_lit("C:\\m\\b.flac")) == second);

    // The next insertion reuses it rather than growing the arrays.
    TrackId third = lib_track_add(&lib, str8_lit("C:\\m\\c.wav"), 4096, 333);
    EXPECT(third == first);
    EXPECT(lib.count == 2 && lib.live_count == 2);
    EXPECT(lib_find_by_path(&lib, str8_lit("C:\\m\\c.wav")) == third);

    // Past LIB_MIN_CAPACITY the block doubles and every column moves with it.
    u32 count = 5000;
    for (u32 i = 0; i < count; i += 1) {
        lib_track_add(&lib, str8f(arena, "C:\\m\\%u.mp3", i), i, i * 2);
    }
    EXPECT(lib.capacity >= count);
    EXPECT(lib.live_count == count + 2);
    b32 intact = 1;
    for (u32 i = 0; i < count; i += 1) {
        TrackId id = lib_find_by_path(&lib, str8f(arena, "C:\\m\\%u.mp3", i));
        if (id == LIB_TRACK_NONE || lib.size[id] != i || lib.mtime_us[id] != i * 2) { intact = 0; }
    }
    EXPECT(intact);
    EXPECT(str8_eq(lib_track_path(&lib, second), str8_lit("C:\\m\\b.flac")));
    // 100 000 tracks fit in the budget of the ticket, strings excluded.
    EXPECT((u64)100000 * LIB_BYTES_PER_TRACK < MB(40));
    arena_release(text);
}

TEST(library_paths_and_codecs) {
    EXPECT(str8_eq(os_path_extension(str8_lit("C:\\m\\a.FLAC")), str8_lit("FLAC")));
    EXPECT(os_path_extension(str8_lit("C:\\m\\noext")).size == 0);
    EXPECT(str8_eq(os_path_filename(str8_lit("C:\\m\\a.mp3")), str8_lit("a.mp3")));
    EXPECT(str8_eq(os_path_parent(str8_lit("C:\\m\\sub\\a.mp3")), str8_lit("C:\\m\\sub")));
    EXPECT(str8_eq(os_path_join(arena, str8_lit("C:\\m\\"), str8_lit("\\a.mp3")),
                   str8_lit("C:\\m\\a.mp3")));
    EXPECT(str8_eq(os_path_normalize(arena, str8_lit("C:/m/sub/")), str8_lit("C:\\m\\sub")));

    EXPECT(lib_codec_from_extension(str8_lit("MP3")) == LibCodec_MP3);
    EXPECT(lib_codec_from_extension(str8_lit("aiff")) == LibCodec_AIFF);
    EXPECT(lib_codec_from_extension(str8_lit("opus")) == LibCodec_OPUS);
    EXPECT(lib_codec_from_extension(str8_lit("txt")) == LibCodec_Unknown);
    EXPECT(lib_codec_from_extension(str8_lit("")) == LibCodec_Unknown);
    EXPECT(lib_codec_from_extension(str8_lit("verylongextension")) == LibCodec_Unknown);

    String8 music = os_known_folder(arena, OsKnownFolder_Music);
    EXPECT(music.size > 0);
    String8 temp = os_known_folder(arena, OsKnownFolder_Temp);
    EXPECT(temp.size > 0);
}

// --- the scan --------------------------------------------------------------

TEST(library_scan_tree) {
    Arena *text = arena_alloc(MB(64));
    Library lib;
    lib_init(&lib, arena, text);
    LibEventQueue events;
    StructZero(&events);
    LibScan scan;
    String8 root = test_lib_tree_build(arena);

    jobs_init(0);
    test_lib_scan(&scan, &lib, &events, root);
    u32 expected = test_lib_audio_count();
    EXPECT(scan.added == expected);
    EXPECT(lib.live_count == expected);
    EXPECT(scan.files_seen == TEST_LIB_FILES);
    EXPECT(scan.dirs_total == TEST_LIB_FOLDERS);
    EXPECT(scan.dirs_done == TEST_LIB_FOLDERS);
    EXPECT(!scan.cancelled);
    EXPECT(scan.removed == 0);

    // The events the UI would have consumed.
    u32 added_events = 0;
    u32 progress_events = 0;
    u32 done_events = 0;
    LibEvent event;
    while (lib_events_next(&events, &event)) {
        if (event.kind == LibEvent_TracksAdded) { added_events += event.count; }
        if (event.kind == LibEvent_ScanProgress) { progress_events += 1; }
        if (event.kind == LibEvent_ScanDone) { done_events += 1; }
    }
    EXPECT(done_events == 1);
    EXPECT(progress_events >= 1);
    EXPECT(added_events > 0 && added_events <= expected);

    // Every track really is one of the files we wrote, and the .txt/.jpg ones
    // never made it in.
    b32 audio_only = 1;
    for (TrackId id = 0; id < lib.count; id += 1) {
        if (!lib_track_live(&lib, id)) { continue; }
        String8 path = lib_track_path(&lib, id);
        if (!str8_starts_with(path, root)) { audio_only = 0; }
        if (lib.codec[id] == LibCodec_Unknown) { audio_only = 0; }
        if (lib.size[id] != 8) { audio_only = 0; }
    }
    EXPECT(audio_only);

    // --- incremental rescan: nothing changed on disk ------------------------
    LibScan rescan;
    test_lib_scan(&rescan, &lib, &events, root);
    EXPECT(rescan.added == 0);
    EXPECT(rescan.updated == 0);
    EXPECT(rescan.removed == 0);
    EXPECT(rescan.unchanged == expected);
    EXPECT(lib.live_count == expected);

    // --- one file changed, one deleted, one added ---------------------------
    String8 changed = os_path_join(arena, test_lib_folder(arena, root, 0), str8_lit("t000.mp3"));
    String8 deleted = os_path_join(arena, test_lib_folder(arena, root, 1), str8_lit("t001.flac"));
    String8 created = os_path_join(arena, test_lib_folder(arena, root, 2), str8_lit("new.wav"));
    os_file_write_all(changed, str8_lit("minidisk, but longer"));
    os_file_delete(deleted);
    os_file_write_all(created, str8_lit("minidisk"));

    LibScan third;
    test_lib_scan(&third, &lib, &events, root);
    EXPECT(third.updated == 1);
    EXPECT(third.added == 1);
    EXPECT(third.removed == 1);
    EXPECT(third.unchanged == expected - 2);
    EXPECT(lib.live_count == expected);
    EXPECT(lib_find_by_path(&lib, deleted) == LIB_TRACK_NONE);
    TrackId changed_id = lib_find_by_path(&lib, changed);
    EXPECT(changed_id != LIB_TRACK_NONE && lib.size[changed_id] == 20);
    // The deleted slot is the one the new file got: no growth, no leak.
    EXPECT(lib.count == expected + 1);
    jobs_shutdown();
    arena_release(text);
}

TEST(library_scan_cancel) {
    Arena *text = arena_alloc(MB(64));
    Library lib;
    lib_init(&lib, arena, text);
    LibEventQueue events;
    StructZero(&events);
    String8 root = test_lib_root(arena);

    jobs_init(0);
    LibScan scan;
    lib_scan_begin(&scan, &lib, &events, root);
    lib_scan_cancel(&scan);
    u64 start_us = os_time_now_us();
    while (lib_scan_update(&scan)) { os_sleep_us(100); }
    u64 elapsed_us = os_time_now_us() - start_us;
    lib_scan_end(&scan);

    EXPECT(scan.cancelled);
    EXPECT(elapsed_us < 100000);      // the ticket's budget: under 100 ms
    EXPECT(scan.removed == 0);        // a cancelled scan never sweeps
    EXPECT(lib.live_count <= test_lib_audio_count());
    jobs_shutdown();

    test_lib_tree_remove(arena, root);
    OsDirIter iter;
    EXPECT(!os_dir_iter_begin(&iter, root));
    arena_release(text);
}

static void test_library_run_all(void) {
    test_report("library\n");
    RUN(library_string_table);
    RUN(library_soa_add_remove);
    RUN(library_paths_and_codecs);
    RUN(library_scan_tree);
    RUN(library_scan_cancel);
}

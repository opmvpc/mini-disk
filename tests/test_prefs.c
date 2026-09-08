// test_prefs.c - the preferences file (T-013, ADR-010): round trip, missing
// values, out of range values and a corrupt file all end on a usable struct.

static String8 test_prefs_path(Arena *arena, const char *name) {
    String8 temp = os_known_folder(arena, OsKnownFolder_Temp);
    return os_path_join(arena, temp, str8_cstr(name));
}

// A prefs that differs from the defaults in every field, so a round trip that
// drops one is a failing check and not a coincidence.
static void test_prefs_fill(Prefs *prefs) {
    prefs_defaults(prefs);
    for (u32 i = 0; i < AppColumn_COUNT; i += 1) {
        prefs->columns[i].width = 100.0f + (f32)i * 10.0f;
        prefs->columns[i].order = AppColumn_COUNT - 1 - i;
        prefs->columns[i].visible = (i != AppColumn_Year);
    }
    prefs->sort_column = 3;
    prefs->sort_desc = 1;
    prefs->browser_collapsed = 1;
    prefs->window_x = -1234;
    prefs->window_y = 57;
    prefs->window_width = 1440;
    prefs->window_height = 900;
    prefs->window_placed = 1;
    prefs->window_maximized = 1;
    prefs_add_folder(prefs, str8_lit("C:\\music"));
    prefs_add_folder(prefs, str8_lit("D:\\archives\\flac"));
    // T-072: the eleven keys the preferences panel writes.
    prefs->lang = 1;
    prefs->theme = 1;
    prefs->default_mode = 2;
    prefs->loudness_lufs = -165;
    prefs->true_peak_dbtp = -20;
    prefs->trim_silence = 1;
    prefs->fade_in_ms = 250;
    prefs->fade_out_ms = 400;
    prefs->gap_ms = 2000;
    prefs->cache_transcode_mb = 4096;
    prefs->cache_covers_mb = 512;
}

static b32 test_prefs_equal(const Prefs *a, const Prefs *b) {
    for (u32 i = 0; i < AppColumn_COUNT; i += 1) {
        if (a->columns[i].width != b->columns[i].width) { return 0; }
        if (a->columns[i].order != b->columns[i].order) { return 0; }
        if ((a->columns[i].visible != 0) != (b->columns[i].visible != 0)) { return 0; }
    }
    if (a->sort_column != b->sort_column) { return 0; }
    if ((a->sort_desc != 0) != (b->sort_desc != 0)) { return 0; }
    if ((a->browser_collapsed != 0) != (b->browser_collapsed != 0)) { return 0; }
    if (a->window_x != b->window_x || a->window_y != b->window_y) { return 0; }
    if (a->window_width != b->window_width || a->window_height != b->window_height) { return 0; }
    if ((a->window_placed != 0) != (b->window_placed != 0)) { return 0; }
    if ((a->window_maximized != 0) != (b->window_maximized != 0)) { return 0; }
    if (a->lang != b->lang || a->theme != b->theme) { return 0; }
    if (a->default_mode != b->default_mode) { return 0; }
    if (a->loudness_lufs != b->loudness_lufs) { return 0; }
    if (a->true_peak_dbtp != b->true_peak_dbtp) { return 0; }
    if ((a->trim_silence != 0) != (b->trim_silence != 0)) { return 0; }
    if (a->fade_in_ms != b->fade_in_ms || a->fade_out_ms != b->fade_out_ms) { return 0; }
    if (a->gap_ms != b->gap_ms) { return 0; }
    if (a->cache_transcode_mb != b->cache_transcode_mb) { return 0; }
    if (a->cache_covers_mb != b->cache_covers_mb) { return 0; }
    if (a->folder_count != b->folder_count) { return 0; }
    for (u32 i = 0; i < a->folder_count; i += 1) {
        if (!str8_eq(prefs_folder(a, i), prefs_folder(b, i))) { return 0; }
    }
    return 1;
}

TEST(prefs_round_trip) {
    Prefs written;
    test_prefs_fill(&written);
    String8 text = prefs_serialize(arena, &written);
    EXPECT(text.size > 0);
    EXPECT(str8_find(text, str8_lit("version=1"), 0) != text.size);
    EXPECT(str8_find(text, str8_lit("folder=D:\\archives\\flac"), 0) != text.size);
    EXPECT(str8_find(text, str8_lit("column.duration.width=140"), 0) != text.size);

    Prefs read;
    EXPECT(prefs_parse(&read, text));
    EXPECT(test_prefs_equal(&written, &read));
}

TEST(prefs_defaults_are_sane) {
    Prefs prefs;
    prefs_defaults(&prefs);
    EXPECT(prefs.folder_count == 0);
    EXPECT(prefs.sort_column == 0);
    EXPECT(!prefs.sort_desc);
    EXPECT(!prefs.browser_collapsed);
    EXPECT(!prefs.window_placed);
    EXPECT(prefs.window_width >= PREFS_WINDOW_MIN_W);
    u32 seen = 0;
    for (u32 i = 0; i < AppColumn_COUNT; i += 1) {
        EXPECT(prefs.columns[i].visible);
        EXPECT(prefs.columns[i].width >= PREFS_COLUMN_MIN);
        seen |= 1u << prefs.columns[i].order;
    }
    EXPECT(seen == (1u << AppColumn_COUNT) - 1);  // the orders are a permutation
}

// A file that only says a couple of things leaves everything else alone.
TEST(prefs_missing_values) {
    Prefs defaults;
    prefs_defaults(&defaults);

    Prefs prefs;
    EXPECT(prefs_parse(&prefs, str8_lit("# a comment\r\nversion=1\r\nsort.desc=1\r\n")));
    EXPECT(prefs.sort_desc);
    EXPECT(prefs.sort_column == defaults.sort_column);
    EXPECT(prefs.window_width == defaults.window_width);
    EXPECT(prefs.folder_count == 0);
    for (u32 i = 0; i < AppColumn_COUNT; i += 1) {
        EXPECT(prefs.columns[i].width == defaults.columns[i].width);
    }

    // Unknown keys, empty values and lines without an '=' are dropped in
    // silence: an older or newer build must not lose the file.
    EXPECT(prefs_parse(&prefs, str8_lit("version=1\nfuture.key=42\njunk\nsort.desc=\n")));
    EXPECT(!prefs.sort_desc);
}

TEST(prefs_corrupt_falls_back_to_defaults) {
    Prefs defaults;
    prefs_defaults(&defaults);
    Prefs prefs;

    // No version line at all, even though the keys are ones we know.
    EXPECT(!prefs_parse(&prefs, str8_lit("sort.desc=1\nbrowser.collapsed=1\n")));
    EXPECT(test_prefs_equal(&defaults, &prefs));

    // A version we do not write.
    EXPECT(!prefs_parse(&prefs, str8_lit("version=99\nsort.desc=1\n")));
    EXPECT(test_prefs_equal(&defaults, &prefs));

    // Binary garbage, an empty file, and a truncated one.
    u8 noise[64];
    for (u32 i = 0; i < ArrayCount(noise); i += 1) { noise[i] = (u8)(i * 37 + 11); }
    EXPECT(!prefs_parse(&prefs, str8(noise, sizeof(noise))));
    EXPECT(test_prefs_equal(&defaults, &prefs));
    EXPECT(!prefs_parse(&prefs, str8(0, 0)));
    EXPECT(test_prefs_equal(&defaults, &prefs));
    EXPECT(!prefs_parse(&prefs, str8_lit("versio")));
    EXPECT(test_prefs_equal(&defaults, &prefs));
}

// Every number that survives the parse is inside its range, so the view can
// use it without a second thought (ADR-012).
TEST(prefs_values_are_clamped) {
    Prefs defaults;
    prefs_defaults(&defaults);
    Prefs prefs;
    EXPECT(prefs_parse(&prefs, str8_lit("version=1\n"
                                        "column.title.width=99999\n"
                                        "column.artist.width=1\n"
                                        "column.album.width=-40\n"
                                        "column.year.visible=7\n"
                                        "window.width=10\n"
                                        "window.height=99999999\n"
                                        "sort.column=4000000000\n")));
    EXPECT(prefs.columns[AppColumn_Title].width == PREFS_COLUMN_MAX);
    EXPECT(prefs.columns[AppColumn_Artist].width == PREFS_COLUMN_MIN);
    EXPECT(prefs.columns[AppColumn_Album].width == defaults.columns[AppColumn_Album].width);
    EXPECT(prefs.columns[AppColumn_Year].visible);  // 7 is not a boolean: dropped
    EXPECT(prefs.window_width == PREFS_WINDOW_MIN_W);
    EXPECT(prefs.window_height == PREFS_WINDOW_MAX);
    EXPECT(prefs.sort_column == defaults.sort_column);

    // An order that is not a permutation would hide a column behind another:
    // the whole set goes back to the natural order.
    EXPECT(prefs_parse(&prefs, str8_lit("version=1\ncolumn.title.order=3\n"
                                        "column.album.order=3\n")));
    for (u32 i = 0; i < AppColumn_COUNT; i += 1) { EXPECT(prefs.columns[i].order == i); }
}

TEST(prefs_folders) {
    Prefs prefs;
    prefs_defaults(&prefs);
    EXPECT(prefs_add_folder(&prefs, str8_lit("C:\\music")));
    EXPECT(prefs_add_folder(&prefs, str8_lit("C:\\music")));  // idempotent
    EXPECT(prefs.folder_count == 1);
    EXPECT(!prefs_add_folder(&prefs, str8(0, 0)));

    for (u32 i = 1; i < PREFS_MAX_FOLDERS; i += 1) {
        EXPECT(prefs_add_folder(&prefs, str8f(arena, "D:\\lib%u", i)));
    }
    EXPECT(prefs.folder_count == PREFS_MAX_FOLDERS);
    EXPECT(!prefs_add_folder(&prefs, str8_lit("E:\\one too many")));

    // A path longer than the slot is refused rather than truncated.
    u8 long_path[PREFS_FOLDER_CAP + 8];
    mem_set(long_path, 'x', sizeof(long_path));
    Prefs other;
    prefs_defaults(&other);
    EXPECT(!prefs_add_folder(&other, str8(long_path, sizeof(long_path))));
    EXPECT(other.folder_count == 0);
}

// The same round trip, through the file: written atomically, read back, then
// deliberately damaged.
TEST(prefs_file_round_trip) {
    String8 path = test_prefs_path(arena, "minidisk_test.prefs");
    os_file_delete(path);

    Prefs written;
    test_prefs_fill(&written);
    EXPECT(prefs_save(&written, path));

    Prefs read;
    EXPECT(prefs_load(&read, path, arena));
    EXPECT(test_prefs_equal(&written, &read));

    // The atomic write leaves nothing behind.
    OsFileInfo info;
    StructZero(&info);
    EXPECT(!os_file_stat(str8_cat(arena, path, str8_lit(".tmp")), &info));

    Prefs defaults;
    prefs_defaults(&defaults);
    EXPECT(os_file_write_all(path, str8_lit("\x01\x02 not our file at all\n")));
    EXPECT(!prefs_load(&read, path, arena));
    EXPECT(test_prefs_equal(&defaults, &read));

    os_file_delete(path);
    EXPECT(!prefs_load(&read, path, arena));
    EXPECT(test_prefs_equal(&defaults, &read));
}

static void test_prefs_run_all(void) {
    test_report("prefs\n");
    RUN(prefs_defaults_are_sane);
    RUN(prefs_round_trip);
    RUN(prefs_missing_values);
    RUN(prefs_corrupt_falls_back_to_defaults);
    RUN(prefs_values_are_clamped);
    RUN(prefs_folders);
    RUN(prefs_file_round_trip);
}

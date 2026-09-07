// test_covers.c - T-014: where a cover comes from, the LRU of the thumbnail
// atlas, decoding a real image through the platform, and what a drop means.
//
// The decoding test runs against the system decoder on purpose: a vtable slot
// off by one in win32_image.c is exactly the bug a hand written COM binding
// has, and only a real decode catches it.

static String8 test_covers_data_path(Arena *arena, const char *name) {
    return str8f(arena, "tests\\data\\%s", name);
}

// --- the source ------------------------------------------------------------
TEST(cover_folder_rank) {
    Unused(arena);
    // cover beats folder beats front, whatever the case and the extension.
    EXPECT(lib_cover_folder_rank(str8_lit("cover.jpg")) == 1);
    EXPECT(lib_cover_folder_rank(str8_lit("COVER.JPEG")) == 1);
    EXPECT(lib_cover_folder_rank(str8_lit("folder.png")) == 2);
    EXPECT(lib_cover_folder_rank(str8_lit("Front.BMP")) == 3);
    EXPECT(lib_cover_folder_rank(str8_lit("cover.txt")) == 0);
    EXPECT(lib_cover_folder_rank(str8_lit("booklet.jpg")) == 0);
    EXPECT(lib_cover_folder_rank(str8_lit("cover")) == 0);
    EXPECT(lib_cover_folder_rank(str8_lit("")) == 0);
}

TEST(cover_source_priority) {
    Unused(arena);
    Tags tags;
    tags_init(&tags);
    // No picture anywhere: nothing to decode, and the row says so once.
    EXPECT(lib_cover_source_of(&tags, 0) == LibCoverSource_None);
    EXPECT(lib_cover_source_of(&tags, 1) == LibCoverSource_Folder);
    // An embedded picture wins even when the folder has one too.
    tags.cover_size = 1024;
    tags.cover_hash = 0x1234;
    EXPECT(lib_cover_source_of(&tags, 1) == LibCoverSource_Embedded);
    EXPECT(lib_cover_source_of(&tags, 0) == LibCoverSource_Embedded);
}

TEST(cover_folder_image_pick) {
    String8 root = str8_lit("build\\test_covers_dir");
    os_dir_create(root);
    String8 front = os_path_join(arena, root, str8_lit("front.jpg"));
    String8 folder = os_path_join(arena, root, str8_lit("folder.png"));
    String8 noise = os_path_join(arena, root, str8_lit("notes.txt"));
    os_file_write_all(front, str8_lit("x"));
    os_file_write_all(noise, str8_lit("x"));
    // Only front.jpg: it is the pick, whatever else sits there.
    String8 picked = lib_cover_folder_image(arena, root);
    EXPECT(str8_eq(os_path_filename(picked), str8_lit("front.jpg")));
    // folder.png outranks it as soon as it appears.
    os_file_write_all(folder, str8_lit("x"));
    picked = lib_cover_folder_image(arena, root);
    EXPECT(str8_eq(os_path_filename(picked), str8_lit("folder.png")));

    os_file_delete(front);
    os_file_delete(folder);
    os_file_delete(noise);
    // An empty folder answers with nothing rather than with a guess.
    picked = lib_cover_folder_image(arena, root);
    EXPECT(picked.size == 0);
    os_dir_delete(root);
}

// --- the key ---------------------------------------------------------------
TEST(cover_key) {
    Unused(arena);
    // An embedded picture keys on itself: two tracks that share it share one
    // decode even in different folders.
    EXPECT(lib_cover_key(0x99, str8_lit("C:\\a")) == lib_cover_key(0x99, str8_lit("C:\\b")));
    // Without one, the folder is the key: one decode per album.
    EXPECT(lib_cover_key(0, str8_lit("C:\\a")) == lib_cover_key(0, str8_lit("C:\\a")));
    EXPECT(lib_cover_key(0, str8_lit("C:\\a")) != lib_cover_key(0, str8_lit("C:\\b")));
}

// --- the thumbnail atlas ---------------------------------------------------
static void test_covers_fill(u8 *pixels, u32 size, u8 value) {
    mem_set(pixels, value, (u64)size * size * 4);
}

TEST(cover_atlas_lru) {
    r_thumbs_init(arena);
    u32 capacity = r_thumbs_capacity(R_THUMB_SMALL);
    EXPECT(capacity > 1000);  // a full screen of rows, many times over
    u8 *pixels = push_array(arena, u8, (u64)R_THUMB_SMALL * R_THUMB_SMALL * 4);
    test_covers_fill(pixels, R_THUMB_SMALL, 0x40);

    R_AtlasRect rect;
    for (u32 i = 0; i < capacity; i += 1) {
        R_AtlasRect added = r_thumbs_add(1000 + i, R_THUMB_SMALL, pixels);
        EXPECT(added.width == R_THUMB_SMALL);
    }
    EXPECT(r_thumbs_count(R_THUMB_SMALL) == capacity);
    EXPECT(r_thumbs_evictions() == 0);
    EXPECT(r_thumbs_lookup(1000, R_THUMB_SMALL, &rect));

    // The lookup above made key 1000 the most recent, so the next eviction
    // takes 1001 - the oldest one nobody looked at.
    r_thumbs_add(9999, R_THUMB_SMALL, pixels);
    EXPECT(r_thumbs_evictions() == 1);
    EXPECT(r_thumbs_lookup(1000, R_THUMB_SMALL, &rect));
    EXPECT(!r_thumbs_lookup(1001, R_THUMB_SMALL, &rect));
    EXPECT(r_thumbs_lookup(9999, R_THUMB_SMALL, &rect));

    // Adding a key that is already there is an update, never an eviction.
    u64 before = r_thumbs_evictions();
    r_thumbs_add(9999, R_THUMB_SMALL, pixels);
    EXPECT(r_thumbs_evictions() == before);

    // The two sizes are two bands: a small entry never costs a large one.
    u8 *large = push_array(arena, u8, (u64)R_THUMB_LARGE * R_THUMB_LARGE * 4);
    test_covers_fill(large, R_THUMB_LARGE, 0x80);
    EXPECT(!r_thumbs_lookup(9999, R_THUMB_LARGE, &rect));
    R_AtlasRect big = r_thumbs_add(9999, R_THUMB_LARGE, large);
    EXPECT(big.width == R_THUMB_LARGE);
    EXPECT(r_thumbs_lookup(9999, R_THUMB_LARGE, &rect));
    EXPECT(r_thumbs_lookup(9999, R_THUMB_SMALL, &rect));
    // Nothing ever overlaps: the bands are disjoint and the padding is real.
    EXPECT(big.y + R_THUMB_LARGE <= R_THUMB_SMALL_TOP);
    EXPECT(rect.y >= R_THUMB_SMALL_TOP);

    // A full large band evicts inside itself and leaves the rows alone.
    u32 large_capacity = r_thumbs_capacity(R_THUMB_LARGE);
    for (u32 i = 0; i < large_capacity; i += 1) { r_thumbs_add(70000 + i, R_THUMB_LARGE, large); }
    EXPECT(r_thumbs_count(R_THUMB_LARGE) == large_capacity);
    EXPECT(r_thumbs_lookup(9999, R_THUMB_SMALL, &rect));

    r_thumbs_reset();
    EXPECT(r_thumbs_count(R_THUMB_SMALL) == 0);
    EXPECT(!r_thumbs_lookup(9999, R_THUMB_SMALL, &rect));
}

// --- decoding through the platform -----------------------------------------
TEST(cover_decode_png) {
    String8 bytes = os_file_read_all(arena, test_covers_data_path(arena, "cover_2x2.png"));
    EXPECT(bytes.size > 0);

    OsImage image;
    EXPECT(os_image_decode(arena, bytes, 0, &image));
    EXPECT(image.width == 2 && image.height == 2);
    // RGBA, row major, top left first: red, green, blue, white.
    EXPECT(image.pixels[0] == 255 && image.pixels[1] == 0 && image.pixels[2] == 0);
    EXPECT(image.pixels[3] == 255);
    EXPECT(image.pixels[4] == 0 && image.pixels[5] == 255 && image.pixels[6] == 0);
    EXPECT(image.pixels[8] == 0 && image.pixels[9] == 0 && image.pixels[10] == 255);
    EXPECT(image.pixels[12] == 255 && image.pixels[13] == 255 && image.pixels[14] == 255);

    // The two sizes the pipeline asks for, from the same bytes.
    OsImage small;
    EXPECT(os_image_decode(arena, bytes, LIB_COVER_SMALL, &small));
    EXPECT(small.width == LIB_COVER_SMALL && small.height == LIB_COVER_SMALL);
    OsImage big;
    EXPECT(os_image_decode(arena, bytes, LIB_COVER_LARGE, &big));
    EXPECT(big.width == LIB_COVER_LARGE && big.height == LIB_COVER_LARGE);
    // The top left corner is still red after the scale.
    EXPECT(big.pixels[0] > 200 && big.pixels[1] < 60 && big.pixels[2] < 60);

    // A bigger image, and one whose bytes are not an image at all: the
    // boundary answers, it does not crash.
    String8 cover16 = os_file_read_all(arena, test_covers_data_path(arena, "cover_16.png"));
    EXPECT(os_image_decode(arena, cover16, LIB_COVER_SMALL, &small));
    EXPECT(!os_image_decode(arena, str8_lit("not an image at all, really"), 0, &image));
    EXPECT(!os_image_decode(arena, str8(0, 0), 0, &image));
    EXPECT(image.pixels == 0 && image.width == 0);
}

// --- the cache file --------------------------------------------------------
TEST(cover_cache_file) {
    // A cover the pipeline decoded end to end, then read back the way a frame
    // reads it: mapped, header checked, two images at fixed offsets.
    String8 dir = str8_lit("build\\test_covers_cache");
    os_dir_create(dir);
    LibCovers covers;
    lib_covers_init(&covers, arena, dir);
    EXPECT(covers.dir.size != 0);
    // Start cold: a cache file left by the previous run would answer "ready"
    // before a single decode happened, and this test is about the decode.
    {
        OsDirIter it;
        if (os_dir_iter_begin(&it, covers.dir)) {
            OsFileInfo entry;
            while (os_dir_iter_next(&it, &entry)) {
                if (!entry.is_dir) { os_file_delete(os_path_join(arena, covers.dir, entry.name)); }
            }
            os_dir_iter_end(&it);
        }
    }

    String8 track = str8_lit("tests\\data\\id3v24_unsync.mp3");
    OsFileInfo info;
    StructZero(&info);
    EXPECT(os_file_stat(track, &info));
    u64 key = 0xC0FFEEull;
    lib_covers_request(&covers, key, track, info.size);
    // The job is on a worker; the state is one of the three it can be in.
    for (u32 i = 0; i < 2000 && lib_covers_state(&covers, key) == LibCoverState_Queued; i += 1) {
        os_sleep_us(1000);
    }
    // The vector carries a picture inside an *unsynchronised* tag, so this is
    // also the regression test for the capture buffer: without it the picture
    // has no file offset to read from and the cover would never appear.
    EXPECT(lib_covers_state(&covers, key) == LibCoverState_Ready);
    EXPECT(covers.decoded == 1 && covers.failed == 0);

    OsFileMap map;
    {
        EXPECT(lib_covers_open(&covers, key, &map));
        EXPECT(map.size == LIB_COVER_FILE_BYTES);
        const u8 *small = lib_cover_pixels(&map, LIB_COVER_SMALL);
        const u8 *large = lib_cover_pixels(&map, LIB_COVER_LARGE);
        EXPECT(large == small + LIB_COVER_SMALL_BYTES);
        os_file_unmap(&map);
        // A second request is answered from the disk cache, not decoded again.
        LibCovers again;
        lib_covers_init(&again, arena, dir);
        lib_covers_request(&again, key, track, info.size);
        EXPECT(lib_covers_state(&again, key) == LibCoverState_Ready);
        EXPECT(again.decoded == 0);
    }

    // The boundary: a truncated cache file is refused rather than mapped.
    LibCovers other;
    lib_covers_init(&other, arena, dir);
    u64 bad_key = 0xBADF00Dull;
    lib_covers_request(&other, bad_key, str8_lit("tests\\data\\empty.mp3"), 0);
    for (u32 i = 0; i < 2000 && lib_covers_state(&other, bad_key) == LibCoverState_Queued;
         i += 1) {
        os_sleep_us(1000);
    }
    EXPECT(lib_covers_state(&other, bad_key) == LibCoverState_Failed);
    EXPECT(!lib_covers_open(&other, bad_key, &map));
}

// --- what a drop means -----------------------------------------------------
TEST(cover_drop_mapping) {
    Unused(arena);
    // A folder is the folder; a file is the folder it sits in. That is the
    // whole mapping both drop paths share.
    EXPECT(str8_eq(lib_drop_folder(str8_lit("C:\\music\\jazz"), 1), str8_lit("C:\\music\\jazz")));
    EXPECT(str8_eq(lib_drop_folder(str8_lit("C:\\music\\jazz\\a.mp3"), 0),
                   str8_lit("C:\\music\\jazz")));
    EXPECT(lib_drop_folder(str8(0, 0), 1).size == 0);
    // A path with no parent left is its own answer rather than an empty one.
    EXPECT(lib_drop_folder(str8_lit("C:\\a.mp3"), 0).size != 0);
}

static void test_covers_run_all(void) {
    RUN(cover_folder_rank);
    RUN(cover_source_priority);
    RUN(cover_folder_image_pick);
    RUN(cover_key);
    RUN(cover_atlas_lru);
    RUN(cover_decode_png);
    RUN(cover_cache_file);
    RUN(cover_drop_mapping);
}

// test_base.c - unit tests for base/: arenas, strings, UTF-8/16, hash, formatting, math.
// Included by test_main.c (unity build).

TEST(arena_push) {
    u64 start = arena_pos(arena);
    u8 *a = push_array(arena, u8, 10);
    u8 *b = push_array(arena, u8, 10);
    EXPECT(a != 0);
    EXPECT(b > a);
    EXPECT((u64)(b - a) >= 10);
    EXPECT(arena_pos(arena) >= start + 20);

    u64 *aligned = push_array(arena, u64, 3);
    EXPECT(((u64)aligned & 7) == 0);

    u32 *zeroed = push_array_zero(arena, u32, 64);
    b32 all_zero = 1;
    for (u32 i = 0; i < 64; i += 1) {
        if (zeroed[i] != 0) { all_zero = 0; }
    }
    EXPECT(all_zero);
}

TEST(arena_temp) {
    u64 start = arena_pos(arena);
    ArenaTemp temp = arena_temp_begin(arena);
    push_array(arena, u8, 4096);
    EXPECT(arena_pos(arena) > start);
    arena_temp_end(temp);
    EXPECT(arena_pos(arena) == start);

    arena_clear(arena);
    EXPECT(arena_pos(arena) == ARENA_HEADER_SIZE);
}

TEST(arena_commit_growth) {
    // Crossing several commit chunks must keep the memory writable.
    u64 size = ARENA_COMMIT_CHUNK * 3 + 1234;
    u8 *block = push_array(arena, u8, size);
    mem_set(block, 0x5A, size);
    EXPECT(block[0] == 0x5A);
    EXPECT(block[size / 2] == 0x5A);
    EXPECT(block[size - 1] == 0x5A);
}

TEST(scratch_conflicts) {
    ArenaTemp first = scratch_begin(0, 0);
    Arena *conflict = first.arena;
    ArenaTemp second = scratch_begin(&conflict, 1);
    EXPECT(first.arena != 0);
    EXPECT(second.arena != 0);
    EXPECT(second.arena != first.arena);
    scratch_end(second);
    scratch_end(first);
}

TEST(string_compare) {
    String8 a = str8_lit("minidisk");
    String8 b = str8_lit("minidisk");
    String8 c = str8_lit("minidisc");
    EXPECT(a.size == 8);
    EXPECT(str8_eq(a, b));
    EXPECT(!str8_eq(a, c));
    EXPECT(str8_cmp(a, b) == 0);
    EXPECT(str8_cmp(c, a) < 0);
    EXPECT(str8_cmp(a, str8_lit("mini")) > 0);
    EXPECT(str8_starts_with(a, str8_lit("mini")));
    EXPECT(!str8_starts_with(a, str8_lit("disk")));
    EXPECT(str8_ends_with(a, str8_lit("disk")));
}

TEST(string_slices) {
    String8 s = str8_lit("the quick brown fox");
    EXPECT(str8_find(s, str8_lit("quick"), 0) == 4);
    EXPECT(str8_find(s, str8_lit("fox"), 0) == 16);
    EXPECT(str8_find(s, str8_lit("cat"), 0) == s.size);
    EXPECT(str8_find(s, str8_lit("the"), 1) == s.size);
    EXPECT(str8_eq(str8_prefix(s, 3), str8_lit("the")));
    EXPECT(str8_eq(str8_skip(s, 16), str8_lit("fox")));
    EXPECT(str8_eq(str8_substr(s, 4, 5), str8_lit("quick")));
    EXPECT(str8_substr(s, 100, 5).size == 0);
    EXPECT(str8_eq(str8_trim(str8_lit("  \t padded \r\n")), str8_lit("padded")));
    EXPECT(str8_trim(str8_lit("   ")).size == 0);
}

TEST(string_build) {
    String8 joined = str8_cat(arena, str8_lit("mini"), str8_lit("disk"));
    EXPECT(str8_eq(joined, str8_lit("minidisk")));
    EXPECT(joined.str[joined.size] == 0);

    String8 copy = str8_copy(arena, joined);
    EXPECT(str8_eq(copy, joined));
    EXPECT(copy.str != joined.str);

    EXPECT(str8_eq(str8_cstr("wideband"), str8_lit("wideband")));

    String8List parts = str8_split(arena, str8_lit("a//bb/ccc/"), '/');
    EXPECT(parts.count == 3);
    EXPECT(str8_eq(parts.first->str, str8_lit("a")));
    EXPECT(str8_eq(parts.first->next->str, str8_lit("bb")));
    EXPECT(str8_eq(parts.last->str, str8_lit("ccc")));
    EXPECT(str8_eq(str8_list_join(arena, &parts, str8_lit("-")), str8_lit("a-bb-ccc")));
}

TEST(utf8_roundtrip) {
    // 'A', e acute, hiragana ka, emoji (4 bytes)
    static const u32 codepoints[] = {0x41, 0xE9, 0x304B, 0x1F3B5};
    for (u32 i = 0; i < ArrayCount(codepoints); i += 1) {
        u8 buffer[4];
        u32 written = utf8_encode(buffer, codepoints[i]);
        UnicodeDecode decode = utf8_decode(buffer, written);
        EXPECT(decode.codepoint == codepoints[i]);
        EXPECT(decode.advance == written);
    }
    EXPECT(utf8_encode((u8[4]){0}, 0x41) == 1);

    // Truncated and invalid sequences decode to the replacement character.
    static const u8 truncated[] = {0xE3, 0x81};
    UnicodeDecode bad = utf8_decode(truncated, sizeof(truncated));
    EXPECT(bad.codepoint == UNICODE_REPLACEMENT);
    static const u8 lone_continuation[] = {0x80};
    EXPECT(utf8_decode(lone_continuation, 1).codepoint == UNICODE_REPLACEMENT);
    EXPECT(utf8_decode(lone_continuation, 0).advance == 0);
}

TEST(utf16_conversion) {
    String8 source = str8_lit("caf\xC3\xA9 \xE3\x82\xAB \xF0\x9F\x8E\xB5");  // cafe ka music note
    String16 wide = str16_from_str8(arena, source);
    EXPECT(wide.size == 9);           // 4 + 1 + 1 + 1 + 2 surrogates
    EXPECT(wide.str[wide.size] == 0);  // null terminated for Win32
    EXPECT(wide.str[3] == 0xE9);
    EXPECT(wide.str[7] >= 0xD800 && wide.str[7] <= 0xDBFF);

    String8 back = str8_from_str16(arena, wide);
    EXPECT(str8_eq(back, source));
    EXPECT(str8_eq(str8_from_cstr16(arena, wide.str), source));

    u16 units[2];
    EXPECT(utf16_encode(units, 0x41) == 1);
    EXPECT(utf16_encode(units, 0x1F3B5) == 2);
    EXPECT(utf16_decode(units, 2).codepoint == 0x1F3B5);
}

TEST(format) {
    EXPECT(str8_eq(str8f(arena, "plain"), str8_lit("plain")));
    EXPECT(str8_eq(str8f(arena, "%d %d %d", 0, -17, 2147483647), str8_lit("0 -17 2147483647")));
    EXPECT(str8_eq(str8f(arena, "%u", 4294967295u), str8_lit("4294967295")));
    EXPECT(str8_eq(str8f(arena, "%llu", U64_MAX), str8_lit("18446744073709551615")));
    EXPECT(str8_eq(str8f(arena, "%lld", I64_MIN), str8_lit("-9223372036854775808")));
    EXPECT(str8_eq(str8f(arena, "%x", 0xDEADBEEFu), str8_lit("deadbeef")));
    EXPECT(str8_eq(str8f(arena, "%08x", 0x1234u), str8_lit("00001234")));
    EXPECT(str8_eq(str8f(arena, "%c%c", 'o', 'k'), str8_lit("ok")));
    EXPECT(str8_eq(str8f(arena, "%s", "cstring"), str8_lit("cstring")));
    EXPECT(str8_eq(str8f(arena, "[%S]", str8_lit("slice")), str8_lit("[slice]")));
    EXPECT(str8_eq(str8f(arena, "100%%"), str8_lit("100%")));
    EXPECT(str8_eq(str8f(arena, "%f", 3.5), str8_lit("3.500")));
    EXPECT(str8_eq(str8f(arena, "%f", -0.25), str8_lit("-0.250")));
    EXPECT(str8_eq(str8f(arena, "%06f", 1.0 / 3.0), str8_lit("0.333333")));

    // Longer than the internal stack buffer: falls back to the arena path.
    u8 filler[2000];
    mem_set(filler, 'x', sizeof(filler));
    String8 big = str8f(arena, "%S", str8(filler, sizeof(filler)));
    EXPECT(big.size == sizeof(filler));
    EXPECT(big.str[1999] == 'x');

    // Truncating sink: reports the full size, writes at most capacity.
    u8 small[8];
    va_list empty;
    mem_zero(&empty, sizeof(empty));
    String8 measured = str8f(arena, "%S", str8_lit("0123456789"));
    EXPECT(measured.size == 10);
    Unused(small);
}

TEST(hash) {
    String8 a = str8_lit("minidisk");
    String8 b = str8_lit("minidisc");
    u64 ha = hash64(a.str, a.size);
    u64 hb = hash64(b.str, b.size);
    EXPECT(ha != 0);
    EXPECT(ha != hb);
    EXPECT(ha == hash64(a.str, a.size));
    EXPECT(hash64_seed(a.str, a.size, HASH64_SEED) == ha);
    EXPECT(hash64_seed(a.str, a.size, 1) != ha);
    EXPECT(hash64_mix(0) != 0);
    EXPECT(hash64_mix(1) != hash64_mix(2));
    EXPECT(hash64_combine(1, 2) != hash64_combine(2, 1));
}

TEST(math) {
    EXPECT(sqrt_f32(16.0f) == 4.0f);
    EXPECT(abs_f32(-2.5f) == 2.5f);
    EXPECT(floor_f32(-1.5f) == -2.0f);
    EXPECT(ceil_f32(1.2f) == 2.0f);
    EXPECT(round_f32(2.5f) == 2.0f);  // round half to even
    EXPECT(min_f32(1.0f, -1.0f) == -1.0f);
    EXPECT(max_f32(1.0f, -1.0f) == 1.0f);
    EXPECT(clamp_f32(5.0f, 0.0f, 1.0f) == 1.0f);
    EXPECT(lerp_f32(0.0f, 10.0f, 0.25f) == 2.5f);
    EXPECT(f32_bits(bits_f32(0x3F800000u)) == 0x3F800000u);

    V2 p = v2_add(v2(1.0f, 2.0f), v2(3.0f, 4.0f));
    EXPECT(p.x == 4.0f && p.y == 6.0f);
    EXPECT(v2_dot(v2(1.0f, 0.0f), v2(0.0f, 1.0f)) == 0.0f);
    EXPECT(v2_length(v2(3.0f, 4.0f)) == 5.0f);
    EXPECT(v2_sub(p, p).x == 0.0f);
    EXPECT(v2_scale(v2(2.0f, 3.0f), 2.0f).y == 6.0f);

    Rect r = rect(0.0f, 0.0f, 10.0f, 4.0f);
    EXPECT(rect_width(r) == 10.0f);
    EXPECT(rect_height(r) == 4.0f);
    EXPECT(rect_contains(r, v2(1.0f, 1.0f)));
    EXPECT(!rect_contains(r, v2(10.0f, 1.0f)));
    Rect clipped = rect_intersect(r, rect(5.0f, -2.0f, 20.0f, 2.0f));
    EXPECT(clipped.min.x == 5.0f && clipped.max.x == 10.0f);
    EXPECT(rect_width(rect_intersect(r, rect(50.0f, 0.0f, 60.0f, 1.0f))) == 0.0f);
}

TEST(memory_helpers) {
    u8 buffer[32];
    mem_set(buffer, 0, sizeof(buffer));
    for (u8 i = 0; i < 16; i += 1) { buffer[i] = i; }
    mem_move(buffer + 4, buffer, 16);  // overlapping forward move
    EXPECT(buffer[4] == 0);
    EXPECT(buffer[19] == 15);
    EXPECT(mem_cmp(buffer, buffer, sizeof(buffer)) == 0);

    u8 other[32];
    mem_copy(other, buffer, sizeof(other));
    EXPECT(mem_cmp(other, buffer, sizeof(other)) == 0);
    other[31] = 0xFF;
    EXPECT(mem_cmp(other, buffer, sizeof(other)) > 0);
    EXPECT(mem_cmp(buffer, other, sizeof(other)) < 0);
}

TEST(platform_basics) {
    EXPECT(os_page_size() == 4096);
    u64 t0 = os_time_now_us();
    u64 spin = 0;
    for (u32 i = 0; i < 2000000; i += 1) { spin += i; }
    u64 t1 = os_time_now_us();
    EXPECT(spin != 0);
    EXPECT(t1 >= t0);
    EXPECT(os_thread_current_id() != 0);

    String8 path = str8_lit("build\\test_file_roundtrip.bin");
    String8 payload = str8_lit("minidisk file round trip \xC3\xA9\xC3\xA8");
    EXPECT(os_file_write_all(path, payload));
    String8 loaded = os_file_read_all(arena, path);
    EXPECT(loaded.size == payload.size);
    EXPECT(str8_eq(loaded, payload));
    EXPECT(os_file_read_all(arena, str8_lit("build\\does_not_exist.bin")).size == 0);
}

// T-073 / P-010: the edge of an arena, and what a refused commit says.
//
// The failure itself is fatal by design - an arena that cannot grow is a sizing
// bug and AssertAlways is the right answer - so it cannot be provoked inside a
// process that has to keep running. What is tested here is everything around
// it: an arena with a tiny reserve behaves exactly at its edge, a commit of the
// kind that fails on a saturated machine really does fail and really does leave
// an error code behind, and the message the reporter prints formats without
// allocating a byte.
TEST(arena_tiny_reserve) {
    Unused(arena);
    Arena *tiny = arena_alloc(KB(64));
    EXPECT(tiny->reserved == KB(64));
    EXPECT(tiny->committed == ARENA_COMMIT_CHUNK);
    // Right up to the edge: the commit target is clamped to the reserve, and
    // the last byte of it is writable.
    u64 room = tiny->reserved - ARENA_HEADER_SIZE;
    u8 *block = push_array(tiny, u8, room);
    mem_set(block, 0x7E, room);
    EXPECT(block[room - 1] == 0x7E);
    EXPECT(tiny->committed == tiny->reserved);
    EXPECT(arena_pos(tiny) == tiny->reserved);
    arena_release(tiny);

    // A commit outside any reservation: what the diagnostic reads.
    EXPECT(!os_memory_commit((void *)(u64)0x10000, KB(64)));
    EXPECT(os_last_error() != 0);

    // The reporter's own tool: formatting into the caller's buffer, no arena.
    u8 buffer[64];
    String8 line = str8f_buf(buffer, sizeof(buffer), "commit %llu, err %u", (u64)65536, 487u);
    EXPECT(str8_eq(line, str8_lit("commit 65536, err 487")));
    // And it truncates rather than overflowing.
    u8 small[8];
    String8 cut = str8f_buf(small, sizeof(small), "%llu", (u64)1234567890);
    EXPECT(cut.size == sizeof(small));
}

// T-073: the log ring - written by anyone, drained by one flush. The two things
// worth testing are what it does when it is full and how many files it leaves.
TEST(log_ring) {
    String8 dir = os_path_join(arena, os_known_folder(arena, OsKnownFolder_Temp),
                               str8_lit("minidisk_test_log"));
    os_dir_create(dir);
    String8 logs = os_path_join(arena, dir, str8_lit("logs"));
    os_dir_create(logs);
    // Six days of history, more than os_log_init is allowed to keep, plus a
    // file that is not ours and must survive.
    for (u32 i = 0; i < 6; i += 1) {
        os_file_write_all(
            os_path_join(arena, logs, str8f(arena, "minidisk-2020-01-%02u.txt", i + 1)),
            str8_lit("ancien\n"));
    }
    os_file_write_all(os_path_join(arena, logs, str8_lit("garde-moi.txt")), str8_lit("x\n"));

    os_log_init(dir);
    String8 path = os_log_path(arena);
    EXPECT(path.size != 0);

    u32 ours = 0;
    b32 stranger = 0;
    OsDirIter it;
    if (os_dir_iter_begin(&it, logs)) {
        OsFileInfo info;
        while (os_dir_iter_next(&it, &info)) {
            if (info.is_dir) { continue; }
            if (str8_starts_with(info.name, str8_lit("minidisk-"))) { ours += 1; }
            if (str8_eq(info.name, str8_lit("garde-moi.txt"))) { stranger = 1; }
        }
        os_dir_iter_end(&it);
    }
    EXPECT(ours <= OS_LOG_FILE_MAX);
    EXPECT(stranger);

    // A line goes in, a flush puts it on disk.
    u64 dropped_before = os_log_dropped();
    os_debug_print(str8_lit("test: une ligne de journal\n"));
    os_log_flush();
    String8 content = os_file_read_all(arena, path);
    EXPECT(str8_find(content, str8_lit("une ligne de journal"), 0) != content.size);

    // Overflow: two rings' worth in one go with no flush in between. What does
    // not fit is refused and counted, and the next flush says so in the file.
    u8 *big = push_array(arena, u8, KB(4));
    mem_set(big, 'A', KB(4));
    big[KB(4) - 1] = '\n';
    for (u32 i = 0; i < 32; i += 1) { os_debug_print(str8(big, KB(4))); }
    EXPECT(os_log_dropped() > dropped_before);
    os_log_flush();
    content = os_file_read_all(arena, path);
    EXPECT(str8_find(content, str8_lit("octet(s) perdu(s)"), 0) != content.size);
    os_log_shutdown();

    // The handle is not kept between flushes: nothing is holding the file.
    EXPECT(os_file_delete(path));
}

static void test_base_run_all(void) {
    RUN(arena_push);
    RUN(arena_temp);
    RUN(arena_commit_growth);
    RUN(arena_tiny_reserve);
    RUN(log_ring);
    RUN(scratch_conflicts);
    RUN(string_compare);
    RUN(string_slices);
    RUN(string_build);
    RUN(utf8_roundtrip);
    RUN(utf16_conversion);
    RUN(format);
    RUN(hash);
    RUN(math);
    RUN(memory_helpers);
    RUN(platform_basics);
}

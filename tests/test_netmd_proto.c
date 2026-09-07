// test_netmd_proto.c - T-021: the frame language, the disc queries, the group
// syntax, the charset, and the four transcripts replayed end to end.
//
// Every hex string below is quoted from docs/research/01-netmd-protocol.md. A
// test that disagrees with the research document is a protocol bug, which is
// the only reason to write these by hand rather than generate them.

// --- the mini language (s3.3) ------------------------------------------------

TEST(netmd_query_literals) {
    // s3.7.1, getDiscFlags, verbatim.
    String8 frame = netmd_query(arena, "00 1806 01101000 ff00 0001000b");
    EXPECT(frame.size == 13);
    static const u8 expected[13] = {0x00, 0x18, 0x06, 0x01, 0x10, 0x10, 0x00,
                                    0xFF, 0x00, 0x00, 0x01, 0x00, 0x0B};
    EXPECT(mem_cmp(frame.str, expected, sizeof(expected)) == 0);
    // Whitespace is decoration: the same bytes come out however it is grouped.
    String8 same = netmd_query(arena, "0018 06 01 1010 00ff000001000b");
    EXPECT(same.size == frame.size && mem_cmp(same.str, frame.str, frame.size) == 0);
}

TEST(netmd_query_formats) {
    // One of each specifier, big-endian unless said otherwise.
    String8 frame = netmd_query(arena, "%b %w %d %q", 0x12u, 0x3456u, 0x789ABCDEu,
                                (u64)0x0102030405060708ull);
    EXPECT(frame.size == 15);
    EXPECT(frame.str[0] == 0x12);
    EXPECT(frame.str[1] == 0x34 && frame.str[2] == 0x56);
    EXPECT(frame.str[3] == 0x78 && frame.str[6] == 0xDE);
    EXPECT(frame.str[7] == 0x01 && frame.str[14] == 0x08);

    String8 little = netmd_query(arena, "%<w %<d", 0x1234u, 0x89ABCDEFu);
    EXPECT(little.size == 6);
    EXPECT(little.str[0] == 0x34 && little.str[1] == 0x12);
    EXPECT(little.str[2] == 0xEF && little.str[5] == 0x89);

    // BCD: 42 decimal is 0x42 on the wire (s3.3).
    String8 bcd = netmd_query(arena, "%B %W", 42u, 1234u);
    EXPECT(bcd.size == 3);
    EXPECT(bcd.str[0] == 0x42 && bcd.str[1] == 0x12 && bcd.str[2] == 0x34);

    u8 payload[3] = {'a', 'b', 'c'};
    String8 sized = netmd_query(arena, "%x", payload, 3u);
    EXPECT(sized.size == 5 && sized.str[0] == 0 && sized.str[1] == 3 && sized.str[2] == 'a');
    String8 short_sized = netmd_query(arena, "%z", payload, 3u);
    EXPECT(short_sized.size == 4 && short_sized.str[0] == 3);
    // %s is %x plus the NUL it counts in its own length.
    String8 nul = netmd_query(arena, "%s", payload, 3u);
    EXPECT(nul.size == 6 && nul.str[1] == 4 && nul.str[5] == 0);
    String8 raw = netmd_query(arena, "ff %* ee", payload, 3u);
    EXPECT(raw.size == 5 && raw.str[0] == 0xFF && raw.str[4] == 0xEE);
}

TEST(netmd_scan_round_trip) {
    u8 payload[4] = {'H', 'i', '!', '?'};
    String8 frame = netmd_query(arena, "18 %b %w %d %q %B %W %x %z", 0x7Fu, 0xBEEFu,
                                0xCAFEBABEu, (u64)0x1122334455667788ull, 59u, 4321u, payload, 4u,
                                payload, 2u);
    u32 one = 0, two = 0, four = 0, bcd1 = 0, bcd2 = 0;
    u64 eight = 0;
    String8 sized, short_sized;
    EXPECT(netmd_scan(frame, "18 %b %w %d %q %B %W %x %z", &one, &two, &four, &eight, &bcd1,
                      &bcd2, &sized, &short_sized));
    EXPECT(one == 0x7Fu && two == 0xBEEFu && four == 0xCAFEBABEu);
    EXPECT(eight == 0x1122334455667788ull);
    EXPECT(bcd1 == 59u && bcd2 == 4321u);
    EXPECT(sized.size == 4 && sized.str[0] == 'H');
    EXPECT(short_sized.size == 2 && short_sized.str[1] == 'i');

    // %? skips, %* takes what is left.
    String8 rest;
    u32 skipped_after = 0;
    EXPECT(netmd_scan(frame, "18 %? %w %*", &skipped_after, &rest));
    EXPECT(skipped_after == 0xBEEFu);
    EXPECT(rest.size == frame.size - 4);

    // A literal that does not match is a different reply model, not a longer
    // one - and leftover bytes are the same kind of failure (s3.3).
    EXPECT(!netmd_scan(frame, "19 %b %*", &one, &rest));
    EXPECT(!netmd_scan(frame, "18 %b", &one));
    EXPECT(netmd_scan(str8_lit("\x09\x18\x06"), "09 1806"));
}

TEST(netmd_scan_research_examples) {
    // s3.8.2, the reply to "read the half-width title of track 3", verbatim.
    static const u8 reply[] = {0x09, 0x18, 0x06, 0x02, 0x20, 0x18, 0x02, 0x00, 0x03, 0x30,
                               0x00, 0x0A, 0x00, 0x10, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00,
                               0x0C, 0x00, 0x0A, 0x00, 0x06, 'H',  'e',  'l',  'l',  'o',
                               '!'};
    String8 title;
    EXPECT(netmd_scan(str8((u8 *)reply, sizeof(reply)),
                      "09 1806 022018%? %?%? %?%? %?%? 1000 00%?0000 00%?000a %x", &title));
    EXPECT(str8_eq(title, str8_lit("Hello!")));

    // s3.7.4a, the length of track 0: 00h 03m 47s 26f, BCD.
    static const u8 length_reply[] = {0x09, 0x18, 0x06, 0x02, 0x20, 0x10, 0x01, 0x00, 0x00,
                                      0x30, 0x00, 0x01, 0x00, 0x10, 0x00, 0x00, 0x0B, 0x00,
                                      0x00, 0x00, 0x0A, 0x00, 0x01, 0x00, 0x06, 0x00, 0x00,
                                      0x00, 0x03, 0x47, 0x26};
    String8 payload;
    EXPECT(netmd_scan(str8((u8 *)length_reply, sizeof(length_reply)),
                      "09 1806 02201001 %?%? %?%? %?%? 1000 00%?0000 %x", &payload));
    u32 hours = 0, minutes = 0, seconds = 0, frames = 0;
    EXPECT(netmd_scan(payload, "0001 0006 0000 %B %B %B %B", &hours, &minutes, &seconds,
                      &frames));
    EXPECT(hours == 0 && minutes == 3 && seconds == 47 && frames == 26);
    NetmdTime time = netmd_time_make(hours, minutes, seconds, frames);
    // 1 second = 512 TOC frames (s7.2).
    EXPECT(time.total_frames == (3u * 60u + 47u) * 512u + 26u);
    EXPECT(time.ms == 227050u);
}

TEST(netmd_time_and_bcd) {
    EXPECT(netmd_bcd_from_u8(0) == 0x00);
    EXPECT(netmd_bcd_from_u8(42) == 0x42);
    EXPECT(netmd_bcd_from_u8(99) == 0x99);
    EXPECT(netmd_bcd_to_u8(0x42) == 42);
    EXPECT(netmd_bcd_to_u8(0x99) == 99);

    NetmdTime zero = netmd_time_make(0, 0, 0, 0);
    EXPECT(zero.total_frames == 0 && zero.ms == 0);
    // 80 minutes of SP, the capacity of the commonest disc (s7.1).
    NetmdTime eighty = netmd_time_make(1, 20, 0, 0);
    EXPECT(eighty.total_frames == 80u * 60u * 512u);
    EXPECT(eighty.ms == 80u * 60u * 1000u);
    NetmdTime mixed = netmd_time_make(0, 3, 47, 26);
    EXPECT(mixed.minutes == 3 && mixed.seconds == 47 && mixed.frames == 26);
    Unused(arena);
}

// --- the charset (s3.11), both directions ------------------------------------

TEST(netmd_charset_decode) {
    u8 out[64];
    // ASCII passes through untouched: it is the same byte in Shift-JIS.
    static const u8 ascii[] = {'M', 'D', ' ', '8', '0'};
    u64 size = netmd_sjis_to_utf8(ascii, sizeof(ascii), out, sizeof(out));
    EXPECT(str8_eq(str8(out, size), str8_lit("MD 80")));

    // Half-width katakana, the single byte range 0xA1..0xDF -> U+FF61..U+FF9F.
    // 0xB6 0xC0 0xB6 0xC5 is KA TA KA NA.
    static const u8 kana[] = {0xB6, 0xC0, 0xB6, 0xC5};
    size = netmd_sjis_to_utf8(kana, sizeof(kana), out, sizeof(out));
    EXPECT(str8_eq(str8(out, size), str8_lit("\xEF\xBD\xB6\xEF\xBE\x80\xEF\xBD\xB6"
                                             "\xEF\xBE\x85")));

    // Two byte sequences from the generated table: 0x8260 is full-width A.
    static const u8 wide[] = {0x82, 0x60, 0x81, 0x40};
    size = netmd_sjis_to_utf8(wide, sizeof(wide), out, sizeof(out));
    EXPECT(str8_eq(str8(out, size), str8_lit("\xEF\xBC\xA1\xE3\x80\x80")));

    // The TOC pads with NUL; a padded title stops there.
    static const u8 padded[] = {'A', 'B', 0x00, 'C'};
    size = netmd_sjis_to_utf8(padded, sizeof(padded), out, sizeof(out));
    EXPECT(str8_eq(str8(out, size), str8_lit("AB")));

    // A kanji is not in the table (see the generator) and a lead byte with
    // nothing behind it is a truncated title: both become '?' rather than
    // costing the user the whole disc listing.
    static const u8 unknown[] = {0x88, 0x9F, 0x82};
    size = netmd_sjis_to_utf8(unknown, sizeof(unknown), out, sizeof(out));
    EXPECT(str8_eq(str8(out, size), str8_lit("??")));

    // The capacity is honoured to the byte: a truncated buffer never overruns.
    u8 tiny[2];
    size = netmd_sjis_to_utf8(kana, sizeof(kana), tiny, sizeof(tiny));
    EXPECT(size == 0);  // one half-width kana needs three UTF-8 bytes
    Unused(arena);
}

TEST(netmd_charset_encode) {
    // The write direction, the one T-031 generated: full-width and accents fold
    // to what the TOC can hold. The two tables are generated by the same script
    // from the same Unicode data, which is what makes the pair coherent.
    PlanTitlePreview *preview = push_struct(arena, PlanTitlePreview);
    plan_toc_preview(str8_lit("Café \xEF\xBC\xA1"), 0, preview);
    EXPECT(str8_eq(str8(preview->text, preview->size), str8_lit("Cafe A")));
    // A voiced kana costs two half-width characters, which is the whole reason
    // the budget is counted in cells (s3.11).
    plan_toc_preview(str8_lit("\xE3\x82\xAC"), 0, preview);  // KA with dakuten
    EXPECT(str8_eq(str8(preview->text, preview->size), str8_lit("\xEF\xBD\xB6\xEF\xBE\x9E")));
}

// --- the group syntax (s3.10) ------------------------------------------------

static DiscLayout *test_netmd_groups(Arena *arena, u32 track_count, String8 raw) {
    DiscLayout *layout = push_struct_zero(arena, DiscLayout);
    layout->track_count = track_count;
    netmd_parse_groups(layout, raw);
    return layout;
}

static b32 test_netmd_group_named(const DiscLayout *layout, u32 group, const char *name) {
    return str8_eq(str8((u8 *)layout->groups[group].name, layout->groups[group].name_size),
                   str8_cstr(name));
}

TEST(netmd_group_parsing) {
    // s3.10, the worked example.
    DiscLayout *layout = test_netmd_groups(arena, 9, str8_lit("0;Mon Album//1-4;Face A//"
                                                              "5-9;Face B//"));
    EXPECT(str8_eq(str8(layout->title, layout->title_size), str8_lit("Mon Album")));
    EXPECT(layout->group_count == 2);
    EXPECT(test_netmd_group_named(layout, 0, "Face A"));
    EXPECT(layout->groups[0].first == 0 && layout->groups[0].count == 4);
    EXPECT(test_netmd_group_named(layout, 1, "Face B"));
    EXPECT(layout->groups[1].first == 4 && layout->groups[1].count == 5);
    EXPECT(layout->ungrouped_count == 0);
    EXPECT(layout->tracks[0].group == 0 && layout->tracks[8].group == 1);

    // No "0;" prefix: groups, and no disc title at all.
    layout = test_netmd_groups(arena, 10, str8_lit("1;Intro//2-10;Corps//"));
    EXPECT(layout->title_size == 0);
    EXPECT(layout->group_count == 2);
    EXPECT(layout->groups[0].count == 1 && layout->groups[1].count == 9);

    // No "//" anywhere: the whole string is the title and nothing else.
    layout = test_netmd_groups(arena, 3, str8_lit("Simplement un titre"));
    EXPECT(str8_eq(str8(layout->title, layout->title_size), str8_lit("Simplement un titre")));
    EXPECT(layout->group_count == 0 && layout->ungrouped_count == 3);

    // A title that contains "//" but does not end with it is not a group string.
    layout = test_netmd_groups(arena, 2, str8_lit("http://example"));
    EXPECT(str8_eq(str8(layout->title, layout->title_size), str8_lit("http://example")));
    EXPECT(layout->group_count == 0);
}

TEST(netmd_group_edge_cases) {
    // An empty segment is skipped, and so is one with no ';' at all.
    DiscLayout *layout = test_netmd_groups(arena, 5, str8_lit("0;Titre////3-5;Fin//"));
    EXPECT(str8_eq(str8(layout->title, layout->title_size), str8_lit("Titre")));
    EXPECT(layout->group_count == 1 && layout->groups[0].first == 2);
    EXPECT(layout->ungrouped_count == 2);
    EXPECT(layout->tracks[0].group == NETMD_NO_GROUP);

    layout = test_netmd_groups(arena, 4, str8_lit("0;Titre//pas un groupe//1-2;Vrai//"));
    EXPECT(layout->group_count == 1);
    EXPECT(test_netmd_group_named(layout, 0, "Vrai"));

    // s3.10: a range is not rewritten when a track is erased, so it can point
    // past the end of the disc. It is clamped, never trusted.
    layout = test_netmd_groups(arena, 3, str8_lit("0;T//1-9;Trop long//"));
    EXPECT(layout->group_count == 1);
    EXPECT(layout->groups[0].first == 0 && layout->groups[0].count == 3);
    layout = test_netmd_groups(arena, 3, str8_lit("0;T//7-9;Hors disque//"));
    EXPECT(layout->group_count == 0);

    // A group name may hold a ';' - the split is on the first one only.
    layout = test_netmd_groups(arena, 4, str8_lit("0;T//1-4;Rock;Pop//"));
    EXPECT(layout->group_count == 1);
    EXPECT(test_netmd_group_named(layout, 0, "Rock;Pop"));

    // One track belongs to one group: an overlap is a corrupt title, and the
    // first claim wins rather than the last.
    layout = test_netmd_groups(arena, 6, str8_lit("0;T//1-4;A//3-6;B//"));
    EXPECT(layout->group_count == 1);
    EXPECT(test_netmd_group_named(layout, 0, "A"));

    // A single track range, and the group string with no disc title.
    layout = test_netmd_groups(arena, 2, str8_lit("2;Seule//"));
    EXPECT(layout->title_size == 0);
    EXPECT(layout->group_count == 1);
    EXPECT(layout->groups[0].first == 1 && layout->groups[0].count == 1);

    // s3.10, full width: the service characters are full width too, and mean
    // exactly the same thing. U+FF10 '0', U+FF1B ';', U+FF0F '/', U+FF0D '-'.
    layout = test_netmd_groups(
            arena, 4,
            str8_lit("\xEF\xBC\x90\xEF\xBC\x9B" "Titre" "\xEF\xBC\x8F\xEF\xBC\x8F"
                     "\xEF\xBC\x91\xEF\xBC\x8D\xEF\xBC\x94\xEF\xBC\x9B" "Groupe"
                     "\xEF\xBC\x8F\xEF\xBC\x8F"));
    EXPECT(str8_eq(str8(layout->title, layout->title_size), str8_lit("Titre")));
    EXPECT(layout->group_count == 1);
    EXPECT(layout->groups[0].first == 0 && layout->groups[0].count == 4);
    EXPECT(test_netmd_group_named(layout, 0, "Groupe"));
}

// --- the transcripts ---------------------------------------------------------

typedef struct TestNetmdReplay {
    NetmdReplay replay;
    UsbTransport transport;
    NetmdSession session;
} TestNetmdReplay;

static b32 test_netmd_open(TestNetmdReplay *fixture, Arena *arena, const char *name) {
    StructZero(fixture);
    String8 path = str8f(arena, "tests/netmd/%s", name);
    if (!netmd_replay_load(&fixture->replay, arena, path)) { return 0; }
    netmd_replay_transport(&fixture->replay, &fixture->transport);
    netmd_session_init(&fixture->session, &fixture->transport, 0x054C, 0x0084);
    return 1;
}

TEST(netmd_replay_no_disc) {
    TestNetmdReplay fixture;
    EXPECT(test_netmd_open(&fixture, arena, "mzn505_nodisc.trace"));
    DiscLayout *layout = push_struct_zero(arena, DiscLayout);
    EXPECT(netmd_read_disc(&fixture.session, arena, layout) == NetmdResult_Ok);
    // An empty bay is an answer, not a failure: no flags, no tracks, and the
    // panel says "no disc" rather than "unreachable".
    EXPECT((layout->flags & NetmdDiscFlag_Present) == 0);
    EXPECT(layout->track_count == 0);
    EXPECT(netmd_replay_ok(&fixture.replay));
    EXPECT(netmd_replay_done(&fixture.replay));
}

TEST(netmd_replay_blank_disc) {
    TestNetmdReplay fixture;
    EXPECT(test_netmd_open(&fixture, arena, "mzn505_blank.trace"));
    DiscLayout *layout = push_struct_zero(arena, DiscLayout);
    EXPECT(netmd_read_disc(&fixture.session, arena, layout) == NetmdResult_Ok);
    EXPECT((layout->flags & NetmdDiscFlag_Present) != 0);
    EXPECT((layout->flags & NetmdDiscFlag_Writable) != 0);
    EXPECT((layout->flags & NetmdDiscFlag_WriteProtected) == 0);
    EXPECT((layout->flags & NetmdDiscFlag_Empty) != 0);
    EXPECT(layout->track_count == 0);
    EXPECT(layout->title_size == 0);
    // 80 minutes of SP, nothing recorded, all of it free.
    EXPECT(layout->capacity.total.ms == 80u * 60u * 1000u);
    EXPECT(layout->capacity.recorded.ms == 0);
    EXPECT(layout->capacity.available.ms == 80u * 60u * 1000u);
    EXPECT(!layout->capacity.halved);
    EXPECT(netmd_replay_ok(&fixture.replay));
    EXPECT(netmd_replay_done(&fixture.replay));
}

TEST(netmd_replay_protected_disc) {
    TestNetmdReplay fixture;
    EXPECT(test_netmd_open(&fixture, arena, "mzn505_protected.trace"));
    DiscLayout *layout = push_struct_zero(arena, DiscLayout);
    EXPECT(netmd_read_disc(&fixture.session, arena, layout) == NetmdResult_Ok);
    EXPECT((layout->flags & NetmdDiscFlag_WriteProtected) != 0);
    EXPECT(layout->track_count == 1);
    EXPECT(str8_eq(str8(layout->title, layout->title_size), str8_lit("Demo")));
    EXPECT(layout->group_count == 0);
    // s3.7.4c: 0x03 is a checked-out track, the one thing that cannot simply be
    // erased. It is why the panel marks it.
    EXPECT(layout->tracks[0].protect == 1);
    EXPECT(str8_eq(str8(layout->tracks[0].title, layout->tracks[0].title_size),
                   str8_lit("Piste unique")));
    EXPECT(layout->tracks[0].duration_ms == 12u * 60u * 1000u + 30u * 1000u);
    EXPECT(netmd_replay_ok(&fixture.replay));
    EXPECT(netmd_replay_done(&fixture.replay));
}

TEST(netmd_replay_full_disc) {
    TestNetmdReplay fixture;
    EXPECT(test_netmd_open(&fixture, arena, "mzn505_full.trace"));
    DiscLayout *layout = push_struct_zero(arena, DiscLayout);
    u64 started = os_time_now_us();
    EXPECT(netmd_read_disc(&fixture.session, arena, layout) == NetmdResult_Ok);
    u64 elapsed_ms = (os_time_now_us() - started) / 1000u;
    // The acceptance criterion, measured against the transcript: no device, so
    // no poll ever has to wait, but the command count is the real one.
    test_report("  netmd: 10 track disc replayed in %llu ms, %u commands\n", elapsed_ms,
                fixture.session.exchanges);
    EXPECT(elapsed_ms < 2000);

    EXPECT((layout->flags & NetmdDiscFlag_Present) != 0);
    EXPECT((layout->flags & NetmdDiscFlag_Empty) == 0);
    EXPECT(layout->track_count == 10);
    EXPECT(str8_eq(str8(layout->title, layout->title_size), str8_lit("Nuit blanche")));
    EXPECT(layout->group_count == 2);
    EXPECT(test_netmd_group_named(layout, 0, "Face A"));
    EXPECT(layout->groups[0].first == 0 && layout->groups[0].count == 4);
    EXPECT(test_netmd_group_named(layout, 1, "Face B"));
    EXPECT(layout->groups[1].first == 4 && layout->groups[1].count == 5);
    // The tenth track is in no group: the groups were not rewritten when it was
    // added, which is exactly what happens on a real disc (s3.10).
    EXPECT(layout->ungrouped_count == 1);
    EXPECT(layout->tracks[9].group == NETMD_NO_GROUP);

    EXPECT(str8_eq(str8(layout->tracks[0].title, layout->tracks[0].title_size),
                   str8_lit("Ouverture")));
    EXPECT(layout->tracks[0].encoding == NetmdEncoding_SP);
    EXPECT(layout->tracks[0].mono == 0);
    EXPECT(layout->tracks[0].duration_ms == 3u * 60000u + 47u * 1000u);
    // A full-width title lives in its own space and comes back decoded: the
    // full-width Latin block is in the generated table. "KYOTO", full width.
    EXPECT(str8_eq(str8(layout->tracks[2].title_full, layout->tracks[2].title_full_size),
                   str8_lit("\xEF\xBC\xAB\xEF\xBC\xB9\xEF\xBC\xAF\xEF\xBC\xB4\xEF\xBC\xAF")));
    // A kanji is not in it: the table stops before the ~6500 ideographs, which
    // would cost 26 KB of a 360 KB executable for titles this application's
    // users cannot read. An unmapped pair decodes to '?' rather than costing
    // the whole listing (tools/gen_charset_tables.py says the same thing).
    EXPECT(str8_eq(str8(layout->tracks[5].title_full, layout->tracks[5].title_full_size),
                   str8_lit("??")));
    EXPECT(layout->tracks[3].mono == 1);
    EXPECT(layout->tracks[4].encoding == NetmdEncoding_LP2);
    EXPECT(layout->tracks[7].encoding == NetmdEncoding_LP4);
    EXPECT(layout->tracks[8].protect == 1);
    // Half-width katakana survives the round trip through the generated table.
    EXPECT(str8_eq(str8(layout->tracks[6].title, layout->tracks[6].title_size),
                   str8_lit("\xEF\xBD\xB6\xEF\xBE\x80\xEF\xBD\xB6\xEF\xBE\x85")));
    // An untitled track answers REJECTED, which is an empty title (s3.8.2).
    EXPECT(layout->tracks[9].title_size == 0);

    EXPECT(netmd_replay_ok(&fixture.replay));
    EXPECT(netmd_replay_done(&fixture.replay));
}

// --- the transport commands (s3.13-3.15) -------------------------------------

TEST(netmd_control_frames) {
    // The exact frames of s3.13 and s3.15, checked by making the replay demand
    // them: a wrong byte here is a diverged transcript, not a silent mistake.
    NetmdReplay replay;
    netmd_replay_init(&replay,
                      str8_lit("# play\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 00 81 00 00\n"
                               "> 41 80 00 00 00 00 08 00 00 18 c3 ff 75 00 00 00\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 01 81 08 00\n"
                               "> c1 81 00 00 00 00 08 00\n"
                               "< 09 18 c3 00 75 00 00 00\n"
                               "# pause\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 00 81 00 00\n"
                               "> 41 80 00 00 00 00 08 00 00 18 c3 ff 7d 00 00 00\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 01 81 08 00\n"
                               "> c1 81 00 00 00 00 08 00\n"
                               "< 09 18 c3 00 7d 00 00 00\n"
                               "# stop\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 00 81 00 00\n"
                               "> 41 80 00 00 00 00 08 00 00 18 c5 ff 00 00 00 00\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 01 81 08 00\n"
                               "> c1 81 00 00 00 00 08 00\n"
                               "< 09 18 c5 00 00 00 00 00\n"
                               "# next: direction 0x8001\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 00 81 00 00\n"
                               "> 41 80 00 00 00 00 0b 00 00 18 50 ff 10 00 00 00 00 80 01\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 01 81 09 00\n"
                               "> c1 81 00 00 00 00 09 00\n"
                               "< 09 18 50 00 10 00 00 00 00\n"
                               "# prev: direction 0x0002\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 00 81 00 00\n"
                               "> 41 80 00 00 00 00 0b 00 00 18 50 ff 10 00 00 00 00 00 02\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 01 81 09 00\n"
                               "> c1 81 00 00 00 00 09 00\n"
                               "< 09 18 50 00 10 00 00 00 00\n"
                               "# eject: NOT IMPLEMENTED on a portable (s3.15)\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 00 81 00 00\n"
                               "> 41 80 00 00 00 00 06 00 00 18 c1 ff 60 00\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 01 81 06 00\n"
                               "> c1 81 00 00 00 00 06 00\n"
                               "< 08 18 c1 ff 60 00\n"));
    UsbTransport transport;
    netmd_replay_transport(&replay, &transport);
    NetmdSession session;
    netmd_session_init(&session, &transport, 0x054C, 0x0084);

    EXPECT(netmd_play(&session, arena) == NetmdResult_Ok);
    EXPECT(netmd_pause(&session, arena) == NetmdResult_Ok);
    EXPECT(netmd_stop(&session, arena) == NetmdResult_Ok);
    EXPECT(netmd_next(&session, arena) == NetmdResult_Ok);
    EXPECT(netmd_prev(&session, arena) == NetmdResult_Ok);
    EXPECT(netmd_eject(&session, arena) == NetmdResult_NotImplemented);
    EXPECT(netmd_replay_ok(&replay));
    EXPECT(netmd_replay_done(&replay));
    EXPECT(session.exchanges == 6);
}

TEST(netmd_status_bytes) {
    // s3.2: the AV/C values, not the ones netmd-js prints. 0x0F is INTERIM and
    // means "the answer is coming", so the exchange polls again instead of
    // handing a promise back as if it were a reply.
    NetmdReplay replay;
    netmd_replay_init(&replay,
                      str8_lit("> c1 01 00 00 00 00 04 00\n"
                               "< 00 81 00 00\n"
                               "> 41 80 00 00 00 00 08 00 00 18 c3 ff 75 00 00 00\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 01 81 08 00\n"
                               "> c1 81 00 00 00 00 08 00\n"
                               "< 0f 18 c3 00 75 00 00 00\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 01 81 08 00\n"
                               "> c1 81 00 00 00 00 08 00\n"
                               "< 09 18 c3 00 75 00 00 00\n"));
    UsbTransport transport;
    netmd_replay_transport(&replay, &transport);
    NetmdSession session;
    netmd_session_init(&session, &transport, 0x054C, 0x0084);
    EXPECT(netmd_play(&session, arena) == NetmdResult_Ok);
    EXPECT(session.last_status == NetmdStatus_Accepted);
    // One command was sent, not two: an INTERIM is followed, never resent.
    EXPECT(session.exchanges == 1);
    EXPECT(netmd_replay_done(&replay));

    // A REJECTED is a domain answer and stays one.
    NetmdReplay rejected;
    netmd_replay_init(&rejected,
                      str8_lit("> c1 01 00 00 00 00 04 00\n"
                               "< 00 81 00 00\n"
                               "> 41 80 00 00 00 00 08 00 00 18 c3 ff 75 00 00 00\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 01 81 08 00\n"
                               "> c1 81 00 00 00 00 08 00\n"
                               "< 0a 18 c3 00 75 00 00 00\n"));
    netmd_replay_transport(&rejected, &transport);
    netmd_session_init(&session, &transport, 0x054C, 0x0084);
    EXPECT(netmd_play(&session, arena) == NetmdResult_Rejected);
    EXPECT(session.last_status == NetmdStatus_Rejected);
}

TEST(netmd_orphan_reply_is_drained) {
    // s2.5, invariant 1: a reply still pending belongs to a command nobody
    // collected. Sending on top of it would answer that one for the rest of the
    // session, so the exchange reads it and throws it away first.
    NetmdReplay replay;
    netmd_replay_init(&replay,
                      str8_lit("# the poll says four bytes are still waiting\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 01 81 04 00\n"
                               "# read and dropped\n"
                               "> c1 81 00 00 00 00 04 00\n"
                               "< 09 18 c3 00\n"
                               "> 41 80 00 00 00 00 08 00 00 18 c3 ff 75 00 00 00\n"
                               "> c1 01 00 00 00 00 04 00\n"
                               "< 01 81 08 00\n"
                               "> c1 81 00 00 00 00 08 00\n"
                               "< 09 18 c3 00 75 00 00 00\n"));
    UsbTransport transport;
    netmd_replay_transport(&replay, &transport);
    NetmdSession session;
    netmd_session_init(&session, &transport, 0x054C, 0x0084);
    EXPECT(netmd_play(&session, arena) == NetmdResult_Ok);
    EXPECT(netmd_replay_ok(&replay));
    EXPECT(netmd_replay_done(&replay));
}

// --- the trace writer (--netmd-trace) ----------------------------------------

TEST(netmd_trace_round_trip) {
    // What --netmd-trace writes has to be what netmd_replay.c reads, or a
    // capture from the real device would be useless. So: replay a session
    // through the trace writer, then replay the transcript it produced.
    NetmdReplay source;
    netmd_replay_init(&source, str8_lit("> c1 01 00 00 00 00 04 00\n"
                                        "< 00 81 00 00\n"
                                        "> 41 80 00 00 00 00 08 00 00 18 c3 ff 75 00 00 00\n"
                                        "> c1 01 00 00 00 00 04 00\n"
                                        "< 01 81 08 00\n"
                                        "> c1 81 00 00 00 00 08 00\n"
                                        "< 09 18 c3 00 75 00 00 00\n"));
    UsbTransport inner;
    netmd_replay_transport(&source, &inner);

    NetmdTrace trace;
    netmd_trace_init(&trace, arena, &inner);
    UsbTransport traced;
    netmd_trace_transport(&trace, &traced);
    NetmdSession session;
    netmd_session_init(&session, &traced, 0x054C, 0x0084);
    EXPECT(netmd_play(&session, arena) == NetmdResult_Ok);
    EXPECT(trace.exchanges == 4);

    String8 text = str8_list_join(arena, &trace.lines, str8_lit("\n"));
    EXPECT(text.size != 0);
    NetmdReplay again;
    netmd_replay_init(&again, text);
    UsbTransport replayed;
    netmd_replay_transport(&again, &replayed);
    NetmdSession second;
    netmd_session_init(&second, &replayed, 0x054C, 0x0084);
    EXPECT(netmd_play(&second, arena) == NetmdResult_Ok);
    EXPECT(netmd_replay_ok(&again));
    EXPECT(netmd_replay_done(&again));
}

// --- the device thread (T-020 queues, T-021 payload) -------------------------

TEST(netmd_device_read_disc) {
    NetmdReplay replay;
    // The ping the panel sends on open, then the whole disc.
    String8 ping = str8_lit("> c1 01 00 00 00 00 04 00\n"
                            "< 00 81 00 00\n"
                            "> 41 80 00 00 00 00 0d 00 00 18 06 01 10 10 00 ff 00 00 01 00 0b\n"
                            "> c1 01 00 00 00 00 04 00\n"
                            "< 01 81 09 00\n"
                            "> c1 81 00 00 00 00 09 00\n"
                            "< 09 18 06 01 10 10 00 00 0b\n");
    String8 disc = os_file_read_all(arena, str8_lit("tests/netmd/mzn505_full.trace"));
    EXPECT(disc.size != 0);
    netmd_replay_init(&replay, str8f(arena, "%S%S", ping, disc));
    UsbTransport transport;
    netmd_replay_transport(&replay, &transport);

    NetmdDevice device;
    StructZero(&device);
    netmd_device_set_test_transport(&device, &transport, 0x054C, 0x0084);
    netmd_device_start(&device, arena);

    NetmdEvent event;
    EXPECT(netmd_device_post(&device, NetmdCmd_Enumerate, 0));
    EXPECT(netmd_device_wait_event(&device, &event, 2000000));
    EXPECT(event.kind == NetmdEvent_Devices);
    EXPECT(netmd_device_post(&device, NetmdCmd_Open, 0));
    EXPECT(netmd_device_wait_event(&device, &event, 2000000));
    EXPECT(event.kind == NetmdEvent_Opened);
    EXPECT(netmd_device_post(&device, NetmdCmd_Ping, 0));
    EXPECT(netmd_device_wait_event(&device, &event, 2000000));
    EXPECT(event.kind == NetmdEvent_Pong && event.status == NetmdStatus_Accepted);

    // Nothing is published before the read: the UI asking early gets nothing,
    // never a half filled layout.
    EXPECT(netmd_device_disc(&device) == 0);
    EXPECT(netmd_device_post(&device, NetmdCmd_ReadDisc, 0));
    EXPECT(netmd_device_wait_event(&device, &event, 5000000));
    EXPECT(event.kind == NetmdEvent_Disc);
    EXPECT(event.result == NetmdResult_Ok);
    EXPECT(event.track_count == 10);
    EXPECT((event.disc_flags & NetmdDiscFlag_Present) != 0);
    EXPECT(event.elapsed_ms < 2000);

    const DiscLayout *layout = netmd_device_disc(&device);
    EXPECT(layout != 0);
    if (layout) {
        EXPECT(layout->track_count == 10);
        EXPECT(str8_eq(str8((u8 *)layout->title, layout->title_size), str8_lit("Nuit blanche")));
        EXPECT(layout->group_count == 2);
    }

    netmd_device_stop(&device);
    EXPECT(netmd_replay_ok(&replay));
    EXPECT(netmd_replay_done(&replay));
}

static void test_netmd_proto_run_all(void) {
    RUN(netmd_query_literals);
    RUN(netmd_query_formats);
    RUN(netmd_scan_round_trip);
    RUN(netmd_scan_research_examples);
    RUN(netmd_time_and_bcd);
    RUN(netmd_charset_decode);
    RUN(netmd_charset_encode);
    RUN(netmd_group_parsing);
    RUN(netmd_group_edge_cases);
    RUN(netmd_replay_no_disc);
    RUN(netmd_replay_blank_disc);
    RUN(netmd_replay_protected_disc);
    RUN(netmd_replay_full_disc);
    RUN(netmd_control_frames);
    RUN(netmd_status_bytes);
    RUN(netmd_orphan_reply_is_drained);
    RUN(netmd_trace_round_trip);
    RUN(netmd_device_read_disc);
}

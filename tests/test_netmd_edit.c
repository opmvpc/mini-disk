// test_netmd_edit.c - T-022: the simulation, the TOC budget, the backup and the
// six edit transcripts.
//
// The transcripts under tests/netmd/mzn505_edit_*.trace are SYNTHETIC, exactly
// like the read ones of T-021: they are assembled by tools/gen_netmd_traces.py
// from the hex of docs/research/01-netmd-protocol.md, because no edit has been
// performed on the real device yet. They pin down the bytes we send, which is
// what matters here: a wrong oldLen or a wrong wchar corrupts a real TOC, and
// this is the only place that can catch it before a disc does.
//
// tests/netmd/mzn505_real_read.trace is the exception: it was captured from the
// MZ-N505 itself with --netmd-trace once WinUSB was bound, and the last test of
// this file replays it.

// --- a disc to simulate on ----------------------------------------------------

static DiscLayout *test_edit_disc(Arena *arena, u32 track_count) {
    DiscLayout *layout = push_struct_zero(arena, DiscLayout);
    layout->flags = NetmdDiscFlag_Present | NetmdDiscFlag_Writable;
    layout->track_count = track_count;
    layout->ungrouped_count = track_count;
    for (u32 i = 0; i < track_count; i += 1) {
        NetmdTrack *track = &layout->tracks[i];
        track->group = NETMD_NO_GROUP;
        track->encoding = NetmdEncoding_SP;
        track->duration_ms = 180000;
        track->frames = 180 * NETMD_FRAMES_PER_SECOND;
    }
    return layout;
}

static void test_edit_set_text(u8 *dst, u16 *size, u64 cap, String8 text) {
    u64 copied = Min(text.size, cap);
    if (copied != 0) { mem_copy(dst, text.str, copied); }
    *size = (u16)copied;
}

static void test_edit_name_group(DiscLayout *layout, u32 first, u32 count, String8 name) {
    u32 index = layout->group_count;
    NetmdGroup *group = &layout->groups[index];
    group->first = (u16)first;
    group->count = (u16)count;
    test_edit_set_text(group->name, &group->name_size, NETMD_TITLE_MAX, name);
    for (u32 i = first; i < first + count; i += 1) {
        layout->tracks[i].group = (u8)index;
        layout->ungrouped_count -= 1;
    }
    layout->group_count += 1;
}

static NetmdEditRequest *test_edit_request(Arena *arena, u32 kind, u32 track, String8 title) {
    NetmdEditRequest *request = push_struct_zero(arena, NetmdEditRequest);
    request->kind = kind;
    request->track = track;
    test_edit_set_text(request->title, (u16 *)&request->title_size, NETMD_TITLE_MAX, title);
    request->title_size = (u32)Min(title.size, (u64)NETMD_TITLE_MAX);
    return request;
}

// --- Shift-JIS, the write direction (s3.11) -----------------------------------

TEST(netmd_edit_sjis) {
    u8 out[64];
    // s3.9's own example: "Track A" is seven ASCII bytes.
    u64 size = netmd_utf8_to_sjis(str8_lit("Track A"), out, sizeof(out));
    EXPECT(size == 7);
    EXPECT(mem_cmp(out, "Track A", 7) == 0);
    // Sanitizing first is what makes the encoding total: an accent folds, and a
    // full-width letter folds, before a single byte is emitted.
    size = netmd_utf8_to_sjis(str8_lit("Caf\xC3\xA9 \xEF\xBC\xA1"), out, sizeof(out));
    EXPECT(size == 6);
    EXPECT(mem_cmp(out, "Cafe A", 6) == 0);
    // Half-width katakana is one byte each, at 0xA1 + (cp - U+FF61).
    size = netmd_utf8_to_sjis(str8_lit("\xEF\xBD\xB6\xEF\xBE\x9E"), out, sizeof(out));
    EXPECT(size == 2);
    EXPECT(out[0] == 0xB6 && out[1] == 0xDE);
    // What comes back out has to be what went in (the tables are one pair).
    u8 text[64];
    u64 text_size = netmd_sjis_to_utf8(out, size, text, sizeof(text));
    EXPECT(str8_eq(str8(text, text_size), str8_lit("\xEF\xBD\xB6\xEF\xBE\x9E")));
    (void)arena;
}

// --- the simulation, ten edge cases (ADR-011 D4) ------------------------------

TEST(netmd_edit_simulate_rename) {
    DiscLayout *disc = test_edit_disc(arena, 3);
    test_edit_set_text(disc->title, &disc->title_size, NETMD_DISC_TITLE_MAX, str8_lit("Demo"));
    test_edit_set_text(disc->tracks[0].title, &disc->tracks[0].title_size, NETMD_TITLE_MAX,
                       str8_lit("Un"));
    DiscLayout *after = push_struct_zero(arena, DiscLayout);
    DiscDiff *diff = push_struct(arena, DiscDiff);

    NetmdEditRequest *rename =
            test_edit_request(arena, NetmdEditKind_RenameDisc, 0, str8_lit("Concert"));
    netmd_edit_simulate(disc, rename, after, diff);
    EXPECT(diff->allowed);
    EXPECT(diff->changed == 1 && diff->writes == 1);
    EXPECT(str8_eq(str8(diff->before, diff->before_size), str8_lit("Demo")));
    EXPECT(str8_eq(str8(diff->after, diff->after_size), str8_lit("Concert")));
    EXPECT(str8_eq(str8(after->title, after->title_size), str8_lit("Concert")));

    // 1. The same title is not an edit at all (pitfall 10).
    NetmdEditRequest *same =
            test_edit_request(arena, NetmdEditKind_RenameDisc, 0, str8_lit("Demo"));
    netmd_edit_simulate(disc, same, after, diff);
    EXPECT(!diff->allowed && diff->refusal == NetmdEditRefusal_Nothing);

    // The proposed title is sanitized before it is compared, so a title that
    // only differs by an accent is also "the same title" once written.
    test_edit_set_text(disc->title, &disc->title_size, NETMD_DISC_TITLE_MAX, str8_lit("Cafe"));
    NetmdEditRequest *accent =
            test_edit_request(arena, NetmdEditKind_RenameDisc, 0, str8_lit("Caf\xC3\xA9"));
    netmd_edit_simulate(disc, accent, after, diff);
    EXPECT(!diff->allowed && diff->refusal == NetmdEditRefusal_Nothing);

    // 2. A track that does not exist.
    NetmdEditRequest *out_of_range =
            test_edit_request(arena, NetmdEditKind_RenameTrack, 9, str8_lit("X"));
    netmd_edit_simulate(disc, out_of_range, after, diff);
    EXPECT(!diff->allowed && diff->refusal == NetmdEditRefusal_Range);

    // 3. A track rename that goes through.
    NetmdEditRequest *track =
            test_edit_request(arena, NetmdEditKind_RenameTrack, 0, str8_lit("One"));
    netmd_edit_simulate(disc, track, after, diff);
    EXPECT(diff->allowed && diff->writes == 1);
    EXPECT(str8_eq(str8(after->tracks[0].title, after->tracks[0].title_size), str8_lit("One")));
}

TEST(netmd_edit_simulate_refusals) {
    DiscLayout *disc = test_edit_disc(arena, 2);
    DiscLayout *after = push_struct_zero(arena, DiscLayout);
    DiscDiff *diff = push_struct(arena, DiscDiff);
    NetmdEditRequest *rename =
            test_edit_request(arena, NetmdEditKind_RenameDisc, 0, str8_lit("X"));

    // 4. No disc in the bay.
    disc->flags = 0;
    netmd_edit_simulate(disc, rename, after, diff);
    EXPECT(!diff->allowed && diff->refusal == NetmdEditRefusal_NoDisc);

    // 5. The tab at the back of the disc is open (s7.5): nothing gets through.
    disc->flags = NetmdDiscFlag_Present | NetmdDiscFlag_Writable | NetmdDiscFlag_WriteProtected;
    netmd_edit_simulate(disc, rename, after, diff);
    EXPECT(!diff->allowed && diff->refusal == NetmdEditRefusal_Protected);

    // A pre-mastered album is read only for good: no `writable` flag at all.
    disc->flags = NetmdDiscFlag_Present;
    netmd_edit_simulate(disc, rename, after, diff);
    EXPECT(!diff->allowed && diff->refusal == NetmdEditRefusal_Protected);

    // 6. A track checked out by SonicStage refuses eraseTrack (s7.5).
    disc->flags = NetmdDiscFlag_Present | NetmdDiscFlag_Writable;
    disc->tracks[1].protect = 1;
    NetmdEditRequest *erase = test_edit_request(arena, NetmdEditKind_EraseTracks, 0, str8(0, 0));
    netmd_mask_set(erase->mask, 1);
    netmd_edit_simulate(disc, erase, after, diff);
    EXPECT(!diff->allowed && diff->refusal == NetmdEditRefusal_TrackProtected);

    // An empty selection is nothing to do, not an error.
    NetmdEditRequest *nothing =
            test_edit_request(arena, NetmdEditKind_EraseTracks, 0, str8(0, 0));
    netmd_edit_simulate(disc, nothing, after, diff);
    EXPECT(!diff->allowed && diff->refusal == NetmdEditRefusal_Nothing);

    // 7. The budget (s7.4): 254 tracks with a seven character title each is
    // already 254 cells of the 255, so one more character anywhere overflows.
    DiscLayout *full = test_edit_disc(arena, 254);
    for (u32 i = 0; i < full->track_count; i += 1) {
        test_edit_set_text(full->tracks[i].title, &full->tracks[i].title_size, NETMD_TITLE_MAX,
                           str8_lit("1234567"));
    }
    EXPECT(netmd_layout_cells(full, 0, 0, 0) == 254);
    NetmdEditRequest *long_title =
            test_edit_request(arena, NetmdEditKind_RenameDisc, 0,
                              str8_lit("Un titre de disque qui ne rentre pas du tout"));
    netmd_edit_simulate(full, long_title, after, diff);
    EXPECT(!diff->allowed && diff->refusal == NetmdEditRefusal_Budget);
    EXPECT(diff->cells_after > PLAN_TOC_CELLS);

    // The same disc takes a title that fits in the one cell that is left.
    NetmdEditRequest *short_title =
            test_edit_request(arena, NetmdEditKind_RenameDisc, 0, str8_lit("Demo"));
    netmd_edit_simulate(full, short_title, after, diff);
    EXPECT(diff->allowed);
    EXPECT(diff->cells_after == 255 && diff->cells_free_after == 0);
}

TEST(netmd_edit_simulate_groups) {
    DiscLayout *disc = test_edit_disc(arena, 5);
    test_edit_set_text(disc->title, &disc->title_size, NETMD_DISC_TITLE_MAX, str8_lit("Demo"));
    test_edit_name_group(disc, 0, 2, str8_lit("Face A"));
    test_edit_name_group(disc, 2, 2, str8_lit("Face B"));
    EXPECT(disc->group_count == 2 && disc->ungrouped_count == 1);
    DiscLayout *after = push_struct_zero(arena, DiscLayout);
    DiscDiff *diff = push_struct(arena, DiscDiff);

    // 8. The last track of a group is erased: the group goes with it.
    NetmdEditRequest *erase = test_edit_request(arena, NetmdEditKind_EraseTracks, 0, str8(0, 0));
    netmd_mask_set(erase->mask, 2);
    netmd_mask_set(erase->mask, 3);
    netmd_edit_simulate(disc, erase, after, diff);
    EXPECT(diff->allowed);
    EXPECT(diff->changed == 2 && diff->tracks_after == 3);
    EXPECT(diff->groups_before == 2 && diff->groups_after == 1);
    // Two erasures and the one disc title rewrite the renumbering costs (s6.5).
    EXPECT(diff->writes == 3);

    // 9. A track moved across a group boundary joins the group it lands in.
    NetmdEditRequest *move = test_edit_request(arena, NetmdEditKind_MoveTrack, 0, str8(0, 0));
    move->dest = 2;
    netmd_edit_simulate(disc, move, after, diff);
    EXPECT(diff->allowed);
    EXPECT(after->groups[0].count == 1);   // Face A lost one
    EXPECT(after->groups[1].count == 3);   // Face B gained it
    EXPECT(after->tracks[2].group == 1);

    // 10. Grouping a run that is not one, and grouping tracks already grouped.
    NetmdEditRequest *sparse =
            test_edit_request(arena, NetmdEditKind_CreateGroup, 0, str8_lit("Face C"));
    netmd_mask_set(sparse->mask, 4);
    netmd_mask_set(sparse->mask, 0);
    netmd_edit_simulate(disc, sparse, after, diff);
    EXPECT(!diff->allowed && diff->refusal == NetmdEditRefusal_Grouped);

    NetmdEditRequest *taken =
            test_edit_request(arena, NetmdEditKind_CreateGroup, 0, str8_lit("Face C"));
    netmd_mask_set(taken->mask, 0);
    netmd_edit_simulate(disc, taken, after, diff);
    EXPECT(!diff->allowed && diff->refusal == NetmdEditRefusal_Grouped);

    // The last track is free: it becomes a group of its own.
    NetmdEditRequest *create =
            test_edit_request(arena, NetmdEditKind_CreateGroup, 0, str8_lit("Face C"));
    netmd_mask_set(create->mask, 4);
    netmd_edit_simulate(disc, create, after, diff);
    EXPECT(diff->allowed && diff->groups_after == 3);
    EXPECT(str8_eq(str8(after->groups[2].name, after->groups[2].name_size), str8_lit("Face C")));
    EXPECT(str8_eq(str8(diff->after, diff->after_size),
                   str8_lit("0;Demo//1-2;Face A//3-4;Face B//5;Face C//")));

    // Dissolving gives its tracks back to nobody, and the disc title shrinks.
    NetmdEditRequest *dissolve =
            test_edit_request(arena, NetmdEditKind_DissolveGroup, 0, str8(0, 0));
    netmd_edit_simulate(disc, dissolve, after, diff);
    EXPECT(diff->allowed && diff->groups_after == 1 && after->ungrouped_count == 3);
    EXPECT(str8_eq(str8(diff->after, diff->after_size), str8_lit("0;Demo//3-4;Face B//")));

    NetmdEditRequest *no_group =
            test_edit_request(arena, NetmdEditKind_DissolveGroup, 7, str8(0, 0));
    netmd_edit_simulate(disc, no_group, after, diff);
    EXPECT(!diff->allowed && diff->refusal == NetmdEditRefusal_Range);

    // Erasing the whole disc leaves nothing at all behind.
    NetmdEditRequest *erase_all =
            test_edit_request(arena, NetmdEditKind_EraseDisc, 0, str8(0, 0));
    netmd_edit_simulate(disc, erase_all, after, diff);
    EXPECT(diff->allowed && diff->changed == 5);
    EXPECT(after->track_count == 0 && after->group_count == 0 && after->title_size == 0);
    EXPECT(diff->cells_after == 0);
}

TEST(netmd_edit_cells) {
    // The unit is the cell, not the character (pitfall 33): eight characters
    // cost as much as fourteen.
    DiscLayout *disc = test_edit_disc(arena, 2);
    test_edit_set_text(disc->tracks[0].title, &disc->tracks[0].title_size, NETMD_TITLE_MAX,
                       str8_lit("12345678"));
    EXPECT(netmd_layout_cells(disc, 0, 0, 0) == 2);
    test_edit_set_text(disc->tracks[0].title, &disc->tracks[0].title_size, NETMD_TITLE_MAX,
                       str8_lit("12345678901234"));
    EXPECT(netmd_layout_cells(disc, 0, 0, 0) == 2);
    // Pitfall 34: an LP track costs a cell even with no title.
    disc->tracks[1].encoding = NetmdEncoding_LP2;
    EXPECT(netmd_layout_cells(disc, 0, 0, 0) == 3);
    // The group syntax is in the disc title and is counted with it (D3).
    test_edit_set_text(disc->title, &disc->title_size, NETMD_DISC_TITLE_MAX, str8_lit("Demo"));
    test_edit_name_group(disc, 0, 2, str8_lit("A"));
    u8 raw[PLAN_TOC_RAW_MAX];
    u64 raw_size = 0;
    u32 cells = netmd_layout_cells(disc, raw, sizeof(raw), &raw_size);
    EXPECT(str8_eq(str8(raw, raw_size), str8_lit("0;Demo//1-2;A//")));
    EXPECT(cells == 3 + plan_toc_cells_for_chars(15));
}

// --- the TOC backup (D4) --------------------------------------------------------

TEST(netmd_backup_round_trip) {
    DiscLayout *disc = test_edit_disc(arena, 3);
    test_edit_set_text(disc->title, &disc->title_size, NETMD_DISC_TITLE_MAX,
                       str8_lit("Nuit blanche"));
    test_edit_set_text(disc->tracks[0].title, &disc->tracks[0].title_size, NETMD_TITLE_MAX,
                       str8_lit("Ouverture"));
    test_edit_set_text(disc->tracks[1].title, &disc->tracks[1].title_size, NETMD_TITLE_MAX,
                       str8_lit("Le train de 7h"));
    disc->tracks[1].encoding = NetmdEncoding_LP2;
    disc->tracks[1].mono = 1;
    disc->tracks[2].protect = 1;
    disc->capacity.total = netmd_time_from_frames(4800u * NETMD_FRAMES_PER_SECOND);
    disc->capacity.recorded = netmd_time_from_frames(540u * NETMD_FRAMES_PER_SECOND);
    test_edit_name_group(disc, 0, 2, str8_lit("Face A"));

    String8 text = netmd_backup_serialize(arena, disc);
    EXPECT(str8_starts_with(text, str8_lit("minidisk-toc 1")));
    DiscLayout *back = push_struct(arena, DiscLayout);
    EXPECT(netmd_backup_parse(text, back));
    EXPECT(back->track_count == disc->track_count);
    EXPECT(back->group_count == 1 && back->ungrouped_count == 1);
    EXPECT(str8_eq(str8(back->title, back->title_size), str8_lit("Nuit blanche")));
    EXPECT(str8_eq(str8(back->groups[0].name, back->groups[0].name_size), str8_lit("Face A")));
    EXPECT(str8_eq(str8(back->tracks[1].title, back->tracks[1].title_size),
                   str8_lit("Le train de 7h")));
    EXPECT(back->tracks[1].encoding == NetmdEncoding_LP2 && back->tracks[1].mono == 1);
    EXPECT(back->tracks[2].protect == 1);
    EXPECT(back->tracks[0].group == 0 && back->tracks[2].group == NETMD_NO_GROUP);
    EXPECT(back->capacity.total.total_frames == disc->capacity.total.total_frames);
    EXPECT(netmd_disc_id(back) == netmd_disc_id(disc));

    // A file that is not ours is not read as one.
    DiscLayout *rejected = push_struct(arena, DiscLayout);
    EXPECT(!netmd_backup_parse(str8_lit("something else entirely\n"), rejected));

    // And through the file system, the way an edit really takes it.
    String8 dir = os_path_join(arena, os_known_folder(arena, OsKnownFolder_Temp),
                               str8_lit("minidisk-test-toc"));
    String8 path = str8(0, 0);
    EXPECT(netmd_backup_write(arena, dir, disc, &path));
    EXPECT(str8_ends_with(path, str8_lit(".txt")));
    String8 written = os_file_read_all(arena, path);
    EXPECT(written.size == text.size);
    DiscLayout *loaded = push_struct(arena, DiscLayout);
    EXPECT(netmd_backup_parse(written, loaded));
    EXPECT(loaded->track_count == disc->track_count);
    os_file_delete(path);
    os_dir_delete(dir);
}

// --- the transcripts ------------------------------------------------------------

typedef struct TestEditSession {
    NetmdReplay replay;
    UsbTransport transport;
    NetmdSession session;
    DiscLayout *layout;
} TestEditSession;

// Reads the disc the way the device thread does, so what follows simulates on
// the same layout the application would have.
static TestEditSession *test_edit_open(Arena *arena, String8 path) {
    TestEditSession *test = push_struct_zero(arena, TestEditSession);
    String8 text = os_file_read_all(arena, path);
    if (text.size == 0) { return 0; }
    netmd_replay_init(&test->replay, text);
    netmd_replay_transport(&test->replay, &test->transport);
    netmd_session_init(&test->session, &test->transport, 0x054C, 0x0084);
    test->layout = push_struct_zero(arena, DiscLayout);
    if (netmd_read_disc(&test->session, arena, test->layout) != NetmdResult_Ok) { return 0; }
    return test;
}

static u32 test_edit_apply(Arena *arena, TestEditSession *test, const NetmdEditRequest *request,
                           DiscDiff *diff, u32 *writes) {
    DiscLayout *after = push_struct_zero(arena, DiscLayout);
    netmd_edit_simulate(test->layout, request, after, diff);
    if (!diff->allowed) { return NetmdResult_Rejected; }
    return netmd_edit_apply(&test->session, arena, after, request, writes);
}

TEST(netmd_edit_replay_rename_disc) {
    TestEditSession *test =
            test_edit_open(arena, str8_lit("tests/netmd/mzn505_edit_rename_disc.trace"));
    EXPECT(test != 0);
    if (!test) { return; }
    EXPECT(str8_eq(str8(test->layout->title, test->layout->title_size), str8_lit("Demo")));
    NetmdEditRequest *request =
            test_edit_request(arena, NetmdEditKind_RenameDisc, 0, str8_lit("Concert"));
    DiscDiff *diff = push_struct(arena, DiscDiff);
    u32 writes = 0;
    EXPECT(test_edit_apply(arena, test, request, diff, &writes) == NetmdResult_Ok);
    EXPECT(writes == 1);
    EXPECT(netmd_replay_ok(&test->replay));
    EXPECT(netmd_replay_done(&test->replay));
}

TEST(netmd_edit_replay_rename_track) {
    TestEditSession *test =
            test_edit_open(arena, str8_lit("tests/netmd/mzn505_edit_rename_track.trace"));
    EXPECT(test != 0);
    if (!test) { return; }
    NetmdEditRequest *request =
            test_edit_request(arena, NetmdEditKind_RenameTrack, 1, str8_lit("Two"));
    DiscDiff *diff = push_struct(arena, DiscDiff);
    u32 writes = 0;
    EXPECT(test_edit_apply(arena, test, request, diff, &writes) == NetmdResult_Ok);
    EXPECT(writes == 1);
    EXPECT(netmd_replay_ok(&test->replay));
    EXPECT(netmd_replay_done(&test->replay));
}

TEST(netmd_edit_replay_oldlen) {
    // s3.9: the current title is two full-width characters, so oldLen is 4 - the
    // Shift-JIS byte count, not the character count. The transcript holds the
    // frame with the 4 in it; anything else diverges here instead of on a disc.
    TestEditSession *test =
            test_edit_open(arena, str8_lit("tests/netmd/mzn505_edit_oldlen.trace"));
    EXPECT(test != 0);
    if (!test) { return; }
    NetmdEditRequest *request =
            test_edit_request(arena, NetmdEditKind_RenameTrack, 0, str8_lit("AB"));
    DiscDiff *diff = push_struct(arena, DiscDiff);
    u32 writes = 0;
    EXPECT(test_edit_apply(arena, test, request, diff, &writes) == NetmdResult_Ok);
    EXPECT(writes == 1);
    EXPECT(netmd_replay_ok(&test->replay));
    EXPECT(netmd_replay_done(&test->replay));
}

TEST(netmd_edit_replay_same_title) {
    // Pitfall 10: the title is read back, found identical, and nothing is
    // written. The transcript has no write in it, so a write would diverge.
    TestEditSession *test =
            test_edit_open(arena, str8_lit("tests/netmd/mzn505_edit_same_title.trace"));
    EXPECT(test != 0);
    if (!test) { return; }
    b32 written = 1;
    EXPECT(netmd_set_track_title(&test->session, arena, 1, str8_lit("Deux"), &written) ==
           NetmdResult_Ok);
    EXPECT(!written);
    EXPECT(netmd_replay_ok(&test->replay));
    EXPECT(netmd_replay_done(&test->replay));

    // And the simulation refuses it before the device is even spoken to.
    NetmdEditRequest *request =
            test_edit_request(arena, NetmdEditKind_RenameTrack, 1, str8_lit("Deux"));
    DiscDiff *diff = push_struct(arena, DiscDiff);
    netmd_edit_simulate(test->layout, request, 0, diff);
    netmd_edit_simulate(test->layout, request, push_struct_zero(arena, DiscLayout), diff);
    EXPECT(!diff->allowed && diff->refusal == NetmdEditRefusal_Nothing);
}

TEST(netmd_edit_replay_move) {
    TestEditSession *test = test_edit_open(arena, str8_lit("tests/netmd/mzn505_edit_move.trace"));
    EXPECT(test != 0);
    if (!test) { return; }
    NetmdEditRequest *request = test_edit_request(arena, NetmdEditKind_MoveTrack, 0, str8(0, 0));
    request->dest = 2;
    DiscDiff *diff = push_struct(arena, DiscDiff);
    u32 writes = 0;
    EXPECT(test_edit_apply(arena, test, request, diff, &writes) == NetmdResult_Ok);
    // The move, then the one disc title rewrite the ranges cost (s3.10, s6.5).
    EXPECT(writes == 2);
    EXPECT(netmd_replay_ok(&test->replay));
    EXPECT(netmd_replay_done(&test->replay));
}

TEST(netmd_edit_replay_erase) {
    TestEditSession *test =
            test_edit_open(arena, str8_lit("tests/netmd/mzn505_edit_erase.trace"));
    EXPECT(test != 0);
    if (!test) { return; }
    NetmdEditRequest *request = test_edit_request(arena, NetmdEditKind_EraseTracks, 0, str8(0, 0));
    netmd_mask_set(request->mask, 1);
    DiscDiff *diff = push_struct(arena, DiscDiff);
    u32 writes = 0;
    EXPECT(test_edit_apply(arena, test, request, diff, &writes) == NetmdResult_Ok);
    EXPECT(writes == 2);
    EXPECT(netmd_replay_ok(&test->replay));
    EXPECT(netmd_replay_done(&test->replay));
}

TEST(netmd_edit_replay_group) {
    TestEditSession *test =
            test_edit_open(arena, str8_lit("tests/netmd/mzn505_edit_group.trace"));
    EXPECT(test != 0);
    if (!test) { return; }
    NetmdEditRequest *request =
            test_edit_request(arena, NetmdEditKind_CreateGroup, 0, str8_lit("Face B"));
    netmd_mask_set(request->mask, 2);
    DiscDiff *diff = push_struct(arena, DiscDiff);
    u32 writes = 0;
    EXPECT(test_edit_apply(arena, test, request, diff, &writes) == NetmdResult_Ok);
    EXPECT(writes == 1);
    EXPECT(netmd_replay_ok(&test->replay));
    EXPECT(netmd_replay_done(&test->replay));
}

TEST(netmd_edit_replay_erase_disc) {
    TestEditSession *test =
            test_edit_open(arena, str8_lit("tests/netmd/mzn505_edit_erase_disc.trace"));
    EXPECT(test != 0);
    if (!test) { return; }
    NetmdEditRequest *request = test_edit_request(arena, NetmdEditKind_EraseDisc, 0, str8(0, 0));
    DiscDiff *diff = push_struct(arena, DiscDiff);
    u32 writes = 0;
    EXPECT(test_edit_apply(arena, test, request, diff, &writes) == NetmdResult_Ok);
    EXPECT(writes == 1);
    EXPECT(netmd_replay_ok(&test->replay));
    EXPECT(netmd_replay_done(&test->replay));
}

// --- the device thread's edit command (the whole shape of it) --------------------

TEST(netmd_edit_device_session) {
    NetmdReplay replay;
    String8 ping = str8_lit("> c1 01 00 00 00 00 04 00\n"
                            "< 00 81 00 00\n"
                            "> 41 80 00 00 00 00 0d 00 00 18 06 01 10 10 00 ff 00 00 01 00 0b\n"
                            "> c1 01 00 00 00 00 04 00\n"
                            "< 01 81 09 00\n"
                            "> c1 81 00 00 00 00 09 00\n"
                            "< 09 18 06 01 10 10 00 00 0b\n");
    String8 session = os_file_read_all(arena, str8_lit("tests/netmd/mzn505_edit_session.trace"));
    EXPECT(session.size != 0);
    netmd_replay_init(&replay, str8f(arena, "%S%S", ping, session));
    UsbTransport transport;
    netmd_replay_transport(&replay, &transport);

    NetmdDevice device;
    StructZero(&device);
    netmd_device_set_test_transport(&device, &transport, 0x054C, 0x0084);
    netmd_device_start(&device, arena);

    NetmdEvent event;
    EXPECT(netmd_device_post(&device, NetmdCmd_Enumerate, 0));
    EXPECT(netmd_device_wait_event(&device, &event, 2000000));
    EXPECT(netmd_device_post(&device, NetmdCmd_Open, 0));
    EXPECT(netmd_device_wait_event(&device, &event, 2000000));
    EXPECT(netmd_device_post(&device, NetmdCmd_Ping, 0));
    EXPECT(netmd_device_wait_event(&device, &event, 2000000));
    EXPECT(netmd_device_post(&device, NetmdCmd_ReadDisc, 0));
    EXPECT(netmd_device_wait_event(&device, &event, 5000000));
    EXPECT(event.kind == NetmdEvent_Disc && event.track_count == 3);
    EXPECT(!netmd_device_toc_dirty(&device));

    NetmdEditRequest request;
    StructZero(&request);
    request.kind = NetmdEditKind_RenameTrack;
    request.track = 1;
    mem_copy(request.title, "Two", 3);
    request.title_size = 3;
    EXPECT(netmd_device_post_edit(&device, &request));

    // The edit answers, the disc is read back, and the flag goes down only when
    // the device has handed its own description over (s6.3).
    EXPECT(netmd_device_wait_event(&device, &event, 10000000));
    EXPECT(event.kind == NetmdEvent_Edit);
    EXPECT(event.refusal == NetmdEditRefusal_None);
    EXPECT(event.result == NetmdResult_Ok && event.writes == 1);
    EXPECT(event.toc_dirty == 1);
    EXPECT(netmd_device_wait_event(&device, &event, 10000000));
    EXPECT(event.kind == NetmdEvent_Disc && event.result == NetmdResult_Ok);
    EXPECT(netmd_device_wait_event(&device, &event, 10000000));
    EXPECT(event.kind == NetmdEvent_Edit && event.toc_dirty == 0);
    EXPECT(!netmd_device_toc_dirty(&device));

    const DiscLayout *layout = netmd_device_disc(&device);
    EXPECT(layout != 0);
    if (layout) {
        EXPECT(str8_eq(str8((u8 *)layout->tracks[1].title, layout->tracks[1].title_size),
                       str8_lit("Two")));
    }

    // An edit the simulation refuses never reaches the device, so the replay
    // stays exactly where the transcript ended.
    NetmdEditRequest same;
    StructZero(&same);
    same.kind = NetmdEditKind_RenameTrack;
    same.track = 1;
    mem_copy(same.title, "Two", 3);
    same.title_size = 3;
    EXPECT(netmd_device_post_edit(&device, &same));
    EXPECT(netmd_device_wait_event(&device, &event, 5000000));
    EXPECT(event.kind == NetmdEvent_Edit && event.refusal == NetmdEditRefusal_Nothing);

    netmd_device_stop(&device);
    EXPECT(netmd_replay_ok(&replay));
    EXPECT(netmd_replay_done(&replay));
}

// --- the real device (the one capture that is not synthetic) --------------------

TEST(netmd_edit_real_capture) {
    // tests/netmd/mzn505_real_read.trace was captured with --netmd-trace from
    // the MZ-N505 itself once WinUSB was bound: 88 commands, an 8 track disc
    // whose raw title is "0;202001//1-8;//". Everything the reader and the group
    // parser do is exercised here against bytes no generator invented - and the
    // budget of that real disc is what an edit of it would be measured against.
    String8 text = os_file_read_all(arena, str8_lit("tests/netmd/mzn505_real_read.trace"));
    EXPECT(text.size != 0);
    if (text.size == 0) { return; }
    NetmdReplay replay;
    netmd_replay_init(&replay, text);
    UsbTransport transport;
    netmd_replay_transport(&replay, &transport);
    NetmdSession session;
    netmd_session_init(&session, &transport, 0x054C, 0x0084);

    // The ping the panel sends on open, then the disc.
    String8 reply;
    EXPECT(netmd_command(&session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                         "00 1806 01101000 ff00 0001000b") == NetmdResult_Ok);
    DiscLayout *disc = push_struct_zero(arena, DiscLayout);
    EXPECT(netmd_read_disc(&session, arena, disc) == NetmdResult_Ok);
    EXPECT(netmd_replay_ok(&replay));
    EXPECT((disc->flags & NetmdDiscFlag_Present) != 0);
    EXPECT((disc->flags & NetmdDiscFlag_Writable) != 0);
    EXPECT((disc->flags & NetmdDiscFlag_WriteProtected) == 0);
    EXPECT(disc->track_count == 8);
    EXPECT(str8_eq(str8(disc->title, disc->title_size), str8_lit("202001")));
    EXPECT(disc->group_count == 1 && disc->groups[0].count == 8);

    // An edit of that disc simulates without touching anything.
    NetmdEditRequest *request =
            test_edit_request(arena, NetmdEditKind_RenameTrack, 0, str8_lit("Essai"));
    DiscDiff *diff = push_struct(arena, DiscDiff);
    netmd_edit_simulate(disc, request, push_struct_zero(arena, DiscLayout), diff);
    EXPECT(diff->allowed && diff->writes == 1);
    EXPECT(diff->cells_after <= PLAN_TOC_CELLS);
}

static void test_netmd_edit_run_all(void) {
    RUN(netmd_edit_sjis);
    RUN(netmd_edit_simulate_rename);
    RUN(netmd_edit_simulate_refusals);
    RUN(netmd_edit_simulate_groups);
    RUN(netmd_edit_cells);
    RUN(netmd_backup_round_trip);
    RUN(netmd_edit_replay_rename_disc);
    RUN(netmd_edit_replay_rename_track);
    RUN(netmd_edit_replay_oldlen);
    RUN(netmd_edit_replay_same_title);
    RUN(netmd_edit_replay_move);
    RUN(netmd_edit_replay_erase);
    RUN(netmd_edit_replay_group);
    RUN(netmd_edit_replay_erase_disc);
    RUN(netmd_edit_device_session);
    RUN(netmd_edit_real_capture);
}

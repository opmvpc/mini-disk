// test_capacity.c - T-031: the cluster arithmetic against the numeric limits of
// research/01 s7.6, the 255 x 7 title budget, the sanitizer and the ordered
// shortening, and the multi disc first fit.

typedef struct TestCap {
    Plan *plan;
    Arena *arena;
    Arena *text;
} TestCap;

static void test_cap_open(TestCap *tc, Arena *host) {
    tc->arena = arena_alloc(MB(16));
    tc->text = arena_alloc(MB(16));
    tc->plan = push_struct(host, Plan);
    plan_init(tc->plan, tc->arena, tc->text);
}

static void test_cap_close(TestCap *tc) {
    arena_release(tc->text);
    arena_release(tc->arena);
}

// One entry of `duration_ms` in `mode`, appended at the end of disc 0.
static void test_cap_add(Plan *plan, u32 duration_ms, u32 mode, b32 mono) {
    PlanDisc *disc = plan_disc(plan, 0);
    PlanEntry entry;
    StructZero(&entry);
    entry.track_id = LIB_TRACK_NONE;
    entry.path_id = lib_intern(&plan->strings, str8_lit("C:/x.flac"));
    entry.title_override = LIB_STRING_NONE;
    entry.duration_ms = duration_ms;
    entry.gain_db = PLAN_GAIN_NONE;
    entry.mode = (u8)mode;
    entry.flags = mono ? PlanEntryFlag_Mono : 0;
    entry.group_id = PLAN_GROUP_NONE;
    AssertAlways(plan_add(plan, 0, disc->entry_count, entry));
}

// T-045: the one thing in this file that was measured rather than derived.
// Seven SP uploads to a real MZ-N505 on 2026-09-07, one session each, the
// device's own free time read before and after every one of them; three of
// them are below with the cost the device actually charged.
//
//   duration   free before    free after     device cost   this module
//      5 s     3 383 134 ms   3 375 105 ms     8 029 ms     4 x 2 000 =  8 000
//     25 s     3 375 105 ms   3 347 003 ms    28 102 ms    14 x 2 000 = 28 000
//     61 s     3 347 003 ms   3 283 113 ms    63 890 ms    32 x 2 000 = 64 000
//
// The device charges in whole 2 000 ms clusters, so a measurement can only ever
// pin the answer to within one step: each of the three is inside its own step,
// and none of them is without the link cluster (which would give 6 000, 26 000
// and 62 000 - a whole cluster short, every time).
TEST(capacity_track_overhead_matches_the_device) {
    EXPECT(PLAN_TRACK_OVERHEAD_CLUSTERS == 1);
    EXPECT(plan_clusters_for(5000, PlanCapMode_SP) == 4);
    EXPECT(plan_clusters_for(25000, PlanCapMode_SP) == 14);
    EXPECT(plan_clusters_for(61000, PlanCapMode_SP) == 32);
    EXPECT(plan_clusters_for(5000, PlanCapMode_SP) * PLAN_CLUSTER_SP_MS == 8000);
    EXPECT(plan_clusters_for(25000, PlanCapMode_SP) * PLAN_CLUSTER_SP_MS == 28000);
    EXPECT(plan_clusters_for(61000, PlanCapMode_SP) * PLAN_CLUSTER_SP_MS == 64000);
    // Within the 2 000 ms step of the measurement, on both sides.
    EXPECT(8000u + 2000u > 8029u && 8000u < 8029u + 2000u);
    EXPECT(28000u + 2000u > 28102u && 28000u < 28102u + 2000u);
    EXPECT(64000u + 2000u > 63890u && 64000u < 63890u + 2000u);
    // The three of them in one plan, as they were written in one session:
    // 100 022 ms measured, 100 000 ms here.
    EXPECT((plan_clusters_for(5000, PlanCapMode_SP) + plan_clusters_for(25000, PlanCapMode_SP) +
            plan_clusters_for(61000, PlanCapMode_SP)) *
               PLAN_CLUSTER_SP_MS ==
           100000);
    (void)arena;
}

// The table of cases the ticket asks for: what the numbers of research/01 s7.6
// say must happen on an 80 minute disc.
TEST(capacity_table) {
    TestCap tc;
    test_cap_open(&tc, arena);
    Plan *plan = tc.plan;
    PlanDisc *disc = plan_disc(plan, 0);
    PlanCapacity *cap = push_struct(arena, PlanCapacity);

    // 80 min = 2400 clusters of 2 s; 74 and 60 in proportion.
    EXPECT(plan_clusters_capacity(80) == 2400);
    EXPECT(plan_clusters_capacity(74) == 2220);
    EXPECT(plan_clusters_capacity(60) == 1800);

    // The rounding itself: 3 s of LP4 costs the cluster of 8 s (research/02 s3.1),
    // plus the link cluster every track costs (T-045).
    EXPECT(PLAN_TRACK_OVERHEAD_CLUSTERS == 1);
    EXPECT(plan_clusters_for(3000, PlanCapMode_LP4) == 1 + 1);
    // The padding is the wasted *audio* and nothing else: the link cluster is a
    // cluster of the disc, not five seconds of silence to hatch.
    EXPECT(plan_padding_ms(3000, PlanCapMode_LP4) == 5000);
    EXPECT(plan_clusters_for(1, PlanCapMode_SP) == 2);      // never below 1 + overhead
    EXPECT(plan_clusters_for(2000, PlanCapMode_SP) == 2);   // exactly one audio cluster
    EXPECT(plan_clusters_for(2001, PlanCapMode_SP) == 3);   // one millisecond over
    EXPECT(plan_padding_ms(2000, PlanCapMode_SP) == 0);

    // 40 tracks of 2:00,001 in SP: 120:01 rounds to 61 audio clusters plus the
    // link cluster = 62 each, 2480 in all, so they do NOT fit on an 80 minute
    // disc - and neither would 40 x 2:00, which the old arithmetic let through.
    for (u32 i = 0; i < 40; i += 1) { test_cap_add(plan, 120001, PlanMode_SP, 0); }
    plan_capacity_compute(disc, cap);
    EXPECT(cap->clusters[0] == 62);
    EXPECT(cap->used_clusters == 2480);
    EXPECT(cap->overflow_clusters == 80);
    EXPECT(cap->free_clusters == 0);
    EXPECT(cap->audio_ms == 40ull * 120001ull);
    EXPECT(cap->billed_ms == 40ull * 124000ull);
    // padding_ms is the wasted audio alone; the 40 link clusters are billed but
    // are not padding, so billed - audio is padding + 40 x 2 000 ms.
    EXPECT(cap->padding_ms == 40ull * 1999ull);
    EXPECT(cap->billed_ms - cap->audio_ms == cap->padding_ms + 40ull * 2000ull);
    // The track that straddles the end of the disc moved down two places: 38 x
    // 62 = 2356 fit whole, the 39th (index 38) crosses 2400.
    EXPECT(cap->fit[37] == PlanFit_Fits);
    EXPECT(cap->fit[38] == PlanFit_Partial);
    EXPECT(cap->fit[39] == PlanFit_Overflow);
    EXPECT(cap->first_overflow == 38);

    // The headline case of D2: 79:55,3 of audio, 84:16 of disc. 79 tracks of
    // 1:00,7 sum to 79:55,3 linearly but each one costs 31 audio clusters plus
    // its link cluster = 32.
    plan_clear(plan);
    disc = plan_disc(plan, 0);
    for (u32 i = 0; i < 79; i += 1) { test_cap_add(plan, 60700, PlanMode_SP, 0); }
    plan_capacity_compute(disc, cap);
    EXPECT(cap->audio_ms == 4795300);          // 79:55,3 - it fits, says the sum
    EXPECT(cap->audio_ms < 80ull * 60000ull);
    EXPECT(cap->used_clusters == 79 * 32);     // 2528 clusters = 84:16 of disc
    EXPECT(cap->used_clusters > cap->capacity_clusters);
    EXPECT(cap->overflow_clusters == 128);

    // LP4 carries four times the audio of SP for the same clusters, mono twice.
    plan_clear(plan);
    disc = plan_disc(plan, 0);
    // Each one plus its link cluster, which is one *disc* cluster in every mode.
    test_cap_add(plan, 300000, PlanMode_SP, 0);   // 5:00 SP  = 150 + 1 clusters
    test_cap_add(plan, 300000, PlanMode_LP2, 0);  // 5:00 LP2 =  75 + 1
    test_cap_add(plan, 300000, PlanMode_LP4, 0);  // 5:00 LP4 =  38 + 1 (ceil 37,5)
    test_cap_add(plan, 300000, PlanMode_SP, 1);   // 5:00 mono=  75 + 1
    plan_capacity_compute(disc, cap);
    EXPECT(cap->clusters[0] == 151);
    EXPECT(cap->clusters[1] == 76);
    EXPECT(cap->clusters[2] == 39);
    EXPECT(cap->clusters[3] == 76);
    EXPECT(cap->used_clusters == 342);
    EXPECT(cap->fit[0] == PlanFit_Fits && cap->fit[3] == PlanFit_Fits);

    // What would still fit, in each mode: the same clusters, four answers.
    u32 free_clusters = 2400 - 342;  // 2058
    EXPECT(cap->free_clusters == free_clusters);
    EXPECT(cap->remaining_ms[PlanCapMode_SP] == free_clusters * 2000);
    EXPECT(cap->remaining_ms[PlanCapMode_Mono] == free_clusters * 4000);
    EXPECT(cap->remaining_ms[PlanCapMode_LP2] == free_clusters * 4000);
    EXPECT(cap->remaining_ms[PlanCapMode_LP4] == free_clusters * 8000);
    EXPECT(cap->remaining_ms[PlanCapMode_LP4] == 4 * cap->remaining_ms[PlanCapMode_SP]);
    EXPECT(cap->remaining_entries == PLAN_ENTRY_MAX - 4);
    EXPECT(plan_capacity_would_fit(cap, 3600000, PlanCapMode_LP4));
    EXPECT(!plan_capacity_would_fit(cap, 4200000, PlanCapMode_SP));

    // An empty plan: a full disc of free space and nothing pending.
    plan_clear(plan);
    plan_capacity_compute(plan_disc(plan, 0), cap);
    EXPECT(cap->used_clusters == 0 && cap->free_clusters == 2400);
    EXPECT(cap->first_overflow == 0 && cap->entry_count == 0);
    test_cap_close(&tc);
}

// A disc title with three groups plus twenty LP2 tracks: the exact cells.
TEST(capacity_toc_cells) {
    TestCap tc;
    test_cap_open(&tc, arena);
    Plan *plan = tc.plan;
    PlanTocBudget *budget = push_struct(arena, PlanTocBudget);

    // ceil(n / 7), the only arithmetic there is.
    EXPECT(plan_toc_cells_for_chars(0) == 0);
    EXPECT(plan_toc_cells_for_chars(1) == 1);
    EXPECT(plan_toc_cells_for_chars(7) == 1);
    EXPECT(plan_toc_cells_for_chars(8) == 2);
    EXPECT(PLAN_TOC_CELLS * PLAN_TOC_CELL_CHARS == 1785);

    // An untitled LP2 track still costs one cell: the device writes "LP: ".
    EXPECT(plan_toc_cells_for_title(str8_lit(""), 1) == 1);
    EXPECT(plan_toc_cells_for_title(str8_lit(""), 0) == 0);
    EXPECT(plan_toc_cells_for_title(str8_lit("Seven..."), 0) == 2);

    for (u32 i = 0; i < 20; i += 1) { test_cap_add(plan, 200000, PlanMode_LP2, 0); }
    // "Track 01".."Track 20": 8 characters each, 2 cells each, 40 in all.
    for (u32 i = 0; i < 20; i += 1) {
        String8 title = str8f(arena, "Track %02u", i + 1);
        AssertAlways(plan_set_title(plan, 0, i, title, 0));
        plan_coalesce_break(plan);
    }
    AssertAlways(plan_set_disc_title(plan, 0, str8_lit("Mon Album"), 0));
    AssertAlways(plan_group(plan, 0, 0, 4, str8_lit("Face A")));
    AssertAlways(plan_group(plan, 0, 4, 5, str8_lit("Face B")));
    AssertAlways(plan_group(plan, 0, 9, 11, str8_lit("Face C")));

    plan_toc_budget(plan, 0, 0, budget);
    EXPECT(budget->cells_tracks == 40);
    // "0;Mon Album//1-4;Face A//5-9;Face B//10-20;Face C//" = 51 characters,
    // ceil(51 / 7) = 8 cells.
    EXPECT(budget->raw_size == 51);
    EXPECT(str8_eq(str8(budget->raw, budget->raw_size),
                   str8_lit("0;Mon Album//1-4;Face A//5-9;Face B//10-20;Face C//")));
    EXPECT(budget->cells_disc == 8);
    EXPECT(budget->cells_used == 48);
    EXPECT(budget->groups_kept == 3 && budget->groups_total == 3);
    EXPECT(budget->cells_free == 255 - 48);
    EXPECT(budget->chars_free == (255 - 48) * 7);
    EXPECT(!budget->overflow);
    EXPECT(budget->cells[0] == 2);

    // A budget too small for every group keeps the ones that fit and, when no
    // group survives, falls back to the bare disc title (research/01 s7.4).
    u32 kept = 0;
    u8 raw[PLAN_TOC_RAW_MAX];
    u64 size = plan_toc_compile_disc_title(plan, plan_disc(plan, 0), 5, raw, sizeof(raw), &kept);
    // 5 cells = 35 characters: the disc title and one group, the other two dropped.
    EXPECT(kept == 1 && size == 25);
    EXPECT(str8_eq(str8(raw, size), str8_lit("0;Mon Album//1-4;Face A//")));
    size = plan_toc_compile_disc_title(plan, plan_disc(plan, 0), 2, raw, sizeof(raw), &kept);
    EXPECT(kept == 0 && str8_eq(str8(raw, size), str8_lit("Mon Album")));
    size = plan_toc_compile_disc_title(plan, plan_disc(plan, 0), 0, raw, sizeof(raw), &kept);
    EXPECT(kept == 0 && size == 0);
    test_cap_close(&tc);
}

// The sanitizer: what the TOC can hold, and nothing else.
TEST(capacity_sanitize) {
    u8 out[PLAN_TITLE_MAX];
    u32 chars = 0;
    u64 size;

    size = plan_toc_sanitize(str8_lit("Bj\xc3\xb6rk - Jo\xcc\x81ga"), out, sizeof(out), &chars);
    // The combining acute of the decomposed "o" is dropped, the o stays.
    EXPECT(str8_eq(str8(out, size), str8_lit("Bjork - Joga")));
    EXPECT(chars == 12);

    size = plan_toc_sanitize(str8_lit("\xc3\xa9t\xc3\xa9 \xc3\xa0 Ca\xc3\xb1"), out, sizeof(out), &chars);
    EXPECT(str8_eq(str8(out, size), str8_lit("ete a Can")));

    // Katakana folds to half-width; a voiced kana becomes two characters, which
    // is exactly why it costs two of the budget.
    size = plan_toc_sanitize(str8_lit("\xe3\x82\xab"), out, sizeof(out), &chars);  // U+30AB KA
    EXPECT(size == 3 && chars == 1);
    EXPECT(out[0] == 0xEF && out[1] == 0xBD && out[2] == 0xB6);  // U+FF76
    size = plan_toc_sanitize(str8_lit("\xe3\x82\xac"), out, sizeof(out), &chars);  // U+30AC GA
    EXPECT(size == 6 && chars == 2);
    EXPECT(plan_toc_cells_for_title(str8_lit("\xe3\x82\xac"), 0) == 1);
    // Hiragana goes through katakana.
    size = plan_toc_sanitize(str8_lit("\xe3\x81\x82"), out, sizeof(out), &chars);  // U+3042 A
    EXPECT(size == 3 && chars == 1 && out[2] == 0xB1);

    // Full width ASCII folds down, the emoji is dropped, the rest survives.
    size = plan_toc_sanitize(str8_lit("\xef\xbc\xa1\xef\xbc\xa2 \xf0\x9f\x8e\xb5 OK"), out,
                             sizeof(out), &chars);
    EXPECT(str8_eq(str8(out, size), str8_lit("AB  OK")));
    EXPECT(chars == 6);
    size = plan_toc_sanitize(str8_lit("\xf0\x9f\x8e\xb5\xf0\x9f\x92\xbf"), out, sizeof(out), &chars);
    EXPECT(size == 0 && chars == 0);

    // Typography: curly quotes, en dash, ellipsis.
    size = plan_toc_sanitize(str8_lit("\xe2\x80\x9c" "A\xe2\x80\x9d \xe2\x80\x93 B\xe2\x80\xa6"),
                             out, sizeof(out), &chars);
    EXPECT(str8_eq(str8(out, size), str8_lit("\"A\" - B...")));
    (void)arena;
}

// The shortening: ordered, and only on overflow.
TEST(capacity_shorten) {
    PlanTitlePreview *preview = push_struct(arena, PlanTitlePreview);

    // Under budget: nothing happens, brackets and all.
    plan_toc_preview(str8_lit("Song (Remastered)"), 40, preview);
    EXPECT(str8_eq(str8(preview->text, preview->size), str8_lit("Song (Remastered)")));
    EXPECT(preview->applied == 0 && !preview->truncated);
    EXPECT(preview->chars == 17 && preview->cells == 3);

    // Step 1: the featuring credit goes first, the aside stays.
    plan_toc_preview(str8_lit("Artist - Song (feat. Someone) (Remastered)"), 24, preview);
    EXPECT((preview->applied & PlanShorten_Feat) != 0);
    EXPECT((preview->applied & PlanShorten_Brackets) != 0);
    EXPECT(str8_eq(str8(preview->text, preview->size), str8_lit("Artist - Song")));
    EXPECT(!preview->truncated);

    // Step 3: the artist gives way before the title is touched.
    plan_toc_preview(str8_lit("A Very Long Artist Name - The Song"), 20, preview);
    EXPECT((preview->applied & PlanShorten_Artist) != 0);
    EXPECT(!preview->truncated);
    EXPECT(preview->chars <= 20);
    EXPECT(str8_ends_with(str8(preview->text, preview->size), str8_lit("- The Song")));

    // Step 3, tight: one or two characters of room for the artist is no room at
    // all - the artist goes, the title stays whole (u32 underflow regression).
    plan_toc_preview(str8_lit("Somebody - Title"), 7, preview);
    EXPECT((preview->applied & PlanShorten_Artist) != 0);
    EXPECT(!preview->truncated);
    EXPECT(str8_eq(str8(preview->text, preview->size), str8_lit("Title")));

    // Step 4: nothing left to give, the title itself is cut - and says so.
    plan_toc_preview(str8_lit("An Extremely Long Single Word Title Here"), 10, preview);
    EXPECT(preview->applied == PlanShorten_Title && preview->truncated);
    EXPECT(preview->chars == 10);
    EXPECT(str8_eq(str8(preview->text, preview->size), str8_lit("An Extreme")));

    // Deterministic: the same input gives the same output, every time.
    PlanTitlePreview *again = push_struct(arena, PlanTitlePreview);
    plan_toc_preview(str8_lit("An Extremely Long Single Word Title Here"), 10, again);
    EXPECT(again->size == preview->size &&
           mem_cmp(again->text, preview->text, preview->size) == 0);

    // A trailing " ft. X" with no brackets at all.
    plan_toc_preview(str8_lit("Song ft. Guest"), 6, preview);
    EXPECT(str8_eq(str8(preview->text, preview->size), str8_lit("Song")));
    EXPECT(preview->applied == PlanShorten_Feat);

    // The templates.
    u8 out[PLAN_TITLE_MAX];
    u64 size = plan_toc_format(PlanTitleTemplate_Title, str8_lit("A"), str8_lit("T"), 3, out,
                               sizeof(out));
    EXPECT(str8_eq(str8(out, size), str8_lit("T")));
    size = plan_toc_format(PlanTitleTemplate_ArtistTitle, str8_lit("A"), str8_lit("T"), 3, out,
                           sizeof(out));
    EXPECT(str8_eq(str8(out, size), str8_lit("A - T")));
    size = plan_toc_format(PlanTitleTemplate_NumberTitle, str8_lit("A"), str8_lit("T"), 3, out,
                           sizeof(out));
    EXPECT(str8_eq(str8(out, size), str8_lit("3. T")));
    // No artist: the template falls back to the title rather than to " - T".
    size = plan_toc_format(PlanTitleTemplate_ArtistTitle, str8(0, 0), str8_lit("T"), 1, out,
                           sizeof(out));
    EXPECT(str8_eq(str8(out, size), str8_lit("T")));
}

// The multi disc split, both policies.
TEST(capacity_split) {
    TestCap tc;
    test_cap_open(&tc, arena);
    Plan *plan = tc.plan;
    PlanSplit *split = push_struct(arena, PlanSplit);

    // 30 tracks of 5:00 = 150 + 1 clusters each: 15 fit on an 80 minute disc
    // (15 x 151 = 2265; a sixteenth would be 2416 of 2400).
    for (u32 i = 0; i < 30; i += 1) { test_cap_add(plan, 300000, PlanMode_SP, 0); }
    plan_capacity_split(plan_disc(plan, 0), 0, PlanSplit_FirstFit, 80, split);
    EXPECT(split->disc_count == 2);
    EXPECT(split->counts[0] == 15 && split->counts[1] == 15);
    EXPECT(split->clusters[0] == 2265 && split->clusters[1] == 2265);
    EXPECT(split->disc_of[14] == 0 && split->disc_of[15] == 1);
    EXPECT(split->unplaced == 0 && split->entry_count == 30);

    // First fit, not next fit: a short track after the boundary goes back to
    // the first disc when there is still room for it there.
    plan_clear(plan);
    // 16 x 4:50 = 16 x 146 = 2336 clusters, 64 left on the first disc.
    for (u32 i = 0; i < 16; i += 1) { test_cap_add(plan, 290000, PlanMode_SP, 0); }
    test_cap_add(plan, 600000, PlanMode_SP, 0);  // 10:00 = 301, only disc 2 fits it
    test_cap_add(plan, 30000, PlanMode_SP, 0);   // 0:30 = 16, disc 1 still has room
    plan_capacity_split(plan_disc(plan, 0), 0, PlanSplit_FirstFit, 80, split);
    EXPECT(split->disc_count == 2);
    EXPECT(split->disc_of[16] == 1);
    EXPECT(split->disc_of[17] == 0);

    // Keeping albums together: three albums of 6 tracks of 5:00 (906 clusters
    // each). Two albums fit on a disc, the third opens a second one instead of
    // being cut in half where first fit would have split it.
    plan_clear(plan);
    Arena *lib_arena = arena_alloc(MB(4));
    Arena *lib_text = arena_alloc(MB(4));
    Library *lib = push_struct(arena, Library);
    lib_init(lib, lib_arena, lib_text);
    u32 albums[3];
    albums[0] = lib_intern(&lib->strings, str8_lit("Album A"));
    albums[1] = lib_intern(&lib->strings, str8_lit("Album B"));
    albums[2] = lib_intern(&lib->strings, str8_lit("Album C"));
    for (u32 a = 0; a < 3; a += 1) {
        for (u32 i = 0; i < 6; i += 1) {
            String8 path = str8f(arena, "C:/a%u/t%u.flac", a, i);
            TrackId id = lib_track_add(lib, path, 1, 1);
            lib->album_id[id] = albums[a];
            lib->artist_id[id] = albums[0];
            test_cap_add(plan, 300000, PlanMode_SP, 0);
            PlanDisc *d = plan_disc(plan, 0);
            d->track_id[d->entry_count - 1] = id;
        }
    }
    plan_capacity_split(plan_disc(plan, 0), lib, PlanSplit_KeepAlbums, 80, split);
    EXPECT(split->disc_count == 2);
    EXPECT(split->counts[0] == 12 && split->counts[1] == 6);
    EXPECT(split->disc_of[11] == 0 && split->disc_of[12] == 1);
    plan_capacity_split(plan_disc(plan, 0), lib, PlanSplit_FirstFit, 80, split);
    EXPECT(split->counts[0] == 15);  // first fit cuts album C after three tracks

    // The proposals read the same columns: one album per run, and a disc title
    // from the artist every entry shares.
    PlanProposedGroup groups[8];
    u32 group_count = plan_toc_propose_groups(plan, lib, 0, groups, 8);
    EXPECT(group_count == 3);
    EXPECT(groups[0].first == 0 && groups[0].count == 6);
    EXPECT(groups[2].first == 12 && str8_eq(groups[2].name, str8_lit("Album C")));
    EXPECT(str8_eq(plan_toc_propose_disc_title(plan, lib, 0), str8_lit("Album A")));
    arena_release(lib_text);
    arena_release(lib_arena);

    // A single track longer than a whole disc is placed anyway, alone.
    plan_clear(plan);
    test_cap_add(plan, 90u * 60000u, PlanMode_SP, 0);
    plan_capacity_split(plan_disc(plan, 0), 0, PlanSplit_FirstFit, 80, split);
    EXPECT(split->disc_count == 1 && split->disc_of[0] == 0 && split->unplaced == 0);
    test_cap_close(&tc);
}

static void test_capacity_run_all(void) {
    RUN(capacity_track_overhead_matches_the_device);
    RUN(capacity_table);
    RUN(capacity_toc_cells);
    RUN(capacity_sanitize);
    RUN(capacity_shorten);
    RUN(capacity_split);
}

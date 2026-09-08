// test_view_plan.c - the plan view without a window (T-032): the geometry of
// the capacity gauge, and the mapping from a gesture to the command it issues.
//
// Everything here goes through plan_view.c, which is exactly what view_plan.c
// calls: a click on a badge is plan_mode_cycle then plan_set_mode, a drop is
// plan_drop_index then plan_move. Testing that is testing the view, minus the
// pixels the capture in the Livraison is there for.

global Arena *test_view_plan_arena;

typedef struct TestPlanFixture {
    Plan *plan;
    Arena *arena;
    Arena *text;
    PlanCapacity *capacity;
    PlanGaugeLayout *layout;
} TestPlanFixture;

static void test_view_plan_begin(TestPlanFixture *fixture, Arena *arena) {
    fixture->arena = arena_alloc(MB(8));
    fixture->text = arena_alloc(MB(8));
    fixture->plan = push_struct(arena, Plan);
    fixture->capacity = push_struct(arena, PlanCapacity);
    fixture->layout = push_struct(arena, PlanGaugeLayout);
    plan_init(fixture->plan, fixture->arena, fixture->text);
}

static void test_view_plan_end(TestPlanFixture *fixture) {
    arena_release(fixture->arena);
    arena_release(fixture->text);
}

// One entry with a duration and a mode, appended. No library behind it: the
// plan carries its own durations, which is what makes it read standalone.
static void test_view_plan_add(TestPlanFixture *fixture, u32 duration_ms, u32 mode,
                               const char *title) {
    PlanEntry entry;
    StructZero(&entry);
    entry.track_id = LIB_TRACK_NONE;
    entry.path_id = lib_intern(&fixture->plan->strings, str8_lit("C:/music/x.flac"));
    entry.title_override =
        title ? lib_intern(&fixture->plan->strings, str8_cstr(title)) : LIB_STRING_NONE;
    entry.duration_ms = duration_ms;
    entry.gain_db = PLAN_GAIN_NONE;
    entry.mode = (u8)mode;
    entry.group_id = PLAN_GROUP_NONE;
    plan_add(fixture->plan, 0, plan_disc(fixture->plan, 0)->entry_count, entry);
}

static void test_view_plan_recompute(TestPlanFixture *fixture, f32 width) {
    plan_capacity_compute(plan_disc(fixture->plan, 0), fixture->capacity);
    plan_gauge_layout(fixture->capacity, width, fixture->layout);
}

// --- geometry ------------------------------------------------------------------
TEST(view_plan_gauge_segments_tile_the_bar) {
    TestPlanFixture fixture;
    test_view_plan_begin(&fixture, arena);
    // Twenty tracks, three modes: the mix the capture of the ticket shows.
    for (u32 i = 0; i < 20; i += 1) {
        u32 mode = (i < 8) ? PlanMode_SP : ((i < 14) ? PlanMode_LP2 : PlanMode_LP4);
        test_view_plan_add(&fixture, 180000 + i * 1013, mode, 0);
    }
    f32 width = 800.0f;
    test_view_plan_recompute(&fixture, width);
    PlanGaugeLayout *layout = fixture.layout;
    EXPECT(layout->count == 20);  // nothing is thin enough to be merged here
    EXPECT(!layout->overflow);

    // Order preserved, no gap and no overlap, and the total is the used width
    // to the pixel: the running total is what places the edges, so the error
    // never accumulates however many segments there are.
    f32 sum = 0.0f;
    f32 previous_end = 0.0f;
    u32 previous_first = 0;
    for (u32 i = 0; i < layout->count; i += 1) {
        PlanGaugeSegment *segment = &layout->segments[i];
        EXPECT(segment->x == previous_end);
        EXPECT(i == 0 || segment->first > previous_first);
        EXPECT(segment->width > 0.0f);
        // The hatched tail is inside its own segment, and it is empty only for
        // a track whose duration lands exactly on a cluster boundary.
        EXPECT(segment->hatch_x >= segment->x);
        EXPECT(segment->hatch_x <= segment->x + segment->width);
        u32 padding = fixture.capacity->entry_padding_ms[segment->first];
        EXPECT((padding == 0) == (segment->hatch_x == segment->x + segment->width));
        sum += segment->width;
        previous_end = segment->x + segment->width;
        previous_first = segment->first;
    }
    EXPECT(abs_f32(sum - layout->used_width) < 1.0f);
    EXPECT(layout->used_width <= width);

    // The used width is the used clusters, at one pixel of tolerance.
    f32 expected = width * (f32)fixture.capacity->used_clusters /
                   (f32)fixture.capacity->capacity_clusters;
    EXPECT(abs_f32(layout->used_width - expected) < 1.0f);
    test_view_plan_end(&fixture);
}

TEST(view_plan_gauge_hatch_and_alternation) {
    TestPlanFixture fixture;
    test_view_plan_begin(&fixture, arena);
    // Exactly two audio clusters of SP and no padding at all: 4 s. Plus the
    // link cluster of T-045, which is charged and is not padding: 3 in all.
    test_view_plan_add(&fixture, 4000, PlanMode_SP, 0);
    // One millisecond more: a third audio cluster, almost all of it wasted, 4.
    test_view_plan_add(&fixture, 4001, PlanMode_SP, 0);
    test_view_plan_recompute(&fixture, 2400.0f);  // one pixel per cluster
    PlanGaugeLayout *layout = fixture.layout;
    EXPECT(layout->count == 2);
    EXPECT(layout->segments[0].width == 3.0f);
    // Nothing wasted, so no hatching: the tail starts at the end. The link
    // cluster widens the segment, it never hatches it.
    EXPECT(layout->segments[0].hatch_x == layout->segments[0].x + 3.0f);
    EXPECT(layout->segments[1].width == 4.0f);
    // 1999 ms of padding over 4 clusters of 2 000 -> one pixel of the four.
    EXPECT(layout->segments[1].hatch_x == layout->segments[1].x + 3.0f);
    // Two neighbours of the same mode alternate, so the border is visible even
    // without a separator (s9.3).
    EXPECT(layout->segments[0].alternate == 0);
    EXPECT(layout->segments[1].alternate == 1);

    // A third one in another mode restarts the alternation.
    test_view_plan_add(&fixture, 4000, PlanMode_LP2, 0);
    test_view_plan_recompute(&fixture, 2400.0f);
    EXPECT(fixture.layout->segments[2].alternate == 0);
    EXPECT(fixture.layout->segments[2].mode == PlanCapMode_LP2);
    test_view_plan_end(&fixture);
}

TEST(view_plan_gauge_overflow_zone) {
    TestPlanFixture fixture;
    test_view_plan_begin(&fixture, arena);
    // An 80 minute disc is 2400 clusters. A three minute SP track is 90 audio
    // clusters plus its link cluster, 91; twenty seven of them are 2457, and
    // the twenty seventh is the one that no longer fits whole (26 x 91 = 2366).
    for (u32 i = 0; i < 27; i += 1) { test_view_plan_add(&fixture, 180000, PlanMode_SP, 0); }
    f32 width = 1200.0f;
    test_view_plan_recompute(&fixture, width);
    PlanCapacity *capacity = fixture.capacity;
    PlanGaugeLayout *layout = fixture.layout;
    EXPECT(capacity->first_overflow == 26);
    EXPECT(layout->overflow);
    // The red zone starts where the twenty seventh track does: 26 * 91 clusters
    // of 2400, over 1200 pixels.
    f32 expected = round_f32(width * (f32)(26 * 91) / 2400.0f);
    EXPECT(layout->overflow_x == expected);
    EXPECT(layout->overflow_x < width);
    // The bar keeps its width: what spills is clamped, never drawn past the end.
    for (u32 i = 0; i < layout->count; i += 1) {
        EXPECT(layout->segments[i].x + layout->segments[i].width <= width);
    }
    // An empty plan has no red zone and no segment.
    plan_batch_begin(fixture.plan);
    while (plan_disc(fixture.plan, 0)->entry_count != 0) { plan_remove(fixture.plan, 0, 0); }
    plan_batch_end(fixture.plan);
    test_view_plan_recompute(&fixture, width);
    EXPECT(fixture.layout->count == 0 && !fixture.layout->overflow);
    EXPECT(fixture.layout->used_width == 0.0f);
    test_view_plan_end(&fixture);
}

TEST(view_plan_gauge_merges_thin_segments) {
    TestPlanFixture fixture;
    test_view_plan_begin(&fixture, arena);
    // A full disc of very short tracks, on the compact 120 px bar of the status
    // bar: no segment can be two pixels wide, so neighbours merge.
    for (u32 i = 0; i < PLAN_ENTRY_MAX; i += 1) {
        test_view_plan_add(&fixture, 8000, PlanMode_SP, 0);
    }
    f32 width = 120.0f;
    test_view_plan_recompute(&fixture, width);
    PlanGaugeLayout *layout = fixture.layout;
    EXPECT(layout->count < PLAN_ENTRY_MAX);
    EXPECT(layout->count > 0);
    u32 covered = 0;
    f32 previous_end = 0.0f;
    for (u32 i = 0; i < layout->count; i += 1) {
        EXPECT(layout->segments[i].x == previous_end);
        EXPECT(layout->segments[i].first == covered);
        EXPECT(layout->segments[i].count >= 1);
        covered += layout->segments[i].count;
        previous_end = layout->segments[i].x + layout->segments[i].width;
    }
    // Every entry is in exactly one segment, and the whole is still the width.
    EXPECT(covered == PLAN_ENTRY_MAX);
    EXPECT(abs_f32(previous_end - layout->used_width) < 1.0f);

    // The same plan on the full width has one segment per entry again.
    test_view_plan_recompute(&fixture, 2400.0f);
    EXPECT(fixture.layout->count == PLAN_ENTRY_MAX);
    test_view_plan_end(&fixture);
}

TEST(view_plan_gauge_hit_test_and_scale) {
    TestPlanFixture fixture;
    test_view_plan_begin(&fixture, arena);
    for (u32 i = 0; i < 4; i += 1) { test_view_plan_add(&fixture, 300000, PlanMode_SP, 0); }
    f32 width = 1000.0f;
    test_view_plan_recompute(&fixture, width);
    PlanGaugeLayout *layout = fixture.layout;
    // The pointer over the middle of segment 2 finds segment 2, and the free
    // zone past the end finds nothing to point at.
    PlanGaugeSegment *second = &layout->segments[2];
    EXPECT(plan_gauge_segment_at(layout, second->x + second->width * 0.5f) == 2);
    EXPECT(plan_gauge_segment_at(layout, layout->used_width + 10.0f) == layout->count);

    // The scale is graduated in the mode most of the plan is written in.
    EXPECT(plan_gauge_reference_mode(fixture.capacity) == PlanCapMode_SP);
    // Half the bar is half of the disc: 40 minutes of an 80 minute disc in SP.
    u64 middle = plan_gauge_time_at(fixture.capacity, layout, width * 0.5f);
    EXPECT(middle > 2390000 && middle < 2410000);

    for (u32 i = 0; i < 12; i += 1) { test_view_plan_add(&fixture, 300000, PlanMode_LP4, 0); }
    test_view_plan_recompute(&fixture, width);
    EXPECT(plan_gauge_reference_mode(fixture.capacity) == PlanCapMode_LP4);
    test_view_plan_end(&fixture);
}

// --- gestures --------------------------------------------------------------------
TEST(view_plan_badge_click_cycles_the_mode) {
    TestPlanFixture fixture;
    test_view_plan_begin(&fixture, arena);
    test_view_plan_add(&fixture, 180000, PlanMode_SP, 0);
    PlanDisc *disc = plan_disc(fixture.plan, 0);

    // SP -> LP2 -> LP4 -> SP mono -> SP: four clicks are the identity, and each
    // one is a command of its own.
    u32 modes[4] = {PlanMode_LP2, PlanMode_LP4, PlanMode_SP, PlanMode_SP};
    u32 monos[4] = {0, 0, 1, 0};
    for (u32 i = 0; i < 4; i += 1) {
        PlanModeStep step =
            plan_mode_cycle(disc->mode[0], (disc->flags[0] & PlanEntryFlag_Mono) != 0);
        EXPECT(step.mode == modes[i]);
        EXPECT(step.mono == (b32)monos[i]);
        EXPECT(plan_set_mode(fixture.plan, 0, 0, step.mode, step.mono));
        EXPECT(disc->mode[0] == modes[i]);
        EXPECT(((disc->flags[0] & PlanEntryFlag_Mono) != 0) == (b32)monos[i]);
    }
    EXPECT(plan_cap_mode_of(disc, 0) == PlanCapMode_SP);
    // And four undos put it back, one per click.
    for (u32 i = 0; i < 4; i += 1) { EXPECT(plan_undo_step(fixture.plan)); }
    EXPECT(disc->mode[0] == PlanMode_SP);
    EXPECT(!plan_can_undo(fixture.plan) || disc->entry_count == 1);
    test_view_plan_end(&fixture);
}

TEST(view_plan_drop_issues_one_move) {
    TestPlanFixture fixture;
    test_view_plan_begin(&fixture, arena);
    for (u32 i = 0; i < 5; i += 1) {
        test_view_plan_add(&fixture, 60000 + i * 1000, PlanMode_SP, 0);
    }
    PlanDisc *disc = plan_disc(fixture.plan, 0);

    // Dragging row 0 into the gap before row 3 leaves it at index 2: the entry
    // is pulled out before it is put back, so the gap shifts by one.
    EXPECT(plan_drop_index(0, 3) == 2);
    // Dragging row 4 into the gap before row 1 lands on index 1: upwards, the
    // gap is the index.
    EXPECT(plan_drop_index(4, 1) == 1);
    // The two gaps around a row are both "leave it alone".
    EXPECT(plan_drop_index(2, 2) == 2);
    EXPECT(plan_drop_index(2, 3) == 2);
    // A drop at the very end lands on the last index.
    EXPECT(plan_drop_index(1, 5) == 4);

    u32 before = fixture.plan->done;
    u32 durations[5];
    for (u32 i = 0; i < 5; i += 1) { durations[i] = disc->duration_ms[i]; }
    EXPECT(plan_move(fixture.plan, 0, 0, plan_drop_index(0, 3)));
    // Exactly one command, whatever the distance travelled.
    EXPECT(fixture.plan->done == before + 1);
    EXPECT(disc->duration_ms[0] == durations[1]);
    EXPECT(disc->duration_ms[2] == durations[0]);
    EXPECT(disc->duration_ms[4] == durations[4]);
    EXPECT(plan_undo_step(fixture.plan));
    for (u32 i = 0; i < 5; i += 1) { EXPECT(disc->duration_ms[i] == durations[i]); }
    test_view_plan_end(&fixture);
}

TEST(view_plan_delete_of_a_selection_is_one_undo_step) {
    TestPlanFixture fixture;
    test_view_plan_begin(&fixture, arena);
    for (u32 i = 0; i < 6; i += 1) { test_view_plan_add(&fixture, 60000 + i, PlanMode_SP, 0); }
    PlanDisc *disc = plan_disc(fixture.plan, 0);
    u32 durations[6];
    for (u32 i = 0; i < 6; i += 1) { durations[i] = disc->duration_ms[i]; }

    // Three rows, not contiguous, deleted in one gesture.
    u32 selection[3] = {1, 3, 4};
    EXPECT(plan_remove_entries(fixture.plan, 0, selection, 3) == 3);
    EXPECT(disc->entry_count == 3);
    EXPECT(disc->duration_ms[0] == durations[0]);
    EXPECT(disc->duration_ms[1] == durations[2]);
    EXPECT(disc->duration_ms[2] == durations[5]);

    // One Ctrl+Z brings all three back, in their places.
    EXPECT(plan_undo_step(fixture.plan));
    EXPECT(disc->entry_count == 6);
    for (u32 i = 0; i < 6; i += 1) { EXPECT(disc->duration_ms[i] == durations[i]); }
    // And one Ctrl+Y takes them away again.
    EXPECT(plan_redo_step(fixture.plan));
    EXPECT(disc->entry_count == 3);
    EXPECT(plan_undo_step(fixture.plan));
    EXPECT(disc->entry_count == 6);

    // A grouped mode change is one step too (MI-12).
    PlanModeStep step;
    step.mode = PlanMode_LP4;
    step.mono = 0;
    EXPECT(plan_set_mode_entries(fixture.plan, 0, selection, 3, step) == 3);
    EXPECT(disc->mode[1] == PlanMode_LP4 && disc->mode[4] == PlanMode_LP4);
    EXPECT(plan_undo_step(fixture.plan));
    EXPECT(disc->mode[1] == PlanMode_SP && disc->mode[3] == PlanMode_SP &&
           disc->mode[4] == PlanMode_SP);
    test_view_plan_end(&fixture);
}

TEST(view_plan_fill_remaining_stops_where_it_must) {
    TestPlanFixture fixture;
    test_view_plan_begin(&fixture, arena);
    // Twenty six tracks of 2:56 at 88 audio clusters + 1 link cluster = 89 each,
    // 2314 of the 2400 an 80 minute disc holds: 86 clusters left, 2:52 in SP.
    for (u32 i = 0; i < 26; i += 1) { test_view_plan_add(&fixture, 176000, PlanMode_SP, 0); }
    plan_capacity_compute(plan_disc(fixture.plan, 0), fixture.capacity);
    EXPECT(fixture.capacity->used_clusters == 26 * 89);
    EXPECT(fixture.capacity->free_clusters == 86);

    // A selection of a one minute track, another one minute track, then a five
    // minute one: the walk takes the first two and stops at the third rather
    // than skipping it, because the order on screen is the order it fills in.
    // In SP that is 31 + 31 = 62 of the 86, and the third would be 151 more.
    u32 durations[4] = {60000, 60000, 300000, 30000};
    EXPECT(plan_fill_count(fixture.capacity, durations, 4, PlanCapMode_SP) == 2);
    // In LP2 (16 + 16, then 76) the five minute track still does not fit, but in
    // LP4 - four times the audio per cluster - the whole selection goes in at
    // 9 + 9 + 39 + 5 = 62 clusters.
    EXPECT(plan_fill_count(fixture.capacity, durations, 4, PlanCapMode_LP2) == 2);
    EXPECT(plan_fill_count(fixture.capacity, durations, 4, PlanCapMode_LP4) == 4);
    // Nothing fits on a disc that is already over.
    for (u32 i = 0; i < 4; i += 1) { test_view_plan_add(&fixture, 180000, PlanMode_SP, 0); }
    plan_capacity_compute(plan_disc(fixture.plan, 0), fixture.capacity);
    EXPECT(fixture.capacity->free_clusters == 0);
    EXPECT(plan_fill_count(fixture.capacity, durations, 4, PlanCapMode_SP) == 0);
    test_view_plan_end(&fixture);
}

TEST(view_plan_shorten_fits_the_toc_in_one_step) {
    TestPlanFixture fixture;
    test_view_plan_begin(&fixture, arena);
    PlanTocBudget *budget = push_struct(arena, PlanTocBudget);
    // Sixty long titles: 60 * 8 cells is well past the 255 the TOC holds.
    for (u32 i = 0; i < 60; i += 1) {
        test_view_plan_add(&fixture, 180000, PlanMode_SP,
                           "Un titre beaucoup trop long pour la table des matieres");
    }
    plan_toc_budget(fixture.plan, 0, 0, budget);
    EXPECT(budget->cells_used > PLAN_TOC_CELLS);

    u32 revision = fixture.plan->revision;
    u32 quota = plan_shorten_apply(fixture.plan, 0, 0, PLAN_TOC_CELLS - budget->cells_disc);
    EXPECT(quota != 0);
    EXPECT(fixture.plan->revision > revision);

    // Every title is inside the quota, and the budget now fits.
    PlanTitlePreview preview;
    for (u32 i = 0; i < 60; i += 1) {
        plan_toc_preview(plan_entry_title(fixture.plan, 0, 0, i), 0, &preview);
        EXPECT(preview.chars <= quota);
    }
    plan_toc_budget(fixture.plan, 0, 0, budget);
    EXPECT(budget->cells_used <= PLAN_TOC_CELLS);
    EXPECT(!budget->overflow);

    // And it is one undo step, not sixty.
    EXPECT(plan_undo_step(fixture.plan));
    plan_toc_budget(fixture.plan, 0, 0, budget);
    EXPECT(budget->cells_used > PLAN_TOC_CELLS);
    EXPECT(!plan_can_undo(fixture.plan) == 0);  // the sixty adds are still there
    test_view_plan_end(&fixture);
}

TEST(view_plan_shorten_quota_shares_what_is_left) {
    Unused(arena);
    // Four titles, two of them short: the short ones keep what they take and
    // the budget that is left is split between the two long ones.
    u32 chars[4] = {3, 5, 90, 120};
    u8 non_sp[4] = {0, 0, 0, 0};
    // 1 + 1 cells for the short ones leaves 8 cells, so 4 each: 28 characters.
    u32 quota = plan_shorten_quota(chars, non_sp, 4, 10);
    EXPECT(quota == 28);
    EXPECT(plan_toc_cells_for_chars(3) + plan_toc_cells_for_chars(5) +
               2 * plan_toc_cells_for_chars(quota) <=
           10);
    // One more character each would not fit any more: the quota is the largest.
    EXPECT(plan_toc_cells_for_chars(3) + plan_toc_cells_for_chars(5) +
               2 * plan_toc_cells_for_chars(quota + 1) >
           10);
    // A budget that cannot even pay the mandatory cell of an LP track is a
    // refusal, not a silent truncation to nothing.
    u8 lp[4] = {1, 1, 1, 1};
    EXPECT(plan_shorten_quota(chars, lp, 4, 3) == 0);
    test_report("");  // keeps the case name attached to the checks above
}

// --- T-071 --------------------------------------------------------------------

// The Disc panel used to decide its header line and its body separately: on
// 2026-09-07 that produced "pilote manquant" over "utilise par une autre
// application". The table below is the whole contract now - one state in, one
// header and one body out - and it is a table precisely because two sets of ifs
// could not be compared against one another.
TEST(view_plan_device_panel_says_one_thing) {
    Unused(arena);
    struct TestPanelCase {
        u32 device_state;
        b32 burning;
        u32 panel;
        Str header;
        Str body;
        Str hint;
    };
    static const struct TestPanelCase cases[] = {
        {AppDeviceState_None, 0, NetmdPanel_NoDevice, Str_DeviceNone, Str_DeviceNone,
         Str_DeviceNoneHint},
        {AppDeviceState_NoDriver, 0, NetmdPanel_NoDriver, Str_DeviceNoDriver, Str_DeviceNoDriver,
         Str_DeviceNoDriverBody},
        {AppDeviceState_InUse, 0, NetmdPanel_InUse, Str_DeviceInUse, Str_DeviceInUse,
         Str_DeviceInUseHint},
        {AppDeviceState_Connected, 0, NetmdPanel_Connected, Str_DevicePanelName,
         Str_DeviceConnected, Str_COUNT},
        {AppDeviceState_Connected, 1, NetmdPanel_Burning, Str_DeviceBurning, Str_DeviceBurning,
         Str_DeviceBurningHint},
        {AppDeviceState_Error, 0, NetmdPanel_Unreachable, Str_DeviceUnreachable,
         Str_DeviceUnreachable, Str_DeviceUnreachableHint},
        // A burn that lost the cable is an unreachable device, not a burn: the
        // flag only ever refines a connection that is still there.
        {AppDeviceState_Error, 1, NetmdPanel_Unreachable, Str_DeviceUnreachable,
         Str_DeviceUnreachable, Str_DeviceUnreachableHint},
        {AppDeviceState_None, 1, NetmdPanel_NoDevice, Str_DeviceNone, Str_DeviceNone,
         Str_DeviceNoneHint},
    };
    for (u32 i = 0; i < ArrayCount(cases); i += 1) {
        u32 panel = netmd_panel_state(cases[i].device_state, cases[i].burning);
        EXPECT(panel == cases[i].panel);
        EXPECT(netmd_panel_header_string(panel) == cases[i].header);
        EXPECT(netmd_panel_body_string(panel) == cases[i].body);
        EXPECT(netmd_panel_hint_string(panel) == cases[i].hint);
    }
    // Every state of both enums is covered above, so a state added without a
    // sentence to go with it fails here rather than on screen.
    EXPECT(NetmdPanel_COUNT == 6);
    EXPECT(AppDeviceState_COUNT == 5);
}

// The plan header and the gauge readout are the same number, on five plans that
// mix the four billing modes. They were not: the header showed the per mode
// billed sum ("84:08") and the gauge the disc equivalent ("63:02 / 80:00").
TEST(view_plan_header_reads_what_the_gauge_reads) {
    static const u32 modes[5][4] = {
        {PlanMode_SP, PlanMode_SP, PlanMode_SP, PlanMode_SP},
        {PlanMode_SP, PlanMode_LP2, PlanMode_SP, PlanMode_LP2},
        {PlanMode_LP4, PlanMode_LP4, PlanMode_LP2, PlanMode_SP},
        {PlanMode_LP2, PlanMode_LP4, PlanMode_LP4, PlanMode_LP4},
        {PlanMode_SP, PlanMode_LP4, PlanMode_LP2, PlanMode_SP},
    };
    for (u32 p = 0; p < 5; p += 1) {
        TestPlanFixture fixture;
        test_view_plan_begin(&fixture, arena);
        for (u32 i = 0; i < 16; i += 1) {
            test_view_plan_add(&fixture, 137000 + i * 4099, modes[p][i & 3], 0);
        }
        test_view_plan_recompute(&fixture, 800.0f);
        const PlanCapacity *capacity = fixture.capacity;
        // What the header says, what the status bar says and what the pre-flight
        // says all come out of this one function, so there is nothing to keep in
        // step by hand.
        u64 header_ms = plan_disc_used_ms(capacity);
        EXPECT(header_ms == (u64)capacity->used_clusters * PLAN_CLUSTER_SP_MS);
        EXPECT(plan_disc_total_ms(capacity) ==
               (u64)capacity->capacity_clusters * PLAN_CLUSTER_SP_MS);
        // And it is a different number from the billed sum as soon as anything
        // is not SP - which is the whole reason showing both was confusing.
        b32 all_sp = 1;
        for (u32 i = 0; i < 4; i += 1) { all_sp = all_sp && (modes[p][i] == PlanMode_SP); }
        if (!all_sp) { EXPECT(capacity->billed_ms != header_ms); }
        test_view_plan_end(&fixture);
    }
}

// The hatch is anchored on an origin, not on the area it fills: two areas that
// share an origin show one continuous set of stripes, and moving an area over a
// fixed origin slides the window instead of restarting the pattern. That is
// what "stable under scroll" means, and it is arithmetic.
TEST(view_plan_hatch_tiles_keep_their_phase) {
    Unused(arena);
    R_HatchTile tiles[R_HATCH_MAX_TILES];
    R_HatchTile shifted[R_HATCH_MAX_TILES];
    f32 tile = (f32)R_HATCH_TILE_PX;

    // An area exactly on the grid: whole tiles, first uv at 0, last at 1.
    u32 count = r_hatch_tiles(rect(0.0f, 0.0f, tile * 3.0f, tile), v2(0.0f, 0.0f), tiles,
                              R_HATCH_MAX_TILES);
    EXPECT(count == 3);
    EXPECT(tiles[0].uv0.x == 0.0f && tiles[0].uv1.x == 1.0f);
    EXPECT(tiles[2].dst.min.x == tile * 2.0f);

    // A short, off grid area: one tile, cut on both sides, and its uv say which
    // part of the pattern it is - not 0..1, which would stretch it.
    count = r_hatch_tiles(rect(20.0f, 4.0f, 44.0f, 18.0f), v2(0.0f, 0.0f), tiles,
                          R_HATCH_MAX_TILES);
    EXPECT(count == 1);
    EXPECT(tiles[0].uv0.x == 20.0f / tile);
    EXPECT(tiles[0].uv1.x == 44.0f / tile);
    EXPECT(tiles[0].uv0.y == 4.0f / tile);

    // Moved by a whole tile: the same tiles, to the pixel. This is the case that
    // shimmered - a pattern restarted at the left edge of whatever it fills
    // travels with the thing it fills.
    u32 count2 = r_hatch_tiles(rect(20.0f + tile, 4.0f, 44.0f + tile, 18.0f), v2(0.0f, 0.0f),
                               shifted, R_HATCH_MAX_TILES);
    EXPECT(count == count2);
    for (u32 i = 0; i < count; i += 1) {
        EXPECT(tiles[i].uv0.x == shifted[i].uv0.x);
        EXPECT(tiles[i].uv1.x == shifted[i].uv1.x);
        EXPECT(shifted[i].dst.min.x - tiles[i].dst.min.x == tile);
    }

    // Two neighbouring tails of one gauge: the right hand one carries on where
    // the left hand one stopped, because both are cut on the same origin.
    count = r_hatch_tiles(rect(0.0f, 0.0f, 30.0f, 14.0f), v2(0.0f, 0.0f), tiles,
                          R_HATCH_MAX_TILES);
    count2 = r_hatch_tiles(rect(30.0f, 0.0f, 90.0f, 14.0f), v2(0.0f, 0.0f), shifted,
                           R_HATCH_MAX_TILES);
    EXPECT(count == 1 && count2 == 2);
    EXPECT(tiles[0].uv1.x == shifted[0].uv0.x);

    // A degenerate area produces nothing rather than a quad of zero pixels, and
    // a very wide one is bounded by `max` rather than by the caller's luck.
    EXPECT(r_hatch_tiles(rect(10.0f, 10.0f, 10.0f, 20.0f), v2(0.0f, 0.0f), tiles,
                         R_HATCH_MAX_TILES) == 0);
    EXPECT(r_hatch_tiles(rect(0.0f, 0.0f, 100.0f, 10.0f), v2(0.0f, 0.0f), tiles, 0) == 0);
    EXPECT(r_hatch_tiles(rect(0.0f, 0.0f, 100000.0f, 14.0f), v2(0.0f, 0.0f), tiles, 4) == 4);
}

static void test_view_plan_run_all(void) {
    test_report("view plan\n");
    test_view_plan_arena = arena_alloc(MB(16));

    RUN(view_plan_gauge_segments_tile_the_bar);
    RUN(view_plan_gauge_hatch_and_alternation);
    RUN(view_plan_gauge_overflow_zone);
    RUN(view_plan_gauge_merges_thin_segments);
    RUN(view_plan_gauge_hit_test_and_scale);
    RUN(view_plan_badge_click_cycles_the_mode);
    RUN(view_plan_drop_issues_one_move);
    RUN(view_plan_delete_of_a_selection_is_one_undo_step);
    RUN(view_plan_fill_remaining_stops_where_it_must);
    RUN(view_plan_shorten_fits_the_toc_in_one_step);
    RUN(view_plan_shorten_quota_shares_what_is_left);
    RUN(view_plan_device_panel_says_one_thing);
    RUN(view_plan_header_reads_what_the_gauge_reads);
    RUN(view_plan_hatch_tiles_keep_their_phase);
}

// test_plan.c - T-030: every command and its exact inverse, a thousand random
// operations undone and redone step by step, the coalescing of a typed title,
// the re-resolution of a track by path, both file formats round tripped, and
// the loader refusing a version it does not know or a header that lies.

typedef struct TestPlan {
    Plan *plan;
    Arena *arena;
    Arena *text;
} TestPlan;

static void test_plan_open(TestPlan *tp, Arena *host) {
    tp->arena = arena_alloc(MB(16));
    tp->text = arena_alloc(MB(16));
    tp->plan = push_struct(host, Plan);
    plan_init(tp->plan, tp->arena, tp->text);
}

static void test_plan_close(TestPlan *tp) {
    arena_release(tp->text);
    arena_release(tp->arena);
}

// Everything the document means, and nothing it merely happens to hold: the
// columns past entry_count are scratch and are deliberately left out, which is
// what makes "the undo brought us back" a statement about the document.
static u64 test_plan_hash(const Plan *plan) {
    u64 h = hash64_mix(plan->disc_count * 1000003ull + plan->title);
    for (u32 d = 0; d < plan->disc_count; d += 1) {
        const PlanDisc *disc = &plan->discs[d];
        h ^= hash64_mix(h + disc->title + ((u64)disc->length_min << 20) +
                        ((u64)disc->default_mode << 40) + ((u64)disc->entry_count << 44) +
                        ((u64)disc->group_live << 8));
        for (u32 g = 0; g < PLAN_GROUP_MAX; g += 1) {
            if (!(disc->group_live & (1u << g))) { continue; }
            h ^= hash64_mix(h + g + disc->groups[g].name * 31ull +
                            ((u64)disc->groups[g].first << 32) +
                            ((u64)disc->groups[g].count << 48));
        }
        for (u32 i = 0; i < disc->entry_count; i += 1) {
            PlanEntry entry = plan_entry(disc, i);
            h ^= hash64_mix(h + hash64(&entry, sizeof(entry)) + i);
        }
    }
    return h;
}

static u32 test_plan_rand(u64 *state) {
    u64 x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return (u32)(x >> 32);
}

static PlanEntry test_plan_entry(Plan *plan, u32 n) {
    ArenaTemp scratch = scratch_begin(0, 0);
    PlanEntry entry;
    StructZero(&entry);
    entry.track_id = n;
    entry.path_id =
        lib_intern(&plan->strings, str8f(scratch.arena, "C:\\music\\track%04u.flac", n));
    entry.duration_ms = 60000 + n * 137;
    entry.gain_db = PLAN_GAIN_NONE;
    entry.mode = (u8)(n % PlanMode_COUNT);
    entry.group_id = PLAN_GROUP_NONE;
    scratch_end(scratch);
    return entry;
}

// --- every command, and its inverse -------------------------------------------

TEST(plan_command_inverses) {
    TestPlan tp;
    test_plan_open(&tp, arena);
    Plan *plan = tp.plan;

    // The empty document: one disc, 80 minutes, SP, nothing to undo.
    EXPECT(plan->disc_count == 1);
    EXPECT(plan->discs[0].length_min == 80 && plan->discs[0].default_mode == PlanMode_SP);
    EXPECT(!plan_can_undo(plan) && !plan_can_redo(plan) && !plan->dirty);

    u64 empty = test_plan_hash(plan);
    for (u32 i = 0; i < 8; i += 1) { EXPECT(plan_add(plan, 0, i, test_plan_entry(plan, i))); }
    EXPECT(plan->discs[0].entry_count == 8);
    EXPECT(plan->dirty);
    u64 base = test_plan_hash(plan);

    struct {
        const char *name;
        b32 applied;
    } cases[10];
    u32 case_count = 0;
#define TEST_PLAN_CASE(label, expr)                          \
    do {                                                     \
        u64 before = test_plan_hash(plan);                   \
        EXPECT(expr);                                        \
        EXPECT(test_plan_hash(plan) != before);              \
        EXPECT(plan_undo(plan));                             \
        EXPECT(test_plan_hash(plan) == before);              \
        EXPECT(plan_redo(plan));                             \
        EXPECT(test_plan_hash(plan) != before);              \
        EXPECT(plan_undo(plan));                             \
        EXPECT(test_plan_hash(plan) == before);              \
        cases[case_count].name = label;                      \
        cases[case_count].applied = 1;                       \
        case_count += 1;                                     \
    } while (0)

    TEST_PLAN_CASE("Add", plan_add(plan, 0, 3, test_plan_entry(plan, 42)));
    TEST_PLAN_CASE("Remove", plan_remove(plan, 0, 5));
    TEST_PLAN_CASE("Move", plan_move(plan, 0, 1, 6));
    TEST_PLAN_CASE("SetMode", plan_set_mode(plan, 0, 2, PlanMode_LP4, 1));
    TEST_PLAN_CASE("SetTitle", plan_set_title(plan, 0, 2, str8_lit("Rename"), 0));
    TEST_PLAN_CASE("SetDiscTitle", plan_set_disc_title(plan, 0, str8_lit("Disc one"), 0));
    TEST_PLAN_CASE("Group", plan_group(plan, 0, 2, 3, str8_lit("Side A")));
    TEST_PLAN_CASE("SetDiscLength", plan_set_disc_length(plan, 0, 74));
    TEST_PLAN_CASE("SplitDisc", plan_split_disc(plan, 0, 4));
    EXPECT(case_count == 9);
#undef TEST_PLAN_CASE

    // Ungroup needs a group to exist first: its inverse restores the range and
    // the name, which the hash covers.
    EXPECT(plan_group(plan, 0, 2, 3, str8_lit("Side A")));
    EXPECT(plan->discs[0].groups[0].first == 2 && plan->discs[0].groups[0].count == 3);
    u64 grouped = test_plan_hash(plan);
    EXPECT(plan_ungroup(plan, 0, 0));
    EXPECT(plan->discs[0].group_live == 0);
    EXPECT(plan_undo(plan));
    EXPECT(test_plan_hash(plan) == grouped);
    EXPECT(plan_undo(plan));
    EXPECT(test_plan_hash(plan) == base);

    // Refusals change nothing and push nothing: a full disc, an index past the
    // end, a length that is not one of the three, a group over a grouped range.
    EXPECT(!plan_add(plan, 0, 99, test_plan_entry(plan, 1)));
    EXPECT(!plan_remove(plan, 0, 8));
    EXPECT(!plan_move(plan, 0, 0, 0));
    EXPECT(!plan_set_disc_length(plan, 0, 90));
    EXPECT(!plan_set_mode(plan, 0, 0, PlanMode_COUNT, 0));
    EXPECT(!plan_split_disc(plan, 0, 0));
    EXPECT(!plan_ungroup(plan, 0, 7));
    EXPECT(!plan_add(plan, 3, 0, test_plan_entry(plan, 1)));
    EXPECT(test_plan_hash(plan) == base);

    // The 254 track ceiling (B-28).
    while (plan->discs[0].entry_count < PLAN_ENTRY_MAX) {
        EXPECT(plan_add(plan, 0, plan->discs[0].entry_count,
                        test_plan_entry(plan, plan->discs[0].entry_count)));
    }
    EXPECT(plan->discs[0].entry_count == PLAN_ENTRY_MAX);
    EXPECT(!plan_add(plan, 0, PLAN_ENTRY_MAX, test_plan_entry(plan, 999)));

    // Everything, all the way back: the stack is 256 deep and 254 - 8 + 8 fits.
    while (plan_can_undo(plan)) { EXPECT(plan_undo(plan)); }
    EXPECT(test_plan_hash(plan) == empty);
    test_plan_close(&tp);
}

// --- a thousand random operations ------------------------------------------------

TEST(plan_random_round_trip) {
    TestPlan tp;
    test_plan_open(&tp, arena);
    Plan *plan = tp.plan;
    u64 seed = 0x9E3779B97F4A7C15ull;

    // A short run first, entirely inside the 256 command stack: undone to the
    // last command, the document is byte for byte the one we started from.
    u64 empty = test_plan_hash(plan);
    for (u32 i = 0; i < 200; i += 1) {
        plan_add(plan, 0, plan->discs[0].entry_count, test_plan_entry(plan, i));
    }
    while (plan_can_undo(plan)) { plan_undo(plan); }
    EXPECT(test_plan_hash(plan) == empty);
    while (plan_can_redo(plan)) { plan_redo(plan); }
    EXPECT(plan->discs[0].entry_count == 200);

    // Then the long one. Every state the document went through is remembered,
    // so undo and redo are checked step by step and not only at the ends.
    u32 op_count = 1000;
    u64 *history = push_array(arena, u64, op_count + 1);
    u32 kept = 0;
    history[0] = test_plan_hash(plan);
    u32 applied = 0;
    for (u32 i = 0; i < op_count; i += 1) {
        u32 disc_index = test_plan_rand(&seed) % plan->disc_count;
        PlanDisc *disc = &plan->discs[disc_index];
        u32 count = disc->entry_count;
        u32 index = count ? test_plan_rand(&seed) % count : 0;
        b32 ok = 0;
        switch (test_plan_rand(&seed) % 10) {
            case 0: ok = plan_add(plan, disc_index, count ? index : 0,
                                  test_plan_entry(plan, i)); break;
            case 1: ok = count ? plan_remove(plan, disc_index, index) : 0; break;
            case 2: ok = count > 1 ? plan_move(plan, disc_index, index,
                                               test_plan_rand(&seed) % count) : 0; break;
            case 3: ok = count ? plan_set_mode(plan, disc_index, index,
                                               test_plan_rand(&seed) % PlanMode_COUNT,
                                               test_plan_rand(&seed) & 1) : 0; break;
            case 4: {
                ArenaTemp scratch = scratch_begin(&arena, 1);
                ok = count ? plan_set_title(plan, disc_index, index,
                                            str8f(scratch.arena, "title %u", i),
                                            (u64)i * PLAN_COALESCE_US * 2) : 0;
                scratch_end(scratch);
            } break;
            case 5: {
                ArenaTemp scratch = scratch_begin(&arena, 1);
                ok = plan_set_disc_title(plan, disc_index, str8f(scratch.arena, "disc %u", i),
                                         (u64)i * PLAN_COALESCE_US * 2);
                scratch_end(scratch);
            } break;
            case 6: ok = count ? plan_group(plan, disc_index, index,
                                            1 + test_plan_rand(&seed) % 3,
                                            str8_lit("group")) : 0; break;
            case 7: ok = plan_ungroup(plan, disc_index, test_plan_rand(&seed) % PLAN_GROUP_MAX);
                break;
            case 8: {
                static const u32 lengths[3] = {60, 74, 80};
                ok = plan_set_disc_length(plan, disc_index, lengths[test_plan_rand(&seed) % 3]);
            } break;
            default: ok = count > 1 ? plan_split_disc(plan, disc_index,
                                                      1 + test_plan_rand(&seed) % (count - 1)) : 0;
                break;
        }
        if (!ok) { continue; }
        applied += 1;
        kept += 1;
        history[kept] = test_plan_hash(plan);
    }
    EXPECT(applied > 400);  // the generator is not producing refusals only

    // Undo to the floor of the bounded stack, checking every intermediate
    // state, then redo forward over the same states.
    u32 depth = plan->done - plan->first;
    EXPECT(depth == PLAN_UNDO_MAX);
    u32 at = kept;
    while (plan_can_undo(plan)) {
        EXPECT(plan_undo(plan));
        at -= 1;
        EXPECT(test_plan_hash(plan) == history[at]);
    }
    while (plan_can_redo(plan)) {
        EXPECT(plan_redo(plan));
        at += 1;
        EXPECT(test_plan_hash(plan) == history[at]);
    }
    EXPECT(at == kept);
    test_plan_close(&tp);
}

// --- coalescing ------------------------------------------------------------------

TEST(plan_coalesce) {
    TestPlan tp;
    test_plan_open(&tp, arena);
    Plan *plan = tp.plan;
    plan_add(plan, 0, 0, test_plan_entry(plan, 1));
    u32 before = plan->done;

    // Typing "Blue" one letter at a time inside the window: one undo step.
    plan_set_title(plan, 0, 0, str8_lit("B"), 1000);
    plan_set_title(plan, 0, 0, str8_lit("Bl"), 2000);
    plan_set_title(plan, 0, 0, str8_lit("Blu"), 3000);
    plan_set_title(plan, 0, 0, str8_lit("Blue"), 4000);
    EXPECT(plan->done == before + 1);
    EXPECT(str8_eq(plan_entry_title(plan, 0, 0, 0), str8_lit("Blue")));
    EXPECT(plan_undo(plan));
    EXPECT(plan->discs[0].title_override[0] == LIB_STRING_NONE);
    EXPECT(plan_redo(plan));
    EXPECT(str8_eq(plan_entry_title(plan, 0, 0, 0), str8_lit("Blue")));

    // A pause longer than the window starts a new step...
    plan_set_title(plan, 0, 0, str8_lit("Blues"), 4000 + PLAN_COALESCE_US + 1);
    EXPECT(plan->done == before + 2);
    // ...and so does an explicit break, which is what a field losing focus does.
    plan_coalesce_break(plan);
    plan_set_title(plan, 0, 0, str8_lit("Bluesy"), 4000 + PLAN_COALESCE_US + 2);
    EXPECT(plan->done == before + 3);
    EXPECT(plan_undo(plan));
    EXPECT(str8_eq(plan_entry_title(plan, 0, 0, 0), str8_lit("Blues")));

    // Any other command closes the run too.
    plan_coalesce_break(plan);
    plan_set_disc_title(plan, 0, str8_lit("A"), 10);
    plan_set_disc_title(plan, 0, str8_lit("Ab"), 20);
    u32 after_disc = plan->done;
    plan_add(plan, 0, 0, test_plan_entry(plan, 2));
    plan_set_disc_title(plan, 0, str8_lit("Abc"), 30);
    EXPECT(plan->done == after_disc + 2);
    test_plan_close(&tp);
}

// --- resolution ----------------------------------------------------------------

TEST(plan_resolution) {
    TestPlan tp;
    test_plan_open(&tp, arena);
    Plan *plan = tp.plan;
    Arena *lib_arena = arena_alloc(MB(64));
    Arena *lib_text = arena_alloc(MB(64));
    Library lib;
    lib_init(&lib, lib_arena, lib_text);
    TrackId a = lib_track_add(&lib, str8_lit("C:\\music\\a.flac"), 1000, 1);
    TrackId b = lib_track_add(&lib, str8_lit("C:\\music\\b.flac"), 1000, 2);
    lib.duration_ms[a] = 200000;
    lib.duration_ms[b] = 300000;

    EXPECT(plan_add_track(plan, &lib, 0, a));
    EXPECT(plan_add_track(plan, &lib, 0, b));
    EXPECT(plan->discs[0].duration_ms[0] == 200000);
    EXPECT(plan_duration_ms(plan) == 500000);

    // A track that is gone: the entry is kept and flagged, never dropped.
    PlanEntry ghost;
    StructZero(&ghost);
    ghost.track_id = 77;
    ghost.path_id = lib_intern(&plan->strings, str8_lit("C:\\music\\gone.flac"));
    ghost.gain_db = PLAN_GAIN_NONE;
    ghost.group_id = PLAN_GROUP_NONE;
    EXPECT(plan_add(plan, 0, 2, ghost));

    // A rescan that renumbered everything: the stored ids now name the wrong
    // files, and the paths are what puts the plan back together.
    Library rebuilt;
    Arena *rebuilt_arena = arena_alloc(MB(64));
    Arena *rebuilt_text = arena_alloc(MB(64));
    lib_init(&rebuilt, rebuilt_arena, rebuilt_text);
    lib_track_add(&rebuilt, str8_lit("C:\\music\\filler.flac"), 1000, 3);
    TrackId new_b = lib_track_add(&rebuilt, str8_lit("C:\\music\\b.flac"), 1000, 2);
    TrackId new_a = lib_track_add(&rebuilt, str8_lit("C:\\music\\a.flac"), 1000, 1);
    rebuilt.duration_ms[new_a] = 200000;
    rebuilt.duration_ms[new_b] = 300000;

    EXPECT(plan_resolve(plan, &rebuilt) == 1);
    EXPECT(plan->discs[0].track_id[0] == new_a);
    EXPECT(plan->discs[0].track_id[1] == new_b);
    EXPECT((plan->discs[0].flags[0] & PlanEntryFlag_Missing) == 0);
    EXPECT(plan->discs[0].track_id[2] == LIB_TRACK_NONE);
    EXPECT((plan->discs[0].flags[2] & PlanEntryFlag_Missing) != 0);
    EXPECT(plan->discs[0].entry_count == 3);

    // The file that came back: the same pass clears the flag.
    lib_track_add(&rebuilt, str8_lit("C:\\music\\gone.flac"), 1000, 4);
    EXPECT(plan_resolve(plan, &rebuilt) == 0);
    EXPECT((plan->discs[0].flags[2] & PlanEntryFlag_Missing) == 0);

    arena_release(rebuilt_text);
    arena_release(rebuilt_arena);
    arena_release(lib_text);
    arena_release(lib_arena);
    test_plan_close(&tp);
}

// --- the files ------------------------------------------------------------------

static String8 test_plan_path(Arena *arena, const char *name) {
    String8 temp = os_known_folder(arena, OsKnownFolder_Temp);
    return os_path_join(arena, temp, str8_cstr(name));
}

// A document with something of everything in it: several discs, groups, every
// mode, mono, an override, a gain, trims and fades.
static void test_plan_fill(Plan *plan, u32 count) {
    plan_set_disc_title(plan, 0, str8_lit("Compil voiture"), 0);
    plan_coalesce_break(plan);
    plan_set_disc_length(plan, 0, 74);
    for (u32 i = 0; i < count; i += 1) {
        PlanEntry entry = test_plan_entry(plan, i);
        entry.gain_db = (i16)(i * 7 - 100);
        entry.trim_head_ms = (u16)(i * 3);
        entry.trim_tail_ms = (u16)(i * 5);
        entry.fade_in_ms = (u16)(i * 11);
        entry.fade_out_ms = (u16)(i * 13);
        entry.flags = (u8)((i % 3 == 0) ? PlanEntryFlag_Mono : 0);
        plan_add(plan, 0, i, entry);
    }
    if (count >= 8) {
        plan_group(plan, 0, 1, 3, str8_lit("Side A"));
        plan_group(plan, 0, 4, 2, str8_lit("Side B"));
        plan_set_title(plan, 0, 6, str8_lit("Un titre \xC3\xA9""crit \xC3\xA0 la main"), 0);
        plan_coalesce_break(plan);
        plan_split_disc(plan, 0, 5);
        plan_set_disc_title(plan, 1, str8_lit("Deuxi\xC3\xA8me disque"), 0);
        plan_coalesce_break(plan);
    }
}

TEST(plan_file_round_trip) {
    TestPlan tp;
    test_plan_open(&tp, arena);
    test_plan_fill(tp.plan, 24);
    u64 hash = test_plan_hash(tp.plan);
    EXPECT(tp.plan->disc_count == 2);

    String8 path = test_plan_path(arena, "minidisk_test.mdplan");
    os_file_delete(path);
    EXPECT(plan_save(tp.plan, path) == PlanFile_Ok);
    OsFileInfo info;
    EXPECT(!os_file_stat(str8_cat(arena, path, str8_lit(".tmp")), &info));  // atomic write

    TestPlan loaded;
    test_plan_open(&loaded, arena);
    EXPECT(plan_load(loaded.plan, path) == PlanFile_Ok);
    EXPECT(test_plan_hash(loaded.plan) == hash);
    EXPECT(loaded.plan->discs[0].length_min == 74);
    EXPECT(str8_eq(plan_string(loaded.plan, loaded.plan->discs[0].title),
                   str8_lit("Compil voiture")));
    EXPECT(loaded.plan->discs[0].group_live != 0);
    // A loaded document has nothing to undo: the file is the new origin.
    EXPECT(!plan_can_undo(loaded.plan) && !plan_can_redo(loaded.plan));

    // Loading again into a used plan must not accumulate anything.
    EXPECT(plan_load(loaded.plan, path) == PlanFile_Ok);
    EXPECT(test_plan_hash(loaded.plan) == hash);

    os_file_delete(path);
    EXPECT(plan_load(loaded.plan, path) == PlanFile_Missing);
    test_plan_close(&loaded);
    test_plan_close(&tp);
}

TEST(plan_text_round_trip) {
    TestPlan tp;
    test_plan_open(&tp, arena);
    test_plan_fill(tp.plan, 12);
    String8 path = test_plan_path(arena, "minidisk_test.mdplan.txt");
    os_file_delete(path);
    EXPECT(plan_export_text(tp.plan, path) == PlanFile_Ok);

    String8 text = os_file_read_all(arena, path);
    EXPECT(str8_starts_with(text, str8_lit("# minidisk plan 1\n")));
    EXPECT(str8_find(text, str8_lit("\ntrack\tSP-MONO\t"), 0) != text.size);
    EXPECT(str8_find(text, str8_lit("C:\\music\\track0003.flac"), 0) != text.size);

    TestPlan imported;
    test_plan_open(&imported, arena);
    EXPECT(plan_import_text(imported.plan, path) == PlanFile_Ok);
    EXPECT(imported.plan->disc_count == tp.plan->disc_count);

    // The text carries no track id: everything but that column, and the flag
    // that says so, must come back identical.
    for (u32 d = 0; d < tp.plan->disc_count; d += 1) {
        PlanDisc *a = &tp.plan->discs[d];
        PlanDisc *b = &imported.plan->discs[d];
        EXPECT(b->entry_count == a->entry_count);
        EXPECT(b->length_min == a->length_min && b->default_mode == a->default_mode);
        EXPECT(str8_eq(plan_string(imported.plan, b->title), plan_string(tp.plan, a->title)));
        EXPECT(b->group_live == a->group_live);
        for (u32 g = 0; g < PLAN_GROUP_MAX; g += 1) {
            if (!(a->group_live & (1u << g))) { continue; }
            EXPECT(str8_eq(plan_string(imported.plan, b->groups[g].name),
                           plan_string(tp.plan, a->groups[g].name)));
            EXPECT(b->groups[g].first == a->groups[g].first);
            EXPECT(b->groups[g].count == a->groups[g].count);
        }
        for (u32 i = 0; i < a->entry_count; i += 1) {
            EXPECT(b->mode[i] == a->mode[i]);
            EXPECT((b->flags[i] & PlanEntryFlag_Mono) == (a->flags[i] & PlanEntryFlag_Mono));
            EXPECT((b->flags[i] & PlanEntryFlag_Missing) != 0);
            EXPECT(b->duration_ms[i] == a->duration_ms[i]);
            EXPECT(b->gain_db[i] == a->gain_db[i]);
            EXPECT(b->trim_head_ms[i] == a->trim_head_ms[i]);
            EXPECT(b->trim_tail_ms[i] == a->trim_tail_ms[i]);
            EXPECT(b->fade_in_ms[i] == a->fade_in_ms[i]);
            EXPECT(b->fade_out_ms[i] == a->fade_out_ms[i]);
            EXPECT(b->group_id[i] == a->group_id[i]);
            EXPECT(str8_eq(plan_string(imported.plan, b->path_id[i]),
                           plan_string(tp.plan, a->path_id[i])));
            EXPECT(str8_eq(plan_string(imported.plan, b->title_override[i]),
                           plan_string(tp.plan, a->title_override[i])));
        }
    }

    // Garbage in a field is a refusal, not a half read document.
    EXPECT(os_file_write_all(path, str8_lit("plan\tx\ndisc\td\t99\tSP\n")));
    EXPECT(plan_import_text(imported.plan, path) == PlanFile_Corrupt);
    EXPECT(os_file_write_all(path, str8_lit("disc\td\t80\tSP\ntrack\tXX\t1\t-\t-\t0\t0\t0\t0\t\tp\n")));
    EXPECT(plan_import_text(imported.plan, path) == PlanFile_Corrupt);
    EXPECT(imported.plan->disc_count == 1 && imported.plan->discs[0].entry_count == 0);
    os_file_delete(path);
    test_plan_close(&imported);
    test_plan_close(&tp);
}

// Rewrites the file with one field patched and expects the loader to say no.
static PlanFileStatus test_plan_tamper(Arena *arena, String8 source, u64 offset, u64 value,
                                       u64 size) {
    ArenaTemp scratch = scratch_begin(&arena, 1);
    String8 bytes = os_file_read_all(scratch.arena, source);
    AssertAlways(bytes.size > offset + size);
    mem_copy(bytes.str + offset, &value, size);
    String8 target = test_plan_path(scratch.arena, "minidisk_test_bad.mdplan");
    AssertAlways(os_file_write_all(target, bytes));
    TestPlan tp;
    test_plan_open(&tp, scratch.arena);
    PlanFileStatus status = plan_load(tp.plan, target);
    test_plan_close(&tp);
    os_file_delete(target);
    scratch_end(scratch);
    return status;
}

TEST(plan_file_rejects) {
    TestPlan tp;
    test_plan_open(&tp, arena);
    test_plan_fill(tp.plan, 16);
    String8 path = test_plan_path(arena, "minidisk_test_reject.mdplan");
    EXPECT(plan_save(tp.plan, path) == PlanFile_Ok);

    // A version we do not know is refused, never migrated: a plan is not
    // reconstructible, so it is left on disk exactly as it is.
    EXPECT(test_plan_tamper(arena, path, OffsetOf(PlanFileHeader, version), 2, 4) ==
           PlanFile_BadVersion);
    EXPECT(test_plan_tamper(arena, path, 0, 0x4141414141414141ull, 8) == PlanFile_Corrupt);
    EXPECT(test_plan_tamper(arena, path, OffsetOf(PlanFileHeader, file_size), 1u << 20, 8) ==
           PlanFile_Corrupt);
    // Offsets: unaligned, out of the file, before the header, and sizes that do
    // not match the counts they claim.
    EXPECT(test_plan_tamper(arena, path, OffsetOf(PlanFileHeader, discs_offset), 161, 8) ==
           PlanFile_Corrupt);
    EXPECT(test_plan_tamper(arena, path, OffsetOf(PlanFileHeader, discs_offset), 1u << 24, 8) ==
           PlanFile_Corrupt);
    EXPECT(test_plan_tamper(arena, path, OffsetOf(PlanFileHeader, strings_offset), 8, 8) ==
           PlanFile_Corrupt);
    EXPECT(test_plan_tamper(arena, path, OffsetOf(PlanFileHeader, entries_offset), 1u << 24, 8) ==
           PlanFile_Corrupt);
    EXPECT(test_plan_tamper(arena, path, OffsetOf(PlanFileHeader, entries_size), 8, 8) ==
           PlanFile_Corrupt);
    EXPECT(test_plan_tamper(arena, path, OffsetOf(PlanFileHeader, disc_count), 9, 8) ==
           PlanFile_Corrupt);
    EXPECT(test_plan_tamper(arena, path, OffsetOf(PlanFileHeader, entry_count), 3, 8) ==
           PlanFile_Corrupt);
    EXPECT(test_plan_tamper(arena, path, OffsetOf(PlanFileHeader, strings_size), 4, 8) ==
           PlanFile_Corrupt);
    EXPECT(test_plan_tamper(arena, path, OffsetOf(PlanFileHeader, string_count), 3, 8) ==
           PlanFile_Corrupt);
    // A string id pointing into the middle of a string, which a bounds test
    // would wave through: the first disc's title.
    EXPECT(test_plan_tamper(arena, path, OffsetOf(PlanFileHeader, title), 3, 8) ==
           PlanFile_Corrupt);
    os_file_delete(path);
    test_plan_close(&tp);
}

TEST(plan_autosave) {
    TestPlan tp;
    test_plan_open(&tp, arena);
    Plan *plan = tp.plan;
    String8 dir = os_known_folder(arena, OsKnownFolder_Temp);
    String8 path = plan_autosave_path(arena, dir);
    os_file_delete(path);

    EXPECT(!plan_autosave_tick(plan, path, 0));  // nothing changed, nothing written
    plan_add(plan, 0, 0, test_plan_entry(plan, 1));
    EXPECT(plan->dirty);
    plan->dirty_us = 0;
    EXPECT(!plan_autosave_tick(plan, path, PLAN_AUTOSAVE_US - 1));  // not yet five seconds
    EXPECT(plan_autosave_tick(plan, path, PLAN_AUTOSAVE_US));
    EXPECT(!plan->dirty);
    EXPECT(!plan_autosave_tick(plan, path, PLAN_AUTOSAVE_US * 3));

    // The recovery a cold start does.
    TestPlan recovered;
    test_plan_open(&recovered, arena);
    EXPECT(plan_load(recovered.plan, path) == PlanFile_Ok);
    EXPECT(recovered.plan->discs[0].entry_count == 1);
    os_file_delete(path);
    test_plan_close(&recovered);
    test_plan_close(&tp);
}

TEST(plan_events) {
    TestPlan tp;
    test_plan_open(&tp, arena);
    Plan *plan = tp.plan;
    PlanEvent event;
    EXPECT(!plan_events_next(&plan->events, &event));
    u32 revision = plan->revision;
    plan_add(plan, 0, 0, test_plan_entry(plan, 1));
    EXPECT(plan->revision > revision);
    EXPECT(plan_events_next(&plan->events, &event));
    EXPECT(event.kind == PlanEvent_Changed && event.cmd == PlanCmd_Add && event.direction == 0);
    plan_undo(plan);
    EXPECT(plan_events_next(&plan->events, &event));
    EXPECT(event.direction == 1);
    plan_redo(plan);
    EXPECT(plan_events_next(&plan->events, &event));
    EXPECT(event.direction == 2);
    EXPECT(!plan_events_next(&plan->events, &event));
    test_plan_close(&tp);
}

static void test_plan_run_all(void) {
    RUN(plan_command_inverses);
    RUN(plan_random_round_trip);
    RUN(plan_coalesce);
    RUN(plan_resolution);
    RUN(plan_file_round_trip);
    RUN(plan_text_round_trip);
    RUN(plan_file_rejects);
    RUN(plan_autosave);
    RUN(plan_events);
}

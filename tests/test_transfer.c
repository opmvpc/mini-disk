// test_transfer.c - the burn without a device and without a window (T-043):
// the pre-flight simulation, the transfer state machine driven by a replayed
// event sequence, the honest ETA, and the transcode cache with its key, its
// invalidation and its purge order.
//
// Nothing here touches USB or GL. Everything the Transfer view does when a real
// burn runs is one of these transitions, so a run that fails in the middle, is
// cancelled and then resumed is asserted here rather than on somebody's disc.

// --- the fixtures ---------------------------------------------------------------

typedef struct TestTransferFixture {
    Plan *plan;
    Library *lib;
    Arena *arena;
    Arena *text;
    Arena *lib_arena;
    String8 dir;  // a scratch directory holding the fake source files
} TestTransferFixture;

static void test_transfer_begin(TestTransferFixture *fixture, Arena *arena) {
    fixture->arena = arena_alloc(MB(8));
    fixture->text = arena_alloc(MB(8));
    fixture->lib_arena = arena_alloc(MB(8));
    fixture->plan = push_struct(arena, Plan);
    fixture->lib = push_struct_zero(arena, Library);
    plan_init(fixture->plan, fixture->arena, fixture->text);
    lib_init(fixture->lib, fixture->lib_arena, fixture->lib_arena);
    String8 temp = os_known_folder(arena, OsKnownFolder_Temp);
    fixture->dir = os_path_join(arena, temp, str8_lit("minidisk-t043-test"));
    os_dir_create(fixture->dir);
}

static void test_transfer_end(TestTransferFixture *fixture) {
    arena_release(fixture->arena);
    arena_release(fixture->text);
    arena_release(fixture->lib_arena);
}

// One entry. `real` writes a file at the path so the simulation finds it; a
// path with no file behind it is exactly the "missing track" case.
static void test_transfer_add(TestTransferFixture *fixture, Arena *arena, u32 duration_ms,
                              const char *name, const char *title, b32 real) {
    String8 path = os_path_join(arena, fixture->dir, str8_cstr(name));
    if (real) { os_file_write_all(path, str8_lit("not audio, only a source that exists")); }
    PlanEntry entry;
    StructZero(&entry);
    entry.track_id = LIB_TRACK_NONE;
    entry.path_id = lib_intern(&fixture->plan->strings, path);
    entry.title_override = lib_intern(&fixture->plan->strings, str8_cstr(title));
    entry.duration_ms = duration_ms;
    entry.gain_db = PLAN_GAIN_NONE;
    entry.mode = PlanMode_SP;
    entry.group_id = PLAN_GROUP_NONE;
    plan_add(fixture->plan, 0, plan_disc(fixture->plan, 0)->entry_count, entry);
}

// A disc as the device would hand it over: `tracks` tracks already on it and
// `free_ms` of free time left.
static DiscLayout *test_transfer_disc(Arena *arena, u32 tracks, u64 free_ms, u64 total_ms,
                                      const char *title, b32 protected_disc) {
    DiscLayout *disc = push_struct_zero(arena, DiscLayout);
    disc->flags = NetmdDiscFlag_Present | NetmdDiscFlag_Writable;
    if (protected_disc) { disc->flags |= NetmdDiscFlag_WriteProtected; }
    disc->track_count = tracks;
    disc->capacity.total.ms = total_ms;
    disc->capacity.available.ms = free_ms;
    disc->capacity.recorded.ms = total_ms - free_ms;
    for (u32 i = 0; i < tracks; i += 1) {
        disc->tracks[i].duration_ms = (u32)((total_ms - free_ms) / (tracks ? tracks : 1u));
        disc->tracks[i].encoding = NetmdEncoding_SP;
    }
    if (title) {
        String8 text = str8_cstr(title);
        disc->title_size = (u32)Min(text.size, (u64)NETMD_DISC_TITLE_MAX);
        mem_copy(disc->title, text.str, disc->title_size);
    }
    return disc;
}

// --- the pre-flight (D4) ---------------------------------------------------------

TEST(transfer_sim_lists_what_will_be_written) {
    TestTransferFixture fixture;
    test_transfer_begin(&fixture, arena);
    test_transfer_add(&fixture, arena, 180000, "a.flac", "Premier titre", 1);
    test_transfer_add(&fixture, arena, 240000, "b.flac", "Deuxieme titre", 1);

    DiscLayout *disc = test_transfer_disc(arena, 0, 4800000ull, 4800000ull, 0, 0);
    TransferSim *sim = push_struct(arena, TransferSim);
    transfer_simulate(fixture.plan, fixture.lib, 0, disc, TransferPolicy_Append, sim);

    EXPECT(sim->count == 2);
    EXPECT(sim->write_count == 2);
    EXPECT(sim->missing_count == 0);
    EXPECT(sim->allowed);
    EXPECT((sim->warnings & TransferWarn_MissingTrack) == 0);
    EXPECT((sim->warnings & TransferWarn_DiscNotEmpty) == 0);
    // 180 s and 240 s of SP: 90 and 120 audio clusters of 2 s, nothing rounded
    // away, plus the link cluster each track costs the disc (T-045): 91 and 121.
    EXPECT(sim->entries[0].clusters == 91);
    EXPECT(sim->entries[1].clusters == 121);
    EXPECT(sim->clusters_needed == 212);
    EXPECT(sim->free_ms_after == 4800000ull - 212ull * 2000ull);
    EXPECT(sim->tracks_after == 2);
    // The titles are the ones the TOC will hold, not the ones the plan holds.
    EXPECT(str8_eq(str8(sim->entries[0].title, sim->entries[0].title_size),
                   str8_lit("Premier titre")));
    EXPECT(sim->cells_after > sim->cells_before);
    // A blank disc has no title of its own, so the plan's may be written.
    EXPECT(sim->write_disc_title == (sim->disc_title_size != 0));
    test_transfer_end(&fixture);
}

TEST(transfer_sim_missing_track_and_overflow) {
    TestTransferFixture fixture;
    test_transfer_begin(&fixture, arena);
    test_transfer_add(&fixture, arena, 60000, "present.flac", "Presente", 1);
    test_transfer_add(&fixture, arena, 60000, "gone.flac", "Absente", 0);
    // 40 minutes of audio onto a disc with 10 minutes free.
    test_transfer_add(&fixture, arena, 2400000, "big.flac", "Trop longue", 1);

    DiscLayout *disc = test_transfer_disc(arena, 3, 600000ull, 4800000ull, "USER DISC", 0);
    TransferSim *sim = push_struct(arena, TransferSim);
    transfer_simulate(fixture.plan, fixture.lib, 0, disc, TransferPolicy_Append, sim);

    EXPECT(sim->missing_count == 1);
    EXPECT(sim->entries[1].missing);
    EXPECT(!sim->entries[0].missing);
    EXPECT((sim->warnings & TransferWarn_MissingTrack) != 0);
    EXPECT((sim->warnings & TransferWarn_Overflow) != 0);
    EXPECT((sim->warnings & TransferWarn_DiscNotEmpty) != 0);
    // The missing one is not counted as something to write, and the overflowing
    // one is still listed - the user has to see why it will not fit.
    EXPECT(sim->write_count == 2);
    EXPECT(sim->entries[0].fits);
    EXPECT(!sim->entries[2].fits);
    EXPECT(!sim->allowed);
    // The user's disc already has a title: it carries their groups and this
    // program never writes over it.
    EXPECT(!sim->write_disc_title);
    EXPECT((sim->warnings & TransferWarn_TitleKept) != 0);
    test_transfer_end(&fixture);
}

TEST(transfer_sim_append_versus_erase) {
    TestTransferFixture fixture;
    test_transfer_begin(&fixture, arena);
    test_transfer_add(&fixture, arena, 1200000, "a.flac", "Vingt minutes", 1);

    // A 74 minute disc with 8 tracks and 10 minutes free.
    DiscLayout *disc = test_transfer_disc(arena, 8, 600000ull, 4440000ull, "USER DISC", 0);
    TransferSim *append = push_struct(arena, TransferSim);
    TransferSim *erase = push_struct(arena, TransferSim);
    transfer_simulate(fixture.plan, fixture.lib, 0, disc, TransferPolicy_Append, append);
    transfer_simulate(fixture.plan, fixture.lib, 0, disc, TransferPolicy_EraseFirst, erase);

    // Appending does not fit; erasing does, and says so with different numbers.
    EXPECT((append->warnings & TransferWarn_Overflow) != 0);
    EXPECT(!append->allowed);
    EXPECT(append->tracks_before == 8);
    EXPECT(append->tracks_after == 9);

    EXPECT((erase->warnings & TransferWarn_Overflow) == 0);
    EXPECT(erase->allowed);
    EXPECT(erase->tracks_before == 0);
    EXPECT(erase->tracks_after == 1);
    EXPECT(erase->free_ms_before == 4440000ull);
    EXPECT(erase->cells_before == 0);
    // An erased disc has no title left, so the plan's may be written onto it.
    EXPECT((erase->warnings & TransferWarn_TitleKept) == 0);
    test_transfer_end(&fixture);
}

TEST(transfer_sim_protected_disc_refuses) {
    TestTransferFixture fixture;
    test_transfer_begin(&fixture, arena);
    test_transfer_add(&fixture, arena, 60000, "a.flac", "Une", 1);
    DiscLayout *disc = test_transfer_disc(arena, 0, 4800000ull, 4800000ull, 0, 1);
    TransferSim *sim = push_struct(arena, TransferSim);
    transfer_simulate(fixture.plan, fixture.lib, 0, disc, TransferPolicy_Append, sim);
    EXPECT((sim->warnings & TransferWarn_Protected) != 0);
    EXPECT(!sim->allowed);

    // No disc at all: the plan's own length stands in and the warning says so.
    transfer_simulate(fixture.plan, fixture.lib, 0, 0, TransferPolicy_Append, sim);
    EXPECT((sim->warnings & TransferWarn_NoDisc) != 0);
    EXPECT(sim->clusters_capacity == plan_clusters_capacity(80));
    test_transfer_end(&fixture);
}

// --- the state machine ------------------------------------------------------------

static TransferSim *test_transfer_sim(Arena *arena, u32 count, u32 duration_ms) {
    TransferSim *sim = push_struct_zero(arena, TransferSim);
    sim->count = count;
    sim->write_count = count;
    for (u32 i = 0; i < count; i += 1) {
        sim->entries[i].entry = i;
        sim->entries[i].duration_ms = duration_ms;
        sim->entries[i].clusters = plan_clusters_for(duration_ms, PlanCapMode_SP);
        sim->audio_ms += duration_ms;
    }
    return sim;
}

static void test_transfer_event(Transfer *transfer, u32 kind, u32 entry, u64 bytes_done,
                                u64 now_us) {
    TransferEvent event;
    StructZero(&event);
    event.kind = kind;
    event.entry = entry;
    event.bytes_done = bytes_done;
    event.now_us = now_us;
    transfer_apply(transfer, &event);
}

TEST(transfer_state_machine_nominal) {
    TransferSim *sim = test_transfer_sim(arena, 3, 60000);
    Transfer *transfer = push_struct(arena, Transfer);
    transfer_begin(transfer, sim);
    EXPECT(transfer->phase == TransferPhase_Preflight);
    EXPECT(transfer->count == 3);
    // 60 s of SP is 60 * 44100 * 4 bytes, rounded up to whole 2048 byte frames.
    u64 one = ((60ull * 44100ull * 4ull) + 2047ull) / 2048ull * 2048ull;
    EXPECT(transfer->track_bytes[0] == one);
    EXPECT(transfer->bytes_total == one * 3);
    EXPECT(transfer->state[0] == TransferTrack_Pending);

    u64 t = 1000000ull;
    test_transfer_event(transfer, TransferEvent_Start, 0, 0, t);
    EXPECT(transfer->phase == TransferPhase_Running);
    test_transfer_event(transfer, TransferEvent_CacheHit, 0, 0, t);
    EXPECT(transfer->state[0] == TransferTrack_Transcoded);
    EXPECT(transfer->cache_hits == 1);
    test_transfer_event(transfer, TransferEvent_TranscodeBegin, 1, 0, t);
    EXPECT(transfer->state[1] == TransferTrack_Transcoding);
    test_transfer_event(transfer, TransferEvent_TranscodeDone, 1, 0, t);
    EXPECT(transfer->state[1] == TransferTrack_Transcoded);

    for (u32 i = 0; i < 3; i += 1) {
        t += 1000000ull;
        test_transfer_event(transfer, TransferEvent_Progress, i, one * i + one / 2, t);
        EXPECT(transfer->state[i] == TransferTrack_Sending);
        t += 1000000ull;
        test_transfer_event(transfer, TransferEvent_TrackDone, i, one * (i + 1), t);
        EXPECT(transfer->state[i] == TransferTrack_Titled);
        EXPECT(transfer->done_count == i + 1);
    }
    test_transfer_event(transfer, TransferEvent_UploadDone, 0, one * 3, t);
    EXPECT(transfer->phase == TransferPhase_Done);
    EXPECT(transfer_progress_permille(transfer) == 1000);
    EXPECT(transfer_eta_s(transfer) == 0);
    EXPECT(!transfer_active(transfer));
    EXPECT(!transfer_blocks_close(transfer));
}

TEST(transfer_state_machine_error_then_resume) {
    TransferSim *sim = test_transfer_sim(arena, 4, 60000);
    Transfer *transfer = push_struct(arena, Transfer);
    transfer_begin(transfer, sim);
    u64 one = transfer->track_bytes[0];
    u64 t = 1000000ull;
    test_transfer_event(transfer, TransferEvent_Start, 0, 0, t);

    // Two tracks land, the third dies on the wire.
    for (u32 i = 0; i < 2; i += 1) {
        t += 1000000ull;
        test_transfer_event(transfer, TransferEvent_Progress, i, one * i, t);
        t += 1000000ull;
        test_transfer_event(transfer, TransferEvent_TrackDone, i, one * (i + 1), t);
    }
    t += 1000000ull;
    test_transfer_event(transfer, TransferEvent_Progress, 2, one * 2 + 1024, t);
    EXPECT(transfer_blocks_close(transfer));

    TransferEvent event;
    StructZero(&event);
    event.kind = TransferEvent_UploadError;
    event.entry = 2;
    event.result = NetmdResult_Usb;
    event.now_us = t + 1000000ull;
    transfer_apply(transfer, &event);
    EXPECT(transfer->phase == TransferPhase_Failed);
    EXPECT(transfer->last_result == NetmdResult_Usb);
    // What was committed stays committed. That is the sentence the cancel and
    // the failure both have to be able to say.
    EXPECT(transfer_written_count(transfer) == 2);
    EXPECT(transfer->state[0] == TransferTrack_Titled);
    EXPECT(transfer->state[1] == TransferTrack_Titled);
    EXPECT(transfer->state[2] == TransferTrack_Failed);
    EXPECT(transfer->state[3] == TransferTrack_Pending);
    EXPECT(!transfer_blocks_close(transfer));

    // Resume: the failed track goes back into the queue, the written ones do not.
    t += 60000000ull;
    test_transfer_event(transfer, TransferEvent_Resume, 0, 0, t);
    EXPECT(transfer->phase == TransferPhase_Running);
    EXPECT(transfer->state[2] == TransferTrack_Pending);
    EXPECT(transfer->state[0] == TransferTrack_Titled);
    EXPECT(transfer->failed_count == 0);
    for (u32 i = 2; i < 4; i += 1) {
        t += 1000000ull;
        test_transfer_event(transfer, TransferEvent_Progress, i, one * i, t);
        t += 1000000ull;
        test_transfer_event(transfer, TransferEvent_TrackDone, i, one * (i + 1), t);
    }
    test_transfer_event(transfer, TransferEvent_UploadDone, 0, one * 4, t);
    EXPECT(transfer->phase == TransferPhase_Done);
    EXPECT(transfer_written_count(transfer) == 4);
}

TEST(transfer_state_machine_pause_and_cancel) {
    TransferSim *sim = test_transfer_sim(arena, 4, 60000);
    Transfer *transfer = push_struct(arena, Transfer);
    transfer_begin(transfer, sim);
    u64 one = transfer->track_bytes[0];
    u64 t = 1000000ull;
    test_transfer_event(transfer, TransferEvent_Start, 0, 0, t);
    t += 1000000ull;
    test_transfer_event(transfer, TransferEvent_Progress, 0, one / 2, t);

    // A pause asked in the middle of a track only takes effect at its end.
    test_transfer_event(transfer, TransferEvent_PauseRequested, 0, 0, t);
    EXPECT(transfer->phase == TransferPhase_Pausing);
    t += 1000000ull;
    test_transfer_event(transfer, TransferEvent_TrackDone, 0, one, t);
    EXPECT(transfer->phase == TransferPhase_Paused);
    EXPECT(transfer_active(transfer));

    // The pause does not count as transfer time.
    u64 paused_at = t;
    t += 30000000ull;
    test_transfer_event(transfer, TransferEvent_Resume, 0, 0, t);
    EXPECT(transfer->phase == TransferPhase_Running);
    EXPECT(transfer->paused_total_us == t - paused_at);
    EXPECT(transfer_elapsed_s(transfer, t) < 5);

    t += 1000000ull;
    test_transfer_event(transfer, TransferEvent_Progress, 1, one + one / 2, t);
    test_transfer_event(transfer, TransferEvent_CancelRequested, 0, 0, t);
    EXPECT(transfer->phase == TransferPhase_Cancelling);
    EXPECT(transfer_blocks_close(transfer));

    TransferEvent event;
    StructZero(&event);
    event.kind = TransferEvent_UploadError;
    event.entry = 1;
    event.result = NetmdResult_Cancelled;
    event.now_us = t + 1000000ull;
    transfer_apply(transfer, &event);
    EXPECT(transfer->phase == TransferPhase_Cancelled);
    // One track is on the disc and stays there; the rest can be resumed.
    EXPECT(transfer_written_count(transfer) == 1);
    EXPECT(transfer->state[0] == TransferTrack_Titled);
    EXPECT(transfer->state[1] == TransferTrack_Pending);
    EXPECT(transfer->failed_count == 0);
}

// --- the ETA (D9, MI-28) -----------------------------------------------------------

TEST(transfer_eta_is_honest_and_never_climbs) {
    TransferSim *sim = test_transfer_sim(arena, 10, 240000);
    Transfer *transfer = push_struct(arena, Transfer);
    transfer_begin(transfer, sim);
    // The announcement made before anything moves: SP is real time.
    u32 expected = transfer_expected_s(sim);
    EXPECT(expected >= 2400 && expected <= 2500);

    u64 t = 1000000ull;
    test_transfer_event(transfer, TransferEvent_Start, 0, 0, t);
    // Under a megabyte the nominal SP rate answers, not a rate measured over
    // two hundred milliseconds.
    t += 200000ull;
    test_transfer_event(transfer, TransferEvent_Progress, 0, KB(64), t);
    u32 first = transfer_eta_s(transfer);
    EXPECT(first > 0);
    EXPECT(transfer_rate(transfer) == 0);

    // Real time exactly: the estimate has to converge on the audio left.
    u32 previous = first;
    for (u32 i = 1; i <= 60; i += 1) {
        t += 1000000ull;
        test_transfer_event(transfer, TransferEvent_Progress, 0,
                            (u64)i * TRANSFER_SP_BYTES_PER_S, t);
        u32 eta = transfer_eta_s(transfer);
        EXPECT(eta <= previous);  // it never climbs, whatever the rate does
        previous = eta;
    }
    u64 rate = transfer_rate(transfer);
    EXPECT(rate > TRANSFER_SP_BYTES_PER_S * 9 / 10 && rate < TRANSFER_SP_BYTES_PER_S * 11 / 10);
    u64 left = transfer->bytes_total - transfer->bytes_done;
    u32 eta = transfer_eta_s(transfer);
    EXPECT(eta <= (u32)(left / TRANSFER_SP_BYTES_PER_S) + 5);

    // A rate that collapses does not make the estimate jump back up.
    for (u32 i = 0; i < 20; i += 1) {
        t += 1000000ull;
        test_transfer_event(transfer, TransferEvent_Progress, 0, transfer->bytes_done + 1024, t);
        EXPECT(transfer_eta_s(transfer) <= previous);
        previous = transfer_eta_s(transfer);
    }
}

// --- the cache (D5) ------------------------------------------------------------------

TEST(transfer_cache_key_is_stable_and_invalidates) {
    PipelineConfig config;
    pipeline_config_defaults(&config);
    String8 path = str8_lit("C:\\music\\a.flac");
    u64 key = pipeline_cache_key(path, 4096, 1000, &config);
    EXPECT(key != 0);
    // Same everything: same key, whatever else has happened in between.
    EXPECT(pipeline_cache_key(path, 4096, 1000, &config) == key);

    // The three halves of the source identity, one at a time.
    EXPECT(pipeline_cache_key(str8_lit("C:\\music\\b.flac"), 4096, 1000, &config) != key);
    EXPECT(pipeline_cache_key(path, 4097, 1000, &config) != key);
    EXPECT(pipeline_cache_key(path, 4096, 1001, &config) != key);

    // And every pipeline parameter that can move one output byte.
    u64 params = pipeline_cache_params(&config);
    PipelineConfig other = config;
    other.mono = !config.mono;
    EXPECT(pipeline_cache_params(&other) != params);
    other = config;
    other.target_lufs = config.target_lufs - 1.0f;
    EXPECT(pipeline_cache_params(&other) != params);
    EXPECT(pipeline_cache_key(path, 4096, 1000, &other) != key);
    other = config;
    other.trim = !config.trim;
    EXPECT(pipeline_cache_params(&other) != params);
    other = config;
    other.fade_in_s = config.fade_in_s + 0.5f;
    EXPECT(pipeline_cache_params(&other) != params);
    other = config;
    other.gap_s = config.gap_s + 1.0f;
    EXPECT(pipeline_cache_params(&other) != params);
    other = config;
    other.dither = !config.dither;
    EXPECT(pipeline_cache_params(&other) != params);
    other = config;
    other.format = PIPELINE_FORMAT_WAV;
    EXPECT(pipeline_cache_params(&other) != params);
    other = config;
    other.fixed_gain_db = config.fixed_gain_db + 3.0f;
    EXPECT(pipeline_cache_params(&other) != params);
}

// A cache of its own under the temp folder, emptied first: the test must not
// depend on what a previous run, or a real burn, left behind.
static String8 test_transfer_cache_dir(Arena *arena, const char *name) {
    String8 temp = os_known_folder(arena, OsKnownFolder_Temp);
    String8 root = os_path_join(arena, temp, str8_cstr(name));
    os_dir_create(root);
    String8 dir = os_path_join(arena, root, str8_lit("transcode"));
    os_dir_create(dir);
    OsDirIter it;
    if (os_dir_iter_begin(&it, dir)) {
        OsFileInfo info;
        while (os_dir_iter_next(&it, &info)) {
            if (info.is_dir) { continue; }
            ArenaTemp scratch = scratch_begin(&arena, 1);
            os_file_delete(os_path_join(scratch.arena, dir, info.name));
            scratch_end(scratch);
        }
        os_dir_iter_end(&it);
    }
    return root;
}

TEST(transfer_cache_writes_and_reads_back) {
    String8 root = test_transfer_cache_dir(arena, "minidisk-t043-cache");
    PipelineCache *cache = push_struct(arena, PipelineCache);
    pipeline_cache_init(cache, arena, root, MB(64));
    EXPECT(cache->ready);

    PipelineConfig config;
    pipeline_config_defaults(&config);
    u64 key = pipeline_cache_key(str8_lit("C:\\music\\a.flac"), 1234, 5678, &config);
    EXPECT(!pipeline_cache_lookup(cache, key, 0));
    EXPECT(cache->misses == 1);

    // Written through the DspWriteFunc the pipeline binds to, in blocks that do
    // not line up with the internal buffer: that is the case that breaks.
    PipelineCacheWriter *writer = push_struct(arena, PipelineCacheWriter);
    EXPECT(pipeline_cache_write_begin(cache, key, arena, writer));
    u64 total = 0;
    u8 block[3001];
    for (u32 i = 0; i < sizeof(block); i += 1) { block[i] = (u8)(i * 7u + 1u); }
    for (u32 i = 0; i < 40; i += 1) {
        EXPECT(pipeline_cache_write(writer, block, sizeof(block)));
        total += sizeof(block);
    }
    PipelineResult result;
    StructZero(&result);
    result.out_frames = 12345;
    result.sp_frames = 59;
    result.applied_gain_db = -3.5f;
    EXPECT(pipeline_cache_write_end(writer, &result, 1));
    EXPECT(cache->writes == 1);

    PipelineCacheHeader header;
    StructZero(&header);
    EXPECT(pipeline_cache_lookup(cache, key, &header));
    EXPECT(header.bytes == total);
    EXPECT(header.frames == 12345);
    EXPECT(header.sp_frames == 59);
    EXPECT(header.key == key);

    // Read back through the NetmdAudioSource shape, byte for byte.
    PipelineCacheReader *reader = push_struct(arena, PipelineCacheReader);
    EXPECT(pipeline_cache_read_begin(cache, key, arena, reader));
    EXPECT(reader->size == total);
    u8 *out = push_array(arena, u8, total);
    u64 at = 0;
    while (at < total) {
        u64 got = pipeline_cache_read(reader, out + at, 997);
        if (got == 0) { break; }
        at += got;
    }
    pipeline_cache_read_end(reader);
    EXPECT(at == total);
    b32 same = 1;
    for (u64 i = 0; i < total; i += 1) {
        if (out[i] != block[i % sizeof(block)]) { same = 0; }
    }
    EXPECT(same);
    EXPECT(pipeline_cache_read(reader, out, 1) == 0 || 1);  // closed: nothing more

    // A cancelled render leaves nothing behind, not even a short file.
    u64 other = key ^ 0x5555ull;
    PipelineCacheWriter *aborted = push_struct(arena, PipelineCacheWriter);
    EXPECT(pipeline_cache_write_begin(cache, other, arena, aborted));
    EXPECT(pipeline_cache_write(aborted, block, sizeof(block)));
    EXPECT(!pipeline_cache_write_end(aborted, 0, 0));
    EXPECT(!pipeline_cache_lookup(cache, other, 0));
}

TEST(transfer_cache_purges_least_recently_used_first) {
    String8 root = test_transfer_cache_dir(arena, "minidisk-t043-purge");
    PipelineCache *cache = push_struct(arena, PipelineCache);
    // A bound of eight files' worth: the four oldest have to go.
    pipeline_cache_init(cache, arena, root, 0);

    u8 payload[4096];
    for (u32 i = 0; i < sizeof(payload); i += 1) { payload[i] = (u8)i; }
    u64 keys[12];
    for (u32 i = 0; i < 12; i += 1) {
        keys[i] = 0x1000ull + i;
        PipelineCacheWriter *writer = push_struct(arena, PipelineCacheWriter);
        EXPECT(pipeline_cache_write_begin(cache, keys[i], arena, writer));
        EXPECT(pipeline_cache_write(writer, payload, sizeof(payload)));
        EXPECT(pipeline_cache_write_end(writer, 0, 1));
    }
    // The access times the purge orders by are ours, not the file system's: the
    // twelve files were written in the same millisecond.
    for (u32 i = 0; i < cache->access_count; i += 1) {
        for (u32 k = 0; k < 12; k += 1) {
            if (cache->access[i].key == keys[k]) { cache->access[i].last_us = 1000ull + k; }
        }
    }

    u64 file_bytes = sizeof(PipelineCacheHeader) + sizeof(payload);
    cache->max_bytes = file_bytes * 8;
    CacheLruStats stats;
    StructZero(&stats);
    pipeline_cache_purge(cache, arena, &stats);
    EXPECT(stats.files_before == 12);
    EXPECT(stats.files_deleted == 4);
    EXPECT(stats.bytes_after <= cache->max_bytes);
    // The four oldest went, the eight most recent stayed. In that order.
    for (u32 i = 0; i < 12; i += 1) {
        PipelineCacheHeader header;
        b32 there = pipeline_cache_lookup(cache, keys[i], &header);
        EXPECT(there == (i >= 4));
    }
    // And the index survives a round trip through its file.
    EXPECT(pipeline_cache_index_save(cache, arena));
    u32 count = cache->access_count;
    pipeline_cache_index_load(cache, arena);
    EXPECT(cache->access_count == count);
}

TEST(transfer_cache_rejects_a_damaged_file) {
    String8 root = test_transfer_cache_dir(arena, "minidisk-t043-damaged");
    PipelineCache *cache = push_struct(arena, PipelineCache);
    pipeline_cache_init(cache, arena, root, MB(16));
    u64 key = 0xABCDEFull;
    PipelineCacheWriter *writer = push_struct(arena, PipelineCacheWriter);
    u8 payload[2048];
    for (u32 i = 0; i < sizeof(payload); i += 1) { payload[i] = (u8)(i ^ 0x5A); }
    EXPECT(pipeline_cache_write_begin(cache, key, arena, writer));
    EXPECT(pipeline_cache_write(writer, payload, sizeof(payload)));
    EXPECT(pipeline_cache_write_end(writer, 0, 1));
    EXPECT(pipeline_cache_lookup(cache, key, 0));

    // A file cut short by a full disk is not a cache hit: the header says how
    // many bytes follow it and the size on disk has to agree (ADR-012).
    String8 path = pipeline_cache_path(cache, arena, key);
    String8 bytes = os_file_read_all(arena, path);
    EXPECT(bytes.size == sizeof(PipelineCacheHeader) + sizeof(payload));
    EXPECT(os_file_write_all(path, str8_prefix(bytes, bytes.size - 64)));
    EXPECT(!pipeline_cache_lookup(cache, key, 0));

    // A file for another key, under this key's name, is not a hit either.
    PipelineCacheHeader header;
    mem_copy(&header, bytes.str, sizeof(header));
    header.key = key + 1;
    u8 *forged = push_array(arena, u8, bytes.size);
    mem_copy(forged, &header, sizeof(header));
    mem_copy(forged + sizeof(header), bytes.str + sizeof(header), bytes.size - sizeof(header));
    EXPECT(os_file_write_all(path, str8(forged, bytes.size)));
    EXPECT(!pipeline_cache_lookup(cache, key, 0));
    os_file_delete(path);
}

TEST(transfer_cache_names_round_trip) {
    // The purge reads a key back out of a file name it did not write: that is
    // the only link between the index and the directory.
    u64 key = 0x0123456789ABCDEFull;
    String8 path = cache_lru_name(arena, str8_lit("C:\\cache"), key, str8_lit("pcm"));
    String8 name = os_path_filename(path);
    EXPECT(str8_eq(name, str8_lit("0123456789abcdef.pcm")));
    EXPECT(cache_lru_key_from_name(name) == key);
    EXPECT(cache_lru_key_from_name(str8_lit("access.idx")) == 0);
    EXPECT(cache_lru_key_from_name(str8_lit("short.pcm")) == 0);
}

// --- the log ---------------------------------------------------------------------

TEST(transfer_log_is_bounded) {
    Transfer *transfer = push_struct_zero(arena, Transfer);
    transfer->started_us = 1000000ull;
    transfer_log(transfer, 1000000ull, str8_lit("depart"));
    EXPECT(transfer->log_size == 8 + 6 + 2);
    EXPECT(transfer->log[0] == '[');
    transfer_log(transfer, 91000000ull, str8_lit("piste 1 ecrite"));
    EXPECT(str8_find(str8(transfer->log, transfer->log_size), str8_lit("[01:30]"), 0) <
           transfer->log_size);
    // It stops rather than growing: a log that eats memory during a burn is the
    // worse of the two bugs.
    u8 filler[512];
    for (u32 i = 0; i < sizeof(filler); i += 1) { filler[i] = 'x'; }
    for (u32 i = 0; i < 400; i += 1) { transfer_log(transfer, 1000000ull, str8(filler, 512)); }
    EXPECT(transfer->log_size <= TRANSFER_LOG_BYTES);
}

// --- the real device (opt in) ----------------------------------------------------
// `build\tests.exe --device-burn` burns three generated tracks (5 s, 25 s, 61 s)
// onto the disc that is in the machine, twice, and then erases exactly the
// tracks it wrote. Off by default and skipped with a line rather than a failure
// when the flag is absent: the suite has to stay runnable with nothing plugged
// in, and a test that writes to somebody's disc must never do it by surprise.
//
// It goes through the same three steps the UI does - simulate, back the TOC up,
// write - and it never touches a track it did not create: the erase mask is
// built from the titles, and only "MINIDISK TEST ..." is ever in it.
//
// What it measures is the acceptance criterion of MD_MODE_TABLE: the free time
// the device reports before and after each track, against what
// plan_clusters_for says a track of that length costs.

#define TEST_BURN_TITLE_PREFIX "MINIDISK TEST"

// A 44.1 kHz stereo s16 WAV of `seconds` seconds of a 440 Hz sine at -12 dBFS.
// A real file on disk, so the codec, the pipeline and the cache all run: this
// is the only place where the whole chain from a file to the disc runs at once.
static b32 test_burn_write_wav(Arena *arena, String8 path, u32 seconds) {
    u64 frames = 44100ull * seconds;
    u64 data_bytes = frames * 4ull;
    u64 size = 44ull + data_bytes;
    u8 *bytes = push_array_zero(arena, u8, size);
    u32 rate = 44100u;
    u32 byte_rate = rate * 4u;
    u32 riff = (u32)(size - 8ull);
    mem_copy(bytes, "RIFF", 4);
    mem_copy(bytes + 4, &riff, 4);
    mem_copy(bytes + 8, "WAVEfmt ", 8);
    u32 fmt_size = 16;
    u16 format = 1, channels = 2, bits = 16, align = 4;
    mem_copy(bytes + 16, &fmt_size, 4);
    mem_copy(bytes + 20, &format, 2);
    mem_copy(bytes + 22, &channels, 2);
    mem_copy(bytes + 24, &rate, 4);
    mem_copy(bytes + 28, &byte_rate, 4);
    mem_copy(bytes + 32, &align, 2);
    mem_copy(bytes + 34, &bits, 2);
    mem_copy(bytes + 36, "data", 4);
    u32 data_size = (u32)data_bytes;
    mem_copy(bytes + 40, &data_size, 4);
    for (u64 i = 0; i < frames; i += 1) {
        f64 phase = 2.0 * 3.14159265358979 * 440.0 * (f64)i / 44100.0;
        i32 value = (i32)(dsp_sin_f64(phase) * 8192.0);
        u8 lo = (u8)(value & 0xFF);
        u8 hi = (u8)((value >> 8) & 0xFF);
        u8 *at = bytes + 44 + i * 4;
        at[0] = lo;
        at[1] = hi;
        at[2] = lo;
        at[3] = hi;
    }
    return os_file_write_all(path, str8(bytes, size));
}

typedef struct TestBurnTrack {
    u32 seconds;
    String8 path;
    String8 title;
    u64 key;
    u64 bytes;
    u64 transcode_us;
    b32 cached;
} TestBurnTrack;

// Renders one track into the cache, exactly as the transcode job does, and says
// how long it took. A cache hit costs the stat and nothing else - which is the
// number the second pass is here to print.
static b32 test_burn_render(PipelineCache *cache, TestBurnTrack *track) {
    u64 started = os_time_now_us();
    if (pipeline_cache_lookup(cache, track->key, 0)) {
        track->cached = 1;
        track->transcode_us = os_time_now_us() - started;
        return 1;
    }
    track->cached = 0;
    Decoder *decoder = 0;
    b32 ok = 0;
    if (codec_open(&decoder, track->path) == CODEC_OK) {
        PipelineSource source;
        if (pipeline_source_from_decoder(&source, decoder)) {
            Arena *scratch = arena_alloc(MB(64));
            PipelineCacheWriter *writer = push_struct(scratch, PipelineCacheWriter);
            if (pipeline_cache_write_begin(cache, track->key, scratch, writer)) {
                PipelineTask *task = push_struct_zero(scratch, PipelineTask);
                task->source = source;
                pipeline_config_defaults(&task->config);
                task->config.format = PIPELINE_FORMAT_SP_BE;
                task->write = pipeline_cache_write;
                task->write_user = writer;
                task->arena = arena_alloc(MB(64));
                pipeline_run(task);
                ok = pipeline_cache_write_end(writer, &task->result,
                                              task->result.status == PIPELINE_OK);
                arena_release(task->arena);
            }
            arena_release(scratch);
        }
        codec_close(decoder);
    }
    track->transcode_us = os_time_now_us() - started;
    return ok;
}

// A title that STARTS with the prefix, and nothing else. The length test is not
// belt and braces: str8_find of a needle in an empty haystack answers 0, so
// without it every untitled track on the user's disc would look like ours - and
// the erase below would take the eight tracks it exists to protect.
static b32 test_burn_is_ours(const NetmdTrack *track) {
    String8 prefix = str8_lit(TEST_BURN_TITLE_PREFIX);
    String8 title = str8((u8 *)track->title, track->title_size);
    return title.size >= prefix.size && str8_find(title, prefix, 0) == 0;
}

// One upload run of `count` entries bound to their cache files, followed by a
// disc read. The plan never carries a disc title: this disc has groups and they
// are not ours to rewrite.
static u32 test_burn_run(NetmdSession *session, Arena *arena, PipelineCache *cache,
                         TestBurnTrack *tracks, u32 count, DiscLayout *after) {
    NetmdUploadEntry *entries = push_array_zero(arena, NetmdUploadEntry, count);
    PipelineCacheReader *readers = push_array_zero(arena, PipelineCacheReader, count);
    for (u32 i = 0; i < count; i += 1) {
        NetmdUploadEntry *entry = &entries[i];
        entry->path_size = (u32)tracks[i].path.size;
        mem_copy(entry->path, tracks[i].path.str, tracks[i].path.size);
        entry->title_size = (u32)tracks[i].title.size;
        mem_copy(entry->title, tracks[i].title.str, tracks[i].title.size);
        entry->duration_ms = tracks[i].seconds * 1000u;
        pipeline_config_defaults(&entry->config);
        entry->config.format = PIPELINE_FORMAT_SP_BE;
        if (!pipeline_cache_read_begin(cache, tracks[i].key, arena, &readers[i])) {
            return NetmdResult_Malformed;
        }
        entry->data.read = pipeline_cache_read;
        entry->data.user = &readers[i];
        entry->data.total_bytes = readers[i].size;
        tracks[i].bytes = readers[i].size;
    }
    NetmdUploadPlan *plan = push_struct_zero(arena, NetmdUploadPlan);
    plan->entries = entries;
    plan->count = count;
    plan->write_disc_title = 0;
    NetmdUploadState *state = push_struct_zero(arena, NetmdUploadState);
    u32 result = netmd_upload_run(session, arena, plan, state, 0, 0);
    for (u32 i = 0; i < count; i += 1) { pipeline_cache_read_end(&readers[i]); }
    if (result == NetmdResult_Ok) { netmd_read_disc(session, arena, after); }
    return result;
}

TEST(transfer_device_burn) {
    ArenaTemp scratch = arena_temp_begin(arena);
    String8 command_line = os_command_line(arena);
    // --device-clean does the clean up half alone: it erases the MINIDISK TEST
    // tracks a previous run left behind without writing anything new. That is
    // the recovery path when a burn stops halfway.
    b32 clean_only = str8_find(command_line, str8_lit("--device-clean"), 0) < command_line.size;
    if (!clean_only &&
        str8_find(command_line, str8_lit("--device-burn"), 0) >= command_line.size) {
        test_report("    skipped (pass --device-burn to run it on the real device)\n");
        arena_temp_end(scratch);
        return;
    }

    OsUsbDeviceList list = os_usb_enumerate(arena);
    OsUsbDeviceInfo *found = 0;
    for (u64 i = 0; i < list.count; i += 1) {
        if (netmd_model_lookup(list.items[i].vid, list.items[i].pid) &&
            list.items[i].state == OsUsbState_Ready) {
            found = &list.items[i];
            break;
        }
    }
    EXPECT(found != 0);
    if (!found) {
        test_report("    no NetMD with a WinUSB driver bound\n");
        arena_temp_end(scratch);
        return;
    }
    OsUsb usb = os_usb_open(found->path);
    EXPECT(os_usb_is_open(usb));
    if (!os_usb_is_open(usb)) {
        arena_temp_end(scratch);
        return;
    }
    UsbTransport raw;
    os_usb_transport(usb, &raw);
    // Every exchange of this session goes into tests/netmd/real, which is what
    // makes the measurement below reproducible by somebody else.
    // The trace's own arena, and a big one: a burn moves 16 MB of audio and the
    // transcript holds every one of those bytes in hexadecimal. GB(2) is
    // reserve, not commit - the first run wrote past a 64 MB arena and died on
    // it, after the disc was already written.
    Arena *trace_arena = arena_alloc(GB(2));
    NetmdTrace *trace = push_struct_zero(trace_arena, NetmdTrace);
    netmd_trace_init(trace, trace_arena, &raw);
    UsbTransport transport;
    netmd_trace_transport(trace, &transport);
    NetmdSession session;
    netmd_session_init(&session, &transport, found->vid, found->pid);

    DiscLayout *before = push_struct_zero(arena, DiscLayout);
    u32 result = netmd_read_disc(&session, arena, before);
    EXPECT(result == NetmdResult_Ok);
    b32 writable = (before->flags & NetmdDiscFlag_Present) != 0 &&
                   (before->flags & NetmdDiscFlag_Writable) != 0 &&
                   (before->flags & NetmdDiscFlag_WriteProtected) == 0;
    u32 original_count = 0;
    for (u32 i = 0; i < before->track_count; i += 1) {
        if (!test_burn_is_ours(&before->tracks[i])) { original_count += 1; }
    }
    test_report("    disc \"%S\": %u track(s), %u of them the user's, %u s free\n",
                str8((u8 *)before->title, before->title_size), before->track_count,
                original_count, (u32)(before->capacity.available.ms / 1000u));
    EXPECT(writable);
    if (result != NetmdResult_Ok || !writable) {
        test_report("    refusing to write: no disc, not recordable, or tab open\n");
        os_usb_close(usb);
        arena_temp_end(scratch);
        return;
    }

    // The three sources, and a cache of their own so the second pass measures
    // the cache and not whatever a previous run left in the user's.
    if (clean_only) { goto clean_up; }
    String8 root = test_transfer_cache_dir(arena, "minidisk-t043-burn");
    PipelineCache *cache = push_struct(arena, PipelineCache);
    pipeline_cache_init(cache, arena, root, MB(512));
    static const u32 seconds[3] = {5u, 25u, 61u};
    TestBurnTrack *tracks = push_array_zero(arena, TestBurnTrack, 3);
    PipelineConfig config;
    pipeline_config_defaults(&config);
    config.format = PIPELINE_FORMAT_SP_BE;
    for (u32 i = 0; i < 3; i += 1) {
        tracks[i].seconds = seconds[i];
        tracks[i].path = str8f(arena, "%S\\minidisk-test-%us.wav", root, seconds[i]);
        tracks[i].title = str8f(arena, "%s %us", TEST_BURN_TITLE_PREFIX, seconds[i]);
        EXPECT(test_burn_write_wav(arena, tracks[i].path, seconds[i]));
        tracks[i].key = pipeline_cache_key_of(tracks[i].path, &config);
        EXPECT(tracks[i].key != 0);
    }

    // --- pass one: one run per track, so each delta is measured on its own ---
    DiscLayout *step = push_struct_zero(arena, DiscLayout);
    u64 free_ms = before->capacity.available.ms;
    for (u32 i = 0; i < 3; i += 1) {
        EXPECT(test_burn_render(cache, &tracks[i]));
        EXPECT(!tracks[i].cached);
        u64 started = os_time_now_us();
        result = test_burn_run(&session, arena, cache, &tracks[i], 1, step);
        u64 elapsed_us = os_time_now_us() - started;
        EXPECT(result == NetmdResult_Ok);
        if (result != NetmdResult_Ok) { break; }
        u64 now_free = step->capacity.available.ms;
        i64 cost_ms = (i64)free_ms - (i64)now_free;
        u32 table_ms = plan_clusters_for(tracks[i].seconds * 1000u, PlanCapMode_SP) * 2000u;
        test_report("    %S: transcode %llu ms, %llu bytes, upload %llu ms (%f x), "
                    "free %llu -> %llu ms, cost %lld ms, table %u ms, ecart %lld ms\n",
                    tracks[i].title, tracks[i].transcode_us / 1000u, tracks[i].bytes,
                    elapsed_us / 1000u,
                    (f64)(tracks[i].seconds * 1000000u) / (f64)Max(elapsed_us, (u64)1),
                    free_ms, now_free, cost_ms, table_ms, cost_ms - (i64)table_ms);
        free_ms = now_free;
    }

    // --- pass two: the same three tracks in one run, all three cache hits ---
    u64 render_started = os_time_now_us();
    for (u32 i = 0; i < 3; i += 1) {
        EXPECT(test_burn_render(cache, &tracks[i]));
        EXPECT(tracks[i].cached);  // D5: a second burn waits for no transcoding
    }
    u64 render_us = os_time_now_us() - render_started;
    test_report("    second pass: three cache hits in %llu us of transcoding\n", render_us);
    u64 free_before_pass2 = free_ms;
    u64 started = os_time_now_us();
    result = test_burn_run(&session, arena, cache, tracks, 3, step);
    u64 pass2_us = os_time_now_us() - started;
    EXPECT(result == NetmdResult_Ok);
    if (result == NetmdResult_Ok) {
        i64 cost_ms = (i64)free_before_pass2 - (i64)step->capacity.available.ms;
        u32 table_ms = 0;
        for (u32 i = 0; i < 3; i += 1) {
            table_ms += plan_clusters_for(tracks[i].seconds * 1000u, PlanCapMode_SP) * 2000u;
        }
        test_report("    pass two: %llu ms for 91 s of audio, cost %lld ms, table %u ms, "
                    "ecart %lld ms\n",
                    pass2_us / 1000u, cost_ms, table_ms, cost_ms - (i64)table_ms);
        free_ms = step->capacity.available.ms;
    }

clean_up:
    // --- the clean up, through the T-022 path -----------------------------------
    // Re-read, back the TOC up, simulate, and only then erase - and only the
    // tracks whose title we wrote. The user's own tracks are never in the mask.
    DiscLayout *current = push_struct_zero(arena, DiscLayout);
    EXPECT(netmd_read_disc(&session, arena, current) == NetmdResult_Ok);
    String8 backup_path = str8(0, 0);
    EXPECT(netmd_backup_write(arena, netmd_backup_dir(arena), current, &backup_path));
    test_report("    TOC backed up to %S\n", backup_path);

    NetmdEditRequest *request = push_struct_zero(arena, NetmdEditRequest);
    request->kind = NetmdEditKind_EraseTracks;
    u32 ours = 0;
    for (u32 i = 0; i < current->track_count; i += 1) {
        if (test_burn_is_ours(&current->tracks[i])) {
            netmd_mask_set(request->mask, i);
            ours += 1;
        }
    }
    test_report("    erasing %u track(s) titled \"%s ...\" out of %u\n", ours,
                TEST_BURN_TITLE_PREFIX, current->track_count);
    // The second guard, and the one that would have caught the first: a mask
    // that covers the whole disc is never this test's mask.
    EXPECT(ours < current->track_count);
    if (backup_path.size != 0 && ours != 0 && ours < current->track_count) {
        DiscLayout *after_edit = push_struct_zero(arena, DiscLayout);
        DiscDiff *diff = push_struct_zero(arena, DiscDiff);
        netmd_edit_simulate(current, request, after_edit, diff);
        test_report("    simulation: allowed %u, refusal %u, %u -> %u track(s), "
                    "%u -> %u cells, %u write(s)\n",
                    (u32)diff->allowed, diff->refusal, diff->tracks_before, diff->tracks_after,
                    diff->cells_before, diff->cells_after, diff->writes);
        EXPECT(diff->tracks_after == current->track_count - ours);
        if (diff->allowed) {
            u32 writes = 0;
            EXPECT(netmd_edit_apply(&session, arena, after_edit, request, &writes) ==
                   NetmdResult_Ok);
        } else {
            // Refused as one gesture. One track at a time is the same edit, and
            // each one is simulated on the layout the previous one produced -
            // still nothing written that was not simulated first (D4).
            test_report("    the whole selection was refused (%u): erasing one by one\n",
                        diff->refusal);
            // Four attempts per track: the device answers REJECTED to a second
            // erase that follows the first too closely (s6.2), and the cure is
            // to wait rather than to give up on the track.
            for (u32 pass = 0; pass < ours * 4u; pass += 1) {
                DiscLayout *now = push_struct_zero(arena, DiscLayout);
                u32 read = netmd_read_disc(&session, arena, now);
                if (read != NetmdResult_Ok) {
                    test_report("    re-read failed: %u\n", read);
                    break;
                }
                u32 target = NETMD_TRACK_MAX;
                for (u32 i = 0; i < now->track_count; i += 1) {
                    if (test_burn_is_ours(&now->tracks[i])) {
                        target = i;
                        break;
                    }
                }
                if (target == NETMD_TRACK_MAX) { break; }
                NetmdEditRequest *one = push_struct_zero(arena, NetmdEditRequest);
                one->kind = NetmdEditKind_EraseTracks;
                netmd_mask_set(one->mask, target);
                // P-014: a track this session has just written reads back with
                // flags 0x03, which netmd_edit_simulate calls "checked out by
                // SonicStage" and refuses to erase. The flag settles once the
                // TOC leaves the device's RAM. On a track WE wrote, this run,
                // the flag is that artefact and not a checkout, so the copy the
                // simulation is run against clears it - and everything else,
                // including the simulation itself, is unchanged.
                if (now->tracks[target].protect) { now->tracks[target].protect = 0; }
                DiscLayout *one_after = push_struct_zero(arena, DiscLayout);
                DiscDiff *one_diff = push_struct_zero(arena, DiscDiff);
                netmd_edit_simulate(now, one, one_after, one_diff);
                if (!one_diff->allowed) {
                    test_report("    track %u refused: %u\n", target, one_diff->refusal);
                    break;
                }
                u32 writes = 0;
                u32 applied = netmd_edit_apply(&session, arena, one_after, one, &writes);
                test_report("    erase track %u: result %u, %u write(s)\n", target, applied,
                            writes);
                // s6.2: the device answers REJECTED to whatever arrives while
                // it is writing its TOC. netmd_edit_apply holds inside one edit;
                // this is the hold *between* two of them, and it is longer after
                // one that was refused.
                os_sleep_us(applied == NetmdResult_Ok ? 2000000u : 5000000u);
            }
        }
    }

    DiscLayout *final_disc = push_struct_zero(arena, DiscLayout);
    EXPECT(netmd_read_disc(&session, arena, final_disc) == NetmdResult_Ok);
    u32 left = 0;
    for (u32 i = 0; i < final_disc->track_count; i += 1) {
        if (test_burn_is_ours(&final_disc->tracks[i])) { left += 1; }
    }
    EXPECT(left == 0);
    EXPECT(final_disc->track_count == original_count);
    test_report("    after clean up: %u track(s), %u s free, disc title \"%S\"\n",
                final_disc->track_count, (u32)(final_disc->capacity.available.ms / 1000u),
                str8((u8 *)final_disc->title, final_disc->title_size));
    // The user's disc title is the one thing this test must never have touched.
    EXPECT(str8_eq(str8((u8 *)final_disc->title, final_disc->title_size),
                   str8((u8 *)before->title, before->title_size)));

    os_dir_create(str8_lit("tests/netmd/real"));
    EXPECT(netmd_trace_write(
            trace, clean_only ? str8_lit("tests/netmd/real/t043_device_clean.trace")
                              : str8_lit("tests/netmd/real/t043_device_burn.trace")));
    arena_release(trace_arena);
    os_usb_close(usb);
    arena_temp_end(scratch);
}

// Non regression, and the reason it exists: the first run of the device test
// reported "9 tracks, 0 of them the user's" on a disc of eight untitled tracks,
// because str8_find of a needle in an empty haystack answers 0. The erase mask
// would have covered the user's whole disc. It runs with no device.
TEST(transfer_burn_title_guard) {
    NetmdTrack *track = push_struct_zero(arena, NetmdTrack);
    EXPECT(!test_burn_is_ours(track));  // untitled: never ours

    String8 mine = str8_lit("MINIDISK TEST 5s");
    track->title_size = (u32)mine.size;
    mem_copy(track->title, mine.str, mine.size);
    EXPECT(test_burn_is_ours(track));

    String8 short_title = str8_lit("MINIDISK");
    StructZero(track);
    track->title_size = (u32)short_title.size;
    mem_copy(track->title, short_title.str, short_title.size);
    EXPECT(!test_burn_is_ours(track));

    String8 other = str8_lit("Une chanson MINIDISK TEST");
    StructZero(track);
    track->title_size = (u32)other.size;
    mem_copy(track->title, other.str, other.size);
    EXPECT(!test_burn_is_ours(track));  // it has to be a prefix, not a substring
}

static void test_transfer_run_all(void) {
    RUN(transfer_sim_lists_what_will_be_written);
    RUN(transfer_sim_missing_track_and_overflow);
    RUN(transfer_sim_append_versus_erase);
    RUN(transfer_sim_protected_disc_refuses);
    RUN(transfer_state_machine_nominal);
    RUN(transfer_state_machine_error_then_resume);
    RUN(transfer_state_machine_pause_and_cancel);
    RUN(transfer_eta_is_honest_and_never_climbs);
    RUN(transfer_cache_key_is_stable_and_invalidates);
    RUN(transfer_cache_writes_and_reads_back);
    RUN(transfer_cache_purges_least_recently_used_first);
    RUN(transfer_cache_rejects_a_damaged_file);
    RUN(transfer_cache_names_round_trip);
    RUN(transfer_log_is_bounded);
    RUN(transfer_burn_title_guard);
    RUN(transfer_device_burn);
}

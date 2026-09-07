// view_transfer.c - the burn as a visible step (T-043, research/02 s8.4).
//
// Three screens in one place in the Disc panel, and they are three states of
// the same thing:
//
//   1. the button, when nothing is running;
//   2. the pre-flight (ADR-011 D4): exactly what will be written, what it costs
//      in clusters and in TOC cells, and every reason it might not work - shown
//      before a single byte moves, with one explicit button that names the
//      number of tracks it is about to write;
//   3. the transfer: per track state, two bars, an honest ETA, pause between
//      tracks, cancel that says what cancelling leaves behind, resume, the "do
//      not eject" banner, and a log file at the end.
//
// The arithmetic and the state machine are transfer.c's, and are tested without
// a window. What is here is the drawing, the job scheduling of the transcodes,
// and the translation of NetmdEvent into TransferEvent.

// --- state -----------------------------------------------------------------
// The upload plan and the cache readers behind it live for the whole length of
// the transfer, which is minutes: globals, not a frame arena.

#define APP_TRANSCODE_INFLIGHT 3  // rendered ahead of the track being sent
// research/02 MI-28: the device thread fires progress every 250 ms and each
// packet moves the byte counter. One redraw per 100 ms is smooth and leaves the
// loop asleep the rest of the time, which is what keeps the idle CPU at zero.
#define APP_TRANSFER_REDRAW_US 100000ull

typedef enum AppTranscodeState {
    AppTranscode_Idle = 0,
    AppTranscode_Queued,
    AppTranscode_Running,
    AppTranscode_Ready,
    AppTranscode_Failed
} AppTranscodeState;

typedef struct AppTranscodeJob {
    volatile u32 state;  // AppTranscodeState, the only thing shared with the worker
    u32 entry;           // index into the upload plan
    u64 key;
    u32 path_size;
    u8 path[NETMD_UPLOAD_PATH_MAX];
    PipelineConfig config;
    // Where the sender reads from once the render is done. Opened on the device
    // thread, by app_transfer_prepare, and never before.
    PipelineCacheReader reader;
    b32 open;
} AppTranscodeJob;

typedef struct AppTransfer {
    Transfer run;
    TransferSim sim;
    PipelineCache cache;
    b32 cache_ready;

    b32 open;            // the pre-flight or the transfer is on screen
    u32 policy;          // TransferPolicy
    b32 confirm_cancel;  // the inline confirmation of MI-30
    b32 toast;           // the end of run toast is up
    u64 toast_us;
    u64 last_redraw_us;
    u64 last_event_us;

    NetmdUploadPlan plan;
    NetmdUploadEntry entries[PLAN_ENTRY_MAX];
    AppTranscodeJob jobs[PLAN_ENTRY_MAX];
    u32 job_count;

    // The layout the simulation was made against: a disc that is inserted,
    // read, or edited while the pre-flight is on screen changes every number
    // there, and a pre-flight that is out of date is worse than none.
    const DiscLayout *sim_disc;

    String8 log_dir;
    String8 log_path;
} AppTransfer;

global AppTransfer app_transfer;

// --- the transcode jobs ------------------------------------------------------
// One job per track, pushed at most APP_TRANSCODE_INFLIGHT ahead of the track
// on the wire. Each one renders into the cache and nothing else: the sender
// reads the file, so a job that finishes early costs no memory at all.

static void app_transcode_job(void *data, u64 begin, u64 end) {
    Unused(begin);
    Unused(end);
    AppTranscodeJob *job = (AppTranscodeJob *)data;
    os_atomic_store_u32(&job->state, AppTranscode_Running);

    Decoder *decoder = 0;
    String8 path = str8(job->path, job->path_size);
    b32 ok = 0;
    if (codec_open(&decoder, path) == CODEC_OK) {
        PipelineSource source;
        if (pipeline_source_from_decoder(&source, decoder)) {
            Arena *scratch = arena_alloc(MB(64));
            PipelineCacheWriter *writer = push_struct(scratch, PipelineCacheWriter);
            if (pipeline_cache_write_begin(&app_transfer.cache, job->key, scratch, writer)) {
                PipelineTask *task = push_struct_zero(scratch, PipelineTask);
                task->source = source;
                task->config = job->config;
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
    os_atomic_store_u32(&job->state, ok ? AppTranscode_Ready : AppTranscode_Failed);
    os_request_redraw();
}

// Called on the DEVICE thread, right before a track goes on the wire: the one
// place that waits for a render, and it waits for exactly one track.
static b32 app_transfer_prepare(void *user, NetmdUploadEntry *entry, u32 index) {
    Unused(user);
    AppTranscodeJob *job = &app_transfer.jobs[index];
    for (;;) {
        u32 state = os_atomic_load_u32(&job->state);
        if (state == AppTranscode_Ready) { break; }
        if (state == AppTranscode_Failed || state == AppTranscode_Idle) { return 0; }
        if (os_atomic_load_u32(&app_transfer.run.phase) == TransferPhase_Cancelling) { return 0; }
        // A track renders above 100x real time, so this sleep is only ever hit
        // for the very first one, and only while the job system is still busy.
        os_sleep_us(5000);
    }
    // The device thread's own scratch, never app.permanent: two threads pushing
    // into the same arena is the race this whole architecture exists to avoid.
    ArenaTemp scratch = scratch_begin(0, 0);
    b32 opened = pipeline_cache_read_begin(&app_transfer.cache, job->key, scratch.arena,
                                           &job->reader);
    scratch_end(scratch);
    if (!opened) { return 0; }
    job->open = 1;
    entry->data.read = pipeline_cache_read;
    entry->data.user = &job->reader;
    entry->data.total_bytes = job->reader.size;
    return 1;
}

// Schedules what may be scheduled: the tracks between the one on the wire and
// APP_TRANSCODE_INFLIGHT past it, skipping the ones the cache already holds.
static void app_transcode_pump(void) {
    if (!app_transfer.cache_ready) { return; }
    u32 first = app_transfer.run.current;
    u32 in_flight = 0;
    for (u32 i = 0; i < app_transfer.job_count; i += 1) {
        u32 state = os_atomic_load_u32(&app_transfer.jobs[i].state);
        if (state == AppTranscode_Queued || state == AppTranscode_Running) { in_flight += 1; }
    }
    for (u32 i = first; i < app_transfer.job_count && in_flight < APP_TRANSCODE_INFLIGHT;
         i += 1) {
        AppTranscodeJob *job = &app_transfer.jobs[i];
        if (os_atomic_load_u32(&job->state) != AppTranscode_Idle) { continue; }
        if (job->key == 0) { continue; }  // a missing source: nothing to render
        TransferEvent event;
        StructZero(&event);
        event.entry = i;
        event.now_us = os_time_now_us();
        if (pipeline_cache_lookup(&app_transfer.cache, job->key, 0)) {
            // D5: this is the whole point. A plan burned a second time waits
            // for nothing at all.
            os_atomic_store_u32(&job->state, AppTranscode_Ready);
            event.kind = TransferEvent_CacheHit;
            transfer_apply(&app_transfer.run, &event);
            continue;
        }
        os_atomic_store_u32(&job->state, AppTranscode_Queued);
        event.kind = TransferEvent_TranscodeBegin;
        transfer_apply(&app_transfer.run, &event);
        jobs_push(0, app_transcode_job, job);
        in_flight += 1;
    }
    // The jobs answer through their state word; this is where the machine hears
    // about it, on the main thread and nowhere else.
    for (u32 i = 0; i < app_transfer.job_count; i += 1) {
        AppTranscodeJob *job = &app_transfer.jobs[i];
        u32 state = os_atomic_load_u32(&job->state);
        u8 known = app_transfer.run.state[i];
        if (state == AppTranscode_Ready && known == TransferTrack_Transcoding) {
            TransferEvent event;
            StructZero(&event);
            event.kind = TransferEvent_TranscodeDone;
            event.entry = i;
            event.now_us = os_time_now_us();
            transfer_apply(&app_transfer.run, &event);
        } else if (state == AppTranscode_Failed && known == TransferTrack_Transcoding) {
            TransferEvent event;
            StructZero(&event);
            event.kind = TransferEvent_TranscodeFailed;
            event.entry = i;
            event.result = NetmdResult_Malformed;
            event.now_us = os_time_now_us();
            transfer_apply(&app_transfer.run, &event);
        }
    }
}

// --- the log ------------------------------------------------------------------
// One file per run under <cache_dir>\logs\transfer-<date>.txt, written once at
// the end: a burn is not the moment to be opening a file per line.

static void app_transfer_log_open(void) {
    OsWallClock clock;
    os_time_local(&clock);
    app_transfer.log_dir = os_path_join(app.permanent, app.cache_dir, str8_lit("logs"));
    os_dir_create(app_transfer.log_dir);
    app_transfer.log_path =
            str8f(app.permanent, "%S\\transfer-%04u%02u%02u-%02u%02u%02u.txt",
                  app_transfer.log_dir, clock.year, clock.month, clock.day, clock.hour,
                  clock.minute, clock.second);
}

static void app_transfer_log_flush(void) {
    if (app_transfer.log_path.size == 0 || app_transfer.run.log_size == 0) { return; }
    os_file_write_all(app_transfer.log_path,
                      str8(app_transfer.run.log, app_transfer.run.log_size));
}

static void app_transfer_logf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 line = str8fv(scratch.arena, fmt, args);
    transfer_log(&app_transfer.run, os_time_now_us(), line);
    scratch_end(scratch);
    va_end(args);
}

// --- the pre-flight -------------------------------------------------------------

static void app_transfer_resimulate(void) {
    app_plan_sync();
    app_transfer.sim_disc = netmd_device_disc(&app_device.thread);
    transfer_simulate(&app.plan, &app.library, app.plan_disc, app_transfer.sim_disc,
                      app_transfer.policy, &app_transfer.sim);
}

void app_transfer_open(void) {
    if (transfer_active(&app_transfer.run)) { return; }
    app_transfer.policy = TransferPolicy_Append;
    app_transfer.confirm_cancel = 0;
    app_transfer.toast = 0;
    app_transfer_resimulate();
    StructZero(&app_transfer.run);
    app_transfer.run.phase = TransferPhase_Preflight;
    app_transfer.open = 1;
    os_request_redraw();
}

// Builds the upload plan out of the simulation - not out of the plan document.
// The simulation is what the user just agreed to, down to each final title.
static void app_transfer_start(void) {
    const TransferSim *sim = &app_transfer.sim;
    StructZero(&app_transfer.plan);
    app_transfer.plan.entries = app_transfer.entries;
    app_transfer.job_count = 0;
    const PlanDisc *disc = &app.plan.discs[app.plan_disc];

    for (u32 i = 0; i < sim->count; i += 1) {
        const TransferSimEntry *sim_entry = &sim->entries[i];
        if (sim_entry->missing) { continue; }
        String8 path = lib_string(&app.plan.strings, disc->path_id[i]);
        if (path.size == 0 || path.size > NETMD_UPLOAD_PATH_MAX) { continue; }
        u32 index = app_transfer.plan.count;
        NetmdUploadEntry *entry = &app_transfer.entries[index];
        StructZero(entry);
        entry->path_size = (u32)path.size;
        mem_copy(entry->path, path.str, path.size);
        entry->title_size = (u32)Min((u64)sim_entry->title_size, (u64)NETMD_TITLE_MAX);
        mem_copy(entry->title, sim_entry->title, entry->title_size);
        entry->duration_ms = sim_entry->duration_ms;
        pipeline_config_defaults(&entry->config);
        entry->config.format = PIPELINE_FORMAT_SP_BE;

        AppTranscodeJob *job = &app_transfer.jobs[index];
        StructZero(job);
        job->entry = index;
        job->config = entry->config;
        job->path_size = entry->path_size;
        mem_copy(job->path, entry->path, entry->path_size);
        job->key = pipeline_cache_key_of(path, &entry->config);
        job->state = AppTranscode_Idle;
        app_transfer.plan.count += 1;
        app_transfer.job_count += 1;
    }
    if (app_transfer.plan.count == 0) { return; }

    // s3.10 / D4: the disc title is written only when the simulation said it
    // could be. On a disc that already has one, nothing here touches it.
    app_transfer.plan.disc_title_size =
            (u32)Min((u64)sim->disc_title_size, (u64)NETMD_DISC_TITLE_MAX);
    mem_copy(app_transfer.plan.disc_title, sim->disc_title,
             app_transfer.plan.disc_title_size);
    app_transfer.plan.write_disc_title =
            sim->write_disc_title && app_transfer.plan.disc_title_size != 0;
    app_transfer.plan.prepare = app_transfer_prepare;

    transfer_begin(&app_transfer.run, sim);
    app_transfer_log_open();
    TransferEvent event;
    StructZero(&event);
    event.kind = TransferEvent_Start;
    event.now_us = os_time_now_us();
    transfer_apply(&app_transfer.run, &event);
    app_transfer_logf("gravure de %u piste(s), %u clusters, duree annoncee %u s",
                      app_transfer.plan.count, sim->clusters_needed,
                      transfer_expected_s(sim));
    app_transcode_pump();
    if (!netmd_device_upload(&app_device.thread, &app_transfer.plan)) {
        app_transfer.run.phase = TransferPhase_Failed;
        app_transfer_logf("le peripherique a refuse la gravure");
    }
}

// --- the events of the device thread ------------------------------------------
// view_device.c drains the queue; this is what it hands over. One TransferEvent
// per NetmdEvent, and one redraw per 100 ms whatever the rate.

void app_transfer_device_event(const NetmdEvent *event) {
    if (app_transfer.run.phase == TransferPhase_Idle) { return; }
    TransferEvent out;
    StructZero(&out);
    out.entry = event->entry;
    out.bytes_done = event->bytes_done;
    out.bytes_total = event->bytes_total;
    out.result = event->result;
    out.now_us = os_time_now_us();
    switch (event->kind) {
        case NetmdEvent_UploadProgress: out.kind = TransferEvent_Progress; break;
        case NetmdEvent_TrackDone: {
            out.kind = TransferEvent_TrackDone;
            transfer_apply(&app_transfer.run, &out);
            app_transfer_logf("piste %u ecrite et titree", event->entry + 1);
            os_request_redraw();
            return;
        }
        case NetmdEvent_UploadDone: out.kind = TransferEvent_UploadDone; break;
        case NetmdEvent_UploadError: out.kind = TransferEvent_UploadError; break;
        default: return;
    }
    transfer_apply(&app_transfer.run, &out);
    if (out.kind == TransferEvent_Progress) {
        // Coalesced: the bar moves ten times a second and the loop sleeps in
        // between, which is the difference between 0 % and 4 % of a core.
        if (out.now_us - app_transfer.last_redraw_us >= APP_TRANSFER_REDRAW_US) {
            app_transfer.last_redraw_us = out.now_us;
            os_request_redraw();
        }
        return;
    }
    if (out.kind == TransferEvent_UploadDone || out.kind == TransferEvent_UploadError) {
        for (u32 i = 0; i < app_transfer.job_count; i += 1) {
            if (app_transfer.jobs[i].open) {
                pipeline_cache_read_end(&app_transfer.jobs[i].reader);
                app_transfer.jobs[i].open = 0;
            }
        }
        app_transfer_logf("fin : %u piste(s) ecrite(s), resultat %u, %u s, cache %u/%u",
                          app_transfer.run.done_count, app_transfer.run.last_result,
                          transfer_elapsed_s(&app_transfer.run, out.now_us),
                          app_transfer.cache.hits, app_transfer.cache.writes);
        app_transfer_log_flush();
        app_transfer.toast = 1;
        app_transfer.toast_us = out.now_us;
        // The bound of D5 is applied between runs and never during one: a file
        // being read is a file the purge would rather not delete.
        ArenaTemp scratch = scratch_begin(0, 0);
        pipeline_cache_purge(&app_transfer.cache, scratch.arena, 0);
        pipeline_cache_purge_covers(scratch.arena, app.covers.dir,
                                    app_transfer.cache.max_bytes / 8, 0);
        scratch_end(scratch);
    }
    os_request_redraw();
}

// --- the frame ------------------------------------------------------------------

// Two flags, for the captures of the Livraison and for nothing else: --transfer
// opens the pre-flight as soon as a disc has been read, --burn starts the run
// from it. Neither one skips a confirmation the user would otherwise give - the
// pre-flight is still computed, still shown, and still refuses what it refuses.
global b32 app_transfer_auto_open;
global b32 app_transfer_auto_burn;

void app_transfer_init(void) {
    ArenaTemp flags = scratch_begin(0, 0);
    String8 command_line = os_command_line(flags.arena);
    app_transfer_auto_open =
            str8_find(command_line, str8_lit("--transfer"), 0) < command_line.size;
    app_transfer_auto_burn = str8_find(command_line, str8_lit("--burn"), 0) < command_line.size;
    if (app_transfer_auto_burn) { app_transfer_auto_open = 1; }
    scratch_end(flags);

    // The transcode cache is opened once, and purged once, at startup: a bound
    // that is only ever checked after a burn would grow without limit for a
    // user who burns once and browses for a month.
    pipeline_cache_init(&app_transfer.cache, app.permanent, app.cache_dir,
                        PIPELINE_CACHE_DEFAULT_BYTES);
    app_transfer.cache_ready = app_transfer.cache.ready;
    ArenaTemp scratch = scratch_begin(0, 0);
    pipeline_cache_purge(&app_transfer.cache, scratch.arena, 0);
    pipeline_cache_purge_covers(scratch.arena, app.covers.dir,
                                PIPELINE_CACHE_DEFAULT_BYTES / 8, 0);
    scratch_end(scratch);
}

void app_transfer_tick(void) {
    if (app_transfer.run.phase == TransferPhase_Running ||
        app_transfer.run.phase == TransferPhase_Pausing) {
        app_transcode_pump();
    }
    // The pre-flight is only worth computing once the device has published a
    // disc: before that it would say "no disc" about a disc that is coming.
    if (app_transfer.open && app_transfer.run.phase == TransferPhase_Preflight &&
        app_transfer.sim_disc != netmd_device_disc(&app_device.thread)) {
        app_transfer_resimulate();
        os_request_redraw();
    }
    if (app_transfer_auto_open && !app_transfer.open && app_plan_count() != 0 &&
        !app_device.disc_reading && netmd_device_disc(&app_device.thread) != 0) {
        app_transfer_auto_open = 0;
        app_transfer_open();
        if (app_transfer_auto_burn && app_transfer.sim.allowed) {
            app_transfer_auto_burn = 0;
            app_transfer_start();
        }
    }
}

// The loop wakes on its own only while something is moving (ADR-004): a burn is
// exactly that, ten times a second, and nothing at all in between.
b32 app_transfer_busy(void) { return transfer_active(&app_transfer.run); }

b32 app_transfer_can_close(void) { return !transfer_blocks_close(&app_transfer.run); }

void app_transfer_close_blocked(void) {
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena, "%S\n", app_str(Str_TransferCloseBlocked)));
    scratch_end(scratch);
    os_request_redraw();
}

// --- the gauge, burn progress variant (research/02 s9.11) ------------------------
// The gauge does not change shape: it acquires a layer. Written tracks are at
// full opacity, the one being written carries the fraction already sent, the
// ones to come are at 45 %, and a one pixel line marks the write position.

#define APP_BURN_UPCOMING_ALPHA 0.45f

static void app_transfer_gauge(f32 panel_width) {
    const UI_Theme *theme = ui_theme();
    f32 side = ui_dp(PLAN_GAUGE_SIDE_DP);
    f32 width = max_f32(panel_width - side * 2.0f, 1.0f);
    f32 bar_height = ui_dp(PLAN_GAUGE_BAR_DP);
    f32 radius = ui_dp(theme->space[UI_Space_2]);
    app_plan_sync();
    PlanGaugeLayout *layout = push_struct(ui_frame_arena(), PlanGaugeLayout);
    plan_gauge_layout(&app.capacity, width, layout);
    const Transfer *run = &app_transfer.run;

    // The fraction of the track being written, from its own byte counter: the
    // gauge and the per track bar are the same number drawn twice.
    const NetmdUploadState *state = netmd_device_upload_state(&app_device.thread);
    u64 track_bytes = (u64)os_atomic_load_u64((volatile u64 *)&state->track_bytes);
    u32 current = run->current;
    f32 fraction = 0.0f;
    if (current < run->count && run->track_bytes[current] != 0) {
        fraction = (f32)track_bytes / (f32)run->track_bytes[current];
        if (fraction > 1.0f) { fraction = 1.0f; }
    }

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(PLAN_GAUGE_BAR_DP + 2 * PLAN_GAUGE_MARGIN_DP), 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->surface) {
        UI_Box *frame = ui_build_box(UI_DrawBackground, str8_lit("###burngauge"));
        UI_Parent(frame) {
            ui_spacer(ui_px(side, 1.0f));
            UI_PrefWidth(ui_px(width, 1.0f))
            UI_PrefHeight(ui_pct(1.0f, 1.0f))
            UI_ChildLayoutAxis(Axis2_Y) {
                UI_Box *column = ui_build_box_from_key(0, 0);
                UI_Parent(column) {
                    ui_spacer(ui_px(ui_dp(PLAN_GAUGE_MARGIN_DP), 1.0f));
                    UI_Box *bar = 0;
                    UI_PrefWidth(ui_px(width, 1.0f))
                    UI_PrefHeight(ui_px(bar_height, 1.0f))
                    UI_BgColor(theme->control)
                    UI_CornerRadius(radius) {
                        bar = ui_build_box(UI_DrawBackground | UI_Clip, str8_lit("###burnbar"));
                    }
                    UI_Parent(bar) {
                        f32 head = 0.0f;
                        for (u32 i = 0; i < layout->count; i += 1) {
                            const PlanGaugeSegment *segment = &layout->segments[i];
                            u32 color = theme->mode[segment->mode];
                            u32 entry = segment->first;
                            u8 track_state = (entry < run->count) ? run->state[entry]
                                                                  : (u8)TransferTrack_Pending;
                            if (track_state == TransferTrack_Titled ||
                                track_state == TransferTrack_Written) {
                                app_gauge_rect(segment->x, 0.0f, segment->width, bar_height,
                                               color, 0.0f, 0);
                                head = segment->x + segment->width;
                            } else if (entry == current &&
                                       track_state == TransferTrack_Sending) {
                                f32 done = segment->width * fraction;
                                app_gauge_rect(segment->x, 0.0f, done, bar_height, color, 0.0f,
                                               0);
                                app_gauge_rect(segment->x + done, 0.0f, segment->width - done,
                                               bar_height,
                                               app_color_alpha(color, APP_BURN_UPCOMING_ALPHA),
                                               0.0f, 0);
                                head = segment->x + done;
                            } else {
                                app_gauge_rect(segment->x, 0.0f, segment->width, bar_height,
                                               app_color_alpha(color, APP_BURN_UPCOMING_ALPHA),
                                               0.0f, 0);
                            }
                        }
                        // s9.11: one white pixel at the exact write position.
                        app_gauge_rect(round_f32(head), 0.0f, 1.0f, bar_height,
                                       theme->fg_primary, 0.0f, 0);
                    }
                }
            }
        }
    }
}

// --- the two screens --------------------------------------------------------------

static void app_transfer_warnings(void) {
    const UI_Theme *theme = ui_theme();
    const TransferSim *sim = &app_transfer.sim;
    if (sim->warnings & TransferWarn_Protected) {
        app_device_line(UI_FontStyle_Emphasis, theme->danger,
                        app_str(Str_TransferWarnProtected));
    }
    if (sim->warnings & TransferWarn_NoDisc) {
        app_device_line(UI_FontStyle_Ui, theme->warning, app_str(Str_TransferWarnNoDisc));
    }
    if (sim->warnings & TransferWarn_Overflow) {
        app_device_line(UI_FontStyle_Emphasis, theme->danger,
                        app_str(Str_TransferWarnOverflow));
    }
    if (sim->warnings & TransferWarn_MissingTrack) {
        app_device_line(UI_FontStyle_Ui, theme->warning,
                        str8f(ui_frame_arena(), app_str_c(Str_TransferWarnMissing),
                              sim->missing_count));
    }
    if (sim->warnings & TransferWarn_TocOverflow) {
        app_device_line(UI_FontStyle_Ui, theme->danger, app_str(Str_TransferWarnToc));
    }
    if (sim->warnings & TransferWarn_Shortened) {
        app_device_line(UI_FontStyle_Caption, theme->fg_secondary,
                        app_str(Str_TransferWarnShortened));
    }
    if (sim->warnings & TransferWarn_TitleKept) {
        app_device_line(UI_FontStyle_Caption, theme->fg_secondary,
                        app_str(Str_TransferTitleKept));
    }
}

// The list of what will be written, with the title exactly as the TOC will hold
// it. Not the plan's titles: the sanitized, shortened ones.
static void app_transfer_track_list(b32 running) {
    const UI_Theme *theme = ui_theme();
    const TransferSim *sim = &app_transfer.sim;
    const Transfer *run = &app_transfer.run;
    u32 shown = 0;
    for (u32 i = 0; i < sim->count && shown < 12; i += 1) {
        const TransferSimEntry *entry = &sim->entries[i];
        u32 color = entry->missing ? theme->danger
                                   : (entry->fits ? theme->fg_secondary : theme->warning);
        String8 status = str8_lit("");
        if (running) {
            u8 state = (shown < run->count) ? run->state[shown] : (u8)TransferTrack_Pending;
            static const Str names[TransferTrack_COUNT] = {
                    Str_TransferStatePending,  Str_TransferStateTranscoding,
                    Str_TransferStateTranscoded, Str_TransferStateSending,
                    Str_TransferStateWritten,  Str_TransferStateTitled,
                    Str_TransferStateFailed,   Str_TransferStateSkipped,
            };
            status = app_str(names[state]);
            if (state == TransferTrack_Titled) { color = theme->success; }
            if (state == TransferTrack_Failed) { color = theme->danger; }
        } else if (entry->missing) {
            status = app_str(Str_TransferStateSkipped);
        }
        UI_PrefWidth(ui_pct(1.0f, 0.0f))
        UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
        UI_ChildLayoutAxis(Axis2_X) {
            UI_Box *row = ui_build_box_from_key(0, 0);
            UI_Parent(row) {
                app_cell(ui_px(ui_dp(28.0f), 1.0f), str8f(ui_frame_arena(), "%u", i + 1),
                         theme->fg_disabled, UI_TextFlag_TabularNumbers, UI_TextAlign_Right);
                app_cell(ui_pct(1.0f, 0.0f), str8((u8 *)entry->title, entry->title_size), color, 0,
                         UI_TextAlign_Left);
                app_cell(ui_px(ui_dp(96.0f), 0.0f), status, color, 0, UI_TextAlign_Right);
                app_cell(ui_px(ui_dp(56.0f), 0.0f), app_ms_duration(entry->duration_ms),
                         theme->fg_muted, UI_TextFlag_TabularNumbers, UI_TextAlign_Right);
            }
        }
        if (!entry->missing) { shown += 1; }
    }
    if (sim->count > 12) {
        app_device_line(UI_FontStyle_Caption, theme->fg_disabled,
                        str8f(ui_frame_arena(), "+ %u", sim->count - 12));
    }
}

static void app_transfer_preflight_panel(void) {
    const UI_Theme *theme = ui_theme();
    const TransferSim *sim = &app_transfer.sim;
    app_device_line(UI_FontStyle_Emphasis, theme->fg_primary, app_str(Str_TransferPreflight));
    app_device_line(UI_FontStyle_Caption, theme->fg_secondary,
                    str8f(ui_frame_arena(), app_str_c(Str_TransferSummary), sim->write_count,
                          app_ms_duration(sim->audio_ms), sim->clusters_needed));
    app_device_line(UI_FontStyle_Caption, theme->fg_secondary,
                    str8f(ui_frame_arena(), app_str_c(Str_TransferCapacity),
                          app_ms_duration(sim->free_ms_before),
                          app_ms_duration(sim->free_ms_after)));
    app_device_line(UI_FontStyle_Caption, theme->fg_muted,
                    str8f(ui_frame_arena(), app_str_c(Str_TransferClusters), sim->clusters_before,
                          sim->clusters_capacity, sim->clusters_after, sim->clusters_capacity));
    app_device_line(UI_FontStyle_Caption, theme->fg_muted,
                    str8f(ui_frame_arena(), app_str_c(Str_TransferToc), sim->cells_before,
                          sim->cells_after, sim->cells_free_after * PLAN_TOC_CELL_CHARS));
    if (sim->write_disc_title) {
        app_device_line(UI_FontStyle_Caption, theme->fg_muted,
                        str8f(ui_frame_arena(), app_str_c(Str_TransferDiscTitle),
                              str8((u8 *)sim->disc_title, sim->disc_title_size)));
    }
    app_transfer_warnings();
    app_transfer_track_list(0);
    app_device_line(UI_FontStyle_Caption, theme->fg_muted,
                    str8f(ui_frame_arena(), app_str_c(Str_TransferExpected),
                          app_duration(transfer_expected_s(sim))));

    // The two policies of s8.4, when the disc is not empty. Erasing is a
    // separate, separately confirmed action and it goes through T-022's path -
    // this only chooses which simulation is being looked at.
    if (sim->warnings & TransferWarn_DiscNotEmpty) {
        app_device_line(UI_FontStyle_Caption, theme->fg_secondary,
                        str8f(ui_frame_arena(), app_str_c(Str_TransferWarnNotEmpty),
                              sim->tracks_before));
        UI_PrefWidth(ui_pct(1.0f, 0.0f))
        UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
        UI_ChildLayoutAxis(Axis2_X) {
            UI_Box *row = ui_build_box_from_key(0, 0);
            UI_Parent(row) {
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
                if (ui_button(str8f(ui_frame_arena(), "%S###append",
                                    app_str(Str_TransferAppend)))
                            .clicked) {
                    app_transfer.policy = TransferPolicy_Append;
                    app_transfer_resimulate();
                }
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
                if (ui_button(str8f(ui_frame_arena(), "%S###erasefirst",
                                    app_str(Str_TransferErase)))
                            .clicked) {
                    app_transfer.policy = TransferPolicy_EraseFirst;
                    app_transfer_resimulate();
                }
                ui_tooltip(app_str(Str_TransferEraseHint));
            }
        }
    }

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *row = ui_build_box_from_key(0, 0);
        UI_Parent(row) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
            if (sim->allowed) {
                // MI-32: the button says the verb and the count, never "OK".
                if (ui_button_primary(str8f(ui_frame_arena(), "%S###burnnow",
                                            str8f(ui_frame_arena(),
                                                  app_str_c(Str_TransferBurnN),
                                                  sim->write_count)))
                            .clicked) {
                    app_transfer_start();
                }
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###burnback", app_str(Str_TransferBack)))
                        .clicked) {
                app_transfer.open = 0;
                app_transfer.run.phase = TransferPhase_Idle;
            }
        }
    }
}

// One bar, drawn out of two boxes: the renderer has one primitive and a
// progress bar is a rectangle inside a rectangle.
static void app_transfer_bar_widget(f32 width, f32 fraction, u32 color) {
    const UI_Theme *theme = ui_theme();
    f32 height = ui_dp(6.0f);
    if (fraction < 0.0f) { fraction = 0.0f; }
    if (fraction > 1.0f) { fraction = 1.0f; }
    UI_PrefWidth(ui_px(width, 1.0f))
    UI_PrefHeight(ui_px(height + ui_dp(4.0f), 1.0f))
    UI_ChildLayoutAxis(Axis2_Y) {
        UI_Box *holder = ui_build_box_from_key(0, 0);
        UI_Parent(holder) {
            UI_Box *track = 0;
            UI_FixedY(ui_dp(2.0f))
            UI_PrefWidth(ui_px(width, 1.0f))
            UI_PrefHeight(ui_px(height, 1.0f))
            UI_BgColor(theme->control)
            UI_CornerRadius(height * 0.5f) {
                track = ui_build_box_from_key(UI_FloatingY | UI_DrawBackground | UI_Clip, 0);
            }
            UI_Parent(track) {
                app_gauge_rect(0.0f, 0.0f, width * fraction, height, color, height * 0.5f, 0);
            }
        }
    }
}

static void app_transfer_running_panel(f32 panel_width) {
    const UI_Theme *theme = ui_theme();
    Transfer *run = &app_transfer.run;
    const NetmdUploadState *state = netmd_device_upload_state(&app_device.thread);
    u64 track_bytes = (u64)os_atomic_load_u64((volatile u64 *)&state->track_bytes);
    u32 current = run->current;
    f32 track_fraction = 0.0f;
    if (current < run->count && run->track_bytes[current] != 0) {
        track_fraction = (f32)track_bytes / (f32)run->track_bytes[current];
    }
    u64 now = os_time_now_us();

    // s6.3: the device is holding a TOC its disc does not have. This banner is
    // up for the whole transfer, and the app will not close under it.
    app_device_line(UI_FontStyle_Emphasis, theme->warning, app_str(Str_TransferNoEject));
    app_device_line(UI_FontStyle_Ui, theme->fg_primary,
                    str8f(ui_frame_arena(), app_str_c(Str_TransferRunning), current + 1,
                          run->count, transfer_progress_permille(run) / 10u,
                          app_duration(transfer_eta_s(run)),
                          app_duration(transfer_elapsed_s(run, now))));
    f32 width = max_f32(panel_width - ui_dp(24.0f), 1.0f);
    app_transfer_bar_widget(width, (f32)transfer_progress_permille(run) / 1000.0f,
                            theme->accent);
    app_transfer_bar_widget(width, track_fraction, theme->mode[PlanCapMode_SP]);
    u64 rate = transfer_rate(run);
    if (rate != 0) {
        app_device_line(UI_FontStyle_Caption, theme->fg_muted,
                        str8f(ui_frame_arena(), app_str_c(Str_TransferRate),
                              (u32)(rate / 1024u)));
    }
    if (run->slow) {
        app_device_line(UI_FontStyle_Caption, theme->fg_secondary, app_str(Str_TransferSlow));
    }
    app_device_line(UI_FontStyle_Caption, theme->fg_disabled,
                    str8f(ui_frame_arena(), app_str_c(Str_TransferCache),
                          app_transfer.cache.hits, app_transfer.cache.writes));
    app_transfer_track_list(1);

    if (app_transfer.confirm_cancel) {
        // MI-30: the confirmation states what cancelling leaves behind, in
        // tracks, because "the transfer stops" is not the part that matters.
        app_device_line(UI_FontStyle_Ui, theme->warning,
                        str8f(ui_frame_arena(), app_str_c(Str_TransferCancelWarn),
                              transfer_written_count(run)));
        UI_PrefWidth(ui_pct(1.0f, 0.0f))
        UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
        UI_ChildLayoutAxis(Axis2_X) {
            UI_Box *row = ui_build_box_from_key(0, 0);
            UI_Parent(row) {
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
                if (ui_button(str8f(ui_frame_arena(), "%S###cancelyes",
                                    app_str(Str_TransferCancelYes)))
                            .clicked) {
                    TransferEvent event;
                    StructZero(&event);
                    event.kind = TransferEvent_CancelRequested;
                    event.now_us = now;
                    transfer_apply(run, &event);
                    netmd_device_post(&app_device.thread, NetmdCmd_CancelUpload, 0);
                    app_transfer_logf("annulation demandee");
                    app_transfer.confirm_cancel = 0;
                }
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
                if (ui_button(str8f(ui_frame_arena(), "%S###cancelno",
                                    app_str(Str_TransferCancelNo)))
                            .clicked) {
                    app_transfer.confirm_cancel = 0;
                }
            }
        }
        return;
    }

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *row = ui_build_box_from_key(0, 0);
        UI_Parent(row) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
            if (run->phase == TransferPhase_Paused) {
                if (ui_button_primary(str8f(ui_frame_arena(), "%S###resume",
                                            app_str(Str_TransferResume)))
                            .clicked) {
                    TransferEvent event;
                    StructZero(&event);
                    event.kind = TransferEvent_Resume;
                    event.now_us = now;
                    transfer_apply(run, &event);
                    netmd_device_upload(&app_device.thread, &app_transfer.plan);
                    app_transfer_logf("reprise");
                }
            } else if (run->phase == TransferPhase_Running) {
                if (ui_button(str8f(ui_frame_arena(), "%S###pause",
                                    app_str(Str_TransferPause)))
                            .clicked) {
                    TransferEvent event;
                    StructZero(&event);
                    event.kind = TransferEvent_PauseRequested;
                    event.now_us = now;
                    transfer_apply(run, &event);
                    // The device thread stops between two tracks: cancelling
                    // the run is what a pause *is* at the protocol level, and
                    // the tracks already committed stay committed.
                    netmd_device_post(&app_device.thread, NetmdCmd_CancelUpload, 0);
                    app_transfer_logf("pause demandee");
                }
                ui_tooltip(app_str(Str_TransferPauseHint));
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            if (run->phase != TransferPhase_Cancelling) {
                if (ui_button(str8f(ui_frame_arena(), "%S###cancel",
                                    app_str(Str_TransferCancel)))
                            .clicked) {
                    app_transfer.confirm_cancel = 1;
                }
            }
        }
    }
}

static void app_transfer_result_line(void) {
    const UI_Theme *theme = ui_theme();
    Transfer *run = &app_transfer.run;
    u32 elapsed = transfer_elapsed_s(run, os_time_now_us());
    if (run->phase == TransferPhase_Done) {
        app_device_line(UI_FontStyle_Emphasis, theme->success,
                        str8f(ui_frame_arena(), app_str_c(Str_TransferDone), run->done_count,
                              app_duration(elapsed)));
    } else if (run->phase == TransferPhase_Cancelled) {
        app_device_line(UI_FontStyle_Ui, theme->warning,
                        str8f(ui_frame_arena(), app_str_c(Str_TransferCancelled),
                              run->done_count));
    } else {
        app_device_line(UI_FontStyle_Ui, theme->danger,
                        str8f(ui_frame_arena(), app_str_c(Str_TransferFailedN),
                              run->last_result, run->done_count));
    }
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *row = ui_build_box_from_key(0, 0);
        UI_Parent(row) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
            // A run that stopped short can be picked up where it stopped: the
            // entries already Done are skipped by netmd_upload_run itself.
            if (run->done_count < run->count) {
                if (ui_button_primary(str8f(ui_frame_arena(), "%S###resumerun",
                                            app_str(Str_TransferResume)))
                            .clicked) {
                    TransferEvent event;
                    StructZero(&event);
                    event.kind = TransferEvent_Resume;
                    event.now_us = os_time_now_us();
                    transfer_apply(run, &event);
                    netmd_device_upload(&app_device.thread, &app_transfer.plan);
                    app_transfer_logf("reprise apres interruption");
                }
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            }
            if (ui_button(str8f(ui_frame_arena(), "%S###burndone",
                                app_str(Str_TransferBack)))
                        .clicked) {
                app_transfer.open = 0;
                app_transfer.run.phase = TransferPhase_Idle;
            }
        }
    }
}

// The burn takes the top of the panel as soon as it is a step of its own: a
// pre-flight or a transfer is what the user is doing, and reading it should not
// mean scrolling past the disc's track list first.
b32 app_transfer_takes_over(void) {
    return app_transfer.run.phase != TransferPhase_Idle;
}

// What the Disc panel calls in place of the T-042 burn button.
void app_transfer_bar(f32 panel_width) {
    const UI_Theme *theme = ui_theme();
    u32 phase = app_transfer.run.phase;
    if (phase == TransferPhase_Running || phase == TransferPhase_Pausing ||
        phase == TransferPhase_Paused || phase == TransferPhase_Cancelling) {
        app_transfer_gauge(panel_width);
        app_transfer_running_panel(panel_width);
        return;
    }
    if (phase == TransferPhase_Done || phase == TransferPhase_Cancelled ||
        phase == TransferPhase_Failed) {
        app_transfer_gauge(panel_width);
        app_transfer_result_line();
        return;
    }
    if (app_transfer.open && phase == TransferPhase_Preflight) {
        app_transfer_preflight_panel();
        return;
    }
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *row = ui_build_box_from_key(0, 0);
        UI_Parent(row) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
            if (ui_button_primary(str8f(ui_frame_arena(), "%S###burn", app_str(Str_DiscBurn)))
                        .clicked) {
                app_transfer_open();
            }
            ui_tooltip(app_str(Str_DiscBurnHint));
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###clear", app_str(Str_DiscClear)))
                        .clicked) {
                app_plan_clear();
            }
            ui_tooltip(app_str(Str_DiscClearHint));
        }
    }
}

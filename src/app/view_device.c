// view_device.c - the device half of the Disc panel (T-020): what the panel
// says when there is no device, when there is one Windows has no driver for
// (P-001), and when the MZ-N505 is actually talking to us.
//
// It owns the device thread and is the only place that speaks to it: the panel
// drains its queue once per frame and never blocks (ADR-008).

typedef enum AppDeviceState {
    AppDeviceState_None = 0,   // nothing on the bus
    AppDeviceState_NoDriver,   // present, code 28: the guided screen
    AppDeviceState_InUse,      // another application holds it
    AppDeviceState_Connected,  // open, and it answered a ping
    AppDeviceState_Error,
    AppDeviceState_COUNT
} AppDeviceState;

typedef struct AppDevice {
    NetmdDevice thread;
    u32 state;  // AppDeviceState
    // The disc, as of the last NetmdEvent_Disc. The layout itself stays in the
    // device thread's double buffer; this is only what the panel needs to know
    // without touching it (T-021).
    b32 disc_reading;
    u32 disc_result;
    u32 disc_elapsed_ms;
    u32 group_collapsed;  // one bit per group, NETMD_GROUP_MAX <= 32
    // T-022, editing. The selection is over track indices (the same mask an
    // edit request carries), and the cursor is where the keyboard is.
    u32 selection[NETMD_MASK_WORDS];
    u32 cursor;
    u32 rename_track;  // the track being renamed, + 1
    UI_TextInput rename;
    b32 focus_editor;
    UI_Key rows_key;  // the box the disc keys are routed to
    b32 drag;
    u32 drag_from;
    u32 drag_to;
    // The edit waiting for the user to confirm it, and the simulation behind
    // the panel that asks (ADR-011 D4). Nothing is posted before Apply.
    NetmdEditRequest pending;
    b32 confirm;
    u32 last_refusal;
    u32 last_writes;
    b32 toc_dirty;
    u16 vid;
    u16 pid;
    i32 error;
    u32 name_size;
    u8 name[NETMD_NAME_MAX];
    // What the acceptance criterion measures: the moment the hotplug event
    // reached us, and the moment the panel changed because of it.
    u64 change_us;
    b32 change_pending;
} AppDevice;

global AppDevice app_device;
// The layout the edit would produce and the diff the panel draws. 70 KB of BSS
// rather than a stack frame or an arena: it is written once per gesture and
// read for as long as the confirmation panel is up.
global DiscLayout app_disc_after;
global DiscDiff app_disc_diff;

// --- the selection (T-022) ----------------------------------------------------
// It is the mask an edit request carries, so a gesture is one memcpy away from
// being an edit: nothing translates between "what is selected" and "what will
// be erased".

static void app_device_clear_selection(void) {
    for (u32 i = 0; i < NETMD_MASK_WORDS; i += 1) { app_device.selection[i] = 0; }
}

static b32 app_device_selected(u32 track) { return netmd_mask_get(app_device.selection, track); }

static u32 app_device_selected_count(void) {
    return netmd_mask_count(app_device.selection, NETMD_TRACK_MAX);
}

static void app_device_select_only(u32 track) {
    app_device_clear_selection();
    netmd_mask_set(app_device.selection, track);
    app_device.cursor = track;
}

static void app_device_select_toggle(u32 track) {
    app_device.selection[track >> 5] ^= 1u << (track & 31u);
    app_device.cursor = track;
}

static String8 app_device_name(void) {
    return str8(app_device.name, app_device.name_size);
}

void app_device_init(void) {
    // "--netmd-trace <file>": every control and bulk exchange of the session,
    // written in the format netmd_replay.c reads back. It is how the fixtures
    // under tests/netmd are meant to be captured, so it has to be reachable
    // without a debugger - one flag, like --plan and --scan.
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 trace = app_command_line_value(scratch.arena, str8_lit("--netmd-trace "));
    if (trace.size != 0) { netmd_device_set_trace(&app_device.thread, trace); }
    scratch_end(scratch);
    // 4 MB of its own: an enumeration walks the bus into it and rewinds, so it
    // never grows, and no other thread ever pushes into it. The two DiscLayout
    // buffers come off the front of it and stay.
    netmd_device_start(&app_device.thread, arena_alloc(MB(4)));
    netmd_device_post(&app_device.thread, NetmdCmd_Enumerate, 0);
}

void app_device_shutdown(void) { netmd_device_stop(&app_device.thread); }

// One OsEvent_DeviceChange: the thread debounces the burst, we only time it.
void app_device_changed(void) {
    if (!app_device.change_pending) {
        app_device.change_us = os_time_now_us();
        app_device.change_pending = 1;
    }
    netmd_device_post(&app_device.thread, NetmdCmd_DeviceChanged, 0);
}

static void app_device_set_state(u32 state) {
    if (app_device.state == state) { return; }
    app_device.state = state;
    if (app_device.change_pending) {
        u64 elapsed_ms = (os_time_now_us() - app_device.change_us) / 1000;
        app_device.change_pending = 0;
        // The measurement the acceptance criterion asks for, in the debug
        // output: plug to panel, end to end. A scratch arena and not the frame
        // one - this runs before the frame is begun.
        ArenaTemp scratch = scratch_begin(0, 0);
        os_debug_print(str8f(scratch.arena, "device: panel updated %llu ms after hotplug\n",
                             elapsed_ms));
        scratch_end(scratch);
    }
}

// Drained once per frame, like the library's scan events.
void app_device_tick(void) {
    NetmdEvent event;
    while (netmd_device_next_event(&app_device.thread, &event)) {
        app_device.vid = event.vid;
        app_device.pid = event.pid;
        app_device.name_size = Min(event.name_size, (u32)NETMD_NAME_MAX);
        if (app_device.name_size != 0) {
            mem_copy(app_device.name, event.name, app_device.name_size);
        }
        // s6.3: every event carries it, so the banner and the refusal to close
        // never lag a frame behind the device thread.
        app_device.toc_dirty = (b32)event.toc_dirty;
        switch (event.kind) {
            case NetmdEvent_Devices: {
                if (event.device_count == 0) {
                    app_device_set_state(AppDeviceState_None);
                    break;
                }
                if (event.state == OsUsbState_NoDriver) {
                    app_device_set_state(AppDeviceState_NoDriver);
                } else if (event.state == OsUsbState_InUse) {
                    app_device_set_state(AppDeviceState_InUse);
                } else {
                    // Ready is not connected yet: the handle has to answer
                    // first, which is what the ping is for.
                    netmd_device_post(&app_device.thread, NetmdCmd_Open, 0);
                }
            } break;
            case NetmdEvent_Opened: netmd_device_post(&app_device.thread, NetmdCmd_Ping, 0); break;
            case NetmdEvent_Pong: {
                app_device_set_state(AppDeviceState_Connected);
                // A device that answers gets read straight away: the criterion
                // is the disc on screen under two seconds after insertion, and
                // waiting for the user to ask would not meet it.
                app_device.disc_reading = 1;
                app_device.group_collapsed = 0;
                netmd_device_post(&app_device.thread, NetmdCmd_ReadDisc, 0);
            } break;
            case NetmdEvent_Disc: {
                app_device.disc_reading = 0;
                app_device.disc_result = event.result;
                app_device.disc_elapsed_ms = event.elapsed_ms;
                ArenaTemp scratch = scratch_begin(0, 0);
                os_debug_print(str8f(scratch.arena, "device: disc read in %u ms (%u tracks)\n",
                                     event.elapsed_ms, event.track_count));
                scratch_end(scratch);
            } break;
            case NetmdEvent_Transport: {
                // Ejecting flushes the TOC and empties the bay (s6.3): whatever
                // was on screen is gone, so the panel asks again.
                if (event.status == NetmdCmd_Eject && event.result == NetmdResult_Ok) {
                    app_device.disc_reading = 1;
                    netmd_device_post(&app_device.thread, NetmdCmd_ReadDisc, 0);
                }
            } break;
            case NetmdEvent_Edit: {
                // The device thread answers every edit exactly once, and again
                // when it has confirmed the write by reading the disc back.
                app_device.last_refusal = event.refusal;
                app_device.last_writes = event.writes;
                if (event.refusal == NetmdEditRefusal_None && event.result == NetmdResult_Ok) {
                    app_device_clear_selection();
                }
            } break;
            case NetmdEvent_Closed: app_device_set_state(AppDeviceState_None); break;
            case NetmdEvent_Error: {
                app_device.error = event.error;
                app_device_set_state(event.state == OsUsbState_InUse ? AppDeviceState_InUse
                                                                     : AppDeviceState_Error);
            } break;
            default: break;
        }
    }
}


// The panel header line, right of the title.
String8 app_device_subtitle(void) {
    if (app_device.state == AppDeviceState_Connected) { return app_device_name(); }
    if (app_device.state == AppDeviceState_None) { return app_str(Str_DeviceNone); }
    return app_str(Str_DeviceNoDriver);
}

md_inline b32 app_device_connected(void) {
    return app_device.state == AppDeviceState_Connected;
}

static void app_device_line(UI_FontStyle style, u32 color, String8 text) {
    const UI_Theme *theme = ui_theme();
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
    UI_TextAlign(UI_TextAlign_Left)
    UI_Font(ui_font(style))
    UI_TextColor(color)
    UI_TextPadding(ui_dp(theme->space[UI_Space_12])) {
        UI_Box *box = ui_build_box_from_key(UI_DrawText, 0);
        box->display_string = text;
    }
}

// The three steps and the one button of P-001, in the panel itself: the first
// wall every user of this app hits is the missing driver, so it is not hidden
// behind a dialog.
static void app_device_driver_help(void) {
    const UI_Theme *theme = ui_theme();
    app_device_line(UI_FontStyle_Emphasis, theme->fg_primary, app_str(Str_DeviceNoDriver));
    String8 name = app_device_name();
    if (name.size == 0) { name = str8_lit("Net MD Walkman"); }
    app_device_line(UI_FontStyle_Caption, theme->fg_secondary,
                    str8f(ui_frame_arena(), app_str_c(Str_DeviceNoDriverBody), name));
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
    app_device_line(UI_FontStyle_Caption, theme->fg_secondary, app_str(Str_DeviceStep1));
    app_device_line(UI_FontStyle_Caption, theme->fg_secondary, app_str(Str_DeviceStep2));
    app_device_line(UI_FontStyle_Caption, theme->fg_secondary, app_str(Str_DeviceStep3));
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *row = ui_build_box_from_key(0, 0);
        UI_Parent(row) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###zadig", app_str(Str_DeviceZadig)))
                        .clicked) {
                os_open_url(str8_lit("https://zadig.akeo.ie"));
            }
            ui_tooltip(app_str(Str_DeviceZadigHint));
        }
    }
}

// What the Disc panel shows above the capacity readout.
void app_device_status(void) {
    const UI_Theme *theme = ui_theme();
    switch (app_device.state) {
        case AppDeviceState_None: {
            app_device_line(UI_FontStyle_Ui, theme->fg_muted, app_str(Str_DeviceNone));
            app_device_line(UI_FontStyle_Caption, theme->fg_disabled,
                            app_str(Str_DeviceNoneHint));
        } break;
        case AppDeviceState_NoDriver: app_device_driver_help(); break;
        case AppDeviceState_InUse: {
            app_device_line(UI_FontStyle_Ui, theme->fg_secondary, app_str(Str_DeviceInUse));
        } break;
        case AppDeviceState_Connected: {
            app_device_line(UI_FontStyle_Emphasis, theme->fg_primary,
                            str8f(ui_frame_arena(), app_str_c(Str_DeviceConnected),
                                  app_device_name()));
        } break;
        default: {
            app_device_line(UI_FontStyle_Ui, theme->fg_secondary,
                            str8f(ui_frame_arena(), app_str_c(Str_DeviceError),
                                  app_device.error));
        } break;
    }
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
    ui_separator();
}

// --- the disc itself (T-021) -------------------------------------------------

// The device's own capacity, in the shape the gauge already knows how to lay
// out. getDiscCapacity wins over our offline cluster estimate whenever a disc
// is inserted (plan_capacity.h, Q-29): this is that rule, in one function.
global PlanCapacity app_disc_capacity;

static u32 app_disc_cap_mode(const NetmdTrack *track) {
    if (track->encoding == NetmdEncoding_LP2) { return PlanCapMode_LP2; }
    if (track->encoding == NetmdEncoding_LP4) { return PlanCapMode_LP4; }
    return track->mono ? PlanCapMode_Mono : PlanCapMode_SP;
}

static void app_disc_build_capacity(const DiscLayout *disc, PlanCapacity *out) {
    StructZero(out);
    // A cluster is 2 s of SP, and a TOC frame is 1/512 s (research/01 s7.2-7.3).
    u32 frames_per_cluster = NETMD_FRAMES_PER_SECOND * (PLAN_CLUSTER_SP_MS / 1000u);
    out->capacity_clusters = disc->capacity.total.total_frames / frames_per_cluster;
    out->length_min = (u32)(disc->capacity.total.ms / 60000u);
    out->entry_count = Min(disc->track_count, (u32)PLAN_ENTRY_MAX);
    for (u32 i = 0; i < out->entry_count; i += 1) {
        const NetmdTrack *track = &disc->tracks[i];
        u32 mode = app_disc_cap_mode(track);
        u32 clusters = plan_clusters_for((u32)track->duration_ms, mode);
        out->clusters[i] = clusters;
        out->entry_mode[i] = (u8)mode;
        out->entry_padding_ms[i] = plan_padding_ms((u32)track->duration_ms, mode);
        out->fit[i] = PlanFit_Fits;  // it is already on the disc: it fits
        out->used_clusters += clusters;
        out->audio_ms += track->duration_ms;
        out->billed_ms += (u64)clusters * PLAN_CLUSTER_SP_MS;
    }
    out->padding_ms = out->billed_ms - out->audio_ms;
    out->first_overflow = out->entry_count;
    // The device's own "available" is the truth, not our sum: a fragmented disc
    // has less room than the arithmetic says (s7.3).
    out->free_clusters = disc->capacity.available.total_frames / frames_per_cluster;
    out->remaining_entries = PLAN_ENTRY_MAX - out->entry_count;
    for (u32 mode = 0; mode < PlanCapMode_COUNT; mode += 1) {
        out->remaining_ms[mode] = out->free_clusters * MD_MODE_TABLE[mode].cluster_ms;
    }
}

// One bar, the segments of plan_gauge_layout, in the plan's own mode colours.
static void app_disc_gauge(const PlanCapacity *capacity) {
    const UI_Theme *theme = ui_theme();
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(PLAN_GAUGE_COMPACT_DP), 1.0f)) {
        UI_Box *slot = ui_build_box_from_key(0, 0);
        f32 width = rect_width(slot->rect) - ui_dp(2.0f * PLAN_GAUGE_SIDE_DP);
        if (width < 1.0f) { return; }
        PlanGaugeLayout *layout = push_struct(ui_frame_arena(), PlanGaugeLayout);
        plan_gauge_layout(capacity, width, layout);
        UI_Parent(slot) {
            f32 bar = ui_dp(PLAN_GAUGE_COMPACT_BAR_DP);
            f32 top = round_f32((ui_dp(PLAN_GAUGE_COMPACT_DP) - bar) * 0.5f);
            // The free zone under everything, then one box per segment.
            UI_FixedX(ui_dp(PLAN_GAUGE_SIDE_DP))
            UI_FixedY(top)
            UI_PrefWidth(ui_px(width, 1.0f))
            UI_PrefHeight(ui_px(bar, 1.0f))
            UI_CornerRadius(ui_dp(2.0f))
            UI_BgColor(theme->control) {
                ui_build_box_from_key(UI_FloatingX | UI_FloatingY | UI_DrawBackground, 0);
            }
            for (u32 i = 0; i < layout->count; i += 1) {
                const PlanGaugeSegment *segment = &layout->segments[i];
                u32 color = theme->mode[segment->mode];
                if (segment->alternate) { color = app_color_lighten(color, 0.06f); }
                UI_FixedX(ui_dp(PLAN_GAUGE_SIDE_DP) + segment->x)
                UI_FixedY(top)
                UI_PrefWidth(ui_px(Max(segment->width, 1.0f), 1.0f))
                UI_PrefHeight(ui_px(bar, 1.0f))
                UI_BgColor(color) {
                    // Key 0: a segment is drawn, never interacted with, so it
                    // needs no identity across frames.
                    ui_build_box_from_key(UI_FloatingX | UI_FloatingY | UI_DrawBackground, 0);
                }
            }
        }
    }
}

static void app_disc_transport_bar(void) {
    const UI_Theme *theme = ui_theme();
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *row = ui_build_box_from_key(0, 0);
        UI_Parent(row) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###dplay", app_str(Str_DiscPlay))).clicked) {
                netmd_device_post(&app_device.thread, NetmdCmd_Play, 0);
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###dpause", app_str(Str_DiscPause)))
                        .clicked) {
                netmd_device_post(&app_device.thread, NetmdCmd_Pause, 0);
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###dstop", app_str(Str_DiscStop))).clicked) {
                netmd_device_post(&app_device.thread, NetmdCmd_Stop, 0);
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###dprev", app_str(Str_DiscPrev))).clicked) {
                netmd_device_post(&app_device.thread, NetmdCmd_Prev, 0);
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###dnext", app_str(Str_DiscNext))).clicked) {
                netmd_device_post(&app_device.thread, NetmdCmd_Next, 0);
            }
        }
    }
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *row = ui_build_box_from_key(0, 0);
        UI_Parent(row) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###dreload", app_str(Str_DiscRefresh)))
                        .clicked) {
                app_device.disc_reading = 1;
                netmd_device_post(&app_device.thread, NetmdCmd_ReadDisc, 0);
            }
            ui_tooltip(app_str(Str_DiscRefreshHint));
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###deject", app_str(Str_DiscEject)))
                        .clicked) {
                netmd_device_post(&app_device.thread, NetmdCmd_Eject, 0);
            }
            ui_tooltip(app_str(Str_DiscEjectHint));
        }
    }
}


// --- editing the disc (T-022) ---------------------------------------------------
// Every gesture below does exactly one thing: it fills in a request and asks
// netmd_edit_simulate what it would do. Nothing is posted to the device thread
// until the user has seen that answer and pressed Apply (ADR-011 D4).

static void app_device_rename_open(u32 track) {
    const DiscLayout *disc = netmd_device_disc(&app_device.thread);
    if (!disc || track >= disc->track_count) { return; }
    app_device.rename_track = track + 1;
    ui_text_input_init(&app_device.rename,
                       str8((u8 *)disc->tracks[track].title, disc->tracks[track].title_size));
    ui_text_input_select_all(&app_device.rename);
    app_device.focus_editor = 1;
}

static void app_device_edit_prepare(u32 kind, u32 track, u32 dest, String8 title,
                                    const u32 *mask) {
    const DiscLayout *disc = netmd_device_disc(&app_device.thread);
    if (!disc) { return; }
    NetmdEditRequest *request = &app_device.pending;
    StructZero(request);
    request->kind = kind;
    request->track = track;
    request->dest = dest;
    request->title_size = (u32)Min(title.size, (u64)NETMD_TITLE_MAX);
    if (request->title_size != 0) { mem_copy(request->title, title.str, request->title_size); }
    if (mask) {
        for (u32 i = 0; i < NETMD_MASK_WORDS; i += 1) { request->mask[i] = mask[i]; }
    }
    netmd_edit_simulate(disc, request, &app_disc_after, &app_disc_diff);
    app_device.last_refusal = app_disc_diff.refusal;
    app_device.last_writes = 0;
    // A refused edit is not a modal: the reason goes under the track list and
    // the user is left exactly where they were.
    app_device.confirm = app_disc_diff.allowed ? 1 : 0;
}

static void app_device_rename_commit(b32 keep) {
    if (app_device.rename_track == 0) { return; }
    u32 track = app_device.rename_track - 1;
    app_device.rename_track = 0;
    if (!keep) { return; }
    // One editor, two targets: the row number NETMD_TRACK_MAX + 1 is the disc
    // title itself, which no track index can ever be.
    if (track == NETMD_TRACK_MAX) {
        app_device_edit_prepare(NetmdEditKind_RenameDisc, 0, 0,
                                ui_text_input_string(&app_device.rename), 0);
        return;
    }
    app_device_edit_prepare(NetmdEditKind_RenameTrack, track, 0,
                            ui_text_input_string(&app_device.rename), 0);
}

// The one place a refusal turns into a sentence. Every value of the enum has
// one: "it did not work" is not something a user can act on.
static Str app_device_refusal_string(u32 refusal) {
    switch (refusal) {
        case NetmdEditRefusal_NoDisc: return Str_DiscRefusedNoDisc;
        case NetmdEditRefusal_Protected: return Str_DiscRefusedProtected;
        case NetmdEditRefusal_TrackProtected: return Str_DiscRefusedTrackProtected;
        case NetmdEditRefusal_Budget: return Str_DiscRefusedBudget;
        case NetmdEditRefusal_Nothing: return Str_DiscRefusedNothing;
        case NetmdEditRefusal_Grouped: return Str_DiscRefusedGrouped;
        case NetmdEditRefusal_Backup: return Str_DiscRefusedBackup;
        default: return Str_DiscRefusedRange;
    }
}

// The verb of the confirmation panel: what is about to happen, said in words
// and with the numbers in it ("Erase 3 tracks"), never "Are you sure?".
static String8 app_device_edit_verb(void) {
    const NetmdEditRequest *request = &app_device.pending;
    switch (request->kind) {
        case NetmdEditKind_RenameDisc: return app_str(Str_DiscVerbRenameDisc);
        case NetmdEditKind_RenameTrack:
            return str8f(ui_frame_arena(), app_str_c(Str_DiscVerbRenameTrack),
                         request->track + 1);
        case NetmdEditKind_MoveTrack:
            return str8f(ui_frame_arena(), app_str_c(Str_DiscVerbMove), request->track + 1,
                         request->dest + 1);
        case NetmdEditKind_EraseTracks:
            return str8f(ui_frame_arena(), app_str_c(Str_DiscVerbErase), app_disc_diff.changed);
        case NetmdEditKind_EraseDisc:
            return str8f(ui_frame_arena(), app_str_c(Str_DiscVerbEraseDisc),
                         app_disc_diff.tracks_before);
        case NetmdEditKind_CreateGroup:
            return str8f(ui_frame_arena(), app_str_c(Str_DiscVerbGroup), app_disc_diff.changed);
        default: return app_str(Str_DiscVerbUngroup);
    }
}

static void app_device_edit_keys(const DiscLayout *disc) {
    if (ui_focus_key() != app_device.rows_key || ui_popup_active()) { return; }
    for (u32 i = 0; i < ui_key_event_count(); i += 1) {
        UI_KeyEvent event = ui_key_event(i);
        b32 ctrl = (event.modifiers & OsMod_Ctrl) != 0;
        b32 shift = (event.modifiers & OsMod_Shift) != 0;
        switch (event.key) {
            case OsKey_F2: app_device_rename_open(app_device.cursor); break;
            case OsKey_Delete: {
                app_device_edit_prepare(NetmdEditKind_EraseTracks, 0, 0, str8(0, 0),
                                        app_device.selection);
            } break;
            case OsKey_G: {
                if (ctrl && shift) {
                    if (app_device.cursor < disc->track_count &&
                        disc->tracks[app_device.cursor].group != NETMD_NO_GROUP) {
                        app_device_edit_prepare(NetmdEditKind_DissolveGroup,
                                                disc->tracks[app_device.cursor].group, 0,
                                                str8(0, 0), 0);
                    }
                } else if (ctrl) {
                    app_device_edit_prepare(NetmdEditKind_CreateGroup, 0, 0,
                                            app_str(Str_DiscEditNewGroup), app_device.selection);
                }
            } break;
            default: break;
        }
    }
}

// The bar of the four gestures, for the hands that do not know the shortcuts.
static void app_device_edit_bar(const DiscLayout *disc) {
    const UI_Theme *theme = ui_theme();
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *row = ui_build_box_from_key(0, 0);
        UI_Parent(row) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###drename", app_str(Str_DiscEditRename)))
                        .clicked) {
                app_device_rename_open(app_device.cursor);
            }
            ui_tooltip(app_str(Str_DiscEditRenameHint));
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###dgroupmake", app_str(Str_DiscEditGroup)))
                        .clicked) {
                app_device_edit_prepare(NetmdEditKind_CreateGroup, 0, 0,
                                        app_str(Str_DiscEditNewGroup), app_device.selection);
            }
            ui_tooltip(app_str(Str_DiscEditGroupHint));
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###dungroup", app_str(Str_DiscEditUngroup)))
                        .clicked &&
                app_device.cursor < disc->track_count &&
                disc->tracks[app_device.cursor].group != NETMD_NO_GROUP) {
                app_device_edit_prepare(NetmdEditKind_DissolveGroup,
                                        disc->tracks[app_device.cursor].group, 0, str8(0, 0), 0);
            }
            ui_tooltip(app_str(Str_DiscEditUngroupHint));
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###derase", app_str(Str_DiscEditErase)))
                        .clicked) {
                app_device_edit_prepare(NetmdEditKind_EraseTracks, 0, 0, str8(0, 0),
                                        app_device.selection);
            }
            ui_tooltip(app_str(Str_DiscEditEraseHint));
        }
    }
}

// The diff, in the panel, with the verb at the top and the budget at the
// bottom. This is the screen ADR-011 D4 exists for.
static void app_device_confirm_panel(void) {
    const UI_Theme *theme = ui_theme();
    ui_separator();
    app_device_line(UI_FontStyle_Emphasis, theme->warning, app_str(Str_DiscConfirmTitle));
    app_device_line(UI_FontStyle_Ui, theme->fg_primary, app_device_edit_verb());
    if (app_disc_diff.before_size != 0) {
        app_device_line(UI_FontStyle_Caption, theme->fg_secondary,
                        str8f(ui_frame_arena(), app_str_c(Str_DiscDiffBefore),
                              str8(app_disc_diff.before, app_disc_diff.before_size)));
    }
    if (app_disc_diff.after_size != 0) {
        app_device_line(UI_FontStyle_Caption, theme->fg_secondary,
                        str8f(ui_frame_arena(), app_str_c(Str_DiscDiffAfter),
                              str8(app_disc_diff.after, app_disc_diff.after_size)));
    }
    app_device_line(UI_FontStyle_Caption, theme->fg_muted,
                    str8f(ui_frame_arena(), app_str_c(Str_DiscDiffBudget),
                          app_disc_diff.cells_after, app_disc_diff.chars_free_after));
    app_device_line(UI_FontStyle_Caption, theme->fg_muted,
                    str8f(ui_frame_arena(), app_str_c(Str_DiscDiffWrites), app_disc_diff.writes));
    app_device_line(UI_FontStyle_Caption, theme->fg_muted, app_str(Str_DiscDiffBackup));

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *row = ui_build_box_from_key(0, 0);
        UI_Parent(row) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
            if (ui_button_primary(str8f(ui_frame_arena(), "%S###dapply",
                                        app_str(Str_DiscConfirmApply)))
                        .clicked) {
                netmd_device_post_edit(&app_device.thread, &app_device.pending);
                app_device.confirm = 0;
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
            if (ui_button(str8f(ui_frame_arena(), "%S###dcancel",
                                app_str(Str_DiscConfirmCancel)))
                        .clicked) {
                app_device.confirm = 0;
            }
        }
    }
    ui_separator();
}

static void app_disc_track_row(const DiscLayout *disc, u32 index, f32 indent) {
    const UI_Theme *theme = ui_theme();
    const NetmdTrack *track = &disc->tracks[index];
    b32 selected = app_device_selected(index);
    UI_Box *row = 0;
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
    UI_BgColor(selected ? theme->row_selected : theme->surface)
    UI_ChildLayoutAxis(Axis2_X) {
        row = ui_build_box(UI_Clickable | (selected ? UI_DrawBackground : 0),
                           str8f(ui_frame_arena(), "###dtrack%u", index));
        UI_Parent(row) {
            ui_spacer(ui_px(indent, 1.0f));
            app_cell_number(ui_dp(24.0f), str8f(ui_frame_arena(), "%u", index + 1),
                            theme->fg_muted);
            // The mode badge, in the plan's own colours: SP here and SP there
            // have to be the same green or the two panels cannot be compared.
            u32 mode = app_disc_cap_mode(track);
            UI_PrefWidth(ui_px(ui_dp(34.0f), 1.0f))
            UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
                UI_Box *cell = ui_build_box_from_key(0, 0);
                UI_Parent(cell)
                UI_Font(ui_font(UI_FontStyle_Caption))
                UI_FixedX(ui_dp(2.0f))
                UI_FixedY(round_f32((ui_dp(theme->row_compact) - ui_dp(15.0f)) * 0.5f))
                UI_PrefWidth(ui_px(ui_dp(30.0f), 1.0f))
                UI_PrefHeight(ui_px(ui_dp(15.0f), 1.0f))
                UI_BgColor(theme->mode[mode])
                UI_TextColor(theme->canvas)
                UI_TextAlign(UI_TextAlign_Center)
                UI_TextPadding(0.0f)
                UI_CornerRadius(ui_dp(3.0f)) {
                    UI_Box *badge = ui_build_box_from_key(
                            UI_FloatingX | UI_FloatingY | UI_DrawBackground | UI_DrawText, 0);
                    badge->display_string = str8_cstr(app_mode_names[mode]);
                }
            }
            if (app_device.rename_track == index + 1) {
                // The inline editor takes the title cell whole; Enter commits,
                // Escape puts back what was there - the plan panel's gesture,
                // because a disc track and a plan entry are renamed the same
                // way (T-032).
                UI_PrefWidth(ui_pct(1.0f, 0.0f))
                UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
                    UI_Box *slot = ui_build_box_from_key(0, 0);
                    UI_Parent(slot) {
                        UI_Signal field = ui_text_input(&app_device.rename, str8_lit(""));
                        if (app_device.focus_editor) {
                            ui_set_focus(field.box->key, 1);
                            app_device.focus_editor = 0;
                        }
                    }
                }
            } else {
                String8 title = str8((u8 *)track->title, track->title_size);
                if (title.size == 0) { title = app_str(Str_DiscTrackUntitled); }
                app_cell(ui_pct(1.0f, 0.0f), title,
                         track->title_size ? theme->fg_primary : theme->fg_disabled, 0,
                         UI_TextAlign_Left);
            }
            if (track->protect) {
                app_cell(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f), str8_lit("\xE2\x9C\xB1"),
                         theme->warning, 0, UI_TextAlign_Center);
                ui_tooltip(app_str(Str_DiscTrackProtected));
            }
            app_cell_number(ui_dp(48.0f), app_duration((u32)(track->duration_ms / 1000u)),
                            theme->fg_secondary);
        }
    }
    if (app_device.rename_track != 0) { return; }
    UI_Signal signal = ui_signal(row);
    if (signal.hovering) { app_device.drag_to = index; }
    if (signal.clicked) {
        if (signal.press_modifiers & OsMod_Ctrl) {
            app_device_select_toggle(index);
        } else {
            app_device_select_only(index);
        }
        ui_set_focus(app_device.rows_key, 0);
    }
    if (signal.double_clicked) { app_device_rename_open(index); }
    // A press that travels four pixels is a reorder, not a click (T-032).
    if (signal.dragging && !app_device.drag && abs_f32(signal.drag_delta.y) > 4.0f) {
        app_device.drag = 1;
        app_device.drag_from = index;
        app_device.drag_to = index;
    }
}

static void app_disc_group_row(const DiscLayout *disc, u32 group) {
    const UI_Theme *theme = ui_theme();
    const NetmdGroup *info = &disc->groups[group];
    b32 collapsed = (app_device.group_collapsed & (1u << group)) != 0;
    UI_Box *header = 0;
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        header = ui_build_box(UI_Clickable,
                              str8f(ui_frame_arena(), "###dgroup%u", group));
        UI_Parent(header) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            app_cell(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f),
                     collapsed ? str8_lit("\xE2\x80\xBA") : str8_lit("\xE2\x8C\x84"),
                     theme->fg_muted, 0, UI_TextAlign_Center);
            String8 name = str8((u8 *)info->name, info->name_size);
            app_cell(ui_pct(1.0f, 0.0f), name, theme->fg_primary, 0, UI_TextAlign_Left);
            app_cell_number(ui_dp(32.0f), str8f(ui_frame_arena(), "%u", info->count),
                            theme->fg_muted);
        }
    }
    if (ui_signal(header).clicked) { app_device.group_collapsed ^= (1u << group); }
    if (collapsed) { return; }
    for (u32 i = 0; i < info->count; i += 1) {
        app_disc_track_row(disc, (u32)info->first + i, ui_dp(theme->space[UI_Space_12]));
    }
}

// The whole panel below the device status: title, gauge, transport, tracks.
void app_device_disc_panel(void) {
    const UI_Theme *theme = ui_theme();
    if (app_device.state != AppDeviceState_Connected) { return; }
    if (app_device.disc_reading) {
        app_device_line(UI_FontStyle_Ui, theme->fg_muted, app_str(Str_DiscReading));
        return;
    }
    const DiscLayout *disc = netmd_device_disc(&app_device.thread);
    if (!disc || (disc->flags & NetmdDiscFlag_Present) == 0) {
        if (app_device.disc_result != NetmdResult_Ok) {
            app_device_line(UI_FontStyle_Ui, theme->fg_secondary,
                            str8f(ui_frame_arena(), app_str_c(Str_DiscReadError),
                                  app_device.disc_result));
        } else {
            app_device_line(UI_FontStyle_Ui, theme->fg_muted, app_str(Str_DiscNone));
        }
        return;
    }

    // The disc title, and the cells the whole TOC spends (D3): both are read
    // from the layout the device thread published, never recomputed elsewhere.
    u32 app_disc_cells = netmd_layout_cells(disc, 0, 0, 0);
    if (app_device.rename_track == NETMD_TRACK_MAX + 1) {
        UI_PrefWidth(ui_pct(1.0f, 0.0f))
        UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f)) {
            UI_Box *slot = ui_build_box_from_key(0, 0);
            UI_Parent(slot) {
                UI_Signal field = ui_text_input(&app_device.rename, str8_lit(""));
                if (app_device.focus_editor) {
                    ui_set_focus(field.box->key, 1);
                    app_device.focus_editor = 0;
                }
            }
        }
    } else {
        String8 title = str8((u8 *)disc->title, disc->title_size);
        if (title.size == 0) { title = app_str(Str_DiscUntitled); }
        UI_PrefWidth(ui_pct(1.0f, 0.0f))
        UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f)) {
            UI_Box *box = ui_build_box(UI_Clickable, str8_lit("###ddisctitle"));
            UI_Parent(box) {
                app_device_line(UI_FontStyle_Emphasis,
                                disc->title_size ? theme->fg_primary : theme->fg_disabled,
                                title);
            }
            if (ui_signal(box).double_clicked) {
                // The disc title is renamed like a track title; the editor tells
                // them apart by a row number no track can have.
                app_device.rename_track = NETMD_TRACK_MAX + 1;
                ui_text_input_init(&app_device.rename,
                                   str8((u8 *)disc->title, disc->title_size));
                ui_text_input_select_all(&app_device.rename);
                app_device.focus_editor = 1;
            }
        }
    }
    app_device_line(UI_FontStyle_Caption, theme->fg_secondary,
                    str8f(ui_frame_arena(), app_str_c(Str_DiscSummary), disc->track_count,
                          app_duration((u32)(disc->capacity.recorded.ms / 1000u)),
                          app_duration((u32)(disc->capacity.total.ms / 1000u))));
    app_device_line(UI_FontStyle_Caption, theme->fg_muted,
                    str8f(ui_frame_arena(), app_str_c(Str_DiscRemaining),
                          app_duration((u32)(disc->capacity.available.ms / 1000u))));
    if (disc->flags & NetmdDiscFlag_WriteProtected) {
        app_device_line(UI_FontStyle_Caption, theme->warning, app_str(Str_DiscProtected));
    }

    // s6.3: the device is holding a TOC the disc does not have yet. This is the
    // banner ADR-011 D4 asks for, and the reason the app will not close.
    if (app_device.toc_dirty) {
        app_device_line(UI_FontStyle_Emphasis, theme->warning, app_str(Str_DiscTocDirty));
        app_device_line(UI_FontStyle_Caption, theme->fg_secondary,
                        app_str(Str_DiscTocDirtyHint));
    }

    app_disc_build_capacity(disc, &app_disc_capacity);
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
    app_disc_gauge(&app_disc_capacity);
    // The title budget, in the same panel as the audio one: both are finite,
    // both are shared, and only one of them is usually known about (D3).
    app_device_line(UI_FontStyle_Caption, theme->fg_muted,
                    str8f(ui_frame_arena(), app_str_c(Str_DiscDiffBudget), app_disc_cells,
                          (PLAN_TOC_CELLS > app_disc_cells)
                                  ? (PLAN_TOC_CELLS - app_disc_cells) * PLAN_TOC_CELL_CHARS
                                  : 0u));
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
    app_disc_transport_bar();
    app_device_edit_bar(disc);
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));

    // The edit waiting to be confirmed, or the reason the last one was refused.
    if (app_device.confirm) {
        app_device_confirm_panel();
    } else if (app_device.last_refusal != NetmdEditRefusal_None) {
        app_device_line(UI_FontStyle_Caption, theme->warning,
                        app_str(app_device_refusal_string(app_device.last_refusal)));
    } else if (app_device.last_writes != 0) {
        app_device_line(UI_FontStyle_Caption, theme->fg_muted,
                        str8f(ui_frame_arena(), app_str_c(Str_DiscEditDone),
                              app_device.last_writes));
    }

    // One keyed box around the rows: it is what the keyboard is routed to, so
    // F2, Delete and Ctrl+G only fire when the disc list is the thing in hand.
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_children_sum(1.0f))
    UI_ChildLayoutAxis(Axis2_Y) {
        UI_Box *rows = ui_build_box(UI_Clickable, str8_lit("###discrows"));
        app_device.rows_key = rows->key;
        UI_Parent(rows) {
            // s3.10: tracks in no group come first, then each group in TOC order.
            if (disc->ungrouped_count != 0 && disc->group_count != 0) {
                app_device_line(UI_FontStyle_Caption, theme->fg_muted,
                                app_str(Str_DiscUngrouped));
            }
            for (u32 i = 0; i < disc->track_count; i += 1) {
                if (disc->tracks[i].group == NETMD_NO_GROUP) {
                    app_disc_track_row(disc, i, 0.0f);
                }
            }
            for (u32 group = 0; group < disc->group_count; group += 1) {
                app_disc_group_row(disc, group);
            }
        }
    }
    app_device_edit_keys(disc);

    // The drop: the row the pointer was last over is the destination, and the
    // move is proposed like every other edit - simulated, then confirmed.
    if (app_device.drag && ui_active_key() == 0) {
        u32 from = app_device.drag_from;
        u32 to = app_device.drag_to;
        app_device.drag = 0;
        if (from != to && from < disc->track_count && to < disc->track_count) {
            app_device_edit_prepare(NetmdEditKind_MoveTrack, from, to, str8(0, 0), 0);
        }
    }

    // The inline editor commits on Enter and gives up on Escape (T-032).
    if (app_device.rename_track != 0) {
        b32 escape = ui_escape_pressed();
        b32 enter = 0;
        for (u32 i = 0; i < ui_key_event_count(); i += 1) {
            if (ui_key_event(i).key == OsKey_Enter) { enter = 1; }
        }
        if (enter || escape) {
            app_device_rename_commit(enter);
            ui_set_focus(app_device.rows_key, 1);
        }
    }
}

// The application refuses to close while the device holds a TOC its disc does
// not have (s6.3): quitting there is exactly how a disc is lost.
b32 app_device_can_close(void) { return !netmd_device_toc_dirty(&app_device.thread); }

void app_device_close_blocked(void) {
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena, "%S\n", app_str(Str_DiscCloseBlocked)));
    scratch_end(scratch);
    os_request_redraw();
}

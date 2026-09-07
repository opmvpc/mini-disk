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

static void app_disc_track_row(const DiscLayout *disc, u32 index, f32 indent) {
    const UI_Theme *theme = ui_theme();
    const NetmdTrack *track = &disc->tracks[index];
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *row = ui_build_box_from_key(0, 0);
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
            String8 title = str8((u8 *)track->title, track->title_size);
            if (title.size == 0) { title = app_str(Str_DiscTrackUntitled); }
            app_cell(ui_pct(1.0f, 0.0f), title,
                     track->title_size ? theme->fg_primary : theme->fg_disabled, 0,
                     UI_TextAlign_Left);
            if (track->protect) {
                app_cell(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f), str8_lit("\xE2\x9C\xB1"),
                         theme->warning, 0, UI_TextAlign_Center);
                ui_tooltip(app_str(Str_DiscTrackProtected));
            }
            app_cell_number(ui_dp(48.0f), app_duration((u32)(track->duration_ms / 1000u)),
                            theme->fg_secondary);
        }
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

    String8 title = str8((u8 *)disc->title, disc->title_size);
    if (title.size == 0) { title = app_str(Str_DiscUntitled); }
    app_device_line(UI_FontStyle_Emphasis,
                    disc->title_size ? theme->fg_primary : theme->fg_disabled, title);
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

    app_disc_build_capacity(disc, &app_disc_capacity);
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
    app_disc_gauge(&app_disc_capacity);
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
    app_disc_transport_bar();
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));

    // s3.10: tracks in no group come first, then each group in TOC order.
    if (disc->ungrouped_count != 0 && disc->group_count != 0) {
        app_device_line(UI_FontStyle_Caption, theme->fg_muted, app_str(Str_DiscUngrouped));
    }
    for (u32 i = 0; i < disc->track_count; i += 1) {
        if (disc->tracks[i].group == NETMD_NO_GROUP) { app_disc_track_row(disc, i, 0.0f); }
    }
    for (u32 group = 0; group < disc->group_count; group += 1) {
        app_disc_group_row(disc, group);
    }
}

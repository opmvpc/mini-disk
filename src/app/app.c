// app.c - the frame loop and the two panels that are still sketches: the plan
// and the disc (T-030 and T-022 will make them real). The application state
// lives in app_state.c, the library panel in view_library.c.
//
// The loop still only wakes on an event or while something animates (ADR-004).
#define APP_MAX_EVENTS 256

// --- the plan panel ---------------------------------------------------------
// The minimal wiring of T-030: the real view, with drag and drop, inline
// editing and the group headers, is T-032. What is real here is the model
// underneath - every row comes from the document, and every key goes through a
// command, so Ctrl+Z takes back the last thing that happened whatever it was.
static u32 app_plan_ui_mode(const PlanDisc *disc, u32 index) {
    if (disc->flags[index] & PlanEntryFlag_Mono) { return UI_Mode_Mono; }
    if (disc->mode[index] == PlanMode_LP2) { return UI_Mode_LP2; }
    if (disc->mode[index] == PlanMode_LP4) { return UI_Mode_LP4; }
    return UI_Mode_SP;
}

// Delete, Ctrl+Z and Ctrl+Y, unless a text field owns the keyboard - in which
// case Delete is a character deletion and nothing to do with the plan.
static void app_plan_keys(void) {
    if (ui_focus_key() != 0) { return; }
    PlanDisc *disc = app_plan_disc();
    for (u32 i = 0; i < ui_key_event_count(); i += 1) {
        UI_KeyEvent event = ui_key_event(i);
        b32 ctrl = (event.modifiers & OsMod_Ctrl) != 0;
        b32 shift = (event.modifiers & OsMod_Shift) != 0;
        if (ctrl && event.key == OsKey_Z) {
            if (shift) { plan_redo(&app.plan); } else { plan_undo(&app.plan); }
        } else if (ctrl && event.key == OsKey_Y) {
            plan_redo(&app.plan);
        } else if (event.key == OsKey_Delete && disc->entry_count != 0) {
            plan_remove(&app.plan, 0, app.plan_cursor);
        } else if (event.key == OsKey_Up && app.plan_cursor != 0) {
            app.plan_cursor -= 1;
        } else if (event.key == OsKey_Down && app.plan_cursor + 1 < disc->entry_count) {
            app.plan_cursor += 1;
        }
    }
}

static void app_plan_panel(void) {
    const UI_Theme *theme = ui_theme();
    app_plan_keys();
    PlanDisc *disc = app_plan_disc();
    u32 count = disc->entry_count;
    if (app.plan_cursor >= count) { app.plan_cursor = count ? count - 1 : 0; }

    String8 subtitle = str8f(ui_frame_arena(), app_str_c(Str_PlanSubtitle), count,
                             app_duration((u32)(plan_disc_duration_ms(disc) / 1000)));
    app_panel_begin(str8_lit("###plan"), ui_pct(1.0f, 0.0f), app_str(Str_PlanTitle), subtitle);

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_pct(1.0f, 0.0f))
    UI_ChildLayoutAxis(Axis2_Y)
    UI_BgColor(theme->surface) {
        UI_Box *body = ui_build_box_from_key(UI_DrawBackground | UI_Clip, 0);
        UI_Parent(body) {
            if (count == 0) {
                UI_PrefHeight(ui_px(ui_dp(theme->row_comfortable), 1.0f))
                UI_TextPadding(ui_dp(theme->space[UI_Space_12])) {
                    ui_label_styled(UI_FontStyle_Ui, theme->fg_muted, app_str(Str_PlanEmptyBody));
                }
            }
            for (u32 i = 0; i < count; i += 1) {
                b32 missing = (disc->flags[i] & PlanEntryFlag_Missing) != 0;
                u32 background = (i == app.plan_cursor) ? theme->row_selected : theme->surface;
                UI_Seed(hash64_mix((u64)i + 1))
                UI_PrefWidth(ui_pct(1.0f, 0.0f))
                UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
                UI_ChildLayoutAxis(Axis2_X)
                UI_BgColor(background) {
                    UI_Box *row = ui_build_box(UI_Clickable | UI_DrawBackground,
                                               str8_lit("###planrow"));
                    if (ui_signal(row).clicked) { app.plan_cursor = i; }
                    UI_Parent(row) {
                        app_cell_number(ui_dp(32.0f), str8f(ui_frame_arena(), "%u", i + 1),
                                        theme->fg_muted);
                        // The mode pastille: colour carries meaning, only here.
                        UI_PrefWidth(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f))
                        UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
                            UI_Box *cell = ui_build_box_from_key(0, 0);
                            UI_Parent(cell)
                            UI_FixedY(ui_dp(7.0f))
                            UI_PrefWidth(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f))
                            UI_PrefHeight(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f))
                            UI_CornerRadius(ui_dp(4.0f))
                            UI_BgColor(theme->mode[app_plan_ui_mode(disc, i)]) {
                                ui_build_box_from_key(UI_FloatingY | UI_DrawBackground, 0);
                            }
                        }
                        // A source that no longer resolves stays in the plan and
                        // says so; nothing is dropped behind the user's back (B-27).
                        app_cell(ui_pct(1.0f, 0.0f),
                                 plan_entry_title(&app.plan, &app.library, 0, i),
                                 missing ? theme->danger : theme->fg_primary, 0,
                                 UI_TextAlign_Left);
                        app_cell_number(ui_dp(64.0f), app_duration(disc->duration_ms[i] / 1000),
                                        theme->fg_secondary);
                    }
                }
            }
        }
    }
    app_panel_end();
}

// A capacity gauge: one segment per planned track, coloured by its mode.
static void app_disc_panel(f32 width) {
    const UI_Theme *theme = ui_theme();
    f32 capacity_s = 80.0f * 60.0f;
    PlanDisc *disc = app_plan_disc();
    u32 used = (u32)(plan_disc_duration_ms(disc) / 1000);
    app_panel_begin(str8_lit("###disc"), ui_px(width, 1.0f), app_str(Str_DiscTitle),
                    str8_lit("MZ-N505"));

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_pct(1.0f, 0.0f))
    UI_ChildLayoutAxis(Axis2_Y)
    UI_BgColor(theme->surface) {
        UI_Box *body = ui_build_box_from_key(UI_DrawBackground | UI_Clip, 0);
        UI_Parent(body) UI_TextPadding(ui_dp(theme->space[UI_Space_12])) {
            ui_label_styled(UI_FontStyle_Emphasis, theme->fg_primary,
                            str8f(ui_frame_arena(), "%S / 80:00", app_duration(used)));

            // The gauge: free space in control, then one segment per track.
            UI_PrefWidth(ui_pct(1.0f, 0.0f))
            UI_PrefHeight(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f))
            UI_ChildLayoutAxis(Axis2_X)
            UI_BgColor(theme->control)
            UI_CornerRadius(ui_dp(theme->space[UI_Space_2])) {
                UI_Box *gauge = ui_build_box_from_key(UI_DrawBackground, 0);
                UI_Parent(gauge) UI_CornerRadius(0.0f) {
                    for (u32 i = 0; i < disc->entry_count; i += 1) {
                        f32 fraction = (f32)disc->duration_ms[i] / (1000.0f * capacity_s);
                        UI_PrefWidth(ui_pct(fraction, 0.0f))
                        UI_PrefHeight(ui_pct(1.0f, 1.0f))
                        UI_BgColor(theme->mode[app_plan_ui_mode(disc, i)]) {
                            ui_build_box_from_key(UI_DrawBackground, 0);
                        }
                    }
                }
            }

            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            static const char *mode_names[UI_Mode_COUNT] = {"SP", "Mono", "LP2", "LP4"};
            for (u32 mode = 0; mode < UI_Mode_COUNT; mode += 1) {
                UI_PrefWidth(ui_pct(1.0f, 0.0f))
                UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
                UI_ChildLayoutAxis(Axis2_X) {
                    UI_Box *row = ui_build_box_from_key(0, 0);
                    UI_Parent(row) {
                        UI_PrefWidth(ui_px(ui_dp(theme->space[UI_Space_24]), 1.0f))
                        UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
                            UI_Box *cell = ui_build_box_from_key(0, 0);
                            UI_Parent(cell)
                            UI_FixedX(ui_dp(theme->space[UI_Space_12]))
                            UI_FixedY(ui_dp(7.0f))
                            UI_PrefWidth(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f))
                            UI_PrefHeight(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f))
                            UI_CornerRadius(ui_dp(4.0f))
                            UI_BgColor(theme->mode[mode]) {
                                ui_build_box_from_key(UI_FloatingX | UI_FloatingY |
                                                          UI_DrawBackground,
                                                      0);
                            }
                        }
                        app_cell(ui_text_size(app_cell_padding(), 1.0f),
                                 str8_cstr(mode_names[mode]), theme->fg_secondary, 0,
                                 UI_TextAlign_Left);
                    }
                }
            }

            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
            UI_PrefWidth(ui_pct(1.0f, 0.0f))
            UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
            UI_ChildLayoutAxis(Axis2_X) {
                UI_Box *row = ui_build_box_from_key(0, 0);
                UI_Parent(row) {
                    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
                    if (ui_button_primary(str8f(ui_frame_arena(), "%S###burn",
                                                app_str(Str_DiscBurn)))
                            .clicked) {
                        app_plan_clear();
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
    }
    app_panel_end();
}

static void app_toolbar(void) {
    const UI_Theme *theme = ui_theme();
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_comfortable), 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->panel) {
        UI_Box *bar = ui_build_box_from_key(UI_DrawBackground, 0);
        UI_Parent(bar) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f)) {
                if (ui_button_icon(R_Icon_Disc, str8_lit("###device")).clicked) {}
                ui_tooltip(app_str(Str_ToolbarDeviceHint));
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
                if (ui_button_icon(R_Icon_Play, str8_lit("###preview")).clicked) {}
                ui_tooltip(app_str(Str_ToolbarPreviewHint));
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
                if (app.scan_active) {
                    if (ui_button(str8f(ui_frame_arena(), "%S###folder",
                                        app_str(Str_ToolbarCancelScan)))
                            .clicked) {
                        lib_scan_cancel(&app.scan);
                    }
                    ui_tooltip(app_str(Str_ToolbarCancelScanHint));
                } else if (ui_button(str8f(ui_frame_arena(), "%S###folder",
                                           app_str(Str_ToolbarAddFolder)))
                               .clicked) {
                    app_scan_start();
                }
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
                if (ui_button(str8f(ui_frame_arena(), "%S###add", app_str(Str_ToolbarAddToPlan)))
                        .clicked) {
                    app_plan_add_selection();
                }
                ui_tooltip(app_str(Str_ToolbarAddToPlanHint));
            }
            ui_spacer(ui_pct(1.0f, 0.0f));
            UI_TextPadding(ui_dp(theme->space[UI_Space_12])) {
                ui_label_styled(UI_FontStyle_Caption, theme->fg_muted,
                                str8f(ui_frame_arena(), app_str_c(Str_ToolbarSelected),
                                      ui_list_selected_count(&app.list)));
            }
        }
    }
}

static void app_status_bar(void) {
    const UI_Theme *theme = ui_theme();
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->panel)
    UI_Font(ui_font(UI_FontStyle_Caption))
    UI_TextPadding(ui_dp(theme->space[UI_Space_12])) {
        UI_Box *bar = ui_build_box_from_key(UI_DrawBackground, 0);
        UI_Parent(bar) {
            ui_label_styled(UI_FontStyle_Caption, theme->fg_muted,
                            str8f(ui_frame_arena(), app_str_c(Str_StatusBoxes), app_track_count(),
                                  app.list.box_count, app.list.visible_count,
                                  ui_frame_box_count()));
            ui_spacer(ui_pct(1.0f, 0.0f));
            ui_label_styled(UI_FontStyle_Caption, theme->fg_muted, app_str(Str_StatusKeys));
        }
    }
}

// The three panels and the two splitters between them. The disc is measured
// from the right and the library from the left; each one is clamped against
// what the *other two* need, which is what keeps all three on screen down to
// 1024 x 640 logical (the bug T-012 left behind, where the library could push
// the disc off a 1200 px screen at 127 %).
static void app_body(void) {
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_pct(1.0f, 0.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        // A keyed box, not an anonymous one: the splitters clamp against the
        // width this box had *last* frame, and an unkeyed box is rebuilt from
        // scratch every frame with an empty rect - which is why the clamps
        // never fired before and the disc panel could be pushed off screen.
        UI_Box *body = ui_build_box(0, str8_lit("###body"));
        UI_Parent(body) {
            f32 scale = ui_dpi_scale();
            // Splitter sizes are physical pixels; a DPI change (another
            // monitor, a display setting) rescales them once, here.
            if (app.split_scale == 0.0f) { app.split_scale = scale; }
            if (scale != app.split_scale) {
                f32 factor = scale / app.split_scale;
                app.library_split.size *= factor;
                app.library_split.default_size *= factor;
                app.disc_split.size *= factor;
                app.disc_split.default_size *= factor;
                app.split_scale = scale;
            }
            f32 handle = ui_dp(ui_theme()->splitter_size);
            f32 total = rect_width(body->rect);

            app.disc_split.min_trailing = APP_MIN_DISC_DP * scale;
            app.disc_split.min_leading = (APP_MIN_LIBRARY_DP + APP_MIN_PLAN_DP) * scale + handle;
            f32 disc_width = ui_splitter_update(&app.disc_split, Axis2_X, total);

            app.library_split.min_leading = APP_MIN_LIBRARY_DP * scale;
            app.library_split.min_trailing = APP_MIN_PLAN_DP * scale + handle + disc_width;
            f32 library_width = ui_splitter_update(&app.library_split, Axis2_X, total);

            app_library_panel(library_width);
            ui_splitter(&app.library_split, Axis2_X);
            app_plan_panel();
            ui_splitter(&app.disc_split, Axis2_X);
            app_disc_panel(disc_width);
        }
    }
}

static void app_build_ui(void) {
    const UI_Theme *theme = ui_theme();
    UI_Box *root = ui_root(UI_Layer_Content);
    root->flags |= UI_DrawBackground;
    root->bg_color = theme->canvas;
    root->child_layout_axis = Axis2_Y;

    UI_Parent(root) {
        app_toolbar();
        ui_separator();
        app_body();
        ui_separator();
        app_status_bar();
    }
    app_library_context_menu();
}

// A dropped folder is a library folder; a dropped file means the folder it is
// in. This is the other half of the empty state's invitation.
static void app_drop(const OsEvent *event) {
    app.drag_active = 0;
    if (event->path_count == 0) { return; }
    // Every dropped path is a folder to watch; the same folder twice is one.
    // The scan queue of app_scan_tick takes them one after the other.
    for (u64 i = 0; i < event->path_count; i += 1) {
        OsFileInfo info;
        StructZero(&info);
        if (!os_file_stat(event->paths[i], &info)) { continue; }
        String8 folder = lib_drop_folder(event->paths[i], info.is_dir);
        if (prefs_add_folder(&app.prefs, folder)) { app.prefs_dirty = 1; }
        if (!app.scan_active) {
            app_scan_folder(folder);
            app.scan_folder = app.prefs.folder_count;
        }
    }
}

static void app_save_placement(OsWindow window) {
    OsWindowPlacement placement;
    StructZero(&placement);
    os_window_get_placement(window, &placement);
    f32 scale = os_window_dpi_scale(window);
    Prefs *prefs = &app.prefs;
    prefs->window_x = placement.x;
    prefs->window_y = placement.y;
    prefs->window_width = (u32)((f32)placement.width / scale);
    prefs->window_height = (u32)((f32)placement.height / scale);
    prefs->window_maximized = placement.maximized;
    prefs->window_placed = 1;
    app.prefs_dirty = 1;
}

static void app_run(void) {
    Arena *permanent = arena_alloc(MB(256));
    Arena *frame_arena = arena_alloc(MB(64));
    os_events_set_frame_arena(frame_arena);
    jobs_init(0);  // one worker per logical core minus this thread
    ui_debug_overlay_set_arenas(permanent, frame_arena);

    // The preferences decide how big the window comes back, so they are read
    // before anything else exists.
    app_prefs_init(permanent);
    OsWindow window = os_window_create(str8_lit("minidisk"), app.prefs.window_width,
                                       app.prefs.window_height);
    // The window was created at the *system* DPI; the monitor it actually
    // landed on may have another one, and the preferences are in dp. Size it
    // again now that the real scale is known, before it is ever shown - this is
    // what kept the three panels from fitting at 1200 px / 127 % (T-012).
    {
        OsWindowPlacement placement;
        StructZero(&placement);
        os_window_get_placement(window, &placement);
        f32 window_scale = os_window_dpi_scale(window);
        if (app.prefs.window_placed) {
            placement.x = app.prefs.window_x;
            placement.y = app.prefs.window_y;
        }
        placement.width = (u32)((f32)app.prefs.window_width * window_scale);
        placement.height = (u32)((f32)app.prefs.window_height * window_scale);
        placement.maximized = app.prefs.window_maximized;
        os_window_set_placement(window, &placement);
    }

    if (!os_gl_init(window) || !r_init(permanent)) {
        os_debug_print(str8_lit("minidisk: OpenGL 3.3 core is required, aborting\n"));
        os_exit(2);
    }
    f32 scale = os_window_dpi_scale(window);
    r_icons_build(frame_arena, (u32)(16.0f * scale));
    if (!os_font_init() || !ui_fonts_build(scale)) {
        os_debug_print(str8_lit("minidisk: no usable system font, aborting\n"));
        os_exit(2);
    }
    UI_Theme theme;
    ui_theme_dark(&theme);
    ui_theme_set(&theme);
    ui_text_init(permanent);
    ui_init(permanent);
    app_init(permanent, scale);
    os_window_show(window, app.prefs.window_maximized);
    // The real DPI of the monitor is only known once the window is on it: if it
    // is not the one the size was computed with, size the window again.
    f32 shown_scale = os_window_dpi_scale(window);
    if (!app.prefs.window_maximized && shown_scale != scale) {
        OsWindowPlacement placement;
        StructZero(&placement);
        os_window_get_placement(window, &placement);
        placement.width = (u32)((f32)app.prefs.window_width * shown_scale);
        placement.height = (u32)((f32)app.prefs.window_height * shown_scale);
        os_window_set_placement(window, &placement);
    }

    // 256 OsEvent is 24 KB: on the arena, not on the stack (C6262).
    OsEvent *events = push_array(permanent, OsEvent, APP_MAX_EVENTS);
    u64 last_us = os_time_now_us();
    b32 running = 1;
    while (running) {
        // A running scan is the only thing besides an animation that makes the
        // loop wake on its own: at rest the timeout is still infinite (P-005).
        // A dirty plan needs one wake up within five seconds so the autosave
        // can run; at rest, with nothing to write, the wait is still infinite.
        b32 busy = ui_animating() || app.scan_active;
        u64 timeout = busy ? 16000 : (app.plan.dirty ? PLAN_AUTOSAVE_US : OS_TIMEOUT_INFINITE);
        os_events_pump(1, timeout);
        app_scan_tick();
        app_plan_tick();

        u64 event_count = 0;
        OsEvent event;
        while (os_event_next(&event)) {
            if (event.kind == OsEvent_Close) { running = 0; }
            if (event.kind == OsEvent_KeyDown && event.key == OsKey_F11) {
                ui_debug_overlay_toggle();
            }
            if (event.kind == OsEvent_DropFiles) { app_drop(&event); }
            if (event.kind == OsEvent_DragEnter || event.kind == OsEvent_DragOver) {
                app.drag_active = 1;
                app.drag_pos = event.pos;
            }
            if (event.kind == OsEvent_DragLeave) { app.drag_active = 0; }
            if (event.kind == OsEvent_DpiChanged) {
                r_atlas_reset();
                r_thumbs_reset();
                ui_text_reset();
                scale = event.dpi_scale;
                r_icons_build(frame_arena, (u32)(16.0f * scale));
                ui_fonts_build(scale);
            }
            if (event_count < APP_MAX_EVENTS) {
                events[event_count] = event;
                event_count += 1;
            }
            os_request_redraw();
        }
        if (!running) { break; }

        u64 now_us = os_time_now_us();
        f32 dt = (f32)(now_us - last_us) * 0.000001f;
        last_us = now_us;

        if (os_redraw_requested() || ui_animating() || app.scan_active) {
            V2 size = os_window_get_size(window);
            r_begin_frame(frame_arena, size.x, size.y, scale);
            r_clear(ui_theme()->canvas);
            app_covers_begin_frame();
            ui_begin(frame_arena, events, event_count, dt, size, scale);
            app_build_ui();
            ui_debug_overlay_build();
            ui_widgets_end_frame();
            ui_end();
            r_end_frame();
            ui_debug_overlay_end_frame();
        }
        arena_clear(frame_arena);
    }

    app_save_placement(window);
    app_shutdown();
    jobs_shutdown();
    os_font_shutdown();
    r_shutdown();
    os_gl_shutdown();
    os_window_destroy(window);
    arena_release(frame_arena);
    arena_release(permanent);
}

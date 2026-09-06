// app.c - demo loop of T-006: three columns built with the three interesting
// size kinds, a panel that resizes with an animation, and a keyboard focus ring
// that Tab walks around. The loop only wakes on an event or while something is
// still animating (ADR-004).

#define APP_MAX_EVENTS 256

typedef struct AppDemo {
    b32 panel_wide;      // the animated panel target
    f32 panel_width;     // smoothed towards the target
    u32 click_count;
    b32 rows_dense;
} AppDemo;

global AppDemo app_demo;

// The palette of the demo: premultiplied, computed once per use, no theme yet.
static u32 app_bg(void) { return r_rgb(0x111113); }
static u32 app_panel(void) { return r_rgb(0x1A1A20); }
static u32 app_surface(void) { return r_rgb(0x24242C); }
static u32 app_accent(void) { return r_rgba(0xF4, 0x9A, 0x2A, 255); }

static void app_column_header(String8 title, String8 subtitle) {
    UI_Font(ui_font(UI_FontStyle_Emphasis))
    UI_TextColor(r_rgba(0xF0, 0xF0, 0xF6, 255)) {
        ui_label(title);
    }
    UI_Font(ui_font(UI_FontStyle_Caption))
    UI_TextColor(r_rgba(0x8A, 0x8A, 0x96, 255)) {
        ui_label(subtitle);
    }
}

static void app_build_ui(f32 scale, f32 dt) {
    // The animated panel: one exponential step per frame, and the loop keeps
    // spinning only because ui_animating() stays true until it converges.
    Unused(dt);
    f32 target = app_demo.panel_wide ? 420.0f * scale : 200.0f * scale;
    app_demo.panel_width = ui_animate(app_demo.panel_width, target, UI_ANIM_RATE_SLOW);

    UI_Box *root = ui_root(UI_Layer_Content);
    root->flags |= UI_DrawBackground;
    root->bg_color = app_bg();

    ui_push_child_layout_axis(Axis2_X);
    ui_push_pref_width(ui_pct(1.0f, 0.0f));
    ui_push_pref_height(ui_pct(1.0f, 0.0f));
    UI_Box *row = ui_build_box(0, str8_lit("###main_row"));
    ui_pop_pref_height();
    ui_pop_pref_width();
    ui_pop_child_layout_axis();

    UI_Parent(row) {
        // --- column 1: ChildrenSum, the column is as wide as its widest child
        UI_PrefWidth(ui_children_sum(1.0f))
        UI_PrefHeight(ui_pct(1.0f, 1.0f))
        UI_ChildLayoutAxis(Axis2_Y)
        UI_BgColor(app_panel())
        UI_CornerRadius(0.0f) {
            UI_Box *column = ui_build_box(UI_DrawBackground, str8_lit("###col_sum"));
            UI_Parent(column) {
                UI_TextPadding(14.0f * scale) {
                    app_column_header(str8_lit("ChildrenSum"),
                                      str8_lit("largeur = celle du plus large enfant"));
                    UI_PrefWidth(ui_text_size(14.0f * scale, 1.0f))
                    UI_PrefHeight(ui_px(28.0f * scale, 1.0f))
                    UI_BgColor(app_surface())
                    UI_CornerRadius(4.0f * scale) {
                        UI_Signal wide = ui_button(str8_lit("Panneau large / etroit##wide"));
                        if (wide.clicked || wide.key_pressed) {
                            app_demo.panel_wide = !app_demo.panel_wide;
                        }
                        UI_Signal dense = ui_button(str8_lit("Lignes denses##dense"));
                        if (dense.clicked || dense.key_pressed) {
                            app_demo.rows_dense = !app_demo.rows_dense;
                        }
                        UI_Signal count = ui_button(str8_lit("Compter les clics##count"));
                        if (count.clicked || count.key_pressed) { app_demo.click_count += 1; }
                    }
                    ui_labelf("clics: %u", app_demo.click_count);
                }
            }
        }

        // --- column 2: PercentOfParent, fills what the two others leave
        UI_PrefWidth(ui_pct(1.0f, 0.0f))
        UI_PrefHeight(ui_pct(1.0f, 1.0f))
        UI_ChildLayoutAxis(Axis2_Y)
        UI_BgColor(r_rgb(0x141419)) {
            UI_Box *column = ui_build_box(UI_DrawBackground | UI_Clip, str8_lit("###col_pct"));
            UI_Parent(column) UI_TextPadding(14.0f * scale) {
                app_column_header(str8_lit("PercentOfParent"),
                                  str8_lit("strictness 0 : cette colonne encaisse les violations"));
                f32 row_height = (app_demo.rows_dense ? 22.0f : 30.0f) * scale;
                for (u32 i = 0; i < 12; i += 1) {
                    UI_Seed(hash64_mix((u64)i + 1))
                    UI_PrefWidth(ui_pct(1.0f, 0.0f))
                    UI_PrefHeight(ui_px(row_height, 1.0f))
                    UI_BgColor((i & 1) ? r_rgb(0x1B1B22) : r_rgb(0x17171D))
                    UI_TextPadding(14.0f * scale) {
                        UI_Box *item = ui_build_box(UI_Clickable | UI_Focusable |
                                                        UI_DrawBackground | UI_DrawText,
                                                    str8_lit("piste###row"));
                        UI_Signal signal = ui_signal(item);
                        item->display_string =
                            str8f(ui_frame_arena(), "%u - piste de demonstration %u", i + 1, i + 1);
                        if (signal.clicked || signal.key_pressed) { app_demo.click_count += 1; }
                    }
                }
            }
        }

        // --- column 3: Pixels, animated width
        UI_PrefWidth(ui_px(app_demo.panel_width, 1.0f))
        UI_PrefHeight(ui_pct(1.0f, 1.0f))
        UI_ChildLayoutAxis(Axis2_Y)
        UI_BgColor(app_panel()) {
            UI_Box *column = ui_build_box(UI_DrawBackground | UI_Clip, str8_lit("###col_px"));
            UI_Parent(column) UI_TextPadding(14.0f * scale) {
                app_column_header(str8_lit("Pixels"), str8_lit("largeur animee, cliquez le bouton"));
                ui_labelf("largeur: %d px", (i32)app_demo.panel_width);
                UI_PrefWidth(ui_pct(1.0f, 0.0f))
                UI_PrefHeight(ui_px(90.0f * scale, 1.0f))
                UI_BgColor(app_surface())
                UI_BorderColor(app_accent())
                UI_CornerRadius(6.0f * scale)
                UI_TextPadding(12.0f * scale) {
                    ui_build_box(UI_DrawBackground | UI_DrawBorder | UI_DrawDropShadow |
                                     UI_DrawText | UI_Clickable | UI_Focusable,
                                 str8_lit("Tab / Shift+Tab pour naviguer##focusdemo"));
                }
            }
        }
    }

    // A tooltip layer root, to prove the floating layers land on top.
    UI_LayerScope(UI_Layer_Tooltip) {
        V2 mouse = ui_mouse();
        UI_FixedX(mouse.x + 16.0f * scale)
        UI_FixedY(mouse.y + 16.0f * scale)
        UI_PrefWidth(ui_text_size(8.0f * scale, 1.0f))
        UI_PrefHeight(ui_px(24.0f * scale, 1.0f))
        UI_BgColor(r_rgba(0x2E, 0x2E, 0x38, 240))
        UI_CornerRadius(4.0f * scale)
        UI_TextPadding(8.0f * scale)
        UI_Font(ui_font(UI_FontStyle_Caption)) {
            UI_Box *tip = ui_build_box(UI_FloatingX | UI_FloatingY | UI_DrawBackground |
                                           UI_DrawDropShadow | UI_DrawText,
                                       str8_lit("###tooltip"));
            tip->display_string = str8f(ui_frame_arena(), "boxes: %llu - anim: %d",
                                        ui_box_count(), ui_animating() ? 1 : 0);
        }
    }
}

static void app_run(void) {
    Arena *permanent = arena_alloc(MB(256));
    Arena *frame_arena = arena_alloc(MB(64));
    os_events_set_frame_arena(frame_arena);
    OsWindow window = os_window_create(str8_lit("minidisk"), 1024, 640);

    if (!os_gl_init(window) || !r_init(permanent)) {
        os_debug_print(str8_lit("minidisk: OpenGL 3.3 core is required, aborting\n"));
        os_exit(2);
    }
    f32 scale = os_window_dpi_scale(window);
    r_icons_build(frame_arena, (u32)(24.0f * scale));
    if (!os_font_init() || !ui_fonts_build(scale)) {
        os_debug_print(str8_lit("minidisk: no usable system font, aborting\n"));
        os_exit(2);
    }
    ui_text_init(permanent);
    ui_init(permanent);
    app_demo.panel_width = 200.0f * scale;

    // 256 OsEvent is 24 KB: on the arena, not on the stack (C6262).
    OsEvent *events = push_array(permanent, OsEvent, APP_MAX_EVENTS);
    u64 last_us = os_time_now_us();
    b32 running = 1;
    while (running) {
        // The whole point of the ticket: a converged UI waits forever, so the
        // process is at strictly zero wakeups until something happens.
        os_events_pump(1, ui_animating() ? 16000 : OS_TIMEOUT_INFINITE);

        u64 event_count = 0;
        OsEvent event;
        while (os_event_next(&event)) {
            if (event.kind == OsEvent_Close) { running = 0; }
            if (event.kind == OsEvent_KeyDown && event.key == OsKey_Escape &&
                ui_focus_key() == 0) {
                running = 0;
            }
            if (event.kind == OsEvent_DpiChanged) {
                r_atlas_reset();
                ui_text_reset();
                scale = event.dpi_scale;
                r_icons_build(frame_arena, (u32)(24.0f * scale));
                ui_fonts_build(scale);
                app_demo.panel_width = app_demo.panel_wide ? 420.0f * scale : 200.0f * scale;
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

        if (os_redraw_requested() || ui_animating()) {
            V2 size = os_window_get_size(window);
            r_begin_frame(frame_arena, size.x, size.y, scale);
            r_clear(app_bg());
            ui_begin(frame_arena, events, event_count, dt, size, scale);
            app_build_ui(scale, dt);
            ui_end();
            r_end_frame();
        }
        arena_clear(frame_arena);
    }

    os_font_shutdown();
    r_shutdown();
    os_gl_shutdown();
    os_window_destroy(window);
    arena_release(frame_arena);
    arena_release(permanent);
}

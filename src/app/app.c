// app.c - demo loop of T-005: the four text styles, Latin with accents, the
// Japanese and halfwidth katakana the system fallback finds for us, a column of
// tabular durations, elided titles, and the icons of T-004 sharing the atlas.

static String8 app_event_description(Arena *arena, const OsEvent *event) {
    switch (event->kind) {
        case OsEvent_KeyDown:
            return str8f(arena, "KeyDown key=%u vk=%u sc=%x mods=%x repeat=%u", event->key,
                         event->vk, event->scancode, event->modifiers, event->repeat);
        case OsEvent_KeyUp:
            return str8f(arena, "KeyUp key=%u vk=%u sc=%x", event->key, event->vk,
                         event->scancode);
        case OsEvent_Char: return str8f(arena, "Char U+%x", event->codepoint);
        case OsEvent_MouseMove:
            return str8f(arena, "MouseMove %d,%d", (i32)event->pos.x, (i32)event->pos.y);
        case OsEvent_MouseDown:
            return str8f(arena, "MouseDown b=%u clicks=%u", event->button, event->click_count);
        case OsEvent_MouseUp: return str8f(arena, "MouseUp b=%u", event->button);
        case OsEvent_Wheel:
            return str8f(arena, "Wheel lines=%d,%d px=%d,%d", (i32)event->wheel_lines.x,
                         (i32)event->wheel_lines.y, (i32)event->wheel_pixels.x,
                         (i32)event->wheel_pixels.y);
        case OsEvent_Resize:
            return str8f(arena, "Resize %dx%d", (i32)event->size.x, (i32)event->size.y);
        case OsEvent_DpiChanged:
            return str8f(arena, "DpiChanged %d%%", (i32)(event->dpi_scale * 100.0f));
        case OsEvent_FocusGain: return str8_lit("FocusGain");
        case OsEvent_FocusLose: return str8_lit("FocusLose");
        case OsEvent_DropFiles: {
            String8 first = event->path_count ? event->paths[0] : str8_lit("");
            return str8f(arena, "DropFiles %u: %S", (u32)event->path_count, first);
        }
        case OsEvent_DeviceChange: return str8_lit("DeviceChange");
        case OsEvent_Close: return str8_lit("Close");
        default: return str8_lit("?");
    }
}

// Logical units in, physical pixels out: the renderer never sees a DPI factor.
static Rect app_rect_scaled(f32 x, f32 y, f32 width, f32 height, f32 scale) {
    return rect(x * scale, y * scale, (x + width) * scale, (y + height) * scale);
}

// A cheap deterministic hash: the demo needs varied colours and sizes without
// a random generator and without a table.
static u32 app_noise(u32 index) {
    u32 x = index * 2654435761u + 0x9E3779B9u;
    x ^= x >> 15;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    return x;
}

// The eight icons cycled over a grid, all sampling the same R8 atlas: one
// texture, so one batch whatever the count.
static void app_draw_icons(f32 x, f32 y, u32 count, u32 columns, u32 seed, f32 scale) {
    f32 size = 24.0f * scale;
    f32 step = 30.0f * scale;
    u32 texture = r_atlas_texture();
    for (u32 i = 0; i < count; i += 1) {
        u32 noise = app_noise(seed + i);
        R_AtlasRect icon = r_icon_rect((R_Icon)(i % R_Icon_COUNT));
        u32 column = i % columns;
        u32 row = i / columns;
        f32 left = x * scale + (f32)column * step;
        f32 top = y * scale + (f32)row * step;
        u32 color = r_rgba((u8)(180 + (noise & 0x3F)), (u8)(180 + ((noise >> 8) & 0x3F)), 230, 255);
        r_rect_textured(rect(left, top, left + size, top + size), texture, icon.uv0, icon.uv1,
                        color, 1);
    }
}

// One row of the track list: index, title elided to the column width, and a
// duration in tabular figures right aligned on the same pixel column.
static void app_draw_track_row(f32 x, f32 y, f32 title_width, u32 index, String8 title,
                               String8 duration) {
    OsFont font = ui_font(UI_FontStyle_Ui);
    u32 dim = r_rgba(0x9A, 0x9A, 0xA6, 255);
    u32 bright = r_rgba(0xE8, 0xE8, 0xF0, 255);
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 number = str8f(scratch.arena, "%u", index);
    f32 number_width = ui_text_width(font, number, UI_TextFlag_TabularNumbers);
    // Right aligned on x: the numbers line up on their last digit.
    ui_text_draw(font, number, v2(x - number_width, y), dim, UI_TextFlag_TabularNumbers);
    scratch_end(scratch);

    ui_text_draw_ellipsized(font, title, v2(x + 16.0f, y), title_width, bright, 0);

    f32 duration_x = x + 16.0f + title_width + 72.0f;
    f32 duration_width = ui_text_width(font, duration, UI_TextFlag_TabularNumbers);
    ui_text_draw(font, duration, v2(duration_x - duration_width, y), dim,
                 UI_TextFlag_TabularNumbers);
}

static void app_draw(OsWindow window, V2 mouse, Arena *frame_arena) {
    V2 size = os_window_get_size(window);
    f32 scale = os_window_dpi_scale(window);

    r_begin_frame(frame_arena, size.x, size.y, scale);
    r_clear(r_rgb(0x111113));

    R_RectParams params;
    StructZero(&params);
    params.dst = app_rect_scaled(12, 12, 1000, 616, scale);
    params.color = r_rgb(0x1A1A20);
    params.corner_radius = 8.0f * scale;
    r_rect(params);

    OsFont heading = ui_font(UI_FontStyle_Heading);
    OsFont body = ui_font(UI_FontStyle_Ui);
    OsFont caption = ui_font(UI_FontStyle_Caption);
    OsFont emphasis = ui_font(UI_FontStyle_Emphasis);
    u32 bright = r_rgba(0xF0, 0xF0, 0xF6, 255);
    u32 normal = r_rgba(0xD0, 0xD0, 0xDA, 255);
    u32 dim = r_rgba(0x8A, 0x8A, 0x96, 255);
    u32 accent = r_rgba(0xF4, 0x9A, 0x2A, 255);

    f32 left = 40.0f * scale;
    f32 y = 56.0f * scale;
    ui_text_draw(heading, str8_lit("minidisk - texte DirectWrite"), v2(left, y), bright, 0);

    y += 32.0f * scale;
    // Non ASCII spelled out in UTF-8 escapes: the source file stays pure ASCII
    // whatever code page the compiler assumes for it.
    ui_text_draw(body,
                 str8_lit("Latin avec accents : \xC3\xA9t\xC3\xA9, na\xC3\xAFve fa\xC3\xA7" "ade, "
                          "c\xC5\x93ur, \xC3\xA6quitas, \xC3\x87" "a ira "
                          "- \xC2\xAB guillemets \xC2\xBB"),
                 v2(left, y), normal, 0);
    y += 22.0f * scale;
    ui_text_draw(caption, str8_lit("Caption 12 dp : the quick brown fox jumps over the lazy dog"
                                   " 0123456789"),
                 v2(left, y), dim, 0);

    y += 40.0f * scale;
    // The point of the whole ticket: not one byte of font in the exe, and the
    // system fallback finds Yu Gothic for the kanji and the halfwidth katakana.
    ui_text_draw(heading,
                 str8_lit("Hello \xE4\xB8\x96\xE7\x95\x8C "          // U+4E16 U+754C
                          "\xEF\xBE\x83\xEF\xBD\xBD\xEF\xBE\x84 "    // halfwidth katakana
                          "\xE3\x82\xAB\xE3\x82\xBF\xE3\x82\xAB\xE3\x83\x8A"),
                 v2(left, y), accent, 0);
    y += 24.0f * scale;
    ui_text_draw(caption,
                 str8_lit("\xE5\xAE\x87\xE5\xA4\x9A\xE7\x94\xB0\xE3\x83\x92\xE3\x82\xAB\xE3\x83\xAB"
                          " - First Love / "
                          "\xEF\xBE\x8A\xEF\xBE\x9B\xEF\xBD\xB0\xEF\xBE\x9C\xEF\xBD\xB0"
                          "\xEF\xBE\x99\xEF\xBE\x84\xEF\xBE\x9E"),
                 v2(left, y), normal, 0);

    y += 44.0f * scale;
    ui_text_draw(emphasis, str8_lit("Chiffres tabulaires, titres elides"), v2(left, y), bright, 0);
    y += 10.0f * scale;
    r_line_1px(v2(left, y), v2(left + 640.0f * scale, y), r_rgba(255, 255, 255, 40));

    f32 row_x = left + 24.0f * scale;
    f32 title_width = 380.0f * scale;
    y += 22.0f * scale;
    f32 row_step = 22.0f * scale;
    app_draw_track_row(row_x, y, title_width, 1, str8_lit("Blue Monday"), str8_lit("07:29"));
    y += row_step;
    app_draw_track_row(row_x, y, title_width, 7, str8_lit("Une chanson au titre beaucoup trop long"
                                                          " pour cette colonne, forcement elide"),
                       str8_lit("03:47"));
    y += row_step;
    app_draw_track_row(row_x, y, title_width, 12,
                       str8_lit("\xE6\x9C\x80\xE5\xBE\x8C\xE3\x81\xAE"
                                "\xE3\x83\x88\xE3\x83\xA9\xE3\x83\x83\xE3\x82\xAF"),
                       str8_lit("11:09"));
    y += row_step;
    app_draw_track_row(row_x, y, title_width, 128, str8_lit("Total"), str8_lit("128:00"));

    // The clip rectangle is the other half of the ellipsis contract: the text
    // is elided to fit, so nothing ever reaches the scissor edge.
    y += 46.0f * scale;
    Rect box = rect(left, y - 16.0f * scale, left + 300.0f * scale, y + 8.0f * scale);
    StructZero(&params);
    params.dst = box;
    params.color = r_rgba(255, 255, 255, 24);
    params.corner_radius = 4.0f * scale;
    params.border = 1.0f * scale;
    r_rect(params);
    r_push_clip(box);
    ui_text_draw_ellipsized(body,
                            str8_lit("D:\\Music\\Utada Hikaru\\First Love\\01 - Automatic.mp3"),
                            v2(left + 8.0f * scale, y), 284.0f * scale, normal, 0);
    r_pop_clip();

    // Icons share the atlas with the glyphs: still one texture, one batch.
    app_draw_icons(640, 120, 24, 6, 101, scale);

    // The one that follows the mouse, on the popup layer as in T-004.
    r_set_layer(R_Layer_Popup);
    f32 half = 20.0f * scale;
    StructZero(&params);
    params.dst = rect(mouse.x - half, mouse.y - half, mouse.x + half, mouse.y + half);
    params.color = r_rgba(0xF4, 0x9A, 0x2A, 160);
    params.corner_radius = 10.0f * scale;
    r_rect(params);
    r_set_layer(R_Layer_Content);

    r_end_frame();
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
    f32 icon_scale = os_window_dpi_scale(window);
    r_icons_build(frame_arena, (u32)(24.0f * icon_scale));
    if (!os_font_init() || !ui_fonts_build(icon_scale)) {
        os_debug_print(str8_lit("minidisk: no usable system font, aborting\n"));
        os_exit(2);
    }
    ui_text_init(permanent);

    V2 mouse = v2(-1000.0f, -1000.0f);
    b32 running = 1;
    while (running) {
        // No animations yet, so the wait is unbounded: strictly zero wakeups
        // until the user or another thread has something to say.
        os_events_pump(1, OS_TIMEOUT_INFINITE);

        String8 title;
        title.str = 0;
        title.size = 0;
        OsEvent event;
        while (os_event_next(&event)) {
            if (event.kind == OsEvent_Close) { running = 0; }
            if (event.kind == OsEvent_KeyDown && event.key == OsKey_Escape) { running = 0; }
            if (event.kind == OsEvent_MouseMove) {
                mouse = event.pos;
                os_request_redraw();
            }
            if (event.kind == OsEvent_DpiChanged) {
                // The masks were rasterized for the old scale: throw them away.
                r_atlas_reset();
                ui_text_reset();
                icon_scale = event.dpi_scale;
                r_icons_build(frame_arena, (u32)(24.0f * icon_scale));
                ui_fonts_build(icon_scale);
            }
            if (event.kind == OsEvent_DropFiles) {
                for (u64 i = 0; i < event.path_count; i += 1) {
                    os_debug_print(str8f(frame_arena, "drop: %S\n", event.paths[i]));
                }
            }
            title = app_event_description(frame_arena, &event);
        }
        if (os_redraw_requested() && running) { app_draw(window, mouse, frame_arena); }
        if (title.size) {
            os_window_set_title(window, str8f(frame_arena, "minidisk - draw calls: %u - %S",
                                              r_draw_call_count(), title));
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

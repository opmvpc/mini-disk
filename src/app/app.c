// app.c - demo loop of T-004: 2 000 rects, 200 icons from the atlas, three
// nested scissor zones, one popup layer, and the draw call count in the title.

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

// `count` small rects laid out in a grid from `x`,`y` in logical units. The
// grid deliberately overflows its zone: the scissor is what cuts it, which is
// the point of the demo.
static void app_draw_rect_grid(f32 x, f32 y, u32 count, u32 columns, u32 seed, f32 scale) {
    f32 cell_w = 18.0f * scale;
    f32 cell_h = 14.0f * scale;
    R_RectParams params;
    for (u32 i = 0; i < count; i += 1) {
        u32 noise = app_noise(seed + i);
        u32 column = i % columns;
        u32 row = i / columns;
        f32 left = x * scale + (f32)column * cell_w;
        f32 top = y * scale + (f32)row * cell_h;
        StructZero(&params);
        params.dst = rect(left, top, left + cell_w - 4.0f * scale, top + cell_h - 4.0f * scale);
        params.color = r_rgba((u8)(60 + (noise & 0x7F)), (u8)(90 + ((noise >> 8) & 0x7F)),
                              (u8)(120 + ((noise >> 16) & 0x5F)), 255);
        params.corner_radius = 2.0f * scale;
        if ((noise & 3) == 0) { params.border = 1.0f * scale; }
        r_rect(params);
    }
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

static void app_draw(OsWindow window, V2 mouse, Arena *frame_arena) {
    V2 size = os_window_get_size(window);
    // The demo is laid out on a 1024x640 design grid fitted to the client area,
    // so the whole scene stays visible at any DPI and any window size. Real UI
    // code will use the DPI scale directly; here we want the picture whole.
    f32 scale = min_f32(size.x / 1024.0f, size.y / 640.0f);

    r_begin_frame(frame_arena, size.x, size.y, os_window_dpi_scale(window));
    r_clear(r_rgb(0x111113));

    R_RectParams params;

    // Panel background, unclipped: the first batch.
    StructZero(&params);
    params.dst = app_rect_scaled(12, 12, 1000, 616, scale);
    params.color = r_rgb(0x1A1A20);
    params.corner_radius = 8.0f * scale;
    r_rect(params);

    // Three nested scissor zones: A holds B, B holds C. Each draws its rects
    // then its icons, which is two batches per zone (no texture, then atlas).
    Rect zone_a = app_rect_scaled(24, 24, 976, 592, scale);
    Rect zone_b = app_rect_scaled(40, 240, 620, 360, scale);
    Rect zone_c = app_rect_scaled(300, 300, 340, 260, scale);

    r_push_clip(zone_a);
    app_draw_rect_grid(28, 30, 700, 52, 1, scale);
    // A 1 px separator: whole physical pixel, no smear. Emitted here, next to
    // the other untextured rects of the zone, so it costs no extra draw call.
    r_line_1px(v2(zone_a.min.x, 232.0f * scale), v2(zone_a.max.x, 232.0f * scale),
               r_rgba(255, 255, 255, 40));
    app_draw_icons(700, 250, 70, 10, 101, scale);

    r_push_clip(zone_b);
    app_draw_rect_grid(44, 244, 700, 34, 2, scale);
    app_draw_icons(44, 545, 70, 10, 202, scale);

    r_push_clip(zone_c);
    app_draw_rect_grid(304, 304, 600, 24, 3, scale);
    app_draw_icons(304, 470, 60, 10, 303, scale);
    r_pop_clip();
    r_pop_clip();
    r_pop_clip();

    // Popup layer: emitted last but, more to the point, it would land on top
    // even if it had been emitted first.
    r_set_layer(R_Layer_Popup);
    Rect card = app_rect_scaled(700, 380, 290, 200, scale);
    r_shadow(card, r_rgba(0, 0, 0, 170), 10.0f * scale, 12.0f * scale, v2(0.0f, 6.0f * scale));

    StructZero(&params);
    params.dst = card;
    params.color = r_rgb(0x24242B);
    params.corner_radius = 10.0f * scale;
    r_rect(params);

    // The one that follows the mouse.
    f32 half = 28.0f * scale;
    StructZero(&params);
    params.dst = rect(mouse.x - half, mouse.y - half, mouse.x + half, mouse.y + half);
    params.color = r_rgba(0xF4, 0x9A, 0x2A, 220);
    params.corner_radius = 12.0f * scale;
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
                icon_scale = event.dpi_scale;
                r_icons_build(frame_arena, (u32)(24.0f * icon_scale));
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

    r_shutdown();
    os_gl_shutdown();
    os_window_destroy(window);
    arena_release(frame_arena);
    arena_release(permanent);
}

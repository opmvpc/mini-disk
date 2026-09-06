// app.c - demo loop of T-003: GL renderer, on demand redraw, last event in the
// title. Four SDF rects: filled, 1 px border, soft shadow, and one on the mouse.

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

static void app_draw(OsWindow window, V2 mouse) {
    V2 size = os_window_get_size(window);
    f32 scale = os_window_dpi_scale(window);

    r_begin_frame(size.x, size.y, scale);
    r_clear(r_rgb(0x111113));

    R_RectParams params;

    // Filled rounded rect.
    StructZero(&params);
    params.dst = app_rect_scaled(48, 48, 200, 120, scale);
    params.color = r_rgb(0x2A6DF4);
    params.corner_radius = 6.0f * scale;
    r_rect(params);

    // 1 px border, the crispness test: whole pixels at 100 % and 150 %.
    StructZero(&params);
    params.dst = app_rect_scaled(280, 48, 200, 120, scale);
    params.color = r_rgb(0xE6E6EA);
    params.corner_radius = 6.0f * scale;
    params.border = 1.0f * scale;
    r_rect(params);

    // Soft shadow, then the card that casts it.
    StructZero(&params);
    params.dst = app_rect_scaled(512, 56, 200, 120, scale);
    params.color = r_rgba(0, 0, 0, 160);
    params.corner_radius = 8.0f * scale;
    params.softness = 10.0f * scale;
    r_rect(params);

    StructZero(&params);
    params.dst = app_rect_scaled(512, 48, 200, 120, scale);
    params.color = r_rgb(0x24242B);
    params.corner_radius = 8.0f * scale;
    r_rect(params);

    // The one that follows the mouse.
    f32 half = 32.0f * scale;
    StructZero(&params);
    params.dst = rect(mouse.x - half, mouse.y - half, mouse.x + half, mouse.y + half);
    params.color = r_rgba(0xF4, 0x9A, 0x2A, 220);
    params.corner_radius = 12.0f * scale;
    r_rect(params);

    r_end_frame();
}

static void app_run(void) {
    Arena *frame_arena = arena_alloc(MB(8));
    os_events_set_frame_arena(frame_arena);
    OsWindow window = os_window_create(str8_lit("minidisk"), 1024, 640);

    if (!os_gl_init(window) || !r_init()) {
        os_debug_print(str8_lit("minidisk: OpenGL 3.3 core is required, aborting\n"));
        os_exit(2);
    }

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
            if (event.kind == OsEvent_DropFiles) {
                for (u64 i = 0; i < event.path_count; i += 1) {
                    os_debug_print(str8f(frame_arena, "drop: %S\n", event.paths[i]));
                }
            }
            title = app_event_description(frame_arena, &event);
        }
        if (title.size) {
            os_window_set_title(window, str8f(frame_arena, "minidisk - %S", title));
        }
        if (os_redraw_requested() && running) { app_draw(window, mouse); }
        arena_clear(frame_arena);
    }

    r_shutdown();
    os_gl_shutdown();
    os_window_destroy(window);
    arena_release(frame_arena);
}

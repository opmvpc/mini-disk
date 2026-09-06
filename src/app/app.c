// app.c - demo loop of T-002: on demand redraw, last event shown in the title.
// The GL renderer replaces os_window_fill_black in T-003.

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

static void app_run(void) {
    Arena *frame_arena = arena_alloc(MB(8));
    os_events_set_frame_arena(frame_arena);
    OsWindow window = os_window_create(str8_lit("minidisk"), 1024, 640);

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
        if (os_redraw_requested() && running) { os_window_fill_black(window); }
        arena_clear(frame_arena);
    }

    os_window_destroy(window);
    arena_release(frame_arena);
}

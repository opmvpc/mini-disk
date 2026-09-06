// test_widgets.c - the widgets without a window: text editing (UTF-8 movement,
// selection, undo), list selection and the visible-row window, and one real
// frame loop that checks the virtualization budget on 100 000 rows.

global Arena *test_widgets_arena;
global Arena *test_widgets_frame;

#define TEST_W_VIEWPORT v2(1000.0f, 600.0f)
#define TEST_W_DT (1.0f / 60.0f)

static void test_widgets_frame_begin(const OsEvent *events, u64 event_count) {
    arena_clear(test_widgets_frame);
    r_begin_frame(test_widgets_frame, TEST_W_VIEWPORT.x, TEST_W_VIEWPORT.y, 1.0f);
    ui_begin(test_widgets_frame, events, event_count, TEST_W_DT, TEST_W_VIEWPORT, 1.0f);
}

static void test_widgets_frame_end(void) {
    ui_end();
    r_end_frame();
}

static b32 test_widgets_text_is(UI_TextInput *input, const char *expected) {
    return str8_eq(ui_text_input_string(input), str8_cstr(expected));
}

// --- text editing ----------------------------------------------------------
TEST(widgets_text_insert_and_delete) {
    Unused(arena);
    UI_TextInput input;
    ui_text_input_init(&input, str8_lit("abc"));
    EXPECT(input.edit.size == 3);
    EXPECT(input.edit.cursor == 3);

    ui_text_input_insert(&input, str8_lit("de"));
    EXPECT(test_widgets_text_is(&input, "abcde"));
    EXPECT(input.edit.cursor == 5);

    ui_text_input_set_cursor(&input, 0, 0);
    ui_text_input_insert(&input, str8_lit("Z"));
    EXPECT(test_widgets_text_is(&input, "Zabcde"));

    ui_text_input_delete(&input, -1, 0);  // Backspace
    EXPECT(test_widgets_text_is(&input, "abcde"));
    ui_text_input_delete(&input, +1, 0);  // Delete
    EXPECT(test_widgets_text_is(&input, "bcde"));

    // A full field refuses the overflow instead of truncating a codepoint.
    ui_text_input_init(&input, str8_lit(""));
    for (u32 i = 0; i < UI_TEXT_INPUT_CAP + 8; i += 1) {
        ui_text_input_insert(&input, str8_lit("x"));
    }
    EXPECT(input.edit.size == UI_TEXT_INPUT_CAP);
    EXPECT(!ui_text_input_insert(&input, str8_lit("y")));
}

TEST(widgets_text_utf8_movement) {
    Unused(arena);
    // "eco" with an accent, a euro sign and a CJK ideogram: 1, 2, 3 and 4 byte
    // sequences in one string.
    UI_TextInput input;
    ui_text_input_init(&input, str8_lit("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x8E\xB5z"));
    String8 text = ui_text_input_string(&input);
    EXPECT(text.size == 11);

    u32 at = 0;
    at = ui_text_offset_next(text, at);
    EXPECT(at == 1);  // a
    at = ui_text_offset_next(text, at);
    EXPECT(at == 3);  // e acute, 2 bytes
    at = ui_text_offset_next(text, at);
    EXPECT(at == 6);  // euro, 3 bytes
    at = ui_text_offset_next(text, at);
    EXPECT(at == 10);  // musical note, 4 bytes
    at = ui_text_offset_next(text, at);
    EXPECT(at == 11);
    EXPECT(ui_text_offset_next(text, at) == 11);  // clamped at the end

    at = ui_text_offset_prev(text, 11);
    EXPECT(at == 10);
    at = ui_text_offset_prev(text, at);
    EXPECT(at == 6);
    at = ui_text_offset_prev(text, at);
    EXPECT(at == 3);
    at = ui_text_offset_prev(text, at);
    EXPECT(at == 1);
    EXPECT(ui_text_offset_prev(text, 0) == 0);

    // Backspace removes one whole codepoint, not one byte.
    ui_text_input_set_cursor(&input, 6, 0);
    ui_text_input_delete(&input, -1, 0);
    EXPECT(ui_text_input_string(&input).size == 8);
    EXPECT(input.edit.cursor == 3);
    EXPECT(str8_eq(ui_text_input_string(&input), str8_lit("a\xC3\xA9\xF0\x9F\x8E\xB5z")));

    // And an arrow crosses it in one step, in both directions.
    ui_text_input_move(&input, -1, 0, 0);
    EXPECT(input.edit.cursor == 1);
    ui_text_input_move(&input, +1, 0, 0);
    EXPECT(input.edit.cursor == 3);
}

TEST(widgets_text_selection) {
    Unused(arena);
    UI_TextInput input;
    ui_text_input_init(&input, str8_lit("nuit de verre"));

    // Shift+Left three times selects the last three bytes.
    ui_text_input_move(&input, -1, 0, 1);
    ui_text_input_move(&input, -1, 0, 1);
    ui_text_input_move(&input, -1, 0, 1);
    EXPECT(input.edit.mark == 13 && input.edit.cursor == 10);

    // Typing over a selection replaces it.
    ui_text_input_insert(&input, str8_lit("X"));
    EXPECT(test_widgets_text_is(&input, "nuit de veX"));

    ui_text_input_select_all(&input);
    EXPECT(input.edit.mark == 0 && input.edit.cursor == 11);
    ui_text_input_delete_selection(&input);
    EXPECT(input.edit.size == 0);

    // A plain arrow collapses the selection instead of moving the caret.
    ui_text_input_init(&input, str8_lit("abcdef"));
    ui_text_input_set_cursor(&input, 2, 0);
    ui_text_input_set_cursor(&input, 5, 1);
    ui_text_input_move(&input, -1, 0, 0);
    EXPECT(input.edit.cursor == 2 && input.edit.mark == 2);

    // Word movement and Ctrl+Backspace.
    ui_text_input_init(&input, str8_lit("nuit de verre"));
    EXPECT(ui_text_offset_word_prev(ui_text_input_string(&input), 13) == 8);
    EXPECT(ui_text_offset_word_next(ui_text_input_string(&input), 0) == 5);
    ui_text_input_delete(&input, -1, 1);
    EXPECT(test_widgets_text_is(&input, "nuit de "));
}

TEST(widgets_text_undo_redo) {
    Unused(arena);
    UI_TextInput input;
    ui_text_input_init(&input, str8_lit(""));
    ui_text_input_insert(&input, str8_lit("nuit"));
    ui_text_input_insert(&input, str8_lit(" de verre"));
    EXPECT(test_widgets_text_is(&input, "nuit de verre"));

    EXPECT(ui_text_input_undo(&input));
    EXPECT(test_widgets_text_is(&input, "nuit"));
    EXPECT(ui_text_input_undo(&input));
    EXPECT(input.edit.size == 0);
    EXPECT(!ui_text_input_undo(&input));  // the bottom of the ring

    EXPECT(ui_text_input_redo(&input));
    EXPECT(test_widgets_text_is(&input, "nuit"));
    EXPECT(ui_text_input_redo(&input));
    EXPECT(test_widgets_text_is(&input, "nuit de verre"));
    EXPECT(!ui_text_input_redo(&input));

    // A new edit after an undo drops the redo branch.
    ui_text_input_undo(&input);
    ui_text_input_insert(&input, str8_lit("!"));
    EXPECT(test_widgets_text_is(&input, "nuit!"));
    EXPECT(!ui_text_input_redo(&input));

    // A run of typed characters coalesces into a single step.
    ui_text_input_init(&input, str8_lit(""));
    for (u32 i = 0; i < 5; i += 1) {
        ui_text_input_insert(&input, str8_lit("a"));
        input.coalesce = 1;
    }
    EXPECT(test_widgets_text_is(&input, "aaaaa"));
    EXPECT(ui_text_input_undo(&input));
    EXPECT(input.edit.size == 0);

    // The ring is bounded: older steps fall off, the recent ones still work.
    ui_text_input_init(&input, str8_lit(""));
    for (u32 i = 0; i < UI_TEXT_INPUT_UNDO * 2; i += 1) {
        ui_text_input_insert(&input, str8_lit("b"));
    }
    EXPECT(input.edit.size == UI_TEXT_INPUT_UNDO * 2);
    u32 steps = 0;
    while (ui_text_input_undo(&input)) { steps += 1; }
    EXPECT(steps == UI_TEXT_INPUT_UNDO - 1);
}

// --- list ------------------------------------------------------------------
TEST(widgets_list_visible_window) {
    Unused(arena);
    // 100 000 rows of 22 px seen through 600 px: 28 rows plus the overscan.
    UI_ListWindow window = ui_list_window(0.0f, 600.0f, 22.0f, 100000);
    EXPECT(window.first == 0);
    EXPECT(window.count == 28 + UI_LIST_OVERSCAN);  // no row above the first one

    window = ui_list_window(22.0f * 1000.0f, 600.0f, 22.0f, 100000);
    EXPECT(window.first == 1000 - UI_LIST_OVERSCAN);
    EXPECT(window.count <= 28 + 2 * UI_LIST_OVERSCAN);
    EXPECT(window.first + window.count > 1027);

    // Half a row scrolled: the partially visible rows on both edges are in.
    window = ui_list_window(11.0f, 600.0f, 22.0f, 100000);
    EXPECT(window.first == 0);
    EXPECT(window.first + window.count >= 28);

    // The end of the list never overruns the row count.
    window = ui_list_window(22.0f * 100000.0f, 600.0f, 22.0f, 100000);
    EXPECT(window.first + window.count == 100000);

    // Degenerate inputs build nothing at all.
    EXPECT(ui_list_window(0.0f, 600.0f, 22.0f, 0).count == 0);
    EXPECT(ui_list_window(0.0f, 0.0f, 22.0f, 100000).count == 0);
}

TEST(widgets_list_selection) {
    Unused(arena);
    u64 bits[16];
    UI_List list;
    ui_list_init(&list, bits, ArrayCount(bits));
    list.row_count = 1000;
    ui_list_select_clear(&list);

    ui_list_select_only(&list, 5);
    EXPECT(ui_list_selected(&list, 5));
    EXPECT(!ui_list_selected(&list, 6));
    EXPECT(ui_list_selected_count(&list) == 1);
    EXPECT(list.anchor == 5 && list.cursor == 5);

    // Ctrl+click adds, then removes.
    ui_list_select_toggle(&list, 9);
    EXPECT(ui_list_selected_count(&list) == 2);
    ui_list_select_toggle(&list, 9);
    EXPECT(ui_list_selected_count(&list) == 1);

    // Shift range, in both directions, replaces the selection.
    ui_list_select_range(&list, 5, 8, 0);
    EXPECT(ui_list_selected_count(&list) == 4);
    EXPECT(ui_list_selected(&list, 5) && ui_list_selected(&list, 8));
    ui_list_select_range(&list, 20, 10, 0);
    EXPECT(ui_list_selected_count(&list) == 11);
    EXPECT(!ui_list_selected(&list, 5));
    EXPECT(list.cursor == 10);

    // Additive range keeps what was there.
    ui_list_select_range(&list, 30, 31, 1);
    EXPECT(ui_list_selected_count(&list) == 13);

    ui_list_select_all(&list);
    EXPECT(ui_list_selected_count(&list) == 1000);
    ui_list_select_clear(&list);
    EXPECT(ui_list_selected_count(&list) == 0);
}

TEST(widgets_list_ensure_visible) {
    Unused(arena);
    UI_List list;
    ui_list_init(&list, 0, 0);
    list.row_count = 1000;
    list.row_height = 22.0f;
    list.view_height = 220.0f;  // ten rows

    ui_list_ensure_visible(&list, 50);
    EXPECT(list.scroll == 51.0f * 22.0f - 220.0f);  // scrolled just enough
    ui_list_ensure_visible(&list, 50);
    EXPECT(list.scroll == 51.0f * 22.0f - 220.0f);  // already visible, no move
    ui_list_ensure_visible(&list, 45);
    EXPECT(list.scroll == 51.0f * 22.0f - 220.0f);  // still visible, still still
    ui_list_ensure_visible(&list, 35);
    EXPECT(list.scroll == 35.0f * 22.0f);  // above the view: scrolled to its top
    ui_list_ensure_visible(&list, 999);
    EXPECT(list.scroll == 1000.0f * 22.0f - 220.0f);
    ui_list_ensure_visible(&list, 0);
    EXPECT(list.scroll == 0.0f);
}

// One frame of a 100 000 row list, laid out for real.
static void test_widgets_list_frame(UI_List *list, const OsEvent *events, u64 event_count) {
    test_widgets_frame_begin(events, event_count);
    UI_Box *root = ui_root(UI_Layer_Content);
    UI_Parent(root) {
        ui_list_begin(list, 100000, 22.0f);
        UI_ListEachRow(list, i) {
            ui_list_row_begin(list, i);
            ui_list_row_end(list);
        }
        ui_list_end(list);
    }
    test_widgets_frame_end();
}

TEST(widgets_list_is_virtualized) {
    Unused(arena);
    u64 *bits = push_array_zero(arena, u64, (100000 + 63) / 64);
    UI_List list;
    ui_list_init(&list, bits, (100000 + 63) / 64);

    test_widgets_list_frame(&list, 0, 0);  // no rect yet: nothing to build
    test_widgets_list_frame(&list, 0, 0);
    EXPECT(list.view_height == TEST_W_VIEWPORT.y);
    EXPECT(list.visible_count > 0);
    EXPECT(list.visible_count < 40);
    EXPECT(ui_frame_box_count() < 200);

    // Scrolling with the wheel moves the window, not the box count.
    OsEvent events[2];
    StructZero(&events[0]);
    events[0].kind = OsEvent_MouseMove;
    events[0].pos = v2(100.0f, 100.0f);
    StructZero(&events[1]);
    events[1].kind = OsEvent_Wheel;
    events[1].wheel_lines = v2(0.0f, -3.0f);
    events[1].wheel_pixels = v2(0.0f, -300.0f);
    test_widgets_list_frame(&list, events, 2);
    EXPECT(list.scroll == 300.0f);
    EXPECT(list.first_visible == 300 / 22 - UI_LIST_OVERSCAN);
    EXPECT(ui_frame_box_count() < 200);

    // A wheel with no pixel delta falls back to lines.
    events[1].wheel_pixels = v2(0.0f, 0.0f);
    test_widgets_list_frame(&list, events, 2);
    EXPECT(list.scroll == 300.0f + 3.0f * 22.0f);

    // And the very end of the list is reachable and clamped.
    list.scroll = 1.0e9f;
    test_widgets_list_frame(&list, 0, 0);
    EXPECT(list.scroll == 100000.0f * 22.0f - TEST_W_VIEWPORT.y);
    EXPECT(list.first_visible + list.visible_count == 100000);
    EXPECT(ui_frame_box_count() < 200);
}

// --- splitter --------------------------------------------------------------
TEST(widgets_splitter_clamps_and_resets) {
    Unused(arena);
    UI_Splitter splitter;
    ui_splitter_init(&splitter, 300.0f, 200.0f, 250.0f);
    EXPECT(ui_splitter_update(&splitter, Axis2_X, 1000.0f) == 300.0f);

    // Nothing to clamp against before the first layout.
    splitter.size = 300.0f;
    EXPECT(ui_splitter_update(&splitter, Axis2_X, 0.0f) == 300.0f);

    splitter.size = 50.0f;
    EXPECT(ui_splitter_update(&splitter, Axis2_X, 1000.0f) == 200.0f);
    splitter.size = 900.0f;
    EXPECT(ui_splitter_update(&splitter, Axis2_X, 1000.0f) == 1000.0f - 250.0f - 6.0f);
    splitter.size = 900.0f;
    EXPECT(ui_splitter_update(&splitter, Axis2_X, 300.0f) == 200.0f);  // min wins
}

// --- context menu and tooltip ----------------------------------------------
static b32 test_widgets_menu_frame(UI_ContextMenu *menu, const OsEvent *events, u64 event_count,
                                   i32 *out_chosen) {
    test_widgets_frame_begin(events, event_count);
    b32 open = ui_context_menu_begin(menu);
    if (open) {
        if (ui_context_menu_item(menu, str8_lit("un"))) { *out_chosen = 0; }
        if (ui_context_menu_item(menu, str8_lit("deux"))) { *out_chosen = 1; }
        ui_context_menu_end(menu);
    }
    test_widgets_frame_end();
    return open;
}

TEST(widgets_context_menu_keyboard) {
    Unused(arena);
    UI_ContextMenu menu;
    StructZero(&menu);
    i32 chosen = -1;

    // The mouse goes far away first: a hovered item owns the highlight, and
    // this case is about the keyboard.
    OsEvent away = test_ui_mouse_event(OsEvent_MouseMove, v2(900.0f, 500.0f), 0, 0);
    EXPECT(!test_widgets_menu_frame(&menu, &away, 1, &chosen));  // closed: builds nothing
    ui_context_menu_open(&menu, v2(100.0f, 100.0f), 42);
    EXPECT(ui_popup_active());  // the list below stops reading the keyboard
    EXPECT(test_widgets_menu_frame(&menu, 0, 0, &chosen));
    EXPECT(menu.payload == 42);

    OsEvent down = test_ui_key_event(OsKey_Down, 0);
    test_widgets_menu_frame(&menu, &down, 1, &chosen);
    EXPECT(menu.hot_item == 1);

    OsEvent enter = test_ui_key_event(OsKey_Enter, 0);
    test_widgets_menu_frame(&menu, &enter, 1, &chosen);
    EXPECT(chosen == 1);
    EXPECT(!menu.open);
    EXPECT(!ui_popup_active());

    // Escape closes it without choosing anything.
    chosen = -1;
    ui_context_menu_open(&menu, v2(100.0f, 100.0f), 7);
    test_widgets_menu_frame(&menu, 0, 0, &chosen);
    OsEvent escape = test_ui_key_event(OsKey_Escape, 0);
    EXPECT(!test_widgets_menu_frame(&menu, &escape, 1, &chosen));
    EXPECT(!menu.open);
    EXPECT(chosen == -1);
}

static void test_widgets_tooltip_frame(const OsEvent *events, u64 event_count) {
    test_widgets_frame_begin(events, event_count);
    UI_Box *root = ui_root(UI_Layer_Content);
    UI_Parent(root)
    UI_PrefWidth(ui_px(200.0f, 1.0f))
    UI_PrefHeight(ui_px(40.0f, 1.0f)) {
        ui_build_box(UI_Clickable, str8_lit("hovered##tip"));
        ui_tooltip(str8_lit("une bulle"));
    }
    test_widgets_frame_end();
}

TEST(widgets_tooltip_waits_half_a_second) {
    Unused(arena);
    OsEvent move = test_ui_mouse_event(OsEvent_MouseMove, v2(1e5f, 1e5f), 0, 0);
    test_widgets_tooltip_frame(&move, 1);  // mouse far away, no hover at all

    move = test_ui_mouse_event(OsEvent_MouseMove, v2(100.0f, 20.0f), 0, 0);
    test_widgets_tooltip_frame(&move, 1);
    EXPECT(ui_root(UI_Layer_Tooltip)->first == 0);
    EXPECT(ui_animating());  // the delay keeps the loop awake, no timer needed

    // 500 ms of hover at 60 Hz: 30 frames, and not one frame less.
    u32 frames = 1;
    while (ui_root(UI_Layer_Tooltip)->first == 0 && frames < 120) {
        test_widgets_tooltip_frame(0, 0);
        frames += 1;
    }
    EXPECT(frames == 31);
    EXPECT(ui_root(UI_Layer_Tooltip)->first != 0);

    // Leaving the box takes it away again, and resets the delay.
    move = test_ui_mouse_event(OsEvent_MouseMove, v2(1e5f, 1e5f), 0, 0);
    test_widgets_tooltip_frame(&move, 1);
    EXPECT(ui_root(UI_Layer_Tooltip)->first == 0);
}

static void test_widgets_run_all(void) {
    test_report("widgets\n");
    test_widgets_arena = arena_alloc(MB(256));
    test_widgets_frame = arena_alloc(MB(64));
    ui_init(test_widgets_arena);

    RUN(widgets_text_insert_and_delete);
    RUN(widgets_text_utf8_movement);
    RUN(widgets_text_selection);
    RUN(widgets_text_undo_redo);
    RUN(widgets_list_visible_window);
    RUN(widgets_list_selection);
    RUN(widgets_list_ensure_visible);
    RUN(widgets_list_is_virtualized);
    RUN(widgets_splitter_clamps_and_resets);
    RUN(widgets_context_menu_keyboard);
    RUN(widgets_tooltip_waits_half_a_second);
}

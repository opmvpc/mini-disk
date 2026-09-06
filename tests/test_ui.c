// test_ui.c - the UI engine without a window: the five layout passes and the
// strictness arithmetic, key stability, orphan eviction, signals synthesized
// from OsEvent, focus traversal and animation convergence. Possible because
// ui_core.c is pure arithmetic over r_* and ui_text_*.

global Arena *test_ui_arena;   // outlives the cases: the box pool lives in it
global Arena *test_ui_frame;   // the frame arena of the fake loop

#define TEST_UI_VIEWPORT v2(1000.0f, 600.0f)
#define TEST_UI_DT (1.0f / 60.0f)

static void test_ui_frame_begin(const OsEvent *events, u64 event_count) {
    arena_clear(test_ui_frame);
    r_begin_frame(test_ui_frame, TEST_UI_VIEWPORT.x, TEST_UI_VIEWPORT.y, 1.0f);
    ui_begin(test_ui_frame, events, event_count, TEST_UI_DT, TEST_UI_VIEWPORT, 1.0f);
}

static void test_ui_frame_end(void) {
    ui_end();
    r_end_frame();
}

static OsEvent test_ui_mouse_event(OsEventKind kind, V2 pos, u32 button, u32 clicks) {
    OsEvent event;
    StructZero(&event);
    event.kind = kind;
    event.pos = pos;
    event.button = button;
    event.click_count = clicks;
    return event;
}

static OsEvent test_ui_key_event(u32 key, u32 modifiers) {
    OsEvent event;
    StructZero(&event);
    event.kind = OsEvent_KeyDown;
    event.key = key;
    event.modifiers = modifiers;
    return event;
}

// A row of `count` children on the X axis inside a parent of `parent_width`.
static UI_Box *test_ui_row(f32 parent_width, f32 parent_height) {
    ui_push_pref_width(ui_px(parent_width, 1.0f));
    ui_push_pref_height(ui_px(parent_height, 1.0f));
    ui_push_child_layout_axis(Axis2_X);
    UI_Box *row = ui_build_box_from_key(0, 0);
    ui_pop_child_layout_axis();
    ui_pop_pref_height();
    ui_pop_pref_width();
    return row;
}

static UI_Box *test_ui_child(UI_Size width, UI_Size height) {
    ui_push_pref_width(width);
    ui_push_pref_height(height);
    UI_Box *box = ui_build_box_from_key(0, 0);
    ui_pop_pref_height();
    ui_pop_pref_width();
    return box;
}

static b32 test_ui_near(f32 a, f32 b) { return abs_f32(a - b) < 0.01f; }

TEST(ui_layout_size_kinds) {
    Unused(arena);
    test_ui_frame_begin(0, 0);
    UI_Box *row = test_ui_row(1000.0f, 200.0f);
    UI_Box *pixels = 0, *percent = 0, *sum = 0, *inner_a = 0, *inner_b = 0;
    UI_Parent(row) {
        pixels = test_ui_child(ui_px(150.0f, 1.0f), ui_pct(1.0f, 1.0f));
        percent = test_ui_child(ui_pct(0.25f, 1.0f), ui_px(40.0f, 1.0f));
        ui_push_child_layout_axis(Axis2_X);
        sum = test_ui_child(ui_children_sum(1.0f), ui_px(40.0f, 1.0f));
        ui_pop_child_layout_axis();
        UI_Parent(sum) {
            inner_a = test_ui_child(ui_px(30.0f, 1.0f), ui_px(10.0f, 1.0f));
            inner_b = test_ui_child(ui_px(70.0f, 1.0f), ui_px(10.0f, 1.0f));
        }
    }
    test_ui_frame_end();

    EXPECT(test_ui_near(pixels->computed_size[Axis2_X], 150.0f));
    EXPECT(test_ui_near(percent->computed_size[Axis2_X], 250.0f));
    EXPECT(test_ui_near(sum->computed_size[Axis2_X], 100.0f));
    EXPECT(test_ui_near(pixels->computed_size[Axis2_Y], 200.0f));
    // Pass 5: positions accumulate along the parent's layout axis.
    EXPECT(test_ui_near(pixels->rect.min.x, 0.0f));
    EXPECT(test_ui_near(percent->rect.min.x, 150.0f));
    EXPECT(test_ui_near(sum->rect.min.x, 400.0f));
    EXPECT(test_ui_near(inner_a->rect.min.x, 400.0f));
    EXPECT(test_ui_near(inner_b->rect.min.x, 430.0f));
    // Off axis children all start at the parent's edge.
    EXPECT(test_ui_near(percent->rect.min.y, 0.0f));
}

TEST(ui_layout_violations) {
    Unused(arena);
    UI_Box *a[6] = {0, 0, 0, 0, 0, 0};
    UI_Box *b[6] = {0, 0, 0, 0, 0, 0};
    UI_Box *nested = 0;
    UI_Box *cross_soft = 0, *cross_strict = 0;

    test_ui_frame_begin(0, 0);
    // 1: both children strict, nothing to take: the parent overflows.
    UI_Box *row = test_ui_row(100.0f, 50.0f);
    UI_Parent(row) {
        a[0] = test_ui_child(ui_px(80.0f, 1.0f), ui_px(10.0f, 1.0f));
        b[0] = test_ui_child(ui_px(80.0f, 1.0f), ui_px(10.0f, 1.0f));
    }
    // 2: one soft, one strict: the soft one pays the whole violation.
    row = test_ui_row(100.0f, 50.0f);
    UI_Parent(row) {
        a[1] = test_ui_child(ui_px(80.0f, 0.0f), ui_px(10.0f, 1.0f));
        b[1] = test_ui_child(ui_px(80.0f, 1.0f), ui_px(10.0f, 1.0f));
    }
    // 3: both soft and equal: they split the violation.
    row = test_ui_row(100.0f, 50.0f);
    UI_Parent(row) {
        a[2] = test_ui_child(ui_px(80.0f, 0.0f), ui_px(10.0f, 1.0f));
        b[2] = test_ui_child(ui_px(80.0f, 0.0f), ui_px(10.0f, 1.0f));
    }
    // 4: same strictness, different sizes: weighted by size * (1 - strictness).
    row = test_ui_row(100.0f, 50.0f);
    UI_Parent(row) {
        a[3] = test_ui_child(ui_px(50.0f, 0.5f), ui_px(10.0f, 1.0f));
        b[3] = test_ui_child(ui_px(150.0f, 0.5f), ui_px(10.0f, 1.0f));
    }
    // 5: no violation at all, nobody moves.
    row = test_ui_row(100.0f, 50.0f);
    UI_Parent(row) {
        a[4] = test_ui_child(ui_px(30.0f, 0.0f), ui_px(10.0f, 1.0f));
        b[4] = test_ui_child(ui_px(40.0f, 0.0f), ui_px(10.0f, 1.0f));
    }
    // 6: the off axis is not a sum, each child is clamped on its own.
    row = test_ui_row(100.0f, 50.0f);
    UI_Parent(row) {
        cross_soft = test_ui_child(ui_px(10.0f, 1.0f), ui_px(200.0f, 0.0f));
        cross_strict = test_ui_child(ui_px(10.0f, 1.0f), ui_px(200.0f, 1.0f));
    }
    // and the percentage of a shrunk parent follows the corrected size.
    row = test_ui_row(60.0f, 50.0f);
    UI_Parent(row) {
        ui_push_child_layout_axis(Axis2_X);
        a[5] = test_ui_child(ui_px(100.0f, 0.0f), ui_px(10.0f, 1.0f));
        ui_pop_child_layout_axis();
        UI_Parent(a[5]) { nested = test_ui_child(ui_pct(0.5f, 1.0f), ui_px(10.0f, 1.0f)); }
    }
    test_ui_frame_end();

    EXPECT(test_ui_near(a[0]->computed_size[Axis2_X], 80.0f));
    EXPECT(test_ui_near(b[0]->computed_size[Axis2_X], 80.0f));

    EXPECT(test_ui_near(a[1]->computed_size[Axis2_X], 20.0f));
    EXPECT(test_ui_near(b[1]->computed_size[Axis2_X], 80.0f));

    EXPECT(test_ui_near(a[2]->computed_size[Axis2_X], 50.0f));
    EXPECT(test_ui_near(b[2]->computed_size[Axis2_X], 50.0f));

    EXPECT(test_ui_near(a[3]->computed_size[Axis2_X], 25.0f));
    EXPECT(test_ui_near(b[3]->computed_size[Axis2_X], 75.0f));

    EXPECT(test_ui_near(a[4]->computed_size[Axis2_X], 30.0f));
    EXPECT(test_ui_near(b[4]->computed_size[Axis2_X], 40.0f));

    EXPECT(test_ui_near(cross_soft->computed_size[Axis2_Y], 50.0f));
    EXPECT(test_ui_near(cross_strict->computed_size[Axis2_Y], 200.0f));

    EXPECT(test_ui_near(a[5]->computed_size[Axis2_X], 60.0f));
    EXPECT(test_ui_near(nested->computed_size[Axis2_X], 30.0f));
}

TEST(ui_keys_are_stable_and_scoped) {
    Unused(arena);
    UI_Key first_key = 0, second_key = 0;
    UI_Box *first_box = 0, *second_box = 0;
    UI_Key seeded_a = 0, seeded_b = 0;
    UI_Key renamed_first = 0, renamed_second = 0;

    test_ui_frame_begin(0, 0);
    first_box = ui_build_box(0, str8_lit("button##stable"));
    first_key = first_box->key;
    UI_Seed(1) { seeded_a = ui_build_box(0, str8_lit("row##list"))->key; }
    UI_Seed(2) { seeded_b = ui_build_box(0, str8_lit("row##list"))->key; }
    renamed_first = ui_build_box(0, str8_lit("Play###transport"))->key;
    test_ui_frame_end();

    test_ui_frame_begin(0, 0);
    second_box = ui_build_box(0, str8_lit("button##stable"));
    second_key = second_box->key;
    UI_Seed(1) { ui_build_box(0, str8_lit("row##list")); }
    UI_Seed(2) { ui_build_box(0, str8_lit("row##list")); }
    // ### : the label changes, the identity does not.
    UI_Box *renamed = ui_build_box(0, str8_lit("Pause###transport"));
    renamed_second = renamed->key;
    test_ui_frame_end();

    EXPECT(first_key != 0);
    EXPECT(first_key == second_key);
    EXPECT(first_box == second_box);  // same UI_Box object, state survived
    EXPECT(seeded_a != seeded_b);     // the seed stack separates two list rows
    EXPECT(renamed_first == renamed_second);
    EXPECT(str8_eq(renamed->display_string, str8_lit("Pause")));
    // An empty identifier is a pure layout node: no key, no table entry.
    test_ui_frame_begin(0, 0);
    EXPECT(ui_build_box(0, str8_lit(""))->key == 0);
    test_ui_frame_end();
}

TEST(ui_orphan_boxes_are_released) {
    Unused(arena);
    // Drain whatever the previous cases left in the table.
    for (u32 i = 0; i < 3; i += 1) {
        test_ui_frame_begin(0, 0);
        test_ui_frame_end();
    }
    u64 base = ui_box_count();

    test_ui_frame_begin(0, 0);
    for (u32 i = 0; i < 8; i += 1) {
        UI_Seed(hash64_mix(i + 1)) { ui_build_box(0, str8_lit("orphan##row")); }
    }
    test_ui_frame_end();
    EXPECT(ui_box_count() == base + 8);

    // The frame right after still needs those rects for the hit test, so the
    // eviction only happens at the beginning of the frame after that.
    test_ui_frame_begin(0, 0);
    test_ui_frame_end();
    EXPECT(ui_box_count() == base + 8);

    test_ui_frame_begin(0, 0);
    test_ui_frame_end();
    EXPECT(ui_box_count() == base);
}

// One frame of the fake loop: rebuild the tree, then read the signal, exactly
// like a real app. A box that stops being built is evicted, on purpose.
static UI_Signal test_ui_target_frame(const OsEvent *events, u64 event_count, UI_Key *out_key) {
    test_ui_frame_begin(events, event_count);
    UI_Box *row = test_ui_row(200.0f, 100.0f);
    UI_Signal signal;
    StructZero(&signal);
    UI_Parent(row) {
        ui_push_pref_width(ui_px(100.0f, 1.0f));
        ui_push_pref_height(ui_px(50.0f, 1.0f));
        UI_Box *target = ui_build_box(UI_Clickable | UI_Focusable | UI_Scrollable,
                                      str8_lit("target##hit"));
        ui_pop_pref_height();
        ui_pop_pref_width();
        signal = ui_signal(target);
        *out_key = target->key;
    }
    test_ui_frame_end();
    return signal;
}

TEST(ui_signals_from_events) {
    Unused(arena);
    UI_Key key = 0;

    // Frame 1: the box exists but has no rect yet, so nothing can be hit.
    test_ui_target_frame(0, 0, &key);

    V2 centre = v2(50.0f, 25.0f);
    V2 outside = v2(500.0f, 500.0f);
    OsEvent events[4];
    UI_Signal signal;

    // Hover.
    events[0] = test_ui_mouse_event(OsEvent_MouseMove, centre, 0, 0);
    signal = test_ui_target_frame(events, 1, &key);
    EXPECT(signal.hovering);
    EXPECT(!signal.clicked);
    EXPECT(ui_hot_key() == key);

    // Press then release on the box: one click, and the focus followed.
    events[0] = test_ui_mouse_event(OsEvent_MouseDown, centre, OsMouseButton_Left, 1);
    events[1] = test_ui_mouse_event(OsEvent_MouseUp, centre, OsMouseButton_Left, 1);
    signal = test_ui_target_frame(events, 2, &key);
    EXPECT(signal.pressed);
    EXPECT(signal.released);
    EXPECT(signal.clicked);
    EXPECT(!signal.double_clicked);
    EXPECT(ui_focus_key() == key);

    // Second click of a double click: the OS counts, we route.
    events[0] = test_ui_mouse_event(OsEvent_MouseDown, centre, OsMouseButton_Left, 2);
    events[1] = test_ui_mouse_event(OsEvent_MouseUp, centre, OsMouseButton_Left, 2);
    signal = test_ui_target_frame(events, 2, &key);
    EXPECT(signal.double_clicked);

    // Right click.
    events[0] = test_ui_mouse_event(OsEvent_MouseDown, centre, OsMouseButton_Right, 1);
    events[1] = test_ui_mouse_event(OsEvent_MouseUp, centre, OsMouseButton_Right, 1);
    signal = test_ui_target_frame(events, 2, &key);
    EXPECT(signal.right_clicked);

    // Press, then drag out: the mouse is captured, no click on release.
    events[0] = test_ui_mouse_event(OsEvent_MouseDown, centre, OsMouseButton_Left, 1);
    events[1] = test_ui_mouse_event(OsEvent_MouseMove, outside, 0, 0);
    signal = test_ui_target_frame(events, 2, &key);
    EXPECT(signal.dragging);
    EXPECT(test_ui_near(signal.drag_delta.x, outside.x - centre.x));
    EXPECT(ui_hot_key() == key);  // capture: still hot although the mouse left

    events[0] = test_ui_mouse_event(OsEvent_MouseUp, outside, OsMouseButton_Left, 1);
    signal = test_ui_target_frame(events, 1, &key);
    EXPECT(signal.released);
    EXPECT(!signal.clicked);

    // Wheel over the box.
    events[0] = test_ui_mouse_event(OsEvent_MouseMove, centre, 0, 0);
    events[1] = test_ui_mouse_event(OsEvent_Wheel, centre, 0, 0);
    events[1].wheel_lines = v2(0.0f, -3.0f);
    signal = test_ui_target_frame(events, 2, &key);
    EXPECT(signal.scrolled);
    EXPECT(test_ui_near(signal.scroll.y, -3.0f));

    // A key event reaches the focused box, and only that one.
    ui_set_focus(key, 1);
    events[0] = test_ui_key_event(OsKey_Space, 0);
    signal = test_ui_target_frame(events, 1, &key);
    EXPECT(signal.key_pressed);
    EXPECT(signal.key == OsKey_Space);
}

// Three focusables, rebuilt every frame like a real app.
static void test_ui_tab_frame(const OsEvent *events, u64 event_count, UI_Key *keys) {
    test_ui_frame_begin(events, event_count);
    UI_Box *row = test_ui_row(300.0f, 100.0f);
    UI_Parent(row) {
        keys[0] = ui_build_box(UI_Focusable | UI_Clickable, str8_lit("a##tab"))->key;
        keys[1] = ui_build_box(UI_Focusable | UI_Clickable, str8_lit("b##tab"))->key;
        keys[2] = ui_build_box(UI_Focusable | UI_Clickable, str8_lit("c##tab"))->key;
    }
    test_ui_frame_end();
}

TEST(ui_focus_traversal) {
    Unused(arena);
    UI_Key keys[3] = {0, 0, 0};

    test_ui_tab_frame(0, 0, keys);
    ui_set_focus(0, 0);
    OsEvent tab = test_ui_key_event(OsKey_Tab, 0);
    OsEvent shift_tab = test_ui_key_event(OsKey_Tab, OsMod_Shift);

    test_ui_tab_frame(&tab, 1, keys);
    EXPECT(ui_focus_key() == keys[0]);  // no focus yet: Tab takes the first one

    test_ui_tab_frame(&tab, 1, keys);
    EXPECT(ui_focus_key() == keys[1]);

    test_ui_tab_frame(&shift_tab, 1, keys);
    EXPECT(ui_focus_key() == keys[0]);

    test_ui_tab_frame(&shift_tab, 1, keys);
    EXPECT(ui_focus_key() == keys[2]);  // wraps around

    OsEvent escape = test_ui_key_event(OsKey_Escape, 0);
    test_ui_tab_frame(&escape, 1, keys);
    EXPECT(ui_focus_key() == 0);
}

static UI_Box *test_ui_anim_frame(void) {
    test_ui_frame_begin(0, 0);
    UI_Box *box = ui_build_box(UI_Focusable | UI_Clickable, str8_lit("anim##box"));
    test_ui_frame_end();
    return box;
}

TEST(ui_animation_converges) {
    Unused(arena);
    UI_Box *box = test_ui_anim_frame();
    UI_Key key = box->key;
    EXPECT(box->focus_t == 0.0f);

    // 120 ms at 60 Hz is about 8 frames.
    ui_set_focus(key, 1);
    u32 frames = 0;
    do {
        box = test_ui_anim_frame();
        frames += 1;
        EXPECT(box->focus_t >= 0.0f && box->focus_t <= 1.0f);
    } while (ui_animating() && frames < 200);
    EXPECT(frames < 60);           // converged in well under a second
    EXPECT(box->focus_t == 1.0f);  // snapped exactly, once inside the epsilon
    EXPECT(!ui_animating());       // and the loop is allowed to sleep again

    // Losing the focus animates back down to exactly zero.
    ui_set_focus(0, 0);
    frames = 0;
    do {
        box = test_ui_anim_frame();
        frames += 1;
    } while (ui_animating() && frames < 200);
    EXPECT(box->focus_t == 0.0f);
    EXPECT(!ui_animating());

    // The public smoothing helper obeys the same contract, and keeps the loop
    // awake on its own while it has not arrived.
    f32 value = 0.0f;
    frames = 0;
    do {
        test_ui_frame_begin(0, 0);
        value = ui_animate(value, 100.0f, UI_ANIM_RATE_FAST);
        test_ui_frame_end();
        frames += 1;
    } while (ui_animating() && frames < 200);
    EXPECT(test_ui_near(value, 100.0f));
    EXPECT(frames < 60);
}

// The popup layer overlaps the content layer; the hit test must see it first.
static void test_ui_layer_frame(const OsEvent *events, u64 event_count, UI_Key *content_key,
                                UI_Key *popup_key) {
    test_ui_frame_begin(events, event_count);
    ui_push_pref_width(ui_px(400.0f, 1.0f));
    ui_push_pref_height(ui_px(400.0f, 1.0f));
    *content_key = ui_build_box(UI_Clickable, str8_lit("below##layer"))->key;
    ui_pop_pref_height();
    ui_pop_pref_width();
    UI_LayerScope(UI_Layer_Popup) {
        UI_FixedX(0.0f) UI_FixedY(0.0f)
        UI_PrefWidth(ui_px(200.0f, 1.0f))
        UI_PrefHeight(ui_px(200.0f, 1.0f)) {
            *popup_key = ui_build_box(UI_Clickable | UI_FloatingX | UI_FloatingY,
                                      str8_lit("above##layer"))->key;
        }
    }
    test_ui_frame_end();
}

TEST(ui_layers_are_hit_in_reverse_order) {
    Unused(arena);
    UI_Key content_key = 0, popup_key = 0;
    test_ui_layer_frame(0, 0, &content_key, &popup_key);

    OsEvent move = test_ui_mouse_event(OsEvent_MouseMove, v2(100.0f, 100.0f), 0, 0);
    test_ui_layer_frame(&move, 1, &content_key, &popup_key);
    EXPECT(ui_hot_key() == popup_key);  // the popup covers the content there

    move = test_ui_mouse_event(OsEvent_MouseMove, v2(300.0f, 300.0f), 0, 0);
    test_ui_layer_frame(&move, 1, &content_key, &popup_key);
    EXPECT(ui_hot_key() == content_key);
}

static void test_ui_run_all(void) {
    test_report("ui\n");
    test_ui_arena = arena_alloc(MB(256));
    test_ui_frame = arena_alloc(MB(64));
    r_atlas_init(test_ui_arena);
    if (os_font_init()) {
        ui_fonts_build(1.0f);
        ui_text_init(test_ui_arena);
    }
    ui_init(test_ui_arena);

    RUN(ui_layout_size_kinds);
    RUN(ui_layout_violations);
    RUN(ui_keys_are_stable_and_scoped);
    RUN(ui_orphan_boxes_are_released);
    RUN(ui_signals_from_events);
    RUN(ui_focus_traversal);
    RUN(ui_animation_converges);
    RUN(ui_layers_are_hit_in_reverse_order);
}

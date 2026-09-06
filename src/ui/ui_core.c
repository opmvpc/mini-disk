// ui_core.c - the engine of ADR-004. Boxes live in a permanent pool with a free
// list and are found by key; the tree that links them is rebuilt every frame in
// the caller's frame arena, laid out in five passes, then handed to r_core.
//
// No GL, no Win32, no platform call: r_*, ui_text_*, ui_font_* and base only.

#include "ui_core.h"

#include "r_atlas.h"
#include "ui_font.h"
#include "ui_text.h"

#define UI_STACK_DEPTH   64
#define UI_BUCKET_COUNT  4096  // power of two
#define UI_MAX_KEY_EVENTS 64
#define UI_DRAG_THRESHOLD 4.0f

typedef struct UI_KeyEvent {
    u32 key;
    u32 modifiers;
} UI_KeyEvent;

typedef struct UI_State {
    Arena *permanent;
    Arena *frame_arena;

    UI_Box **buckets;     // UI_BUCKET_COUNT chains of keyed boxes
    UI_Box *free_first;   // pool free list
    UI_Box *transient_first;  // key 0 boxes of the frame, released at the next begin
    u64 box_count;        // keyed boxes alive in the table

    UI_Box *roots[UI_Layer_COUNT];
    u64 frame_index;
    f32 dt;
    f32 dpi_scale;
    V2 viewport;
    i32 active_animations;

    // interaction, resolved once per frame in ui_begin against the rects of
    // the previous frame (research/03 s3.5).
    V2 mouse, mouse_prev;
    V2 drag_start;
    b32 drag_started;
    UI_Key hot_key, active_key, focus_key;
    UI_Key press_key, release_key, click_key, double_click_key, right_click_key;
    UI_Key right_press_key, scroll_key;
    V2 scroll_delta;
    u32 press_click_count;
    b32 focus_via_keyboard;
    UI_KeyEvent key_events[UI_MAX_KEY_EVENTS];
    u32 key_event_count;

#define X(name, type)                       \
    type name##_stack[UI_STACK_DEPTH];      \
    u32 name##_depth;
    UI_STACK_LIST
#undef X
} UI_State;

global UI_State ui_state;

// --- small math ------------------------------------------------------------
// 2^x for x <= 0, ~1e-4 absolute: enough for an animation, and no libm.
static f32 ui_exp2(f32 x) {
    if (x < -60.0f) { return 0.0f; }
    f32 whole = floor_f32(x);
    f32 f = x - whole;
    f32 poly = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    i32 e = (i32)whole + 127;
    return poly * bits_f32((u32)e << 23);
}

static f32 ui_anim_step(f32 current, f32 target, f32 rate) {
    f32 t = 1.0f - ui_exp2(-rate * ui_state.dt);
    f32 next = current + (target - current) * t;
    if (abs_f32(target - next) < UI_ANIM_EPSILON) { return target; }
    ui_state.active_animations += 1;
    return next;
}

// Premultiplied RGBA8 stays premultiplied under both of these.
static u32 ui_color_lerp(u32 a, u32 b, f32 t) {
    u32 result = 0;
    for (u32 i = 0; i < 4; i += 1) {
        f32 ca = (f32)((a >> (i * 8)) & 0xFFu);
        f32 cb = (f32)((b >> (i * 8)) & 0xFFu);
        f32 c = clamp_f32(ca + (cb - ca) * t, 0.0f, 255.0f);
        result |= ((u32)(c + 0.5f) & 0xFFu) << (i * 8);
    }
    return result;
}

static u32 ui_color_fade(u32 color, f32 t) {
    u32 result = 0;
    for (u32 i = 0; i < 4; i += 1) {
        f32 c = (f32)((color >> (i * 8)) & 0xFFu) * clamp_f32(t, 0.0f, 1.0f);
        result |= ((u32)(c + 0.5f) & 0xFFu) << (i * 8);
    }
    return result;
}

// --- style stacks ----------------------------------------------------------
#define X(name, type)                                                 \
    void ui_push_##name(type value) {                                 \
        Assert(ui_state.name##_depth < UI_STACK_DEPTH);               \
        ui_state.name##_stack[ui_state.name##_depth] = value;         \
        ui_state.name##_depth += 1;                                   \
    }                                                                 \
    type ui_pop_##name(void) {                                        \
        Assert(ui_state.name##_depth > 1);                            \
        ui_state.name##_depth -= 1;                                   \
        return ui_state.name##_stack[ui_state.name##_depth];          \
    }                                                                 \
    type ui_top_##name(void) {                                        \
        Assert(ui_state.name##_depth > 0);                            \
        return ui_state.name##_stack[ui_state.name##_depth - 1];      \
    }
UI_STACK_LIST
#undef X

static void ui_stacks_reset(void) {
    OsFont default_font = ui_font(UI_FontStyle_Ui);
#define X(name, type)                    \
    ui_state.name##_depth = 1;           \
    mem_zero((void *)&ui_state.name##_stack[0], sizeof(type));
    UI_STACK_LIST
#undef X
    ui_state.pref_width_stack[0] = ui_children_sum(1.0f);
    ui_state.pref_height_stack[0] = ui_children_sum(1.0f);
    ui_state.child_layout_axis_stack[0] = Axis2_Y;
    ui_state.text_color_stack[0] = r_rgba(0xE8, 0xE8, 0xF0, 255);
    ui_state.border_color_stack[0] = r_rgba(255, 255, 255, 40);
    ui_state.bg_color_stack[0] = r_rgba(0x24, 0x24, 0x2C, 255);
    ui_state.text_padding_stack[0] = 6.0f * ui_state.dpi_scale;
    ui_state.font_stack[0] = default_font;
}

// --- pool and hash table ---------------------------------------------------
static UI_Box *ui_box_alloc(void) {
    UI_Box *box = ui_state.free_first;
    if (box) {
        ui_state.free_first = box->free_next;
    } else {
        box = push_struct(ui_state.permanent, UI_Box);
    }
    mem_zero(box, sizeof(*box));
    return box;
}

static u64 ui_bucket_index(UI_Key key) { return key & (UI_BUCKET_COUNT - 1); }

UI_Box *ui_box_from_key(UI_Key key) {
    if (key == 0) { return 0; }
    for (UI_Box *box = ui_state.buckets[ui_bucket_index(key)]; box; box = box->hash_next) {
        if (box->key == key) { return box; }
    }
    return 0;
}

static void ui_box_release(UI_Box *box) {
    if (box->key != 0) {
        u64 slot = ui_bucket_index(box->key);
        UI_Box **link = &ui_state.buckets[slot];
        while (*link != box) {
            Assert(*link != 0);
            link = &(*link)->hash_next;
        }
        *link = box->hash_next;
        ui_state.box_count -= 1;
    }
    box->free_next = ui_state.free_first;
    ui_state.free_first = box;
}

// A box the caller stopped building is dead one frame later: the frame that
// follows still needs its rect for the hit test (research/03 s3.5).
static void ui_prune(void) {
    for (UI_Box *box = ui_state.transient_first; box;) {
        UI_Box *next = box->free_next;
        box->free_next = ui_state.free_first;
        ui_state.free_first = box;
        box = next;
    }
    ui_state.transient_first = 0;

    for (u64 slot = 0; slot < UI_BUCKET_COUNT; slot += 1) {
        UI_Box *box = ui_state.buckets[slot];
        while (box) {
            UI_Box *next = box->hash_next;
            if (box->last_frame_touched + 1 < ui_state.frame_index) { ui_box_release(box); }
            box = next;
        }
    }
}

u64 ui_box_count(void) { return ui_state.box_count; }
u64 ui_frame_index(void) { return ui_state.frame_index; }
UI_Key ui_hot_key(void) { return ui_state.hot_key; }
UI_Key ui_active_key(void) { return ui_state.active_key; }
UI_Key ui_focus_key(void) { return ui_state.focus_key; }
V2 ui_mouse(void) { return ui_state.mouse; }
Arena *ui_frame_arena(void) { return ui_state.frame_arena; }
f32 ui_animate(f32 current, f32 target, f32 rate) { return ui_anim_step(current, target, rate); }
b32 ui_animating(void) { return ui_state.active_animations > 0; }
UI_Box *ui_root(UI_Layer layer) { return ui_state.roots[layer]; }

// --- keys ------------------------------------------------------------------
// "Label##id" identifies on the whole string, "Label###id" on the tail only.
static String8 ui_display_part(String8 string) {
    u64 at = str8_find(string, str8_lit("##"), 0);
    return (at == string.size) ? string : str8_prefix(string, at);
}

static String8 ui_id_part(String8 string) {
    u64 at3 = str8_find(string, str8_lit("###"), 0);
    if (at3 != string.size) { return str8_skip(string, at3 + 3); }
    return string;
}

UI_Key ui_key_from_string(UI_Key seed, String8 string) {
    String8 id = ui_id_part(string);
    if (id.size == 0) { return 0; }
    return hash64_combine(seed, hash64(id.str, id.size));
}

// --- construction ----------------------------------------------------------
UI_Box *ui_build_box_from_key(UI_Flags flags, UI_Key key) {
    UI_Box *parent = ui_top_parent();
    UI_Box *box = ui_box_from_key(key);
    if (box && box->last_frame_touched == ui_state.frame_index) {
        // Same key twice in one frame: the caller forgot a seed. Keep the tree
        // well formed by giving the duplicate a transient identity.
        Assert(!"duplicate UI key in one frame");
        key = 0;
        box = 0;
    }
    if (!box) {
        box = ui_box_alloc();
        box->key = key;
        if (key != 0) {
            u64 slot = ui_bucket_index(key);
            box->hash_next = ui_state.buckets[slot];
            ui_state.buckets[slot] = box;
            ui_state.box_count += 1;
        } else {
            box->free_next = ui_state.transient_first;
            ui_state.transient_first = box;
        }
    }
    box->last_frame_touched = ui_state.frame_index;
    box->first = 0;
    box->last = 0;
    box->next = 0;
    box->prev = 0;
    box->parent = parent;
    if (parent) {
        if (parent->last) {
            parent->last->next = box;
            box->prev = parent->last;
            parent->last = box;
        } else {
            parent->first = box;
            parent->last = box;
        }
        box->layer = parent->layer;
    }

    box->flags = flags | ui_top_flags();
    box->pref_size[Axis2_X] = ui_top_pref_width();
    box->pref_size[Axis2_Y] = ui_top_pref_height();
    box->child_layout_axis = ui_top_child_layout_axis();
    box->corner_radius = ui_top_corner_radius();
    box->border_thickness = ui_top_border_thickness();
    box->text_padding = ui_top_text_padding();
    box->bg_color = ui_top_bg_color();
    box->border_color = ui_top_border_color();
    box->text_color = ui_top_text_color();
    box->font = ui_top_font();
    box->fixed_pos = v2(ui_top_fixed_x(), ui_top_fixed_y());
    box->display_string.str = 0;
    box->display_string.size = 0;
    return box;
}

UI_Box *ui_build_box(UI_Flags flags, String8 string) {
    UI_Box *parent = ui_top_parent();
    UI_Key seed = ui_top_seed();
    if (seed == 0 && parent) { seed = parent->key; }
    UI_Box *box = ui_build_box_from_key(flags, ui_key_from_string(seed, string));
    box->display_string = ui_display_part(string);
    return box;
}

void ui_push_layer(UI_Layer layer) { ui_push_parent(ui_state.roots[layer]); }
void ui_pop_layer(void) { ui_pop_parent(); }

// --- hit test --------------------------------------------------------------
// Reverse drawing order: the last box emitted is the one on top.
static UI_Box *ui_hit_test_tree(UI_Box *box, V2 point, UI_Flags mask) {
    for (UI_Box *child = box->last; child; child = child->prev) {
        UI_Box *hit = ui_hit_test_tree(child, point, mask);
        if (hit) { return hit; }
    }
    if ((box->flags & mask) && !(box->flags & UI_Disabled) && rect_contains(box->rect, point) &&
        rect_contains(box->clip_rect, point)) {
        return box;
    }
    return 0;
}

static UI_Box *ui_hit_test(V2 point, UI_Flags mask) {
    for (i32 layer = UI_Layer_COUNT - 1; layer >= 0; layer -= 1) {
        UI_Box *root = ui_state.roots[layer];
        if (!root) { continue; }
        UI_Box *hit = ui_hit_test_tree(root, point, mask);
        if (hit) { return hit; }
    }
    return 0;
}

static UI_Key ui_hit_key(V2 point, UI_Flags mask) {
    UI_Box *box = ui_hit_test(point, mask);
    return box ? box->key : 0;
}

// --- focus traversal -------------------------------------------------------
// The neighbours of the focus in prefix order, found in one walk. No array:
// the tree has no bound on its focusable count and the stack has one on its
// size (C6262), so the four boxes the traversal needs are all we remember.
typedef struct UI_FocusScan {
    UI_Key target;
    UI_Box *first, *last, *prev, *next;
    b32 found;
} UI_FocusScan;

static void ui_focus_scan(UI_Box *box, UI_FocusScan *scan) {
    if ((box->flags & UI_Focusable) && !(box->flags & UI_Disabled) && box->key != 0) {
        if (!scan->first) { scan->first = box; }
        if (scan->found && !scan->next) { scan->next = box; }
        if (box->key == scan->target) {
            scan->found = 1;
            scan->prev = scan->last;
        }
        scan->last = box;
    }
    for (UI_Box *child = box->first; child; child = child->next) { ui_focus_scan(child, scan); }
}

// Tab order is the prefix order of the tree, filtered on UI_Focusable.
static void ui_focus_advance(i32 direction) {
    UI_FocusScan scan;
    StructZero(&scan);
    scan.target = ui_state.focus_key;
    for (u32 layer = 0; layer < UI_Layer_COUNT; layer += 1) {
        if (ui_state.roots[layer]) { ui_focus_scan(ui_state.roots[layer], &scan); }
    }
    if (!scan.first) { return; }
    UI_Box *next;
    if (direction > 0) {
        next = scan.found ? (scan.next ? scan.next : scan.first) : scan.first;
    } else {
        next = scan.found ? (scan.prev ? scan.prev : scan.last) : scan.last;
    }
    ui_state.focus_key = next->key;
    ui_state.focus_via_keyboard = 1;
}

void ui_set_focus(UI_Key key, b32 via_keyboard) {
    ui_state.focus_key = key;
    ui_state.focus_via_keyboard = via_keyboard;
}

// --- events ----------------------------------------------------------------
static void ui_consume_events(const OsEvent *events, u64 event_count) {
    ui_state.press_key = 0;
    ui_state.release_key = 0;
    ui_state.click_key = 0;
    ui_state.double_click_key = 0;
    ui_state.right_click_key = 0;
    ui_state.scroll_key = 0;
    ui_state.scroll_delta = v2(0.0f, 0.0f);
    ui_state.key_event_count = 0;
    ui_state.mouse_prev = ui_state.mouse;

    for (u64 i = 0; i < event_count; i += 1) {
        const OsEvent *event = &events[i];
        switch (event->kind) {
            case OsEvent_MouseMove: {
                ui_state.mouse = event->pos;
                if (ui_state.active_key != 0 &&
                    v2_length(v2_sub(ui_state.mouse, ui_state.drag_start)) > UI_DRAG_THRESHOLD) {
                    ui_state.drag_started = 1;
                }
            } break;

            case OsEvent_MouseDown: {
                ui_state.mouse = event->pos;
                UI_Key hit = ui_hit_key(ui_state.mouse, UI_Clickable);
                if (event->button == OsMouseButton_Left) {
                    ui_state.active_key = hit;
                    ui_state.press_key = hit;
                    ui_state.press_click_count = event->click_count;
                    ui_state.drag_start = ui_state.mouse;
                    ui_state.drag_started = 0;
                    UI_Box *box = ui_box_from_key(hit);
                    if (box && (box->flags & UI_Focusable)) {
                        ui_state.focus_key = hit;
                        ui_state.focus_via_keyboard = 0;
                    } else if (hit == 0) {
                        ui_state.focus_key = 0;
                    }
                } else if (event->button == OsMouseButton_Right) {
                    ui_state.right_press_key = hit;
                }
            } break;

            case OsEvent_MouseUp: {
                ui_state.mouse = event->pos;
                UI_Key hit = ui_hit_key(ui_state.mouse, UI_Clickable);
                if (event->button == OsMouseButton_Left) {
                    ui_state.release_key = ui_state.active_key;
                    if (ui_state.active_key != 0 && hit == ui_state.active_key) {
                        ui_state.click_key = ui_state.active_key;
                        if (ui_state.press_click_count >= 2) {
                            ui_state.double_click_key = ui_state.active_key;
                        }
                    }
                    ui_state.active_key = 0;
                    ui_state.drag_started = 0;
                } else if (event->button == OsMouseButton_Right) {
                    if (ui_state.right_press_key != 0 && hit == ui_state.right_press_key) {
                        ui_state.right_click_key = hit;
                    }
                    ui_state.right_press_key = 0;
                }
            } break;

            case OsEvent_Wheel: {
                ui_state.scroll_key = ui_hit_key(ui_state.mouse, UI_Scrollable);
                ui_state.scroll_delta = v2_add(ui_state.scroll_delta, event->wheel_lines);
            } break;

            case OsEvent_KeyDown: {
                if (event->key == OsKey_Tab) {
                    ui_focus_advance((event->modifiers & OsMod_Shift) ? -1 : 1);
                } else if (event->key == OsKey_Escape) {
                    ui_state.focus_key = 0;
                } else if (ui_state.key_event_count < UI_MAX_KEY_EVENTS) {
                    UI_KeyEvent *slot = &ui_state.key_events[ui_state.key_event_count];
                    slot->key = event->key;
                    slot->modifiers = event->modifiers;
                    ui_state.key_event_count += 1;
                }
            } break;

            case OsEvent_FocusLose: {
                ui_state.active_key = 0;
                ui_state.drag_started = 0;
            } break;

            default: break;
        }
    }

    // Capture: while a button is held the hot box is the pressed one, so a
    // drag that leaves the widget keeps feeding it.
    ui_state.hot_key = ui_state.active_key ? ui_state.active_key
                                           : ui_hit_key(ui_state.mouse, UI_Clickable);
}

UI_Signal ui_signal(UI_Box *box) {
    UI_Signal signal;
    StructZero(&signal);
    signal.box = box;
    signal.mouse = v2_sub(ui_state.mouse, box->rect.min);
    if (box->key == 0 || (box->flags & UI_Disabled)) { return signal; }

    signal.hovering = (ui_state.hot_key == box->key) && (ui_state.active_key == 0 ||
                                                         ui_state.active_key == box->key);
    signal.pressed = (ui_state.press_key == box->key);
    signal.released = (ui_state.release_key == box->key);
    signal.clicked = (ui_state.click_key == box->key);
    signal.double_clicked = (ui_state.double_click_key == box->key);
    signal.right_clicked = (ui_state.right_click_key == box->key);
    signal.dragging = ui_state.drag_started && (ui_state.active_key == box->key);
    if (ui_state.active_key == box->key) {
        signal.drag_delta = v2_sub(ui_state.mouse, ui_state.drag_start);
    }
    if (ui_state.scroll_key == box->key) {
        signal.scrolled = 1;
        signal.scroll = ui_state.scroll_delta;
    }
    if (ui_state.focus_key == box->key && ui_state.key_event_count > 0) {
        signal.key = ui_state.key_events[0].key;
        signal.modifiers = ui_state.key_events[0].modifiers;
        for (u32 i = 0; i < ui_state.key_event_count; i += 1) {
            u32 key = ui_state.key_events[i].key;
            if (key == OsKey_Space || key == OsKey_Enter) { signal.key_pressed = 1; }
        }
    }
    return signal;
}

// --- layout ----------------------------------------------------------------
static UI_Flags ui_floating_flag(Axis2 axis) {
    return (axis == Axis2_X) ? UI_FloatingX : UI_FloatingY;
}

// The five passes of ADR-004 are five *dependency levels*, not five walks. Two
// of them do the work:
//
//   walk A (ui_layout_sizes)  standalone + upward on the way down, downward on
//                             the way up, both axes at once;
//   walk B (ui_layout_place)  violations on the children of a box, then their
//                             positions, then recursion, both axes at once.
//
// Order is preserved exactly: prefix passes still see a parent resolved before
// its children, the postfix pass still sees resolved children.

// Walk A. Pixels/TextContent/Null need nothing, PercentOfParent needs the
// parent (already visited), ChildrenSum needs the children (visited below).
static void ui_layout_sizes(UI_Box *box) {
    UI_Box *parent = box->parent;
    for (u32 axis = 0; axis < Axis2_COUNT; axis += 1) {
        UI_Size size = box->pref_size[axis];
        switch (size.kind) {
            case UI_SizeKind_Null: {
                box->computed_size[axis] = 0.0f;
            } break;
            case UI_SizeKind_Pixels: {
                box->computed_size[axis] = size.value;
            } break;
            case UI_SizeKind_TextContent: {
                f32 content = 0.0f;
                if (box->font.v != 0) {
                    content = (axis == Axis2_X) ? ui_text_width(box->font, box->display_string, 0)
                                                : ui_text_line_height(box->font);
                }
                box->computed_size[axis] = content + 2.0f * size.value;
            } break;
            case UI_SizeKind_PercentOfParent: {
                f32 parent_size = parent ? parent->computed_size[axis] : 0.0f;
                box->computed_size[axis] = parent_size * size.value;
            } break;
            default: break;  // ChildrenSum: resolved on the way back up
        }
    }

    b32 sum_x = (box->pref_size[Axis2_X].kind == UI_SizeKind_ChildrenSum);
    b32 sum_y = (box->pref_size[Axis2_Y].kind == UI_SizeKind_ChildrenSum);
    if (!sum_x && !sum_y) {
        for (UI_Box *child = box->first; child; child = child->next) { ui_layout_sizes(child); }
        return;
    }

    // The sums of the children are accumulated by the same descent, so the
    // postfix pass costs no extra traversal of the child list.
    Axis2 layout_axis = box->child_layout_axis;
    f32 total[Axis2_COUNT] = {0.0f, 0.0f};
    for (UI_Box *child = box->first; child; child = child->next) {
        ui_layout_sizes(child);
        for (u32 axis = 0; axis < Axis2_COUNT; axis += 1) {
            if (child->flags & ui_floating_flag((Axis2)axis)) { continue; }
            f32 child_size = child->computed_size[axis];
            if ((Axis2)axis == layout_axis) {
                total[axis] += child_size;
            } else {
                total[axis] = max_f32(total[axis], child_size);
            }
        }
    }
    if (sum_x) { box->computed_size[Axis2_X] = total[Axis2_X]; }
    if (sum_y) { box->computed_size[Axis2_Y] = total[Axis2_Y]; }
}

md_inline f32 ui_give_factor(UI_Box *box, u32 axis) {
    return 1.0f - clamp_f32(box->pref_size[axis].strictness, 0.0f, 1.0f);
}

// Walk B. The children of a box never overflow it: the excess is taken from
// each child in proportion to size * (1 - strictness), strictness 1 never
// gives. Their positions follow immediately, then the recursion, because
// fixing a child only ever touches the sizes of *its own* children.
static void ui_layout_place(UI_Box *box) {
    Axis2 layout_axis = box->child_layout_axis;
    f32 available[Axis2_COUNT];
    available[Axis2_X] = box->computed_size[Axis2_X];
    available[Axis2_Y] = box->computed_size[Axis2_Y];
    UI_Flags floating_along = ui_floating_flag(layout_axis);
    Axis2 cross_axis = (layout_axis == Axis2_X) ? Axis2_Y : Axis2_X;
    UI_Flags floating_cross = ui_floating_flag(cross_axis);

    f32 total = 0.0f;
    f32 budget = 0.0f;
    for (UI_Box *child = box->first; child; child = child->next) {
        // The parent may have shrunk since walk A: percentages follow it.
        for (u32 axis = 0; axis < Axis2_COUNT; axis += 1) {
            if (child->pref_size[axis].kind == UI_SizeKind_PercentOfParent) {
                child->computed_size[axis] = available[axis] * child->pref_size[axis].value;
            }
        }
        if (!(child->flags & floating_along)) {
            f32 child_size = child->computed_size[layout_axis];
            total += child_size;
            budget += child_size * ui_give_factor(child, layout_axis);
        }
        // Across the layout axis every child is clamped on its own.
        if (!(child->flags & floating_cross)) {
            f32 violation = child->computed_size[cross_axis] - available[cross_axis];
            if (violation > 0.0f) {
                child->computed_size[cross_axis] -= violation * ui_give_factor(child, cross_axis);
            }
        }
    }
    f32 violation = total - available[layout_axis];
    b32 shrinking = (violation > 0.0f && budget > 0.0f);

    f32 cursor = 0.0f;
    Rect child_clip = (box->flags & UI_Clip) ? rect_intersect(box->rect, box->clip_rect)
                                             : box->clip_rect;
    for (UI_Box *child = box->first; child; child = child->next) {
        if (shrinking && !(child->flags & floating_along)) {
            f32 child_size = child->computed_size[layout_axis];
            f32 give = child_size * ui_give_factor(child, layout_axis);
            child->computed_size[layout_axis] -= min_f32(violation * (give / budget), child_size);
        }
        for (u32 axis = 0; axis < Axis2_COUNT; axis += 1) {
            f32 rel;
            if (child->flags & ui_floating_flag((Axis2)axis)) {
                rel = child->fixed_pos.v[axis];
            } else {
                rel = 0.0f;
                if ((Axis2)axis == layout_axis) {
                    rel = cursor;
                    cursor += child->computed_size[axis];
                }
                rel -= box->view_off.v[axis];
            }
            child->computed_rel_pos[axis] = rel;
        }
        f32 x = box->rect.min.x + child->computed_rel_pos[Axis2_X];
        f32 y = box->rect.min.y + child->computed_rel_pos[Axis2_Y];
        child->rect = rect(x, y, x + child->computed_size[Axis2_X],
                           y + child->computed_size[Axis2_Y]);
        child->clip_rect = child_clip;
        ui_layout_place(child);
    }
}

void ui_layout(UI_Box *root) {
    ui_layout_sizes(root);
    root->computed_rel_pos[Axis2_X] = root->fixed_pos.x;
    root->computed_rel_pos[Axis2_Y] = root->fixed_pos.y;
    root->rect = rect(root->fixed_pos.x, root->fixed_pos.y,
                      root->fixed_pos.x + root->computed_size[Axis2_X],
                      root->fixed_pos.y + root->computed_size[Axis2_Y]);
    root->clip_rect = rect(0.0f, 0.0f, ui_state.viewport.x, ui_state.viewport.y);
    ui_layout_place(root);
}

// --- animation -------------------------------------------------------------
static void ui_animate_box(UI_Box *box) {
    f32 hot_target = (box->key != 0 && box->key == ui_state.hot_key) ? 1.0f : 0.0f;
    f32 active_target = (box->key != 0 && box->key == ui_state.active_key) ? 1.0f : 0.0f;
    f32 focus_target =
        (box->key != 0 && box->key == ui_state.focus_key && ui_state.focus_via_keyboard) ? 1.0f
                                                                                         : 0.0f;
    box->hot_t = ui_anim_step(box->hot_t, hot_target, UI_ANIM_RATE_FAST);
    box->active_t = ui_anim_step(box->active_t, active_target, UI_ANIM_RATE_FAST);
    box->focus_t = ui_anim_step(box->focus_t, focus_target, UI_ANIM_RATE_FAST);
    box->view_off.x = ui_anim_step(box->view_off.x, box->view_off_target.x, UI_ANIM_RATE_SLOW);
    box->view_off.y = ui_anim_step(box->view_off.y, box->view_off_target.y, UI_ANIM_RATE_SLOW);
}

// --- rendering -------------------------------------------------------------
#define UI_FOCUS_RING_COLOR r_rgba(0xF4, 0x9A, 0x2A, 255)

static void ui_draw_box(UI_Box *box) {
    f32 scale = ui_state.dpi_scale;
    R_RectParams params;

    if (box->flags & UI_DrawDropShadow) {
        r_shadow(box->rect, r_rgba(0, 0, 0, 120), box->corner_radius, 12.0f * scale,
                 v2(0.0f, 3.0f * scale));
    }
    if (box->flags & UI_DrawBackground) {
        // hot and active are drawn here so every box animates for free; a
        // widget that wants another look pushes its own colours.
        u32 color = box->bg_color;
        color = ui_color_lerp(color, r_rgba(255, 255, 255, 255), 0.08f * box->hot_t);
        color = ui_color_lerp(color, r_rgba(0, 0, 0, 255), 0.10f * box->active_t);
        StructZero(&params);
        params.dst = box->rect;
        params.color = color;
        params.corner_radius = box->corner_radius;
        r_rect(params);
    }
    if (box->flags & UI_DrawBorder) {
        StructZero(&params);
        params.dst = box->rect;
        params.color = box->border_color;
        params.corner_radius = box->corner_radius;
        params.border = max_f32(box->border_thickness, 1.0f * scale);
        r_rect(params);
    }
    if (box->focus_t > UI_ANIM_EPSILON) {
        f32 grow = 2.0f * scale;
        StructZero(&params);
        params.dst = rect(box->rect.min.x - grow, box->rect.min.y - grow, box->rect.max.x + grow,
                          box->rect.max.y + grow);
        params.color = ui_color_fade(UI_FOCUS_RING_COLOR, box->focus_t);
        params.corner_radius = box->corner_radius + grow;
        params.border = 2.0f * scale;
        r_rect(params);
    }
    if ((box->flags & UI_DrawText) && box->font.v != 0 && box->display_string.size != 0) {
        f32 height = rect_height(box->rect);
        f32 line = ui_text_line_height(box->font);
        f32 baseline = box->rect.min.y + round_f32((height - line) * 0.5f) +
                       ui_text_ascent(box->font);
        f32 max_width = rect_width(box->rect) - 2.0f * box->text_padding;
        ui_text_draw_ellipsized(box->font, box->display_string,
                                v2(box->rect.min.x + box->text_padding, baseline), max_width,
                                box->text_color, 0);
    }

    b32 clipped = (box->flags & UI_Clip) != 0;
    if (clipped) { r_push_clip(box->rect); }
    for (UI_Box *child = box->first; child; child = child->next) { ui_draw_box(child); }
    if (clipped) { r_pop_clip(); }
}

// --- frame -----------------------------------------------------------------
void ui_init(Arena *permanent) {
    StructZero(&ui_state);
    ui_state.permanent = permanent;
    ui_state.buckets = push_array_zero(permanent, UI_Box *, UI_BUCKET_COUNT);
    ui_state.dpi_scale = 1.0f;
    ui_state.mouse = v2(-100000.0f, -100000.0f);
}

void ui_begin(Arena *frame_arena, const OsEvent *events, u64 event_count, f32 dt, V2 viewport,
              f32 dpi_scale) {
    // The interaction of this frame is resolved against the rects of the
    // previous one, so the tree of the previous frame must still be intact.
    ui_consume_events(events, event_count);

    ui_state.frame_index += 1;
    ui_prune();

    ui_state.frame_arena = frame_arena;
    ui_state.dt = clamp_f32(dt, 0.0f, UI_DT_MAX);
    ui_state.viewport = viewport;
    ui_state.dpi_scale = dpi_scale;
    ui_state.active_animations = 0;

    ui_stacks_reset();
    ui_state.parent_stack[0] = 0;

    // One floating root per layer, sized to the viewport.
    for (u32 layer = 0; layer < UI_Layer_COUNT; layer += 1) {
        UI_Key key = hash64_combine(0x5511EEull, (u64)layer + 1);
        ui_push_pref_width(ui_px(viewport.x, 1.0f));
        ui_push_pref_height(ui_px(viewport.y, 1.0f));
        UI_Box *root = ui_build_box_from_key(UI_FloatingX | UI_FloatingY, key);
        ui_pop_pref_height();
        ui_pop_pref_width();
        root->layer = (UI_Layer)layer;
        root->fixed_pos = v2(0.0f, 0.0f);
        ui_state.roots[layer] = root;
    }
    ui_state.parent_stack[0] = ui_state.roots[UI_Layer_Content];
}

void ui_end(void) {
    // Every UI_* block must be closed: only the bottom of each stack is left.
#define X(name, type) Assert(ui_state.name##_depth == 1);
    UI_STACK_LIST
#undef X

    for (u32 layer = 0; layer < UI_Layer_COUNT; layer += 1) {
        ui_layout(ui_state.roots[layer]);
    }

    for (u64 slot = 0; slot < UI_BUCKET_COUNT; slot += 1) {
        for (UI_Box *box = ui_state.buckets[slot]; box; box = box->hash_next) {
            ui_animate_box(box);
        }
    }

    for (u32 layer = 0; layer < UI_Layer_COUNT; layer += 1) {
        r_set_layer((R_Layer)layer);
        ui_draw_box(ui_state.roots[layer]);
    }
    r_set_layer(R_Layer_Content);
}

// --- the two widgets the demo needs ----------------------------------------
UI_Box *ui_label(String8 string) {
    UI_Box *box = 0;
    UI_PrefWidth(ui_text_size(ui_top_text_padding(), 0.0f))
    UI_PrefHeight(ui_text_size(2.0f * ui_state.dpi_scale, 1.0f)) {
        box = ui_build_box(UI_DrawText, string);
    }
    return box;
}

UI_Box *ui_labelf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    String8 text = str8fv(ui_state.frame_arena, fmt, args);
    va_end(args);
    return ui_label(text);
}

UI_Signal ui_button(String8 string) {
    UI_Box *box = ui_build_box(UI_Clickable | UI_Focusable | UI_DrawBackground | UI_DrawBorder |
                                   UI_DrawText,
                               string);
    return ui_signal(box);
}

UI_Box *ui_spacer(UI_Size size) {
    UI_Box *box = 0;
    Axis2 axis = ui_top_child_layout_axis();
    if (axis == Axis2_X) {
        UI_PrefWidth(size) { box = ui_build_box_from_key(0, 0); }
    } else {
        UI_PrefHeight(size) { box = ui_build_box_from_key(0, 0); }
    }
    return box;
}

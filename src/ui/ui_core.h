// ui_core.h - immediate API, retained core (ADR-004): the widget tree is rebuilt
// every frame, the state that must survive (hot/active/focus animations, scroll)
// is found again through a hashed key.
//
// Nothing here talks to GL or to Win32. ui_core.c calls r_*, ui_text_*,
// ui_font_* and base only; the OS reaches it as an array of OsEvent.
#ifndef UI_CORE_H
#define UI_CORE_H

#include "../base/base.h"
#include "../base/base_arena.h"
#include "../base/base_hash.h"
#include "../base/base_math.h"
#include "../base/base_string.h"
#include "../platform/platform.h"
#include "r_core.h"

typedef u64 UI_Key;

typedef enum Axis2 {
    Axis2_X = 0,
    Axis2_Y,
    Axis2_COUNT
} Axis2;

typedef u32 UI_Flags;
enum {
    UI_Clickable      = 1u << 0,   // takes part in the hit test, emits signals
    UI_Scrollable     = 1u << 1,   // the wheel over it produces a scroll signal
    UI_Focusable      = 1u << 2,   // reachable with Tab, receives key events
    UI_DrawBackground = 1u << 3,
    UI_DrawBorder     = 1u << 4,
    UI_DrawText       = 1u << 5,
    UI_DrawDropShadow = 1u << 6,
    UI_Clip           = 1u << 7,   // children are scissored to this rect
    UI_FloatingX      = 1u << 8,   // position imposed, ignored by the parent layout
    UI_FloatingY      = 1u << 9,
    UI_Disabled       = 1u << 10,  // no signal, drawn dimmed by the widget
};

typedef enum UI_SizeKind {
    UI_SizeKind_Null = 0,       // occupies nothing
    UI_SizeKind_Pixels,         // physical pixels, DPI already applied
    UI_SizeKind_TextContent,    // measured text + value as padding
    UI_SizeKind_PercentOfParent,
    UI_SizeKind_ChildrenSum,    // sum along the layout axis, max across it
} UI_SizeKind;

// strictness: 0 = shrinks first when the parent overflows, 1 = never shrinks.
typedef struct UI_Size {
    UI_SizeKind kind;
    f32 value;
    f32 strictness;
} UI_Size;

md_inline UI_Size ui_size(UI_SizeKind kind, f32 value, f32 strictness) {
    UI_Size result;
    result.kind = kind;
    result.value = value;
    result.strictness = strictness;
    return result;
}
md_inline UI_Size ui_px(f32 value, f32 strictness) {
    return ui_size(UI_SizeKind_Pixels, value, strictness);
}
md_inline UI_Size ui_text_size(f32 padding, f32 strictness) {
    return ui_size(UI_SizeKind_TextContent, padding, strictness);
}
md_inline UI_Size ui_pct(f32 value, f32 strictness) {
    return ui_size(UI_SizeKind_PercentOfParent, value, strictness);
}
md_inline UI_Size ui_children_sum(f32 strictness) {
    return ui_size(UI_SizeKind_ChildrenSum, 0.0f, strictness);
}
#define ui_fill() ui_pct(1.0f, 0.0f)

// The three z layers, one layout root each, drawn in this order.
typedef enum UI_Layer {
    UI_Layer_Content = 0,
    UI_Layer_Popup,
    UI_Layer_Tooltip,
    UI_Layer_COUNT
} UI_Layer;
// The layers map one to one onto R_Layer: ui_end casts one into the other.
StaticAssert(UI_Layer_COUNT == 3, ui_layer_count_is_three);
StaticAssert(R_Layer_COUNT == 3, r_layer_count_is_three);

typedef struct UI_Box UI_Box;
struct UI_Box {
    // -- hot: exactly what the two layout walks read and write ----------
    // 120 bytes, kept first and in one block. A tree of 10 000 boxes does
    // not fit in L2, so the layout pays a miss per cache line it touches;
    // the cold half below (identity, style, animation) is never read there.
    UI_Box *first;   // the layout only ever walks children forward
    UI_Box *next;
    UI_Box *parent;
    UI_Flags flags;
    Axis2 child_layout_axis;
    UI_Size pref_size[Axis2_COUNT];
    f32 computed_size[Axis2_COUNT];
    f32 computed_rel_pos[Axis2_COUNT];
    V2 fixed_pos;  // UI_FloatingX/Y: relative to the parent
    V2 view_off;   // scroll, subtracted from the children positions
    Rect rect;       // absolute, physical pixels
    Rect clip_rect;  // intersection of the enclosing UI_Clip rects

    // -- cold: allocation and lookup ------------------------------------
    UI_Box *hash_next;   // bucket chain, only for keyed boxes
    UI_Box *free_next;   // free list / transient list of the frame
    UI_Key key;          // 0: pure layout node, no persistence
    u64 last_frame_touched;
    UI_Box *last, *prev;  // hit test and rendering only

    // -- cold: parameters given by the caller ---------------------------
    String8 display_string;  // must stay alive until ui_end
    UI_Layer layer;
    f32 corner_radius;
    f32 border_thickness;
    f32 text_padding;
    u32 bg_color, border_color, text_color;
    OsFont font;

    // -- cold: retained across frames -----------------------------------
    V2 view_off_target;
    f32 hot_t, active_t, focus_t;  // exponentially smoothed, 0..1
};

typedef struct UI_Signal {
    UI_Box *box;
    V2 mouse;       // relative to the top left corner of the box
    V2 drag_delta;  // since the press
    V2 scroll;      // wheel, in lines
    b32 hovering;
    b32 pressed;
    b32 released;
    b32 clicked;
    b32 double_clicked;
    b32 right_clicked;
    b32 dragging;
    b32 scrolled;
    b32 key_pressed;  // Space or Enter while focused
    u32 key;          // OsKey routed to the focus this frame, 0 if none
    u32 modifiers;
} UI_Signal;

// A for that runs its body once and guarantees the pop. Rule: never break,
// return or goto out of a UI_* block (research/03 s3.3).
#define DeferLoopName_(a, b) a##b
#define DeferLoopName(a, b) DeferLoopName_(a, b)
#define DeferLoopVar(begin, end, var) for (i32 var = ((begin), 0); !(var); (var) += 1, (end))
// __COUNTER__ and not __LINE__: two blocks on the same line must not collide.
#define DeferLoop(begin, end) \
    DeferLoopVar(begin, end, DeferLoopName(defer_i_, __COUNTER__))

// --- style stacks ----------------------------------------------------------
// One list, three generated declarations each: push, pop, top.
#define UI_STACK_LIST                \
    X(parent, UI_Box *)              \
    X(seed, UI_Key)                  \
    X(flags, UI_Flags)               \
    X(pref_width, UI_Size)           \
    X(pref_height, UI_Size)          \
    X(child_layout_axis, Axis2)      \
    X(bg_color, u32)                 \
    X(border_color, u32)             \
    X(text_color, u32)               \
    X(corner_radius, f32)            \
    X(border_thickness, f32)         \
    X(text_padding, f32)             \
    X(font, OsFont)                  \
    X(fixed_x, f32)                  \
    X(fixed_y, f32)

#define X(name, type)      \
    void ui_push_##name(type value); \
    type ui_pop_##name(void);        \
    type ui_top_##name(void);
UI_STACK_LIST
#undef X

#define UI_Parent(v)          DeferLoop(ui_push_parent(v), ui_pop_parent())
#define UI_Seed(v)            DeferLoop(ui_push_seed(v), ui_pop_seed())
#define UI_FlagsScope(v)      DeferLoop(ui_push_flags(v), ui_pop_flags())
#define UI_PrefWidth(v)       DeferLoop(ui_push_pref_width(v), ui_pop_pref_width())
#define UI_PrefHeight(v)      DeferLoop(ui_push_pref_height(v), ui_pop_pref_height())
#define UI_ChildLayoutAxis(v) DeferLoop(ui_push_child_layout_axis(v), ui_pop_child_layout_axis())
#define UI_BgColor(v)         DeferLoop(ui_push_bg_color(v), ui_pop_bg_color())
#define UI_BorderColor(v)     DeferLoop(ui_push_border_color(v), ui_pop_border_color())
#define UI_TextColor(v)       DeferLoop(ui_push_text_color(v), ui_pop_text_color())
#define UI_CornerRadius(v)    DeferLoop(ui_push_corner_radius(v), ui_pop_corner_radius())
#define UI_BorderThickness(v) DeferLoop(ui_push_border_thickness(v), ui_pop_border_thickness())
#define UI_TextPadding(v)     DeferLoop(ui_push_text_padding(v), ui_pop_text_padding())
#define UI_Font(v)            DeferLoop(ui_push_font(v), ui_pop_font())
#define UI_FixedX(v)          DeferLoop(ui_push_fixed_x(v), ui_pop_fixed_x())
#define UI_FixedY(v)          DeferLoop(ui_push_fixed_y(v), ui_pop_fixed_y())
#define UI_LayerScope(v)      DeferLoop(ui_push_layer(v), ui_pop_layer())

// --- animation -------------------------------------------------------------
// x += (target - x) * (1 - 2^(-rate * dt)). 30 converges in about 120 ms.
#define UI_ANIM_RATE_FAST 30.0f
#define UI_ANIM_RATE_SLOW 14.0f
#define UI_ANIM_EPSILON   0.001f
#define UI_DT_MAX         0.1f  // a suspended app resumes, it does not jump

// --- frame -----------------------------------------------------------------
void ui_init(Arena *permanent);
void ui_begin(Arena *frame_arena, const OsEvent *events, u64 event_count, f32 dt, V2 viewport,
              f32 dpi_scale);
void ui_end(void);  // 5 layout passes, animations, then rendering through r_*
b32  ui_animating(void);  // an animation has not converged: keep pumping at 16 ms

// --- construction ----------------------------------------------------------
// "Label##id" : "Label" is drawn, the whole string identifies. "Label###id" :
// only "id" identifies, so the label can change without losing the state.
UI_Key  ui_key_from_string(UI_Key seed, String8 string);
UI_Box *ui_build_box(UI_Flags flags, String8 string);
UI_Box *ui_build_box_from_key(UI_Flags flags, UI_Key key);
void    ui_push_layer(UI_Layer layer);  // pushes that layer's floating root
void    ui_pop_layer(void);

UI_Signal ui_signal(UI_Box *box);

UI_Box *ui_root(UI_Layer layer);
UI_Box *ui_box_from_key(UI_Key key);  // 0 when absent
u64     ui_box_count(void);           // live boxes in the table, tests
u64     ui_frame_index(void);
UI_Key  ui_hot_key(void);
UI_Key  ui_active_key(void);
UI_Key  ui_focus_key(void);
void    ui_set_focus(UI_Key key, b32 via_keyboard);
V2      ui_mouse(void);
Arena  *ui_frame_arena(void);
// One exponential step towards `target`, registered with the animation counter
// so the caller's own animations also keep ui_animating() true.
f32     ui_animate(f32 current, f32 target, f32 rate);

// Exposed for the bench: the five passes on one root, without rendering.
void ui_layout(UI_Box *root);

// --- two widgets, just enough for the demo ---------------------------------
UI_Box   *ui_label(String8 string);
UI_Box   *ui_labelf(const char *fmt, ...);
UI_Signal ui_button(String8 string);
UI_Box   *ui_spacer(UI_Size size);

#endif // UI_CORE_H

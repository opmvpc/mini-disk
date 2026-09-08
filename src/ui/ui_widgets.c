// ui_widgets.c - buttons, labels, a text field, a virtualized list, a splitter,
// tooltips and context menus. Everything is boxes: no widget draws behind
// ui_core's back, so clipping, layers, focus and animation are had for free.
//
// Widget state is addressed by the *pointer* the caller owns: a UI_TextInput
// parked in an arena has one stable address for the life of the app, which is
// exactly the identity a retained-mode key needs.

#include "ui_widgets.h"

#include "ui_text.h"

#define UI_WIDGET_KEY(salt, pointer) hash64_combine((salt), (u64)(pointer))

global OsCursor ui_widgets_cursor;
global b32 ui_widgets_popup_active;

b32 ui_popup_active(void) { return ui_widgets_popup_active; }
void ui_popup_set_active(b32 active) { ui_widgets_popup_active = active; }

void ui_cursor_request(OsCursor cursor) { ui_widgets_cursor = cursor; }

void ui_widgets_end_frame(void) {
    os_cursor_set(ui_widgets_cursor);
    ui_widgets_cursor = OsCursor_Arrow;
}

// --- basics ----------------------------------------------------------------
// A label is as wide as its text and never gives that width away. Its height
// follows the parent: inside a row it fills it, so the text sits centred; in a
// column it is one standard control high.
UI_Box *ui_label(String8 string) {
    UI_Box *box = 0;
    Axis2 axis = ui_top_parent()->child_layout_axis;
    UI_PrefWidth(ui_text_size(ui_top_text_padding(), 1.0f))
    UI_PrefHeight(axis == Axis2_X ? ui_pct(1.0f, 1.0f)
                                  : ui_px(ui_dp(ui_theme()->row_standard), 1.0f)) {
        box = ui_build_box(UI_DrawText, string);
    }
    return box;
}

UI_Box *ui_labelf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    String8 text = str8fv(ui_frame_arena(), fmt, args);
    va_end(args);
    return ui_label(text);
}

UI_Box *ui_label_styled(UI_FontStyle style, u32 color, String8 string) {
    UI_Box *box = 0;
    UI_Font(ui_font(style)) UI_TextColor(color) { box = ui_label(string); }
    return box;
}

static UI_Signal ui_button_common(String8 string, R_Icon icon, b32 has_icon, b32 icon_only,
                                  b32 primary) {
    const UI_Theme *theme = ui_theme();
    UI_Box *box = 0;
    f32 padding = ui_dp(theme->space[UI_Space_12]);
    // T-075, root cause 1: a button is control_h tall, never the height of the
    // row it sits in - a row that is exactly its button has no room to put any
    // space between itself and the next one.
    UI_PrefWidth(icon_only ? ui_px(ui_dp(theme->control_h), 1.0f)
                           : ui_text_size(padding, 1.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->control_h), 1.0f))
    UI_BgColor(primary ? theme->accent : theme->control)
    UI_BorderColor(primary ? theme->accent : theme->border_control)
    UI_TextColor(primary ? theme->accent_fg : theme->fg_primary)
    UI_CornerRadius(ui_dp(theme->radius))
    UI_BorderThickness(ui_dp(theme->border))
    UI_TextPadding(padding) {
        box = ui_build_box(UI_Clickable | UI_Focusable | UI_DrawBackground | UI_DrawBorder |
                               UI_DrawText,
                           string);
        if (has_icon) {
            box->flags |= UI_DrawIcon;
            box->icon = (u16)(icon + 1);
        }
    }
    UI_Signal signal = ui_signal(box);
    if (signal.hovering) {
        ui_cursor_request(OsCursor_Hand);
        box->border_color = theme->border_hover;
    }
    signal.clicked = signal.clicked || signal.key_pressed;
    return signal;
}

UI_Signal ui_button(String8 string) { return ui_button_common(string, 0, 0, 0, 0); }

UI_Signal ui_button_primary(String8 string) { return ui_button_common(string, 0, 0, 0, 1); }

UI_Signal ui_button_icon(R_Icon icon, String8 id) {
    return ui_button_common(id, icon, 1, 1, 0);
}

// The axis that matters is the one the *parent* lays its children out on, not
// whatever the style stack happens to carry.
void ui_separator(void) {
    const UI_Theme *theme = ui_theme();
    Axis2 axis = ui_top_parent()->child_layout_axis;
    UI_Size thin = ui_px(ui_dp(theme->border), 1.0f);
    UI_PrefWidth(axis == Axis2_X ? thin : ui_pct(1.0f, 0.0f))
    UI_PrefHeight(axis == Axis2_X ? ui_pct(1.0f, 0.0f) : thin)
    UI_BgColor(theme->border_subtle) { ui_build_box_from_key(UI_DrawBackground, 0); }
}

UI_Box *ui_spacer(UI_Size size) {
    UI_Box *box = 0;
    Axis2 axis = ui_top_parent()->child_layout_axis;
    if (axis == Axis2_X) {
        UI_PrefWidth(size) { box = ui_build_box_from_key(UI_Spacer, 0); }
    } else {
        UI_PrefHeight(size) { box = ui_build_box_from_key(UI_Spacer, 0); }
    }
    return box;
}

// --- tooltip ---------------------------------------------------------------
// One tooltip at a time, so one global: the hovered box and how long it has
// been hovered. The delay is counted in ui_dt and kept alive by
// ui_request_animation, which is what makes the loop wake up 500 ms later
// without anybody owning a timer.
typedef struct UI_TooltipState {
    UI_Key key;
    f32 elapsed;
} UI_TooltipState;

global UI_TooltipState ui_tooltip_state;
// The strip at the bottom of the window a tooltip may not cover: the status bar
// (T-075, S1). Physical pixels, set once a frame by the application.
global f32 ui_tooltip_reserved_bottom;

void ui_tooltip_reserve_bottom(f32 pixels) { ui_tooltip_reserved_bottom = pixels; }

void ui_tooltip_box(UI_Box *box, String8 text) {
    const UI_Theme *theme = ui_theme();
    Assert(box->key != 0);  // a tooltip needs an identity to hang its delay on
    UI_Signal signal = ui_signal(box);
    if (!signal.hovering) {
        if (ui_tooltip_state.key == box->key) {
            ui_tooltip_state.key = 0;
            ui_tooltip_state.elapsed = 0.0f;
        }
        return;
    }
    if (ui_tooltip_state.key != box->key) {
        ui_tooltip_state.key = box->key;
        ui_tooltip_state.elapsed = 0.0f;
    }
    if (ui_tooltip_state.elapsed < UI_TOOLTIP_DELAY_S) {
        ui_tooltip_state.elapsed += ui_dt();
        ui_request_animation();
        return;
    }

    OsFont font = ui_font(UI_FontStyle_Caption);
    f32 padding = ui_dp(theme->space[UI_Space_8]);
    f32 width = ui_text_width(font, text, 0) + 2.0f * padding;
    f32 height = ui_dp(theme->row_standard);
    V2 mouse = ui_mouse();
    V2 viewport = ui_viewport();
    f32 margin = ui_dp(theme->space[UI_Space_12]);
    f32 gap = ui_dp(theme->space[UI_Space_8]);
    width = min_f32(width, max_f32(viewport.x - 2.0f * margin, 1.0f));
    f32 x = min_f32(mouse.x + ui_dp(theme->space[UI_Space_16]), viewport.x - width - margin);
    // S1: the bubble hangs on its anchor, not on the pointer, and it flips above
    // it as soon as it would land on the status bar - the strip the caller has
    // reserved at the bottom of the window.
    f32 limit = viewport.y - ui_tooltip_reserved_bottom - margin;
    f32 y = box->rect.max.y + gap;
    if (y + height > limit) { y = box->rect.min.y - height - gap; }

    UI_LayerScope(UI_Layer_Tooltip)
    UI_FixedX(max_f32(x, 0.0f)) UI_FixedY(max_f32(y, 0.0f))
    UI_PrefWidth(ui_px(width, 1.0f)) UI_PrefHeight(ui_px(height, 1.0f))
    UI_BgColor(theme->control) UI_BorderColor(theme->border_subtle)
    UI_TextColor(theme->fg_primary) UI_CornerRadius(ui_dp(theme->radius))
    UI_BorderThickness(ui_dp(theme->border)) UI_TextPadding(padding)
    UI_Font(font) {
        UI_Box *tip = ui_build_box_from_key(UI_FloatingX | UI_FloatingY | UI_DrawBackground |
                                                UI_DrawBorder | UI_DrawDropShadow | UI_DrawText,
                                            hash64_combine(0x700171Bull, box->key));
        tip->display_string = text;
    }
}

// --- UTF-8 navigation ------------------------------------------------------
md_inline b32 ui_utf8_is_continuation(u8 byte) { return (byte & 0xC0u) == 0x80u; }

u32 ui_text_offset_prev(String8 text, u32 offset) {
    Assert(offset <= text.size);
    if (offset == 0) { return 0; }
    u32 at = offset - 1;
    while (at > 0 && ui_utf8_is_continuation(text.str[at])) { at -= 1; }
    return at;
}

u32 ui_text_offset_next(String8 text, u32 offset) {
    Assert(offset <= text.size);
    if (offset >= text.size) { return (u32)text.size; }
    UnicodeDecode decode = utf8_decode(text.str + offset, text.size - offset);
    return offset + decode.advance;
}

// Word boundaries the way every text field does it: skip the separators, then
// the run of non separators. ASCII separators only, which is what a title has.
md_inline b32 ui_is_word_separator(u8 byte) {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '-' || byte == '_' ||
           byte == '/' || byte == '\\' || byte == '.' || byte == ',' || byte == ':' ||
           byte == ';' || byte == '(' || byte == ')' || byte == '[' || byte == ']';
}

u32 ui_text_offset_word_prev(String8 text, u32 offset) {
    u32 at = offset;
    while (at > 0) {
        u32 prev = ui_text_offset_prev(text, at);
        if (!ui_is_word_separator(text.str[prev])) { break; }
        at = prev;
    }
    while (at > 0) {
        u32 prev = ui_text_offset_prev(text, at);
        if (ui_is_word_separator(text.str[prev])) { break; }
        at = prev;
    }
    return at;
}

u32 ui_text_offset_word_next(String8 text, u32 offset) {
    u32 at = offset;
    while (at < text.size && !ui_is_word_separator(text.str[at])) {
        at = ui_text_offset_next(text, at);
    }
    while (at < text.size && ui_is_word_separator(text.str[at])) {
        at = ui_text_offset_next(text, at);
    }
    return at;
}

// --- text input, model -----------------------------------------------------
String8 ui_text_input_string(UI_TextInput *state) {
    return str8(state->edit.text, state->edit.size);
}

static void ui_text_history_snapshot(UI_TextInput *state) {
    if (state->coalesce && state->history_index > 0) {
        // A run of typed characters is one undo step, not one per key.
        mem_copy(&state->history[state->history_index], &state->edit, sizeof(UI_TextEdit));
        return;
    }
    if (state->history_index + 1 == UI_TEXT_INPUT_UNDO) {
        mem_move(&state->history[0], &state->history[1],
                 sizeof(UI_TextEdit) * (UI_TEXT_INPUT_UNDO - 1));
        state->history_index -= 1;
    }
    state->history_index += 1;
    state->history_count = state->history_index + 1;
    mem_copy(&state->history[state->history_index], &state->edit, sizeof(UI_TextEdit));
}

void ui_text_input_init(UI_TextInput *state, String8 initial) {
    StructZero(state);
    u32 size = (u32)Min(initial.size, (u64)UI_TEXT_INPUT_CAP);
    mem_copy(state->edit.text, initial.str, size);
    state->edit.size = size;
    state->edit.cursor = size;
    state->edit.mark = size;
    state->history_count = 1;
    state->history_index = 0;
    mem_copy(&state->history[0], &state->edit, sizeof(UI_TextEdit));
}

md_inline u32 ui_text_selection_min(UI_TextInput *state) {
    return Min(state->edit.cursor, state->edit.mark);
}
md_inline u32 ui_text_selection_max(UI_TextInput *state) {
    return Max(state->edit.cursor, state->edit.mark);
}

static b32 ui_text_erase(UI_TextInput *state, u32 from, u32 to) {
    Assert(from <= to && to <= state->edit.size);
    if (from == to) { return 0; }
    mem_move(state->edit.text + from, state->edit.text + to, state->edit.size - to);
    state->edit.size -= to - from;
    state->edit.cursor = from;
    state->edit.mark = from;
    return 1;
}

b32 ui_text_input_delete_selection(UI_TextInput *state) {
    b32 erased = ui_text_erase(state, ui_text_selection_min(state), ui_text_selection_max(state));
    if (erased) { ui_text_history_snapshot(state); }
    return erased;
}

b32 ui_text_input_insert(UI_TextInput *state, String8 text) {
    ui_text_erase(state, ui_text_selection_min(state), ui_text_selection_max(state));
    u32 room = UI_TEXT_INPUT_CAP - state->edit.size;
    u32 size = (u32)Min(text.size, (u64)room);
    // Never cut a codepoint in half when the field is nearly full.
    while (size > 0 && size < text.size && ui_utf8_is_continuation(text.str[size])) { size -= 1; }
    if (size == 0) { return 0; }
    u32 at = state->edit.cursor;
    mem_move(state->edit.text + at + size, state->edit.text + at, state->edit.size - at);
    mem_copy(state->edit.text + at, text.str, size);
    state->edit.size += size;
    state->edit.cursor = at + size;
    state->edit.mark = state->edit.cursor;
    ui_text_history_snapshot(state);
    return 1;
}

void ui_text_input_set_cursor(UI_TextInput *state, u32 offset, b32 select) {
    Assert(offset <= state->edit.size);
    state->edit.cursor = offset;
    if (!select) { state->edit.mark = offset; }
    state->coalesce = 0;
}

void ui_text_input_move(UI_TextInput *state, i32 direction, b32 by_word, b32 select) {
    String8 text = ui_text_input_string(state);
    u32 at = state->edit.cursor;
    // A plain arrow with a live selection collapses it instead of moving.
    if (!select && !by_word && state->edit.cursor != state->edit.mark) {
        at = (direction < 0) ? ui_text_selection_min(state) : ui_text_selection_max(state);
        ui_text_input_set_cursor(state, at, 0);
        return;
    }
    if (direction < 0) {
        at = by_word ? ui_text_offset_word_prev(text, at) : ui_text_offset_prev(text, at);
    } else {
        at = by_word ? ui_text_offset_word_next(text, at) : ui_text_offset_next(text, at);
    }
    ui_text_input_set_cursor(state, at, select);
}

void ui_text_input_delete(UI_TextInput *state, i32 direction, b32 by_word) {
    state->coalesce = 0;
    if (state->edit.cursor != state->edit.mark) {
        ui_text_input_delete_selection(state);
        return;
    }
    String8 text = ui_text_input_string(state);
    u32 from = state->edit.cursor;
    u32 to = from;
    if (direction < 0) {
        from = by_word ? ui_text_offset_word_prev(text, from) : ui_text_offset_prev(text, from);
    } else {
        to = by_word ? ui_text_offset_word_next(text, to) : ui_text_offset_next(text, to);
    }
    if (ui_text_erase(state, from, to)) { ui_text_history_snapshot(state); }
}

void ui_text_input_select_all(UI_TextInput *state) {
    state->edit.mark = 0;
    state->edit.cursor = state->edit.size;
    state->coalesce = 0;
}

b32 ui_text_input_undo(UI_TextInput *state) {
    state->coalesce = 0;
    if (state->history_index == 0) { return 0; }
    state->history_index -= 1;
    mem_copy(&state->edit, &state->history[state->history_index], sizeof(UI_TextEdit));
    return 1;
}

b32 ui_text_input_redo(UI_TextInput *state) {
    state->coalesce = 0;
    if (state->history_index + 1 >= state->history_count) { return 0; }
    state->history_index += 1;
    mem_copy(&state->edit, &state->history[state->history_index], sizeof(UI_TextEdit));
    return 1;
}

// --- text input, widget ----------------------------------------------------
static f32 ui_text_prefix_width(OsFont font, UI_TextInput *state, u32 offset) {
    return ui_text_width(font, str8(state->edit.text, offset), 0);
}

// The byte offset the pen at `x` lands on, rounded to the nearest boundary.
static u32 ui_text_offset_from_x(OsFont font, UI_TextInput *state, f32 x) {
    String8 text = ui_text_input_string(state);
    u32 best = 0;
    f32 best_distance = abs_f32(x);
    for (u32 at = 0; at < text.size;) {
        at = ui_text_offset_next(text, at);
        f32 distance = abs_f32(ui_text_prefix_width(font, state, at) - x);
        if (distance < best_distance) {
            best_distance = distance;
            best = at;
        }
    }
    return best;
}

static void ui_text_input_keys(UI_TextInput *state) {
    if (ui_popup_active()) { return; }
    for (u32 i = 0; i < ui_key_event_count(); i += 1) {
        UI_KeyEvent event = ui_key_event(i);
        b32 ctrl = (event.modifiers & OsMod_Ctrl) != 0;
        b32 shift = (event.modifiers & OsMod_Shift) != 0;
        u32 before = state->edit.size;
        switch (event.key) {
            case OsKey_Left:  ui_text_input_move(state, -1, ctrl, shift); break;
            case OsKey_Right: ui_text_input_move(state, +1, ctrl, shift); break;
            case OsKey_Home:  ui_text_input_set_cursor(state, 0, shift); break;
            case OsKey_End:   ui_text_input_set_cursor(state, state->edit.size, shift); break;
            case OsKey_Backspace: ui_text_input_delete(state, -1, ctrl); break;
            case OsKey_Delete:    ui_text_input_delete(state, +1, ctrl); break;
            case OsKey_A: {
                if (ctrl) { ui_text_input_select_all(state); }
            } break;
            case OsKey_C:
            case OsKey_X: {
                if (!ctrl) { break; }
                u32 from = ui_text_selection_min(state);
                u32 to = ui_text_selection_max(state);
                if (from == to) { break; }
                os_clipboard_set(str8(state->edit.text + from, to - from));
                if (event.key == OsKey_X) { ui_text_input_delete_selection(state); }
            } break;
            case OsKey_V: {
                if (!ctrl) { break; }
                state->coalesce = 0;
                String8 pasted = os_clipboard_get(ui_frame_arena());
                // A pasted newline is a paste of one line, not a broken field.
                for (u64 at = 0; at < pasted.size; at += 1) {
                    if (pasted.str[at] == '\n' || pasted.str[at] == '\r') {
                        pasted.size = at;
                        break;
                    }
                }
                if (pasted.size != 0) { ui_text_input_insert(state, pasted); }
            } break;
            case OsKey_Z: {
                if (!ctrl) { break; }
                if (shift) { ui_text_input_redo(state); } else { ui_text_input_undo(state); }
            } break;
            case OsKey_Y: {
                if (ctrl) { ui_text_input_redo(state); }
            } break;
            default: break;
        }
        if (state->edit.size != before) { state->changed = 1; }
    }

    for (u32 i = 0; i < ui_char_event_count(); i += 1) {
        u32 codepoint = ui_char_event(i);
        if (codepoint < 0x20 || codepoint == 0x7F) { continue; }  // control keys
        u8 encoded[4];
        u32 size = utf8_encode(encoded, codepoint);
        if (ui_text_input_insert(state, str8(encoded, size))) {
            state->changed = 1;
            state->coalesce = 1;
        }
    }
}

UI_Signal ui_text_input(UI_TextInput *state, String8 placeholder) {
    const UI_Theme *theme = ui_theme();
    OsFont font = ui_font(UI_FontStyle_Ui);
    f32 padding = ui_dp(theme->space[UI_Space_8]);
    UI_Key key = UI_WIDGET_KEY(0x7E7101ull, state);
    UI_Box *previous = ui_box_from_key(key);
    f32 inner_width = previous ? max_f32(rect_width(previous->rect) - 2.0f * padding, 0.0f) : 0.0f;

    state->changed = 0;
    b32 focused = (ui_focus_key() == key);
    if (focused) { ui_text_input_keys(state); }

    UI_Box *box = 0;
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_BgColor(theme->control)
    UI_BorderColor(focused ? theme->accent : theme->border_control)
    UI_CornerRadius(ui_dp(theme->radius))
    UI_BorderThickness(ui_dp(theme->border))
    UI_TextPadding(padding)
    UI_Font(font) {
        box = ui_build_box_from_key(UI_Clickable | UI_Focusable | UI_DrawBackground |
                                        UI_DrawBorder | UI_Clip,
                                    key);
    }
    UI_Signal signal = ui_signal(box);
    if (signal.hovering) { ui_cursor_request(OsCursor_IBeam); }

    // Mouse: press puts the caret down, a drag extends the selection.
    if (signal.pressed) {
        f32 x = signal.mouse.x - padding + state->scroll_x;
        u32 at = ui_text_offset_from_x(font, state, x);
        ui_text_input_set_cursor(state, at, (signal.press_modifiers & OsMod_Shift) != 0);
        state->mouse_selecting = 1;
    } else if (state->mouse_selecting && signal.dragging) {
        f32 x = ui_mouse().x - box->rect.min.x - padding + state->scroll_x;
        ui_text_input_set_cursor(state, ui_text_offset_from_x(font, state, x), 1);
    } else if (!signal.dragging) {
        state->mouse_selecting = 0;
    }
    if (signal.double_clicked) { ui_text_input_select_all(state); }

    // Horizontal scrolling: the caret always stays inside the field.
    f32 caret_x = ui_text_prefix_width(font, state, state->edit.cursor);
    f32 total_width = ui_text_prefix_width(font, state, state->edit.size);
    if (caret_x - state->scroll_x > inner_width) { state->scroll_x = caret_x - inner_width; }
    if (caret_x < state->scroll_x) { state->scroll_x = caret_x; }
    state->scroll_x = clamp_f32(state->scroll_x, 0.0f, max_f32(total_width - inner_width, 0.0f));

    UI_Parent(box) {
        f32 origin = padding - state->scroll_x;
        if (focused && state->edit.cursor != state->edit.mark) {
            f32 from = ui_text_prefix_width(font, state, ui_text_selection_min(state));
            f32 to = ui_text_prefix_width(font, state, ui_text_selection_max(state));
            UI_FixedX(origin + from) UI_FixedY(ui_dp(theme->space[UI_Space_4]))
            UI_PrefWidth(ui_px(to - from, 1.0f))
            UI_PrefHeight(ui_px(ui_dp(theme->row_standard) - ui_dp(theme->space[UI_Space_8]), 1.0f))
            UI_BgColor(theme->selection_bg) UI_CornerRadius(0.0f) {
                ui_build_box_from_key(UI_FloatingX | UI_FloatingY | UI_DrawBackground, 0);
            }
        }

        b32 empty = (state->edit.size == 0);
        String8 shown = empty ? placeholder : ui_text_input_string(state);
        UI_FixedX(origin) UI_FixedY(0.0f)
        UI_PrefWidth(ui_px(max_f32(total_width, ui_text_width(font, shown, 0)) + ui_dp(4.0f), 1.0f))
        UI_PrefHeight(ui_pct(1.0f, 1.0f))
        UI_TextColor(empty ? theme->fg_muted : theme->fg_primary)
        UI_TextPadding(0.0f) {
            UI_Box *text_box = ui_build_box_from_key(UI_FloatingX | UI_FloatingY | UI_DrawText, 0);
            text_box->display_string = shown;
        }

        // The caret does not blink: a blink would keep the render loop awake
        // for as long as the field has the focus, and 0 % CPU at rest is a
        // requirement of this ticket. Solid, one physical pixel wide.
        if (focused) {
            UI_FixedX(round_f32(origin + caret_x))
            UI_FixedY(ui_dp(theme->space[UI_Space_4]))
            UI_PrefWidth(ui_px(max_f32(ui_dp(theme->caret_width), 1.0f), 1.0f))
            UI_PrefHeight(ui_px(ui_dp(theme->row_standard) - ui_dp(theme->space[UI_Space_8]), 1.0f))
            UI_BgColor(theme->fg_primary) UI_CornerRadius(0.0f) {
                ui_build_box_from_key(UI_FloatingX | UI_FloatingY | UI_DrawBackground, 0);
            }
        }
    }
    return signal;
}

// --- list, selection -------------------------------------------------------
void ui_list_init(UI_List *state, u64 *selection, u64 selection_words) {
    StructZero(state);
    state->selection = selection;
    state->selection_words = selection_words;
}

b32 ui_list_selected(UI_List *state, u64 index) {
    if (!state->selection) { return state->has_cursor && state->cursor == index; }
    Assert(index >> 6 < state->selection_words);
    return (state->selection[index >> 6] >> (index & 63)) & 1;
}

void ui_list_select_clear(UI_List *state) {
    if (!state->selection) { return; }
    mem_zero(state->selection, state->selection_words * sizeof(u64));
}

static void ui_list_select_set(UI_List *state, u64 index, b32 selected) {
    if (!state->selection) { return; }
    Assert(index >> 6 < state->selection_words);
    u64 bit = 1ull << (index & 63);
    if (selected) {
        state->selection[index >> 6] |= bit;
    } else {
        state->selection[index >> 6] &= ~bit;
    }
}

void ui_list_select_only(UI_List *state, u64 index) {
    ui_list_select_clear(state);
    ui_list_select_set(state, index, 1);
    state->cursor = index;
    state->anchor = index;
    state->has_cursor = 1;
}

void ui_list_select_toggle(UI_List *state, u64 index) {
    ui_list_select_set(state, index, !ui_list_selected(state, index));
    state->cursor = index;
    state->anchor = index;
    state->has_cursor = 1;
}

void ui_list_select_range(UI_List *state, u64 from, u64 to, b32 additive) {
    if (!additive) { ui_list_select_clear(state); }
    u64 low = Min(from, to);
    u64 high = Max(from, to);
    for (u64 i = low; i <= high; i += 1) { ui_list_select_set(state, i, 1); }
    state->cursor = to;
    state->has_cursor = 1;
}

void ui_list_select_all(UI_List *state) {
    if (!state->selection) { return; }
    for (u64 i = 0; i < state->row_count; i += 1) { ui_list_select_set(state, i, 1); }
    state->has_cursor = 1;
}

u64 ui_list_selected_count(UI_List *state) {
    if (!state->selection) { return state->has_cursor ? 1 : 0; }
    u64 count = 0;
    for (u64 i = 0; i < state->row_count; i += 1) { count += ui_list_selected(state, i) ? 1 : 0; }
    return count;
}

// --- list, geometry --------------------------------------------------------
UI_ListWindow ui_list_window(f32 scroll, f32 view_height, f32 row_height, u64 row_count) {
    UI_ListWindow window;
    window.first = 0;
    window.count = 0;
    if (row_count == 0 || row_height <= 0.0f || view_height <= 0.0f) { return window; }
    f32 max_scroll = max_f32((f32)row_count * row_height - view_height, 0.0f);
    f32 clamped = clamp_f32(scroll, 0.0f, max_scroll);
    i64 first = (i64)floor_f32(clamped / row_height) - UI_LIST_OVERSCAN;
    i64 last = (i64)ceil_f32((clamped + view_height) / row_height) + UI_LIST_OVERSCAN;
    if (first < 0) { first = 0; }
    if (last > (i64)row_count) { last = (i64)row_count; }
    if (last < first) { last = first; }
    window.first = (u64)first;
    window.count = (u64)(last - first);
    return window;
}

md_inline f32 ui_list_max_scroll(UI_List *state) {
    return max_f32((f32)state->row_count * state->row_height - state->view_height, 0.0f);
}

void ui_list_ensure_visible(UI_List *state, u64 index) {
    f32 top = (f32)index * state->row_height;
    f32 bottom = top + state->row_height;
    if (top < state->scroll) { state->scroll = top; }
    if (bottom > state->scroll + state->view_height) {
        state->scroll = bottom - state->view_height;
    }
    state->scroll = clamp_f32(state->scroll, 0.0f, ui_list_max_scroll(state));
}

static void ui_list_move_cursor(UI_List *state, i64 delta, b32 select, b32 absolute) {
    i64 target = absolute ? delta : (i64)state->cursor + delta;
    if (target < 0) { target = 0; }
    if (target >= (i64)state->row_count) { target = (i64)state->row_count - 1; }
    if (target < 0) { return; }
    u64 index = (u64)target;
    if (select) {
        ui_list_select_range(state, state->anchor, index, 0);
    } else {
        ui_list_select_only(state, index);
    }
    state->cursor = index;
    state->has_cursor = 1;
    ui_list_ensure_visible(state, index);
}

static void ui_list_keys(UI_List *state) {
    if (ui_popup_active()) { return; }
    i64 page = (i64)max_f32(floor_f32(state->view_height / state->row_height) - 1.0f, 1.0f);
    for (u32 i = 0; i < ui_key_event_count(); i += 1) {
        UI_KeyEvent event = ui_key_event(i);
        b32 shift = (event.modifiers & OsMod_Shift) != 0;
        b32 ctrl = (event.modifiers & OsMod_Ctrl) != 0;
        switch (event.key) {
            case OsKey_Up:       ui_list_move_cursor(state, -1, shift, 0); break;
            case OsKey_Down:     ui_list_move_cursor(state, +1, shift, 0); break;
            case OsKey_PageUp:   ui_list_move_cursor(state, -page, shift, 0); break;
            case OsKey_PageDown: ui_list_move_cursor(state, +page, shift, 0); break;
            case OsKey_Home:     ui_list_move_cursor(state, 0, shift, 1); break;
            case OsKey_End:      ui_list_move_cursor(state, (i64)state->row_count - 1, shift, 1); break;
            case OsKey_Space: {
                if (state->has_cursor) { ui_list_select_toggle(state, state->cursor); }
            } break;
            case OsKey_Enter: {
                if (state->has_cursor) {
                    state->activated = 1;
                    state->activated_row = state->cursor;
                }
            } break;
            case OsKey_A: {
                if (ctrl) { ui_list_select_all(state); }
            } break;
            case OsKey_Menu:
            case OsKey_F10: {
                // The context menu without a mouse, on the focused row.
                if (event.key == OsKey_F10 && !shift) { break; }
                state->context = 1;
                state->context_row = state->cursor;
                state->context_pos =
                    v2(state->viewport->rect.min.x + state->row_height,
                       state->viewport->rect.min.y +
                           (f32)state->cursor * state->row_height - state->scroll +
                           state->row_height);
            } break;
            default: break;
        }
    }
}

void ui_list_begin(UI_List *state, u64 row_count, f32 row_height) {
    const UI_Theme *theme = ui_theme();
    UI_Key key = UI_WIDGET_KEY(0x1157ull, state);
    UI_Box *previous = ui_box_from_key(key);

    state->box_mark = ui_frame_box_count();
    state->row_count = row_count;
    state->row_height = row_height;
    // The first frame has no rect to measure the viewport with, so it builds no
    // row at all; ask for one more frame instead of waiting for an event.
    if (!previous) { ui_request_animation(); }
    state->view_height = previous ? rect_height(previous->rect) : 0.0f;
    state->activated = 0;
    state->context = 0;
    if (state->cursor >= row_count) { state->cursor = row_count ? row_count - 1 : 0; }

    state->focused = (ui_focus_key() == key);
    if (state->focused) { ui_list_keys(state); }

    UI_Box *box = 0;
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_pct(1.0f, 0.0f))
    UI_ChildLayoutAxis(Axis2_Y)
    UI_BgColor(theme->surface)
    UI_CornerRadius(0.0f) {
        box = ui_build_box_from_key(UI_Clickable | UI_Focusable | UI_Scrollable |
                                        UI_DrawBackground | UI_Clip,
                                    key);
    }
    state->viewport = box;

    UI_Signal signal = ui_signal(box);
    if (signal.scrolled) {
        // The OS gives both; the pixel delta is the honest one on a precision
        // touchpad, the line count is what a notched wheel means.
        f32 delta = (signal.scroll_pixels.y != 0.0f)
                        ? -signal.scroll_pixels.y
                        : -signal.scroll.y * row_height;
        state->scroll = clamp_f32(state->scroll + delta, 0.0f, ui_list_max_scroll(state));
    }
    state->scroll = clamp_f32(state->scroll, 0.0f, ui_list_max_scroll(state));

    UI_ListWindow window =
        ui_list_window(state->scroll, state->view_height, row_height, row_count);
    state->first_visible = window.first;
    state->visible_count = window.count;
}

b32 ui_list_row_visible(UI_List *state, u64 index) {
    return index >= state->first_visible && index < state->first_visible + state->visible_count;
}

UI_Signal ui_list_row_begin(UI_List *state, u64 index) {
    const UI_Theme *theme = ui_theme();
    Assert(ui_list_row_visible(state, index));
    b32 selected = ui_list_selected(state, index);
    u32 background = selected ? (state->focused ? theme->row_selected
                                                : theme->row_selected_inactive)
                              : theme->surface;

    UI_Box *row = 0;
    ui_push_parent(state->viewport);
    ui_push_seed(hash64_combine(state->viewport->key, index + 1));
    UI_FixedY((f32)index * state->row_height - state->scroll)
    UI_PrefWidth(ui_pct(1.0f, 1.0f))
    UI_PrefHeight(ui_px(state->row_height, 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(background)
    UI_TextColor(theme->fg_primary)
    UI_CornerRadius(0.0f) {
        row = ui_build_box(UI_Clickable | UI_FloatingY | UI_DrawBackground, str8_lit("###row"));
    }
    ui_push_parent(row);

    UI_Signal signal = ui_signal(row);
    if (signal.pressed) {
        ui_set_focus(state->viewport->key, 0);
        state->focused = 1;
        b32 ctrl = (signal.press_modifiers & OsMod_Ctrl) != 0;
        b32 shift = (signal.press_modifiers & OsMod_Shift) != 0;
        if (shift && state->has_cursor) {
            ui_list_select_range(state, state->anchor, index, ctrl);
        } else if (ctrl) {
            ui_list_select_toggle(state, index);
        } else {
            ui_list_select_only(state, index);
        }
        state->cursor = index;
        state->has_cursor = 1;
    }
    if (signal.double_clicked) {
        state->activated = 1;
        state->activated_row = index;
    }
    if (signal.right_clicked) {
        if (!ui_list_selected(state, index)) { ui_list_select_only(state, index); }
        state->cursor = index;
        state->has_cursor = 1;
        state->context = 1;
        state->context_row = index;
        state->context_pos = ui_mouse();
    }
    // The keyboard cursor is not the selection: it gets an outline of its own.
    if (state->focused && state->has_cursor && state->cursor == index) {
        row->flags |= UI_DrawBorder;
        row->border_color = theme->accent;
        row->border_thickness = ui_dp(theme->border);
    }
    return signal;
}

void ui_list_row_end(UI_List *state) {
    Unused(state);
    ui_pop_parent();  // the row
    ui_pop_seed();
    ui_pop_parent();  // the viewport
}

void ui_list_end(UI_List *state) {
    const UI_Theme *theme = ui_theme();
    state->box_count = ui_frame_box_count() - state->box_mark;
    f32 content = (f32)state->row_count * state->row_height;
    if (content <= state->view_height || state->view_height <= 0.0f) { return; }

    UI_Key key = hash64_combine(state->viewport->key, 0x5C801Bull);
    f32 width = ui_dp(theme->scrollbar_width);
    f32 track = state->view_height;
    f32 thumb = max_f32(track * (state->view_height / content), ui_dp(theme->space[UI_Space_24]));
    f32 travel = track - thumb;
    f32 max_scroll = ui_list_max_scroll(state);
    f32 y = (max_scroll > 0.0f) ? travel * (state->scroll / max_scroll) : 0.0f;

    UI_Box *box = 0;
    UI_Parent(state->viewport)
    UI_FixedX(rect_width(state->viewport->rect) - width)
    UI_FixedY(y)
    UI_PrefWidth(ui_px(width, 1.0f))
    UI_PrefHeight(ui_px(thumb, 1.0f))
    UI_CornerRadius(width * 0.5f)
    UI_BgColor(theme->scrollbar_thumb) {
        box = ui_build_box_from_key(UI_Clickable | UI_FloatingX | UI_FloatingY | UI_DrawBackground,
                                    key);
    }
    UI_Signal signal = ui_signal(box);
    state->box_count += 1;
    if (signal.hovering || signal.dragging) { box->bg_color = theme->scrollbar_thumb_hover; }
    if (signal.pressed) { state->scrollbar_origin = state->scroll; }
    if (signal.dragging && travel > 0.0f) {
        state->scroll = clamp_f32(state->scrollbar_origin + signal.drag_delta.y * max_scroll / travel,
                                  0.0f, max_scroll);
    }
}

// --- splitter --------------------------------------------------------------
void ui_splitter_init(UI_Splitter *state, f32 size, f32 min_leading, f32 min_trailing) {
    StructZero(state);
    state->size = size;
    state->default_size = size;
    state->min_leading = min_leading;
    state->min_trailing = min_trailing;
}

f32 ui_splitter_update(UI_Splitter *state, Axis2 axis, f32 total) {
    const UI_Theme *theme = ui_theme();
    f32 handle = ui_dp(theme->splitter_size);
    if (total <= 0.0f) { return state->size; }  // first frame: nothing to clamp against
    f32 low = state->measures_trailing ? state->min_trailing : state->min_leading;
    f32 high = max_f32(total - handle - (state->measures_trailing ? state->min_leading
                                                                 : state->min_trailing),
                       low);
    UI_Box *previous = ui_box_from_key(UI_WIDGET_KEY(0x591170ull, state));
    if (previous) {
        UI_Signal signal = ui_signal(previous);
        if (signal.pressed) { state->drag_origin = state->size; }
        if (signal.dragging) {
            f32 delta = signal.drag_delta.v[axis];
            state->size = state->drag_origin + (state->measures_trailing ? -delta : delta);
        }
        if (signal.double_clicked) { state->size = state->default_size; }
    }
    state->size = clamp_f32(state->size, low, high);
    return state->size;
}

void ui_split_fit_middle(f32 total, f32 min_middle, f32 *leading, f32 min_leading, f32 *trailing,
                         f32 min_trailing) {
    f32 lead = *leading;
    f32 trail = *trailing;
    f32 over = lead + trail + min_middle - total;
    f32 give = min_f32(over, trail - min_trailing);
    if (give > 0.0f) {
        trail -= give;
        over -= give;
    }
    give = min_f32(over, lead - min_leading);
    if (give > 0.0f) { lead -= give; }
    *leading = lead;
    *trailing = trail;
}

void ui_splitter(UI_Splitter *state, Axis2 axis) {
    const UI_Theme *theme = ui_theme();
    f32 handle = ui_dp(theme->splitter_size);
    UI_Box *box = 0;
    UI_Size along = ui_px(handle, 1.0f);
    UI_Size across = ui_pct(1.0f, 0.0f);
    UI_PrefWidth(axis == Axis2_X ? along : across)
    UI_PrefHeight(axis == Axis2_X ? across : along)
    UI_BgColor(theme->canvas)
    UI_CornerRadius(0.0f) {
        box = ui_build_box_from_key(UI_Clickable | UI_DrawBackground,
                                    UI_WIDGET_KEY(0x591170ull, state));
    }
    UI_Signal signal = ui_signal(box);
    if (signal.hovering || signal.dragging) {
        ui_cursor_request(axis == Axis2_X ? OsCursor_ResizeH : OsCursor_ResizeV);
        // T-075 revue: the whole 6 dp handle painted in solid accent read as a
        // blue rule down the window. The feedback is a 2 dp line at 60 %, down
        // the middle of the handle and nowhere else.
        f32 thin = ui_dp(2.0f);
        f32 offset = (handle - thin) * 0.5f;
        u32 tint = r_rgba(ui_color_red(theme->accent), ui_color_green(theme->accent),
                          ui_color_blue(theme->accent), 153);
        UI_Parent(box)
        UI_PrefWidth(axis == Axis2_X ? ui_px(thin, 1.0f) : ui_pct(1.0f, 0.0f))
        UI_PrefHeight(axis == Axis2_X ? ui_pct(1.0f, 0.0f) : ui_px(thin, 1.0f))
        UI_FixedX(axis == Axis2_X ? offset : 0.0f)
        UI_FixedY(axis == Axis2_X ? 0.0f : offset)
        UI_BgColor(tint)
        UI_CornerRadius(0.0f) {
            ui_build_box_from_key(UI_FloatingX | UI_FloatingY | UI_DrawBackground,
                                  UI_WIDGET_KEY(0x591171ull, state));
        }
    }
}

// --- context menu ----------------------------------------------------------
// The flag lives from the open to the close, not from begin to end: the list
// that reads the keyboard is built long before the menu that owns it.
static void ui_context_menu_close(UI_ContextMenu *menu) {
    menu->open = 0;
    ui_widgets_popup_active = 0;
}

void ui_context_menu_open(UI_ContextMenu *menu, V2 pos, u64 payload) {
    menu->open = 1;
    ui_widgets_popup_active = 1;
    menu->pos = pos;
    menu->payload = payload;
    menu->hot_item = 0;
}

b32 ui_context_menu_begin(UI_ContextMenu *menu) {
    const UI_Theme *theme = ui_theme();
    UI_Key key = UI_WIDGET_KEY(0xC0117E7ull, menu);
    if (!menu->open) { return 0; }
    if (ui_escape_pressed()) {
        ui_context_menu_close(menu);
        return 0;
    }
    // Click outside: the press was resolved against last frame's rects, so
    // last frame's rect is exactly the one to test the mouse against.
    UI_Box *previous = ui_box_from_key(key);
    if (ui_mouse_pressed() && previous && !rect_contains(previous->rect, ui_mouse())) {
        ui_context_menu_close(menu);
        return 0;
    }

    // Keyboard: the arrows walk the items, Enter takes the hot one.
    menu->item_index = 0;
    menu->activate_item = -1;
    for (u32 i = 0; i < ui_key_event_count(); i += 1) {
        UI_KeyEvent event = ui_key_event(i);
        if (event.key == OsKey_Down && menu->last_count > 0) {
            menu->hot_item = (menu->hot_item + 1) % menu->last_count;
        } else if (event.key == OsKey_Up && menu->last_count > 0) {
            menu->hot_item = (menu->hot_item + menu->last_count - 1) % menu->last_count;
        } else if (event.key == OsKey_Enter || event.key == OsKey_Space) {
            menu->activate_item = (i32)menu->hot_item;
        }
    }

    f32 width = ui_dp(200.0f);
    V2 viewport = ui_viewport();
    f32 x = min_f32(menu->pos.x, max_f32(viewport.x - width, 0.0f));
    f32 y = min_f32(menu->pos.y, max_f32(viewport.y - ui_dp(120.0f), 0.0f));

    ui_push_layer(UI_Layer_Popup);
    UI_Box *box = 0;
    UI_FixedX(x) UI_FixedY(y)
    UI_PrefWidth(ui_px(width, 1.0f))
    UI_PrefHeight(ui_children_sum(1.0f))
    UI_ChildLayoutAxis(Axis2_Y)
    UI_BgColor(theme->panel)
    UI_BorderColor(theme->border_subtle)
    UI_BorderThickness(ui_dp(theme->border))
    UI_CornerRadius(ui_dp(theme->radius_popup)) {
        box = ui_build_box_from_key(UI_FloatingX | UI_FloatingY | UI_DrawBackground |
                                        UI_DrawBorder | UI_DrawDropShadow,
                                    key);
    }
    ui_push_parent(box);
    ui_push_pref_width(ui_pct(1.0f, 1.0f));
    ui_push_pref_height(ui_px(ui_dp(theme->row_standard), 1.0f));
    ui_push_bg_color(theme->panel);
    ui_push_text_color(theme->fg_primary);
    ui_push_text_padding(ui_dp(theme->space[UI_Space_12]));
    ui_push_corner_radius(0.0f);
    return 1;
}

b32 ui_context_menu_item(UI_ContextMenu *menu, String8 label) {
    UI_Box *box = 0;
    UI_Seed(hash64_combine(0x17E11ull, menu->item_index + 1)) {
        box = ui_build_box(UI_Clickable | UI_DrawBackground | UI_DrawText, label);
    }
    UI_Signal signal = ui_signal(box);
    b32 activated = (menu->activate_item == (i32)menu->item_index);
    if (signal.hovering) {
        menu->hot_item = menu->item_index;
        ui_cursor_request(OsCursor_Hand);
    }
    if (menu->hot_item == menu->item_index) { box->bg_color = ui_theme()->control_hover; }
    menu->item_index += 1;
    if (signal.clicked || activated) { ui_context_menu_close(menu); }
    return signal.clicked || activated;
}

void ui_context_menu_separator(UI_ContextMenu *menu) {
    const UI_Theme *theme = ui_theme();
    Unused(menu);
    UI_PrefHeight(ui_px(ui_dp(theme->border), 1.0f))
    UI_BgColor(theme->border_subtle) { ui_build_box_from_key(UI_DrawBackground, 0); }
}

void ui_context_menu_end(UI_ContextMenu *menu) {
    menu->last_count = menu->item_index;
    ui_pop_corner_radius();
    ui_pop_text_padding();
    ui_pop_text_color();
    ui_pop_bg_color();
    ui_pop_pref_height();
    ui_pop_pref_width();
    ui_pop_parent();
    ui_pop_layer();
}

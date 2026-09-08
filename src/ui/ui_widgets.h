// ui_widgets.h - the widget set of T-007, built on ui_core boxes and the tokens
// of ui_theme. Every piece of state that must survive a frame (a text field, a
// list, a splitter) is a struct the *caller* owns and parks in an arena; the
// widget finds its boxes again from the address of that struct, which is stable
// for as long as the arena lives.
//
// No OS header here either: the clipboard and the cursor go through platform.h.
#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "../base/base.h"
#include "r_icons.h"
#include "ui_core.h"
#include "ui_font.h"
#include "ui_theme.h"

// dp -> physical pixels. The theme is in dp, the boxes are in pixels, and the
// conversion happens here and nowhere else.
md_inline f32 ui_dp(f32 value) { return value * ui_dpi_scale(); }

// --- basics ----------------------------------------------------------------
UI_Box   *ui_label(String8 string);
UI_Box   *ui_labelf(const char *fmt, ...);
UI_Box   *ui_label_styled(UI_FontStyle style, u32 color, String8 string);
UI_Signal ui_button(String8 string);
UI_Signal ui_button_primary(String8 string);
// `id` identifies and is not drawn, so it is written "###zoom" (ui_core keys).
UI_Signal ui_button_icon(R_Icon icon, String8 id);
void      ui_separator(void);
UI_Box   *ui_spacer(UI_Size size);

// Shown after UI_TOOLTIP_DELAY_S of uninterrupted hover, on the tooltip layer.
// `text` must stay alive until ui_end: a literal, or the frame arena.
void ui_tooltip_box(UI_Box *box, String8 text);
md_inline void ui_tooltip(String8 text) { ui_tooltip_box(ui_last_box(), text); }
// The strip at the bottom of the window a tooltip flips above rather than cover
// (T-075, S1): the status bar, in physical pixels.
void ui_tooltip_reserve_bottom(f32 pixels);

// The cursor a widget asked for this frame; ui_widgets_end_frame hands it to
// the platform and goes back to the arrow. Called once, just before ui_end.
void ui_cursor_request(OsCursor cursor);
void ui_widgets_end_frame(void);

// --- text input ------------------------------------------------------------
// A search field, a track title: 256 bytes is four times the longest MD title.
// Fixed size on purpose - the struct is copied into the undo ring as is.
#define UI_TEXT_INPUT_CAP  256
#define UI_TEXT_INPUT_UNDO 16

typedef struct UI_TextEdit {
    u8 text[UI_TEXT_INPUT_CAP];
    u32 size;
    u32 cursor;  // byte offset, always on a codepoint boundary
    u32 mark;    // the other end of the selection, cursor == mark: no selection
} UI_TextEdit;

typedef struct UI_TextInput {
    UI_TextEdit edit;
    UI_TextEdit history[UI_TEXT_INPUT_UNDO];  // ring, history[index] == edit
    u32 history_count;
    u32 history_index;
    b32 coalesce;  // a run of typed characters is one undo step
    f32 scroll_x;
    b32 mouse_selecting;
    b32 changed;  // the text changed during the last ui_text_input call
} UI_TextInput;

void    ui_text_input_init(UI_TextInput *state, String8 initial);
String8 ui_text_input_string(UI_TextInput *state);
b32     ui_text_input_insert(UI_TextInput *state, String8 text);
b32     ui_text_input_delete_selection(UI_TextInput *state);
void    ui_text_input_delete(UI_TextInput *state, i32 direction, b32 by_word);
void    ui_text_input_move(UI_TextInput *state, i32 direction, b32 by_word, b32 select);
void    ui_text_input_set_cursor(UI_TextInput *state, u32 offset, b32 select);
void    ui_text_input_select_all(UI_TextInput *state);
b32     ui_text_input_undo(UI_TextInput *state);
b32     ui_text_input_redo(UI_TextInput *state);

// UTF-8 navigation: offsets always land on a codepoint boundary.
u32 ui_text_offset_prev(String8 text, u32 offset);
u32 ui_text_offset_next(String8 text, u32 offset);
u32 ui_text_offset_word_prev(String8 text, u32 offset);
u32 ui_text_offset_word_next(String8 text, u32 offset);

UI_Signal ui_text_input(UI_TextInput *state, String8 placeholder);

// --- virtualized list ------------------------------------------------------
// Only the visible rows become boxes: 100 000 rows cost the same as 30.
typedef struct UI_ListWindow {
    u64 first;
    u64 count;
} UI_ListWindow;

// One row above and one below, so a half scrolled row is never missing.
#define UI_LIST_OVERSCAN 1

typedef struct UI_List {
    // -- selection, owned by the caller ------------------------------------
    u64 *selection;       // bitset of row_count bits, 0 for a single selection list
    u64 selection_words;

    // -- retained ----------------------------------------------------------
    f32 scroll;   // pixels from the top of the content
    f32 scrollbar_origin;  // scroll when the thumb drag started
    u64 cursor;   // the focused row, moved by the arrows
    u64 anchor;   // the fixed end of a Shift range
    b32 has_cursor;

    // -- per frame, written by ui_list_begin -------------------------------
    u64 row_count;
    f32 row_height;
    f32 view_height;
    u64 first_visible;
    u64 visible_count;
    u64 box_count;  // boxes this list built, the virtualization budget
    u64 box_mark;
    UI_Box *viewport;
    b32 focused;
    b32 activated;  // Enter or a double click
    u64 activated_row;
    b32 context;    // a right click asks for the context menu
    u64 context_row;
    V2 context_pos;
} UI_List;

// `selection` may be 0: the list then keeps a cursor and nothing else.
void ui_list_init(UI_List *state, u64 *selection, u64 selection_words);
UI_ListWindow ui_list_window(f32 scroll, f32 view_height, f32 row_height, u64 row_count);

void ui_list_begin(UI_List *state, u64 row_count, f32 row_height);
b32  ui_list_row_visible(UI_List *state, u64 index);
UI_Signal ui_list_row_begin(UI_List *state, u64 index);
void ui_list_row_end(UI_List *state);
void ui_list_end(UI_List *state);

// Iterates exactly over the rows that ui_list_begin decided to build.
#define UI_ListEachRow(state, var)                                     \
    for (u64 var = (state)->first_visible;                             \
         (var) < (state)->first_visible + (state)->visible_count; (var) += 1)

void ui_list_ensure_visible(UI_List *state, u64 index);
b32  ui_list_selected(UI_List *state, u64 index);
void ui_list_select_only(UI_List *state, u64 index);
void ui_list_select_toggle(UI_List *state, u64 index);
void ui_list_select_range(UI_List *state, u64 from, u64 to, b32 additive);
void ui_list_select_all(UI_List *state);
void ui_list_select_clear(UI_List *state);
u64  ui_list_selected_count(UI_List *state);

// --- splitter --------------------------------------------------------------
typedef struct UI_Splitter {
    f32 size;          // physical pixels given to the pane before the handle
    f32 default_size;  // double click puts it back here
    f32 min_leading;
    f32 min_trailing;
    f32 drag_origin;
    // Set by the caller when `size` measures the pane *after* the handle: the
    // drag then counts the other way round.
    b32 measures_trailing;
} UI_Splitter;

void ui_splitter_init(UI_Splitter *state, f32 size, f32 min_leading, f32 min_trailing);
// Two calls, because a pane needs its width *before* the handle that follows it
// exists. The update reads the drag off the handle of the previous frame - which
// is the frame the press was resolved against anyway - and returns the clamped
// size; ui_splitter then builds the handle where the tree wants it.
f32  ui_splitter_update(UI_Splitter *state, Axis2 axis, f32 total);
void ui_splitter(UI_Splitter *state, Axis2 axis);

// Three zones stacked in one column - leading, middle, trailing - where only
// the middle one has no handle of its own and so no way to defend itself. It
// keeps `min_middle` first: the trailing zone gives way down to `min_trailing`,
// then the leading one down to `min_leading`. `leading` and `trailing` are read
// and written; when even the three minimums do not fit, both come back at their
// minimum and the middle takes what is left. Physical pixels (T-075 revue).
void ui_split_fit_middle(f32 total, f32 min_middle, f32 *leading, f32 min_leading, f32 *trailing,
                         f32 min_trailing);

// --- context menu ----------------------------------------------------------
typedef struct UI_ContextMenu {
    b32 open;
    V2 pos;
    u64 payload;  // whatever the caller was pointing at, a row index in the demo
    u32 item_index;
    u32 hot_item;      // moved by the arrows, activated by Enter
    i32 activate_item; // -1: nothing this frame
    u32 last_count;
} UI_ContextMenu;

// While a menu is open it owns the keyboard: the list and the fields below it
// stop reading key events, so Enter does not fire twice.
b32  ui_popup_active(void);
// An app level modal (the preferences panel of T-072) raises the same flag: it
// owns the keyboard exactly the way a menu does, and the lists below it must
// stop reading keys for the same reason.
void ui_popup_set_active(b32 active);
void ui_context_menu_open(UI_ContextMenu *menu, V2 pos, u64 payload);
b32  ui_context_menu_begin(UI_ContextMenu *menu);
b32  ui_context_menu_item(UI_ContextMenu *menu, String8 label);
void ui_context_menu_separator(UI_ContextMenu *menu);
void ui_context_menu_end(UI_ContextMenu *menu);

#endif // UI_WIDGETS_H

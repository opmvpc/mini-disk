// ui_theme.h - the design tokens of research/02b, in one struct (ADR-011 D8).
// Colours are premultiplied RGBA8, ready for r_*; every metric is in dp, so a
// widget multiplies it by the DPI scale exactly once, at the point of use.
#ifndef UI_THEME_H
#define UI_THEME_H

#include "../base/base.h"
#include "../platform/platform.h"
#include "r_core.h"

// The four MiniDisc recording modes carry a colour of their own, and only ever
// that colour: the capacity gauge and the mode badges read them from here.
typedef enum UI_Mode {
    UI_Mode_SP = 0,
    UI_Mode_Mono,
    UI_Mode_LP2,
    UI_Mode_LP4,
    UI_Mode_COUNT
} UI_Mode;

// The spacing rhythm, 2/4/6/8/12/16/24/32. Indexed, never spelled out.
typedef enum UI_Space {
    UI_Space_2 = 0,
    UI_Space_4,
    UI_Space_6,
    UI_Space_8,
    UI_Space_12,
    UI_Space_16,
    UI_Space_24,
    UI_Space_32,
    UI_Space_COUNT
} UI_Space;

typedef struct UI_Theme {
    b32 dark;

    // -- surfaces -----------------------------------------------------------
    u32 canvas;         // window background, gutter between panels
    u32 panel;          // browser, inspector, device bar
    u32 surface;        // library list, plan list
    u32 control;        // fields, secondary buttons
    u32 control_hover;
    u32 control_pressed;
    u32 row_hover;
    u32 row_selected;           // the panel has the focus
    u32 row_selected_inactive;  // it does not

    // -- lines --------------------------------------------------------------
    u32 border_subtle;
    u32 border_control;
    u32 border_hover;

    // -- text ---------------------------------------------------------------
    u32 fg_primary;
    u32 fg_secondary;
    u32 fg_muted;
    u32 fg_disabled;

    // -- meaning ------------------------------------------------------------
    u32 accent;
    u32 accent_hover;
    u32 accent_fg;
    u32 success;
    u32 warning;
    u32 danger;
    u32 mode[UI_Mode_COUNT];

    // -- overlays -----------------------------------------------------------
    u32 focus_ring;   // 1 px, accent
    u32 focus_halo;   // the same accent at low alpha, one pixel further out
    u32 shadow;
    u32 selection_bg;  // text selection inside a field
    u32 scrollbar_thumb;
    u32 scrollbar_thumb_hover;

    // -- metrics, dp --------------------------------------------------------
    f32 space[UI_Space_COUNT];
    f32 row_compact;      // 22, list rows
    f32 row_standard;     // 28, controls
    f32 row_comfortable;  // 40, headers and toolbars
    // T-075. A button is control_h tall, a row of buttons row_control: the two
    // space_4 above and below are what keeps two consecutive rows from sharing
    // a border.
    f32 control_h;        // 28, a button
    f32 row_control;      // 36, a row of buttons
    f32 radius;           // 4
    f32 radius_popup;     // 6
    f32 border;           // 1
    f32 focus_ring_width; // 1
    f32 scrollbar_width;
    f32 scrollbar_width_hover;
    f32 splitter_size;    // 6, the grab handle
    f32 icon_size;        // 16
    f32 caret_width;
} UI_Theme;

// Hover, then 500 ms, then the tooltip. The delay is spent in the animation
// path of ui_core, never in a timer.
#define UI_TOOLTIP_DELAY_S 0.5f

// What the preferences store and what the settings panel offers (T-072). The
// system entry is resolved against os_system_theme() every time it is applied,
// which is what makes WM_SETTINGCHANGE a one line handler.
typedef enum UI_ThemeChoice {
    UI_ThemeChoice_Dark = 0,
    UI_ThemeChoice_Light,
    UI_ThemeChoice_System,
    UI_ThemeChoice_COUNT
} UI_ThemeChoice;

// Out parameters and not a return value: a 200 byte struct returned by value
// becomes a memcpy under /GL, which has no symbol to bind to without the CRT.
void ui_theme_dark(UI_Theme *out);
void ui_theme_light(UI_Theme *out);

void            ui_theme_set(const UI_Theme *theme);
const UI_Theme *ui_theme(void);

// Resolves the choice, installs the theme, and answers whether the result is
// the dark one - which is exactly what the title bar wants to know. Nothing is
// cached anywhere else: a box takes its colours during the frame it is built,
// so switching a theme is one call and one redraw (T-072).
b32 ui_theme_apply(UI_ThemeChoice choice);
// The choice in force and the theme it resolved to, for the settings panel.
UI_ThemeChoice ui_theme_choice(void);

// --- contrast ---------------------------------------------------------------
// The straight (non premultiplied) components of a theme colour. Every token is
// opaque, so this is a shift; it exists so a test can measure a token without
// knowing how r_rgba packs one (research/02 s10.9).
md_inline u8 ui_color_red(u32 color) { return (u8)(color & 0xFFu); }
md_inline u8 ui_color_green(u32 color) { return (u8)((color >> 8) & 0xFFu); }
md_inline u8 ui_color_blue(u32 color) { return (u8)((color >> 16) & 0xFFu); }
md_inline u8 ui_color_alpha(u32 color) { return (u8)((color >> 24) & 0xFFu); }

#endif // UI_THEME_H

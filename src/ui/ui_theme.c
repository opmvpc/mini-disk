// ui_theme.c - the tokens of research/02b, written once. Nothing here computes:
// a token is a value someone chose, and a value nobody chose is not a token.

#include "ui_theme.h"

global UI_Theme ui_theme_current;

static void ui_theme_metrics(UI_Theme *theme) {
    theme->space[UI_Space_2] = 2.0f;
    theme->space[UI_Space_4] = 4.0f;
    theme->space[UI_Space_6] = 6.0f;
    theme->space[UI_Space_8] = 8.0f;
    theme->space[UI_Space_12] = 12.0f;
    theme->space[UI_Space_16] = 16.0f;
    theme->space[UI_Space_24] = 24.0f;
    theme->space[UI_Space_32] = 32.0f;
    theme->row_compact = 22.0f;
    theme->row_standard = 28.0f;
    theme->row_comfortable = 40.0f;
    theme->radius = 4.0f;
    theme->radius_popup = 6.0f;
    theme->border = 1.0f;
    theme->focus_ring_width = 1.0f;
    theme->scrollbar_width = 8.0f;
    theme->scrollbar_width_hover = 12.0f;
    theme->splitter_size = 6.0f;
    theme->icon_size = 16.0f;
    theme->caret_width = 1.0f;
}

void ui_theme_dark(UI_Theme *out) {
    UI_Theme theme;
    StructZero(&theme);
    theme.dark = 1;

    theme.canvas = r_rgb(0x111113);
    theme.panel = r_rgb(0x18191B);
    theme.surface = r_rgb(0x1F1F1F);
    theme.control = r_rgb(0x212225);
    theme.control_hover = r_rgb(0x272A2D);
    theme.control_pressed = r_rgb(0x2E3135);
    theme.row_hover = r_rgb(0x2A2D2E);
    theme.row_selected = r_rgb(0x04395E);
    theme.row_selected_inactive = r_rgb(0x37373D);

    theme.border_subtle = r_rgb(0x2B2B2B);
    theme.border_control = r_rgb(0x3C3C3C);
    theme.border_hover = r_rgb(0x5A6169);

    theme.fg_primary = r_rgb(0xEDEEF0);
    theme.fg_secondary = r_rgb(0xB0B4BA);
    theme.fg_muted = r_rgb(0x777B84);
    theme.fg_disabled = r_rgb(0x696E77);

    theme.accent = r_rgb(0x0090FF);
    theme.accent_hover = r_rgb(0x3B9EFF);
    theme.accent_fg = r_rgb(0xFFFFFF);
    theme.success = r_rgb(0x3FB950);
    theme.warning = r_rgb(0xD29922);
    theme.danger = r_rgb(0xF85149);

    theme.mode[UI_Mode_SP] = r_rgb(0x0090FF);
    theme.mode[UI_Mode_Mono] = r_rgb(0xBE95FF);
    theme.mode[UI_Mode_LP2] = r_rgb(0x3FB950);
    theme.mode[UI_Mode_LP4] = r_rgb(0xD29922);

    theme.focus_ring = r_rgb(0x0090FF);
    theme.focus_halo = r_rgba(0x00, 0x90, 0xFF, 60);
    theme.shadow = r_rgba(0, 0, 0, 115);  // 0 8px 24px rgba(0,0,0,.45)
    theme.selection_bg = r_rgba(0x00, 0x90, 0xFF, 90);
    theme.scrollbar_thumb = r_rgba(0xFF, 0xFF, 0xFF, 40);
    theme.scrollbar_thumb_hover = r_rgba(0xFF, 0xFF, 0xFF, 80);

    ui_theme_metrics(&theme);
    mem_copy(out, &theme, sizeof(theme));
}

// Sketch of research/02 s10.4: enough to prove nothing in the widgets reads a
// hardcoded colour. The full light theme is phase 7.
void ui_theme_light(UI_Theme *out) {
    UI_Theme theme;
    StructZero(&theme);
    theme.dark = 0;

    theme.canvas = r_rgb(0xF8F8F9);
    theme.panel = r_rgb(0xF1F1F3);
    theme.surface = r_rgb(0xFFFFFF);
    theme.control = r_rgb(0xF1F1F3);
    theme.control_hover = r_rgb(0xE8E8EA);
    theme.control_pressed = r_rgb(0xDCDCE0);
    theme.row_hover = r_rgb(0xE8E8EA);
    theme.row_selected = r_rgb(0xCCE3FA);
    theme.row_selected_inactive = r_rgb(0xE4E4E7);

    theme.border_subtle = r_rgb(0xE4E4E7);
    theme.border_control = r_rgb(0xC9CACE);
    theme.border_hover = r_rgb(0x8B8D94);

    theme.fg_primary = r_rgb(0x1C1C1E);
    theme.fg_secondary = r_rgb(0x5A5C63);
    theme.fg_muted = r_rgb(0x8B8D94);
    theme.fg_disabled = r_rgb(0xA8AAB0);

    theme.accent = r_rgb(0x0069C2);
    theme.accent_hover = r_rgb(0x0078D4);
    theme.accent_fg = r_rgb(0xFFFFFF);
    theme.success = r_rgb(0x1A7F37);
    theme.warning = r_rgb(0x9A6700);
    theme.danger = r_rgb(0xCF222E);

    theme.mode[UI_Mode_SP] = r_rgb(0x0069C2);
    theme.mode[UI_Mode_Mono] = r_rgb(0x8250DF);
    theme.mode[UI_Mode_LP2] = r_rgb(0x1A7F37);
    theme.mode[UI_Mode_LP4] = r_rgb(0x9A6700);

    theme.focus_ring = r_rgb(0x0069C2);
    theme.focus_halo = r_rgba(0x00, 0x69, 0xC2, 60);
    theme.shadow = r_rgba(0, 0, 0, 60);
    theme.selection_bg = r_rgba(0x00, 0x69, 0xC2, 70);
    theme.scrollbar_thumb = r_rgba(0, 0, 0, 45);
    theme.scrollbar_thumb_hover = r_rgba(0, 0, 0, 90);

    ui_theme_metrics(&theme);
    mem_copy(out, &theme, sizeof(theme));
}

void ui_theme_set(const UI_Theme *theme) {
    mem_copy(&ui_theme_current, theme, sizeof(ui_theme_current));
}

const UI_Theme *ui_theme(void) {
    Assert(ui_theme_current.row_compact != 0.0f);  // ui_theme_set was called
    return &ui_theme_current;
}

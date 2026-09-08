// ui_font.c - the four styles of the theme, resolved against whatever the
// machine actually has installed. No font ships with the exe (ADR-006).
#include "ui_font.h"

typedef struct UI_FontSpec {
    f32 size_dp;
    u32 weight;
    b32 italic;
} UI_FontSpec;

global const UI_FontSpec ui_font_specs[UI_FontStyle_COUNT] = {
        {13.0f, 400, 0},  // Ui
        {12.0f, 400, 0},  // Caption
        {14.0f, 600, 0},  // Emphasis
        {16.0f, 600, 0},  // Heading
        {13.0f, 400, 1},  // Italic
};

global OsFont ui_font_table[UI_FontStyle_COUNT];

// Windows 11 has Segoe UI Variable, Windows 10 does not, and a stripped
// machine may have neither: the first family that opens wins. This is a
// boundary (what the OS has), so it is a lookup, not a defensive check.
static OsFont ui_font_open_first(f32 size_px, u32 weight, b32 italic) {
    static const char *families[] = {"Segoe UI Variable Text", "Segoe UI", "Tahoma", "Arial"};
    for (u32 i = 0; i < ArrayCount(families); i += 1) {
        OsFont font = os_font_open(str8_cstr(families[i]), size_px, weight, italic);
        if (font.v != 0) { return font; }
    }
    OsFont none;
    none.v = 0;
    return none;
}

b32 ui_fonts_build(f32 dpi_scale) {
    os_font_close_all();
    for (u32 i = 0; i < UI_FontStyle_COUNT; i += 1) {
        f32 size_px = round_f32(ui_font_specs[i].size_dp * dpi_scale);
        ui_font_table[i] = ui_font_open_first(size_px, ui_font_specs[i].weight,
                                             ui_font_specs[i].italic);
        if (ui_font_table[i].v == 0) { return 0; }
    }
    return 1;
}

OsFont ui_font(UI_FontStyle style) {
    Assert(style < UI_FontStyle_COUNT);
    Assert(ui_font_table[style].v != 0);
    return ui_font_table[style];
}

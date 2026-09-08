// ui_font.h - the four UI text styles, opened once and rebuilt on a DPI change.
// Sizes are in dp; the physical size is rounded to a whole pixel, because
// rasterizing at 16.25 px means nothing and doubles the cache (research/03 s5.11).
#ifndef UI_FONT_H
#define UI_FONT_H

#include "../base/base.h"
#include "../platform/platform.h"

typedef enum UI_FontStyle {
    UI_FontStyle_Ui = 0,   // 13 dp, regular: the default
    UI_FontStyle_Caption,  // 12 dp, regular
    UI_FontStyle_Emphasis, // 14 dp, semibold
    UI_FontStyle_Heading,  // 16 dp, semibold
    // 13 dp, regular, italic. One thing only (research/02 s11.3): the words the
    // app supplies where the data has none - "(sans titre)", "(sans nom)" - so
    // that a glance tells them apart from a track somebody really called that.
    UI_FontStyle_Italic,
    UI_FontStyle_COUNT
} UI_FontStyle;

// Opens the four styles for `dpi_scale`. Called again on DpiChanged, after the
// atlas and the text caches have been reset. 0 when no system font is usable.
b32 ui_fonts_build(f32 dpi_scale);

OsFont ui_font(UI_FontStyle style);

#endif // UI_FONT_H

// ui_text.h - glyph cache, measurement cache, run splitting and drawing.
// Portable by construction: everything here goes through platform.h, so a
// FreeType or CoreText back end changes nothing above this line (ADR-006).
#ifndef UI_TEXT_H
#define UI_TEXT_H

#include "../base/base.h"
#include "../base/base_arena.h"
#include "../base/base_math.h"
#include "../base/base_string.h"
#include "../platform/platform.h"

// Horizontal subpixel positioning on quarter pixels (research/03 s5.6): four
// variants of a glyph, chosen from the fractional pen position, so word
// spacing stays regular instead of dancing when a list scrolls.
#define UI_TEXT_SUBPIXEL_STEPS 4
// Above this advance the glyph gets one variant only: kanji are wide, evenly
// spaced, and would cost four atlas entries each for nothing.
#define UI_TEXT_SUBPIXEL_MAX_ADVANCE 20.0f

#define UI_GLYPH_CACHE_SIZE   4096  // power of two, open addressing
#define UI_MEASURE_CACHE_SIZE 1024  // power of two, one probe then overwrite

typedef enum UI_TextFlag {
    UI_TextFlag_TabularNumbers = 1 << 0,  // digits centred in the widest digit cell
} UI_TextFlag;

// One stretch of text that a single font covers, as the system fallback cut it.
typedef struct UI_TextRun {
    String8 text;
    OsFont font;
} UI_TextRun;

typedef struct UI_TextStats {
    u64 glyph_hits, glyph_misses;
    u64 measure_hits, measure_misses;
    u64 rasterizations;
    u64 glyph_count;  // live entries in the glyph cache
} UI_TextStats;

void ui_text_init(Arena *persistent);
// DPI or theme change: the atlas is gone, so every cached placement is stale.
void ui_text_reset(void);

// Splits `text` from `offset` into the longest run one font can render. The
// return value is the offset of the next run, `text.size` once done.
u64 ui_text_next_run(OsFont font, String8 text, u64 offset, UI_TextRun *out_run);

f32 ui_text_width(OsFont font, String8 text, u32 flags);
f32 ui_text_line_height(OsFont font);
// Distance from the top of the line box to the baseline: ui_core centres text
// vertically in a box without ever touching platform.h.
f32 ui_text_ascent(OsFont font);

// Baseline positioning, physical pixels. Returns the advance width drawn.
f32 ui_text_draw(OsFont font, String8 text, V2 baseline, u32 color, u32 flags);

// Bytes of `text` that fit in `max_width` once the ellipsis is paid for;
// `text.size` when the whole string fits and nothing is elided.
u64 ui_text_ellipsis_split(OsFont font, String8 text, f32 max_width, u32 flags);
f32 ui_text_draw_ellipsized(OsFont font, String8 text, V2 baseline, f32 max_width, u32 color,
                            u32 flags);

UI_TextStats ui_text_stats(void);

#endif // UI_TEXT_H

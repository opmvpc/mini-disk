// ui_text.c - the text engine above platform.h: one hashed glyph cache feeding
// the R8 atlas, one string measurement cache, and a single codepoint loop that
// drawing, measuring and eliding all share so they can never disagree.
#include "ui_text.h"

#include "../base/base_hash.h"
#include "r_atlas.h"
#include "r_core.h"

typedef struct UI_Glyph {
    u64 key;  // 0 : empty slot
    f32 advance;
    i16 offset_x, offset_y;
    u16 width, height;
    V2 uv0, uv1;
} UI_Glyph;

typedef struct UI_MeasureEntry {
    u64 key;  // 0 : empty slot
    f32 width;
} UI_MeasureEntry;

typedef struct UI_TextState {
    UI_Glyph *glyphs;
    UI_MeasureEntry *measures;
    UI_TextStats stats;
} UI_TextState;

global UI_TextState ui_text;

// One rasterization at a time, so this is a scratch buffer, not an allocation:
// the draw path must not touch an arena (T-005: zero allocation on frame two).
global u8 ui_text_coverage[OS_GLYPH_MAX_DIM * OS_GLYPH_MAX_DIM];

#define UI_TEXT_ELLIPSIS str8_lit("\xE2\x80\xA6")  // U+2026

void ui_text_init(Arena *persistent) {
    ui_text.glyphs = push_array_zero(persistent, UI_Glyph, UI_GLYPH_CACHE_SIZE);
    ui_text.measures = push_array_zero(persistent, UI_MeasureEntry, UI_MEASURE_CACHE_SIZE);
    mem_zero(&ui_text.stats, sizeof(ui_text.stats));
}

void ui_text_reset(void) {
    mem_zero(ui_text.glyphs, sizeof(UI_Glyph) * UI_GLYPH_CACHE_SIZE);
    mem_zero(ui_text.measures, sizeof(UI_MeasureEntry) * UI_MEASURE_CACHE_SIZE);
    ui_text.stats.glyph_count = 0;
}

UI_TextStats ui_text_stats(void) { return ui_text.stats; }

// --- glyph cache -----------------------------------------------------------

// (font, glyph, subpixel) in one word. font ids start at 1, so a live key can
// never be zero and zero stays free to mean "empty slot".
md_inline u64 ui_glyph_key(u32 font_id, u32 glyph, u32 subpixel) {
    return ((u64)font_id << 24) | ((u64)glyph << 8) | subpixel;
}

static UI_Glyph *ui_glyph_get(OsFont font, u32 glyph, u32 subpixel) {
    u64 key = ui_glyph_key(os_font_id(font), glyph, subpixel);
    u32 slot = (u32)(hash64_mix(key) & (UI_GLYPH_CACHE_SIZE - 1));
    while (ui_text.glyphs[slot].key != 0 && ui_text.glyphs[slot].key != key) {
        slot = (slot + 1) & (UI_GLYPH_CACHE_SIZE - 1);
    }
    UI_Glyph *entry = &ui_text.glyphs[slot];
    if (entry->key == key) {
        ui_text.stats.glyph_hits += 1;
        return entry;
    }

    ui_text.stats.glyph_misses += 1;
    ui_text.stats.rasterizations += 1;
    OsGlyphMetrics metrics;
    f32 offset = (f32)subpixel / (f32)UI_TEXT_SUBPIXEL_STEPS;
    b32 has_ink = os_font_rasterize(font, glyph, offset, ui_text_coverage,
                                    sizeof(ui_text_coverage), &metrics);
    mem_zero(entry, sizeof(*entry));
    entry->key = key;
    entry->advance = metrics.advance;
    if (has_ink) {
        R_AtlasRect placed = r_atlas_add(metrics.width, metrics.height, ui_text_coverage);
        // A full atlas draws nothing rather than garbage: width stays 0.
        entry->width = placed.width;
        entry->height = placed.height;
        entry->offset_x = (i16)metrics.offset_x;
        entry->offset_y = (i16)metrics.offset_y;
        entry->uv0 = placed.uv0;
        entry->uv1 = placed.uv1;
    }
    ui_text.stats.glyph_count += 1;
    // Linear probing degrades past three quarters full; the DPI change that
    // resets the caches is the only thing that ever empties them.
    Assert(ui_text.stats.glyph_count < (UI_GLYPH_CACHE_SIZE / 4) * 3);
    return entry;
}

// --- the one loop ----------------------------------------------------------

// State of the walk over a string: one codepoint, resolved to the font that
// carries it, its cached glyph, and the pen advance it costs.
typedef struct UI_TextIter {
    OsFont base;
    String8 text;
    u32 flags;
    u64 offset;

    OsFont font;      // font of the current codepoint, after fallback
    u32 codepoint;
    u64 byte_offset;  // offset of the current codepoint
    u64 byte_size;
    UI_Glyph *glyph;
    f32 advance;      // pen advance, tabular cell included
    f32 glyph_x;      // where the glyph sits inside that advance
} UI_TextIter;

md_inline u32 ui_text_subpixel(f32 pen_x, f32 advance) {
    if (advance >= UI_TEXT_SUBPIXEL_MAX_ADVANCE) { return 0; }
    f32 fraction = pen_x - floor_f32(pen_x);
    u32 step = (u32)(fraction * (f32)UI_TEXT_SUBPIXEL_STEPS);
    return Min(step, UI_TEXT_SUBPIXEL_STEPS - 1);
}

static b32 ui_text_iter_next(UI_TextIter *it, f32 pen_x) {
    if (it->offset >= it->text.size) { return 0; }
    UnicodeDecode decoded = utf8_decode(it->text.str + it->offset, it->text.size - it->offset);
    it->byte_offset = it->offset;
    it->byte_size = decoded.advance;
    it->codepoint = decoded.codepoint;
    it->offset += decoded.advance;

    u32 glyph = os_font_glyph_index(it->base, it->codepoint, &it->font);
    // Advance first, subpixel variant second: which variant to rasterize
    // depends on how wide the glyph is.
    UI_Glyph *entry = ui_glyph_get(it->font, glyph, 0);
    u32 subpixel = ui_text_subpixel(pen_x, entry->advance);
    if (subpixel != 0) { entry = ui_glyph_get(it->font, glyph, subpixel); }
    it->glyph = entry;
    it->advance = entry->advance;
    it->glyph_x = 0.0f;

    // Tabular figures: every digit occupies the widest digit's cell and sits
    // centred in it, which is what the `tnum` feature does (research/03 s5.8).
    if ((it->flags & UI_TextFlag_TabularNumbers) && it->codepoint >= '0' && it->codepoint <= '9') {
        f32 cell = os_font_metrics(it->font).digit_advance;
        it->glyph_x = (cell - it->advance) * 0.5f;
        it->advance = cell;
    }
    return 1;
}

static UI_TextIter ui_text_iter_begin(OsFont font, String8 text, u32 flags) {
    UI_TextIter it;
    mem_zero(&it, sizeof(it));
    it.base = font;
    it.text = text;
    it.flags = flags;
    it.font = font;
    return it;
}

u64 ui_text_next_run(OsFont font, String8 text, u64 offset, UI_TextRun *out_run) {
    Assert(offset <= text.size);
    UI_TextIter it = ui_text_iter_begin(font, text, 0);
    it.offset = offset;
    out_run->font = font;
    out_run->text = str8(text.str + offset, 0);
    u32 run_font_id = 0;
    while (ui_text_iter_next(&it, 0.0f)) {
        u32 id = os_font_id(it.font);
        if (run_font_id == 0) {
            run_font_id = id;
            out_run->font = it.font;
        } else if (id != run_font_id) {
            it.offset = it.byte_offset;  // the codepoint belongs to the next run
            break;
        }
        out_run->text.size = it.offset - offset;
    }
    return it.offset;
}

// --- measurement -----------------------------------------------------------

static f32 ui_text_measure_uncached(OsFont font, String8 text, u32 flags) {
    UI_TextIter it = ui_text_iter_begin(font, text, flags);
    f32 width = 0.0f;
    while (ui_text_iter_next(&it, width)) { width += it.advance; }
    return width;
}

f32 ui_text_width(OsFont font, String8 text, u32 flags) {
    u64 key = hash64_combine(hash64(text.str, text.size),
                             ((u64)os_font_id(font) << 8) | (flags & 0xFF));
    key |= 1;  // never zero: zero is the empty slot
    u32 slot = (u32)(hash64_mix(key) & (UI_MEASURE_CACHE_SIZE - 1));
    UI_MeasureEntry *entry = &ui_text.measures[slot];
    if (entry->key == key) {
        ui_text.stats.measure_hits += 1;
        return entry->width;
    }
    // One slot per hash, overwritten on collision: recomputing a width is
    // cheap, and a chain would make the common hit slower for no gain.
    ui_text.stats.measure_misses += 1;
    entry->key = key;
    entry->width = ui_text_measure_uncached(font, text, flags);
    return entry->width;
}

f32 ui_text_line_height(OsFont font) {
    OsFontMetrics metrics = os_font_metrics(font);
    return round_f32(metrics.ascent + metrics.descent + metrics.line_gap);
}

// --- drawing ---------------------------------------------------------------

f32 ui_text_draw(OsFont font, String8 text, V2 baseline, u32 color, u32 flags) {
    // Vertical subpixel positioning is always wrong (research/03 s5.6): a
    // fractional baseline is a blurred line, whatever the rasterizer.
    f32 y = round_f32(baseline.y);
    f32 pen_x = baseline.x;
    u32 texture = r_atlas_texture();
    UI_TextIter it = ui_text_iter_begin(font, text, flags);
    while (ui_text_iter_next(&it, pen_x)) {
        UI_Glyph *glyph = it.glyph;
        if (glyph->width) {
            f32 x = floor_f32(pen_x + it.glyph_x) + (f32)glyph->offset_x;
            f32 top = y + (f32)glyph->offset_y;
            r_rect_textured(rect(x, top, x + (f32)glyph->width, top + (f32)glyph->height), texture,
                            glyph->uv0, glyph->uv1, color, 1);
        }
        pen_x += it.advance;
    }
    return pen_x - baseline.x;
}

// --- ellipsis --------------------------------------------------------------

u64 ui_text_ellipsis_split(OsFont font, String8 text, f32 max_width, u32 flags) {
    if (ui_text_width(font, text, flags) <= max_width) { return text.size; }
    f32 budget = max_width - ui_text_width(font, UI_TEXT_ELLIPSIS, flags);
    if (budget <= 0.0f) { return 0; }

    UI_TextIter it = ui_text_iter_begin(font, text, flags);
    f32 width = 0.0f;
    u64 fits = 0;
    while (ui_text_iter_next(&it, width)) {
        if (width + it.advance > budget) { break; }
        width += it.advance;
        fits = it.offset;
    }
    return fits;
}

f32 ui_text_draw_ellipsized(OsFont font, String8 text, V2 baseline, f32 max_width, u32 color,
                            u32 flags) {
    u64 fits = ui_text_ellipsis_split(font, text, max_width, flags);
    f32 width = ui_text_draw(font, str8_prefix(text, fits), baseline, color, flags);
    if (fits < text.size) {
        width += ui_text_draw(font, UI_TEXT_ELLIPSIS, v2(baseline.x + width, baseline.y), color,
                              flags);
    }
    return width;
}

// test_text.c - the text engine against the real DirectWrite: run splitting on
// the system fallback, ellipsis position, and the two caches. The rasterizer is
// the machine's, so nothing here asserts on exact pixel widths; it asserts on
// the invariants the UI depends on.

global Arena *test_text_arena;  // outlives the cases: the atlas and the caches live in it
global OsFont test_text_font;
global OsFont test_text_heading;

#define TEST_TEXT_JP "\xE4\xB8\x96\xE7\x95\x8C"                          // U+4E16 U+754C
#define TEST_TEXT_HALFWIDTH "\xEF\xBE\x83\xEF\xBD\xBD\xEF\xBE\x84"       // halfwidth katakana U+FF83 U+FF7D U+FF84
#define TEST_TEXT_ELLIPSIS "\xE2\x80\xA6"                                // U+2026

TEST(text_fonts_open) {
    Unused(arena);
    EXPECT(test_text_font.v != 0);
    EXPECT(test_text_heading.v != 0);
    EXPECT(os_font_id(test_text_font) != os_font_id(test_text_heading));

    OsFontMetrics metrics = os_font_metrics(test_text_font);
    EXPECT(metrics.ascent > 0.0f);
    EXPECT(metrics.descent > 0.0f);
    EXPECT(metrics.x_height > 0.0f);
    EXPECT(metrics.cap_height > metrics.x_height);
    EXPECT(metrics.digit_advance > 0.0f);
    // 13 dp at 100 %: the em box is 13 px, so ascent + descent lands near it.
    EXPECT(metrics.ascent + metrics.descent > 10.0f);
    EXPECT(metrics.ascent + metrics.descent < 24.0f);
}

TEST(text_glyph_fallback) {
    Unused(arena);
    OsFont carrier;
    carrier.v = 0;
    u32 latin = os_font_glyph_index(test_text_font, 'A', &carrier);
    EXPECT(latin != 0);
    EXPECT(carrier.v == test_text_font.v);  // no fallback for Latin

    // U+4E16 and the halfwidth katakana U+FF83 are not in Segoe UI: the system fallback
    // is what makes them render at all (ADR-006).
    OsFont jp_font;
    jp_font.v = 0;
    u32 jp = os_font_glyph_index(test_text_font, 0x4E16, &jp_font);
    EXPECT(jp != 0);
    EXPECT(jp_font.v != 0);

    OsFont half_font;
    half_font.v = 0;
    u32 half = os_font_glyph_index(test_text_font, 0xFF83, &half_font);
    EXPECT(half != 0);
    EXPECT(half_font.v != 0);

    // Second lookup goes through the platform's codepoint map, same answer.
    OsFont again;
    again.v = 0;
    EXPECT(os_font_glyph_index(test_text_font, 0x4E16, &again) == jp);
    EXPECT(again.v == jp_font.v);
}

TEST(text_runs_cover_the_string) {
    Unused(arena);
    String8 text = str8_lit("Hello " TEST_TEXT_JP " " TEST_TEXT_HALFWIDTH " ok");
    UI_TextRun run;
    u64 offset = 0;
    u64 run_count = 0;
    u64 covered = 0;
    b32 contiguous = 1;
    while (offset < text.size) {
        u64 next = ui_text_next_run(test_text_font, text, offset, &run);
        EXPECT(next > offset);  // a run always makes progress
        if (run.text.str != text.str + offset) { contiguous = 0; }
        if (run.text.size != next - offset) { contiguous = 0; }
        covered += run.text.size;
        run_count += 1;
        offset = next;
    }
    EXPECT(contiguous);
    EXPECT(covered == text.size);
    // "Hello " and " ok" are Segoe UI, the Japanese is not: at least three runs.
    EXPECT(run_count >= 3);

    // Pure ASCII is one single run, whatever the fallback machinery does.
    String8 ascii = str8_lit("plain latin text");
    UI_TextRun single;
    EXPECT(ui_text_next_run(test_text_font, ascii, 0, &single) == ascii.size);
    EXPECT(single.text.size == ascii.size);
    EXPECT(single.font.v == test_text_font.v);

    // An empty string yields no run and terminates.
    UI_TextRun empty;
    EXPECT(ui_text_next_run(test_text_font, str8_lit(""), 0, &empty) == 0);
}

TEST(text_measure_matches_draw) {
    String8 text = str8_lit("Mesure & rendu");
    f32 width = ui_text_width(test_text_font, text, 0);
    EXPECT(width > 0.0f);

    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    f32 drawn = ui_text_draw(test_text_font, text, v2(20.0f, 40.0f), r_rgb(0xFFFFFF), 0);
    r_end_frame();
    // Same loop underneath: the measured box is exactly the drawn advance.
    EXPECT(abs_f32(drawn - width) < 0.001f);

    // Every glyph of a 14 character string is a quad, minus the spaces.
    const R_Frame *frame = r_frame_state();
    EXPECT(frame->quad_count >= 12);
    EXPECT(frame->quad_count <= 14);
}

TEST(text_tabular_numbers) {
    Unused(arena);
    // Without tnum, "111" is narrower than "000" in most UI fonts; with it,
    // every digit takes the same cell, so any two digit strings of the same
    // length measure the same.
    f32 ones = ui_text_width(test_text_font, str8_lit("11:11"), UI_TextFlag_TabularNumbers);
    f32 zeros = ui_text_width(test_text_font, str8_lit("00:00"), UI_TextFlag_TabularNumbers);
    EXPECT(abs_f32(ones - zeros) < 0.001f);

    OsFontMetrics metrics = os_font_metrics(test_text_font);
    f32 four_digits = ui_text_width(test_text_font, str8_lit("0000"), UI_TextFlag_TabularNumbers);
    EXPECT(abs_f32(four_digits - 4.0f * metrics.digit_advance) < 0.001f);
}

TEST(text_ellipsis_position) {
    Unused(arena);
    String8 text = str8_lit("A very long track title that will not fit");
    f32 full = ui_text_width(test_text_font, text, 0);
    f32 ellipsis = ui_text_width(test_text_font, str8_lit(TEST_TEXT_ELLIPSIS), 0);

    // Enough room: nothing is cut.
    EXPECT(ui_text_ellipsis_split(test_text_font, text, full, 0) == text.size);
    EXPECT(ui_text_ellipsis_split(test_text_font, text, full + 100.0f, 0) == text.size);

    f32 budget = full * 0.5f;
    u64 fits = ui_text_ellipsis_split(test_text_font, text, budget, 0);
    EXPECT(fits > 0);
    EXPECT(fits < text.size);
    // The kept prefix plus the ellipsis fits, and one more codepoint would not.
    String8 kept = str8_prefix(text, fits);
    EXPECT(ui_text_width(test_text_font, kept, 0) + ellipsis <= budget + 0.001f);
    UnicodeDecode next = utf8_decode(text.str + fits, text.size - fits);
    String8 one_more = str8_prefix(text, fits + next.advance);
    EXPECT(ui_text_width(test_text_font, one_more, 0) + ellipsis > budget - 0.001f);

    // The split lands on a codepoint boundary, never inside a UTF-8 sequence.
    String8 jp = str8_lit(TEST_TEXT_JP TEST_TEXT_JP TEST_TEXT_JP TEST_TEXT_JP);
    u64 jp_fits = ui_text_ellipsis_split(test_text_font, jp,
                                         ui_text_width(test_text_font, jp, 0) * 0.5f, 0);
    EXPECT(jp_fits % 3 == 0);

    // No room even for the ellipsis: nothing is kept.
    EXPECT(ui_text_ellipsis_split(test_text_font, text, ellipsis * 0.5f, 0) == 0);
}

// T-075 revue: the alert of the pre-flight, in the 300 px a narrow Disc panel
// leaves it. It has to come back in whole words, never on one cut line.
TEST(text_wrap_point_breaks_on_spaces) {
    Unused(arena);
    String8 text = str8_lit("D\xC3\xA9passement : le plan ne tient pas dans l'espace libre");
    f32 width = 300.0f;
    EXPECT(ui_text_width(test_text_font, text, 0) > width);  // it really does not fit

    u64 at = ui_text_wrap_point(test_text_font, text, width, 0);
    EXPECT(at > 0);
    EXPECT(at < text.size);
    EXPECT(text.str[at] == ' ');  // the break is a space, not a letter
    EXPECT(ui_text_width(test_text_font, str8_prefix(text, at), 0) <= width);

    // It is the *last* space that fits: one word more would overflow.
    String8 rest = str8_skip(text, at + 1);
    u64 next = 0;
    while (next < rest.size && rest.str[next] != ' ') { next += 1; }
    EXPECT(ui_text_width(test_text_font, str8_prefix(text, at + 1 + next), 0) > width);

    // Three lines are enough for it at 300 px, each one broken on a space.
    u32 lines = 1;
    while (ui_text_width(test_text_font, rest, 0) > width) {
        u64 cut = ui_text_wrap_point(test_text_font, rest, width, 0);
        EXPECT(cut < rest.size);
        rest = str8_skip(rest, cut + 1);
        lines += 1;
    }
    lines += 1;
    EXPECT(lines <= 3);

    // A width that holds the whole string, and one that holds no space at all:
    // both answer "no break here".
    EXPECT(ui_text_wrap_point(test_text_font, text, ui_text_width(test_text_font, text, 0), 0) ==
           text.size);
    EXPECT(ui_text_wrap_point(test_text_font, text, 1.0f, 0) == text.size);
    EXPECT(ui_text_wrap_point(test_text_font, text, 0.0f, 0) == text.size);
}

TEST(text_cache_hit_and_miss) {
    ui_text_reset();
    String8 text = str8_lit("cache me if you can");

    UI_TextStats before = ui_text_stats();
    f32 first = ui_text_width(test_text_font, text, 0);
    UI_TextStats after_first = ui_text_stats();
    EXPECT(after_first.measure_misses == before.measure_misses + 1);
    EXPECT(after_first.rasterizations > 0);  // cold cache: every glyph is new

    f32 second = ui_text_width(test_text_font, text, 0);
    UI_TextStats after_second = ui_text_stats();
    EXPECT(second == first);
    EXPECT(after_second.measure_hits == after_first.measure_hits + 1);
    EXPECT(after_second.rasterizations == after_first.rasterizations);

    // The point of the glyph cache: a second frame of the same text rasterizes
    // nothing at all (T-005 acceptance criterion).
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    ui_text_draw(test_text_font, text, v2(10.0f, 20.0f), r_rgb(0xFFFFFF), 0);
    r_end_frame();
    UI_TextStats frame_one = ui_text_stats();

    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    ui_text_draw(test_text_font, text, v2(10.0f, 20.0f), r_rgb(0xFFFFFF), 0);
    r_end_frame();
    UI_TextStats frame_two = ui_text_stats();
    EXPECT(frame_two.rasterizations == frame_one.rasterizations);
    EXPECT(frame_two.glyph_hits > frame_one.glyph_hits);
    EXPECT(frame_two.glyph_misses == frame_one.glyph_misses);

    // A different font is a different key: the same string misses again.
    UI_TextStats before_other = ui_text_stats();
    ui_text_width(test_text_heading, text, 0);
    EXPECT(ui_text_stats().measure_misses == before_other.measure_misses + 1);

    // ui_text_reset empties both caches, as a DPI change does.
    ui_text_reset();
    UI_TextStats before_reset = ui_text_stats();
    ui_text_width(test_text_font, text, 0);
    EXPECT(ui_text_stats().measure_misses == before_reset.measure_misses + 1);
    EXPECT(ui_text_stats().rasterizations > before_reset.rasterizations);
}

TEST(text_subpixel_variants) {
    ui_text_reset();
    UI_TextStats before = ui_text_stats();
    // Drawing at x.0 and at x.5 uses two different quarter pixel variants of
    // the same glyph, so the second draw rasterizes again (research/03 s5.6).
    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    ui_text_draw(test_text_font, str8_lit("iii"), v2(10.0f, 20.0f), r_rgb(0xFFFFFF), 0);
    r_end_frame();
    UI_TextStats aligned = ui_text_stats();

    r_begin_frame(arena, 800.0f, 600.0f, 1.0f);
    ui_text_draw(test_text_font, str8_lit("iii"), v2(10.5f, 20.0f), r_rgb(0xFFFFFF), 0);
    r_end_frame();
    UI_TextStats shifted = ui_text_stats();
    EXPECT(aligned.rasterizations > before.rasterizations);
    EXPECT(shifted.rasterizations > aligned.rasterizations);
}

TEST(text_dpi_rebuild) {
    Unused(arena);
    OsFontMetrics at_100 = os_font_metrics(ui_font(UI_FontStyle_Ui));

    // A DPI change throws the caches away and reopens every style at the new
    // pixel size, rounded to a whole pixel (research/03 s5.11).
    ui_text_reset();
    r_atlas_reset();
    EXPECT(ui_fonts_build(1.5f));
    OsFont scaled = ui_font(UI_FontStyle_Ui);
    OsFontMetrics at_150 = os_font_metrics(scaled);
    f32 ratio = (at_150.ascent + at_150.descent) / (at_100.ascent + at_100.descent);
    EXPECT(ratio > 1.4f && ratio < 1.6f);
    EXPECT(at_150.digit_advance > at_100.digit_advance);
    // 13 dp at 150 % is 19.5 px, rounded to 20: the glyphs are still integral.
    EXPECT(ui_text_width(scaled, str8_lit("0"), UI_TextFlag_TabularNumbers) ==
           at_150.digit_advance);

    ui_text_reset();
    r_atlas_reset();
    EXPECT(ui_fonts_build(1.0f));
    test_text_font = ui_font(UI_FontStyle_Ui);
    test_text_heading = ui_font(UI_FontStyle_Heading);
}

static void test_text_run_all(void) {
    test_report("text\n");
    if (!os_font_init()) {
        test_report("  SKIP text: dwrite.dll unavailable\n");
        return;
    }
    test_text_arena = arena_alloc(MB(64));
    r_atlas_init(test_text_arena);
    b32 built = ui_fonts_build(1.0f);
    test_check(built, "ui_fonts_build(1.0f)", __LINE__);
    ui_text_init(test_text_arena);
    test_text_font = ui_font(UI_FontStyle_Ui);
    test_text_heading = ui_font(UI_FontStyle_Heading);

    RUN(text_fonts_open);
    RUN(text_glyph_fallback);
    RUN(text_runs_cover_the_string);
    RUN(text_measure_matches_draw);
    RUN(text_tabular_numbers);
    RUN(text_ellipsis_position);
    RUN(text_wrap_point_breaks_on_spaces);
    RUN(text_cache_hit_and_miss);
    RUN(text_subpixel_variants);
    RUN(text_dpi_rebuild);

    os_font_shutdown();
    arena_release(test_text_arena);
}

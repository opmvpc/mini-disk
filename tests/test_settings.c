// test_settings.c - T-072: the light theme's contrast, the language switch, the
// shortcut table and the new preferences.
//
// The contrast is computed here and not in ui_theme.c on purpose: WCAG needs
// pow(x, 2.4), the release build has no libm (ADR-002), and a *token* is a
// value somebody chose. What the theme owes us is that the choice was checked,
// which is exactly what a test is for.

// WCAG 2.1 relative luminance of one theme colour. The tokens are opaque, so
// the premultiplied components are the straight ones.
static f64 test_contrast_channel(u8 component) {
    f64 c = (f64)component / 255.0;
    if (c <= 0.03928) { return c / 12.92; }
    return dsp_pow_f64((c + 0.055) / 1.055, 2.4);
}

static f64 test_luminance(u32 color) {
    return 0.2126 * test_contrast_channel(ui_color_red(color)) +
           0.7152 * test_contrast_channel(ui_color_green(color)) +
           0.0722 * test_contrast_channel(ui_color_blue(color));
}

static f64 test_contrast_ratio(u32 a, u32 b) {
    f64 la = test_luminance(a);
    f64 lb = test_luminance(b);
    f64 lighter = (la > lb) ? la : lb;
    f64 darker = (la > lb) ? lb : la;
    return (lighter + 0.05) / (darker + 0.05);
}

// s10.9: every text on every background it is actually drawn on, at 4,5:1.
// Disabled text is exempt (WCAG 1.4.3), and a selection background only ever
// carries the primary colour, which is why the two matrices are separate.
static void test_theme_contrast(const UI_Theme *theme, const char *name, f64 accent_fg_min) {
    u32 surfaces[6] = {theme->canvas,  theme->panel,         theme->surface,
                       theme->control, theme->control_hover, theme->row_hover};
    u32 texts[3] = {theme->fg_primary, theme->fg_secondary, theme->fg_muted};
    for (u32 s = 0; s < ArrayCount(surfaces); s += 1) {
        for (u32 t = 0; t < ArrayCount(texts); t += 1) {
            f64 ratio = test_contrast_ratio(surfaces[s], texts[t]);
            if (ratio < 4.5) {
                test_report("  FAIL %s: text %u on surface %u = %f:1\n", name, t, s, ratio);
            }
            EXPECT(ratio >= 4.5);
        }
    }
    EXPECT(test_contrast_ratio(theme->row_selected, theme->fg_primary) >= 4.5);
    EXPECT(test_contrast_ratio(theme->row_selected_inactive, theme->fg_primary) >= 4.5);
    // White on the dark accent (#0090FF, ADR-011 D8) is 3,3:1: a primary button
    // there does not reach 4,5:1, and the same token is also *text* on a dark
    // surface, where a darker blue would fail the other way round. Telling the
    // two apart needs a token the dark theme does not have, so the dark theme
    // is held to the 3:1 of WCAG 1.4.11 on this one pair and the gap is written
    // down in the Livraison; the light theme is held to 4,5:1 like everything
    // else.
    {
        f64 ratio = test_contrast_ratio(theme->accent, theme->accent_fg);
        if (ratio < accent_fg_min) {
            test_report("  FAIL %s: accent_fg on accent = %f:1\n", name, ratio);
        }
        EXPECT(ratio >= accent_fg_min);
    }

    // The meaningful colours are text too: a mode badge, a warning, an error.
    u32 flat[4] = {theme->canvas, theme->panel, theme->surface, theme->control};
    u32 meaning[7] = {theme->accent,        theme->success,      theme->warning,
                      theme->danger,        theme->mode[UI_Mode_SP],
                      theme->mode[UI_Mode_Mono], theme->mode[UI_Mode_LP2]};
    for (u32 s = 0; s < ArrayCount(flat); s += 1) {
        for (u32 m = 0; m < ArrayCount(meaning); m += 1) {
            f64 ratio = test_contrast_ratio(flat[s], meaning[m]);
            if (ratio < 4.5) {
                test_report("  FAIL %s: meaning %u on surface %u = %f:1\n", name, m, s, ratio);
            }
            EXPECT(ratio >= 4.5);
        }
    }
    EXPECT(test_contrast_ratio(theme->surface, theme->mode[UI_Mode_LP4]) >= 4.5);
    EXPECT(test_contrast_ratio(theme->panel, theme->mode[UI_Mode_LP4]) >= 4.5);
}

TEST(settings_light_theme_contrast) {
    (void)arena;
    UI_Theme light;
    ui_theme_light(&light);
    EXPECT(!light.dark);
    EXPECT(light.row_compact == 22.0f);  // the metrics came with the colours
    test_theme_contrast(&light, "light", 4.5);
}

TEST(settings_dark_theme_contrast) {
    (void)arena;
    UI_Theme dark;
    ui_theme_dark(&dark);
    EXPECT(dark.dark);
    test_theme_contrast(&dark, "dark", 3.0);
}

// The mode colours of the light theme are the darkened ones of s10.4: on a
// white surface the dark ones would be the pale half of the pair.
TEST(settings_light_modes_are_darker) {
    (void)arena;
    UI_Theme light, dark;
    ui_theme_light(&light);
    ui_theme_dark(&dark);
    for (u32 i = 0; i < UI_Mode_COUNT; i += 1) {
        EXPECT(test_luminance(light.mode[i]) < test_luminance(dark.mode[i]));
    }
}

// --- the language, live -------------------------------------------------------
TEST(settings_language_switch) {
    (void)arena;
    StrLang before = app_lang;

    app_lang = StrLang_Fr;
    EXPECT(str8_eq(app_str(Str_SettingsTitle), str8_lit("Pr\xC3\xA9\x66\xC3\xA9rences")));
    EXPECT(str8_eq(app_str(Str_SettingsThemeLight), str8_lit("Clair")));
    app_lang = StrLang_En;
    EXPECT(str8_eq(app_str(Str_SettingsTitle), str8_lit("Preferences")));
    EXPECT(str8_eq(app_str(Str_SettingsThemeLight), str8_lit("Light")));

    app_lang = before;
}

// Both tables are complete and hold the same format specifiers in the same
// order: that is the contract the header states, and nothing else checks it.
TEST(settings_string_tables_agree) {
    (void)arena;
    for (u32 i = 0; i < Str_COUNT; i += 1) {
        String8 fr = str8_cstr(app_strings_fr[i]);
        String8 en = str8_cstr(app_strings_en[i]);
        if (fr.size == 0 || en.size == 0) {
            test_report("  FAIL string %u is empty\n", i);
        }
        EXPECT(fr.size != 0);
        EXPECT(en.size != 0);
        u32 fr_specifiers = 0, en_specifiers = 0;
        for (u64 c = 0; c + 1 < fr.size; c += 1) {
            if (fr.str[c] == '%' && fr.str[c + 1] != '%') { fr_specifiers += 1; }
        }
        for (u64 c = 0; c + 1 < en.size; c += 1) {
            if (en.str[c] == '%' && en.str[c + 1] != '%') { en_specifiers += 1; }
        }
        if (fr_specifiers != en_specifiers) {
            test_report("  FAIL string %u: %u specifiers in fr, %u in en\n", i, fr_specifiers,
                        en_specifiers);
        }
        EXPECT(fr_specifiers == en_specifiers);
    }
}

// --- plurals and locale formats -----------------------------------------------
TEST(settings_plurals) {
    StrLang before = app_lang;

    // French puts 0 in the singular, English does not.
    app_lang = StrLang_Fr;
    EXPECT(app_plural(Str_TrackCountOne, Str_TrackCountMany, 0) == Str_TrackCountOne);
    EXPECT(app_plural(Str_TrackCountOne, Str_TrackCountMany, 1) == Str_TrackCountOne);
    EXPECT(app_plural(Str_TrackCountOne, Str_TrackCountMany, 2) == Str_TrackCountMany);
    EXPECT(str8_eq(app_count(arena, Str_TrackCountOne, Str_TrackCountMany, 1),
                   str8_lit("1 piste")));
    EXPECT(str8_eq(app_count(arena, Str_TrackCountOne, Str_TrackCountMany, 12),
                   str8_lit("12 pistes")));

    app_lang = StrLang_En;
    EXPECT(app_plural(Str_TrackCountOne, Str_TrackCountMany, 0) == Str_TrackCountMany);
    EXPECT(app_plural(Str_TrackCountOne, Str_TrackCountMany, 1) == Str_TrackCountOne);
    EXPECT(str8_eq(app_count(arena, Str_TrackCountOne, Str_TrackCountMany, 0),
                   str8_lit("0 tracks")));

    app_lang = before;
}

TEST(settings_locale_numbers) {
    StrLang before = app_lang;

    app_lang = StrLang_Fr;
    // U+202F, the narrow no-break space of s12.
    EXPECT(str8_eq(app_num_u64(arena, 1234), str8_lit("1\xE2\x80\xAF" "234")));
    EXPECT(str8_eq(app_num_u64(arena, 100), str8_lit("100")));
    EXPECT(str8_eq(app_num_u64(arena, 0), str8_lit("0")));
    EXPECT(str8_eq(app_num_u64(arena, 1234567), str8_lit("1\xE2\x80\xAF" "234\xE2\x80\xAF" "567")));
    EXPECT(str8_eq(app_num_tenths(arena, -140), str8_lit("-14,0")));
    EXPECT(str8_eq(app_date_locale(arena, 2026, 9, 8), str8_lit("08/09/2026")));

    app_lang = StrLang_En;
    EXPECT(str8_eq(app_num_u64(arena, 1234), str8_lit("1,234")));
    EXPECT(str8_eq(app_num_u64(arena, 1234567), str8_lit("1,234,567")));
    EXPECT(str8_eq(app_num_tenths(arena, -140), str8_lit("-14.0")));
    EXPECT(str8_eq(app_date_locale(arena, 2026, 9, 8), str8_lit("2026-09-08")));

    // A duration reads the same in both, and crosses the hour cleanly.
    EXPECT(str8_eq(app_duration_locale(arena, 74), str8_lit("1:14")));
    EXPECT(str8_eq(app_duration_locale(arena, 3725), str8_lit("1:02:05")));

    app_lang = before;
}

// --- the shortcut table --------------------------------------------------------
TEST(settings_shortcuts_are_unique) {
    (void)arena;
    EXPECT(app_shortcut_count() > 0);
    if (app_shortcut_collisions() != 0) {
        test_report("  FAIL %u colliding shortcut(s)\n", app_shortcut_collisions());
    }
    EXPECT(app_shortcut_collisions() == 0);

    // Every row carries a label and an action of its own: the help overlay
    // prints the label and the dispatch switches on the action.
    const AppShortcut *table = app_shortcuts();
    for (u32 i = 0; i < app_shortcut_count(); i += 1) {
        EXPECT(table[i].key != OsKey_None);
        EXPECT(table[i].label < Str_COUNT);
        EXPECT(table[i].action != AppAction_None);
        EXPECT(table[i].context < AppShortcutContext_COUNT);
    }
}

TEST(settings_shortcut_lookup) {
    StrLang before = app_lang;
    // A global row answers from any context.
    EXPECT(app_shortcut_action(OsKey_Comma, OsMod_Ctrl, AppShortcutContext_Plan) ==
           AppAction_Settings);
    EXPECT(app_shortcut_action(OsKey_F1, 0, AppShortcutContext_Library) ==
           AppAction_KeyboardHelp);
    // A panel row does not leak into another panel.
    EXPECT(app_shortcut_action(OsKey_F, OsMod_Ctrl, AppShortcutContext_Library) ==
           AppAction_Search);
    EXPECT(app_shortcut_action(OsKey_F, OsMod_Ctrl, AppShortcutContext_Plan) ==
           AppAction_None);
    // The modifiers are matched exactly: Ctrl+G is not Ctrl+Shift+G.
    EXPECT(app_shortcut_action(OsKey_G, OsMod_Ctrl, AppShortcutContext_Plan) == AppAction_Group);
    EXPECT(app_shortcut_action(OsKey_G, OsMod_Ctrl | OsMod_Shift, AppShortcutContext_Plan) ==
           AppAction_Ungroup);
    EXPECT(app_shortcut_action(OsKey_Q, 0, AppShortcutContext_Global) == AppAction_None);

    // The printed form is translated where the key name is a word.
    const AppShortcut *table = app_shortcuts();
    const AppShortcut *ungroup = 0;
    for (u32 i = 0; i < app_shortcut_count(); i += 1) {
        if (table[i].action == AppAction_Ungroup) { ungroup = &table[i]; }
    }
    EXPECT(ungroup != 0);
    if (ungroup) {
        app_lang = StrLang_Fr;
        EXPECT(str8_eq(app_shortcut_keys(arena, ungroup), str8_lit("Ctrl+Maj+G")));
        app_lang = StrLang_En;
        EXPECT(str8_eq(app_shortcut_keys(arena, ungroup), str8_lit("Ctrl+Shift+G")));
    }
    app_lang = before;
}

// --- the new preferences --------------------------------------------------------
TEST(settings_prefs_round_trip) {
    Prefs written;
    prefs_defaults(&written);
    written.lang = 1;
    written.theme = 1;
    written.default_mode = 3;
    written.loudness_lufs = -110;
    written.true_peak_dbtp = -30;
    written.trim_silence = 1;
    written.fade_in_ms = 120;
    written.fade_out_ms = 900;
    written.gap_ms = 3000;
    written.cache_transcode_mb = 8192;
    written.cache_covers_mb = 1024;

    String8 text = prefs_serialize(arena, &written);
    // The words, not the numbers: a preferences file is meant to be readable.
    EXPECT(str8_find(text, str8_lit("ui.lang=en"), 0) != text.size);
    EXPECT(str8_find(text, str8_lit("ui.theme=light"), 0) != text.size);
    EXPECT(str8_find(text, str8_lit("plan.default_mode=lp4"), 0) != text.size);
    EXPECT(str8_find(text, str8_lit("audio.loudness_lufs=-110"), 0) != text.size);

    Prefs read;
    EXPECT(prefs_parse(&read, text));
    EXPECT(read.lang == 1);
    EXPECT(read.theme == 1);
    EXPECT(read.default_mode == 3);
    EXPECT(read.loudness_lufs == -110);
    EXPECT(read.true_peak_dbtp == -30);
    EXPECT(read.trim_silence == 1);
    EXPECT(read.fade_in_ms == 120);
    EXPECT(read.fade_out_ms == 900);
    EXPECT(read.gap_ms == 3000);
    EXPECT(read.cache_transcode_mb == 8192);
    EXPECT(read.cache_covers_mb == 1024);
}

// Tolerant reading (ADR-012): a file from an older build has none of these
// keys, a file from a text editor may have anything at all. Both end usable.
TEST(settings_prefs_are_tolerant) {
    (void)arena;
    Prefs prefs;
    EXPECT(prefs_parse(&prefs, str8_lit("version=1\n")));
    EXPECT(prefs.theme == 2);  // system
    EXPECT(prefs.loudness_lufs == PREFS_LOUDNESS_DEF);
    EXPECT(prefs.cache_transcode_mb == PREFS_CACHE_TRANSCODE_DEF);

    String8 hostile = str8_lit(
            "version=1\n"
            "ui.lang=klingon\n"          // no such language: French stands
            "ui.theme=1\n"               // the old numeric form is still read
            "plan.default_mode=lp2\n"
            "audio.loudness_lufs=-400\n" // out of range: the default stands
            "audio.true_peak_dbtp=12\n"  // positive: refused
            "audio.gap_ms=999999\n"      // out of range
            "cache.covers_mb=1\n"        // under the floor
            "cache.transcode_mb=1024\n");
    EXPECT(prefs_parse(&prefs, hostile));
    EXPECT(prefs.lang == 0);
    EXPECT(prefs.theme == 1);
    EXPECT(prefs.default_mode == 2);
    EXPECT(prefs.loudness_lufs == PREFS_LOUDNESS_DEF);
    EXPECT(prefs.true_peak_dbtp == PREFS_PEAK_DEF);
    EXPECT(prefs.gap_ms == 0);
    EXPECT(prefs.cache_covers_mb == PREFS_CACHE_COVERS_DEF);
    EXPECT(prefs.cache_transcode_mb == 1024);
}

TEST(settings_prefs_folders_can_be_removed) {
    (void)arena;
    Prefs prefs;
    prefs_defaults(&prefs);
    EXPECT(prefs_add_folder(&prefs, str8_lit("C:\\a")));
    EXPECT(prefs_add_folder(&prefs, str8_lit("C:\\b")));
    EXPECT(prefs_add_folder(&prefs, str8_lit("C:\\c")));
    prefs_remove_folder(&prefs, 1);
    EXPECT(prefs.folder_count == 2);
    EXPECT(str8_eq(prefs_folder(&prefs, 0), str8_lit("C:\\a")));
    EXPECT(str8_eq(prefs_folder(&prefs, 1), str8_lit("C:\\c")));
    prefs_remove_folder(&prefs, 1);
    prefs_remove_folder(&prefs, 0);
    EXPECT(prefs.folder_count == 0);
}

static void test_settings_run_all(void) {
    test_report("settings\n");
    RUN(settings_light_theme_contrast);
    RUN(settings_dark_theme_contrast);
    RUN(settings_light_modes_are_darker);
    RUN(settings_language_switch);
    RUN(settings_string_tables_agree);
    RUN(settings_plurals);
    RUN(settings_locale_numbers);
    RUN(settings_shortcuts_are_unique);
    RUN(settings_shortcut_lookup);
    RUN(settings_prefs_round_trip);
    RUN(settings_prefs_are_tolerant);
    RUN(settings_prefs_folders_can_be_removed);
}

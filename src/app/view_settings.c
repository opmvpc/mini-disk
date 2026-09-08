// view_settings.c - the preferences panel and the keyboard help, the two
// overlays of T-072. Both are modal in the only sense that matters here: they
// float on the popup layer, they own the keyboard while they are up, and Escape
// closes them (research/02 s10.9, MI-43).
//
// Everything the panel edits lives in `app.prefs` and nowhere else. A control
// writes the field, marks the preferences dirty, and - for the language and the
// theme - applies the change on the spot: no restart, no "apply" button, no
// second copy of the value to keep in step.

#define APP_SETTINGS_WIDTH_DP      560.0f
#define APP_SETTINGS_LABEL_DP      210.0f
#define APP_SETTINGS_KEYS_WIDTH_DP 460.0f
#define APP_SETTINGS_KEYS_COL_DP   150.0f

typedef struct AppSettingsState {
    b32 open;
    b32 keys_open;   // the keyboard help overlay
    // The measured size of the two caches, in bytes. Taken when the panel opens
    // and after a clear: walking a cache directory is not a per frame cost.
    u64 transcode_bytes;
    u64 covers_bytes;
    b32 sizes_known;
    b32 cleared;     // the last clear said so, until the panel closes
} AppSettingsState;

global AppSettingsState app_settings;

// --- the state behind the controls ------------------------------------------

// Language and theme are the two settings that change what is already on
// screen. Both are one call: the string table is a lookup and a box takes its
// colours during the frame it is built, so the next frame is simply the new one.
static void app_settings_apply_lang(void) {
    app_lang = (app.prefs.lang == 1) ? StrLang_En : StrLang_Fr;
}

// The window the title bar belongs to. view_settings.c is the only thing that
// changes a theme, so it is the only thing that needs it.
global OsWindow app_settings_window;

static void app_settings_apply_theme(void) {
    b32 dark = ui_theme_apply((UI_ThemeChoice)app.prefs.theme);
    if (app_settings_window.v != 0) { os_window_set_dark_frame(app_settings_window, dark); }
}

void app_settings_system_theme_changed(void) {
    if (app.prefs.theme == (u32)UI_ThemeChoice_System) { app_settings_apply_theme(); }
}

static void app_settings_measure_caches(void) {
    if (app.cache_dir.size == 0) { return; }
    ArenaTemp scratch = scratch_begin(0, 0);
    CacheLruStats stats;
    StructZero(&stats);
    // A purge with an unreachable bound deletes nothing and counts everything:
    // the measurement and the enforcement are the same walk (T-041).
    cache_lru_purge(scratch.arena, app.cache_dir, str8_lit("pcm"), U64_MAX, 0, 0, &stats);
    app_settings.transcode_bytes = stats.bytes_after;
    StructZero(&stats);
    cache_lru_purge(scratch.arena, app.covers.dir, str8_lit("raw"), U64_MAX, 0, 0, &stats);
    app_settings.covers_bytes = stats.bytes_after;
    scratch_end(scratch);
    app_settings.sizes_known = 1;
}

static void app_settings_clear_caches(void) {
    ArenaTemp scratch = scratch_begin(0, 0);
    cache_lru_purge(scratch.arena, app.cache_dir, str8_lit("pcm"), 0, 0, 0, 0);
    cache_lru_purge(scratch.arena, app.covers.dir, str8_lit("raw"), 0, 0, 0, 0);
    scratch_end(scratch);
    app_settings_measure_caches();
    app_settings.cleared = 1;
}

void app_settings_open(void) {
    app_settings.open = 1;
    app_settings.keys_open = 0;
    app_settings.cleared = 0;
    app_settings.sizes_known = 0;
    // The overlay owns the keyboard while it is up, the way a menu does: the
    // three lists below it stop reading keys and Enter cannot fire twice.
    ui_popup_set_active(1);
    os_request_redraw();
}

void app_settings_close(void) {
    app_settings.open = 0;
    ui_popup_set_active(app_settings.keys_open);
    if (app.prefs_dirty) { prefs_save(&app.prefs, app.prefs_path); }
    os_request_redraw();
}

void app_settings_toggle(void) {
    if (app_settings.open) { app_settings_close(); } else { app_settings_open(); }
}

void app_settings_keys_toggle(void) {
    app_settings.keys_open = !app_settings.keys_open;
    ui_popup_set_active(app_settings.open || app_settings.keys_open);
    os_request_redraw();
}

b32 app_settings_overlay_open(void) { return app_settings.open || app_settings.keys_open; }

// The panel that owns the keyboard right now, which is what the help overlay
// lists. Nothing focused means the global rows and nothing else.
AppShortcutContext app_settings_context(void) {
    if (app.list.focused) { return AppShortcutContext_Library; }
    if (app.plan_list.focused) { return AppShortcutContext_Plan; }
    return AppShortcutContext_Global;
}

// --- little widgets, local to this panel -------------------------------------
// A row: a label on the left at a fixed width, the control on the right. The
// caller opens it and fills the rest of the line.
#define AppSettingsRow(label) \
    DeferLoop(app_settings_row_begin(label), app_settings_row_end())

static void app_settings_row_begin(String8 label) {
    const UI_Theme *theme = ui_theme();
    UI_Box *row = 0;
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard + theme->space[UI_Space_2]), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        row = ui_build_box_from_key(0, 0);
    }
    ui_push_parent(row);
    UI_PrefWidth(ui_px(ui_dp(APP_SETTINGS_LABEL_DP), 1.0f))
    UI_PrefHeight(ui_pct(1.0f, 1.0f))
    UI_TextAlign(UI_TextAlign_Left) {
        ui_label_styled(UI_FontStyle_Ui, theme->fg_secondary, label);
    }
}

static void app_settings_row_end(void) { ui_pop_parent(); }

static void app_settings_section(String8 title) {
    const UI_Theme *theme = ui_theme();
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f)) {
        ui_label_styled(UI_FontStyle_Emphasis, theme->fg_primary, title);
    }
    ui_separator();
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
}

// One button of a segmented choice. The selected one is the primary button:
// that is the only difference, and it is enough on both themes.
static b32 app_settings_choice(String8 label, String8 id, b32 selected) {
    String8 text = str8f(ui_frame_arena(), "%S###%S", label, id);
    b32 clicked = selected ? ui_button_primary(text).clicked : ui_button(text).clicked;
    ui_spacer(ui_px(ui_dp(ui_theme()->space[UI_Space_4]), 1.0f));
    return clicked && !selected;
}

// A number with a minus and a plus. `step` is applied to the i32 the caller
// owns and the range is the one prefs.h clamps to, so the panel cannot produce
// a value the file would refuse to read back.
static b32 app_settings_stepper_i32(String8 id, String8 value, i32 *field, i32 step, i32 low,
                                    i32 high) {
    const UI_Theme *theme = ui_theme();
    b32 changed = 0;
    if (ui_button(str8f(ui_frame_arena(), "-###%Sdec", id)).clicked && *field - step >= low) {
        *field -= step;
        changed = 1;
    }
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_6]), 1.0f));
    UI_PrefWidth(ui_px(ui_dp(96.0f), 1.0f))
    UI_PrefHeight(ui_pct(1.0f, 1.0f))
    UI_TextAlign(UI_TextAlign_Center)
    UI_TextFlags(UI_TextFlag_TabularNumbers) {
        ui_label_styled(UI_FontStyle_Ui, theme->fg_primary, value);
    }
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_6]), 1.0f));
    if (ui_button(str8f(ui_frame_arena(), "+###%Sinc", id)).clicked && *field + step <= high) {
        *field += step;
        changed = 1;
    }
    return changed;
}

static b32 app_settings_stepper_u32(String8 id, String8 value, u32 *field, u32 step, u32 low,
                                    u32 high) {
    i32 signed_value = (i32)*field;
    b32 changed = app_settings_stepper_i32(id, value, &signed_value, (i32)step, (i32)low,
                                           (i32)high);
    if (changed) { *field = (u32)signed_value; }
    return changed;
}

static b32 app_settings_toggle_button(String8 id, b32 *field) {
    String8 label = app_str(*field ? Str_SettingsOn : Str_SettingsOff);
    if (!ui_button(str8f(ui_frame_arena(), "%S###%S", label, id)).clicked) { return 0; }
    *field = !*field;
    return 1;
}

// --- the panel, as a table ----------------------------------------------------
// Fourteen of the sixteen rows are the same three shapes over a different field
// of Prefs, so they are data and one loop. What is left - the cache size and
// its Clear button, the list of folders - is written out below.
typedef enum AppSettingKind {
    AppSettingKind_Section = 0,  // a heading, not a control
    AppSettingKind_Choice,       // n buttons, labels Str `first` .. `first`+n-1
    AppSettingKind_Modes,        // the four MD modes, named by app_mode_names
    AppSettingKind_Toggle,
    AppSettingKind_Number,       // a stepper, clamped to the range prefs.h reads
} AppSettingKind;

typedef struct AppSetting {
    u16 label;   // Str: the row, or the heading of a section
    u16 hint;    // Str, 0: no tooltip
    u16 unit;    // Str, a format with one %S; 0: the bare number
    u16 first;   // Choice: the Str of option 0
    u16 offset;  // where the field is inside Prefs
    u8 kind;
    u8 count;    // Choice: how many options
    u8 tenths;   // Number: the field is a signed i32 in tenths of a unit
    u8 live;     // applying it changes what is already on screen
    i32 step, low, high;
} AppSetting;

#define AppSettingField(name) (u16)OffsetOf(Prefs, name)

static const AppSetting app_setting_rows[] = {
    {Str_SettingsGeneral, 0, 0, 0, 0, AppSettingKind_Section, 0, 0, 0, 0, 0, 0},
    {Str_SettingsLanguage, 0, 0, Str_SettingsLangFr, AppSettingField(lang),
     AppSettingKind_Choice, 2, 0, 1, 0, 0, 0},
    {Str_SettingsTheme, 0, 0, Str_SettingsThemeDark, AppSettingField(theme),
     AppSettingKind_Choice, UI_ThemeChoice_COUNT, 0, 1, 0, 0, 0},

    {Str_SettingsAudio, 0, 0, 0, 0, AppSettingKind_Section, 0, 0, 0, 0, 0, 0},
    {Str_SettingsDefaultMode, 0, 0, 0, AppSettingField(default_mode), AppSettingKind_Modes,
     UI_Mode_COUNT, 0, 0, 0, 0, 0},
    {Str_SettingsLoudness, Str_SettingsLoudnessHint, Str_SettingsLoudnessValue, 0,
     AppSettingField(loudness_lufs), AppSettingKind_Number, 0, 1, 0, 5, PREFS_LOUDNESS_MIN,
     PREFS_LOUDNESS_MAX},
    {Str_SettingsTruePeak, Str_SettingsTruePeakHint, Str_SettingsTruePeakValue, 0,
     AppSettingField(true_peak_dbtp), AppSettingKind_Number, 0, 1, 0, 1, PREFS_PEAK_MIN,
     PREFS_PEAK_MAX},
    {Str_SettingsTrim, 0, 0, 0, AppSettingField(trim_silence), AppSettingKind_Toggle, 0, 0, 0,
     0, 0, 0},
    {Str_SettingsFadeIn, 0, Str_SettingsMs, 0, AppSettingField(fade_in_ms),
     AppSettingKind_Number, 0, 0, 0, 50, 0, PREFS_FADE_MAX},
    {Str_SettingsFadeOut, 0, Str_SettingsMs, 0, AppSettingField(fade_out_ms),
     AppSettingKind_Number, 0, 0, 0, 50, 0, PREFS_FADE_MAX},
    {Str_SettingsGap, 0, Str_SettingsMs, 0, AppSettingField(gap_ms), AppSettingKind_Number, 0,
     0, 0, 50, 0, PREFS_GAP_MAX},

    {Str_SettingsCaches, 0, 0, 0, 0, AppSettingKind_Section, 0, 0, 0, 0, 0, 0},
    {Str_SettingsCacheTranscode, 0, Str_SettingsMb, 0, AppSettingField(cache_transcode_mb),
     AppSettingKind_Number, 0, 0, 0, 256, PREFS_CACHE_TRANSCODE_MIN, PREFS_CACHE_TRANSCODE_MAX},
    {Str_SettingsCacheCovers, 0, Str_SettingsMb, 0, AppSettingField(cache_covers_mb),
     AppSettingKind_Number, 0, 0, 0, 32, PREFS_CACHE_COVERS_MIN, PREFS_CACHE_COVERS_MAX},
};

// The field itself. Every row of the table names a u32 or an i32 of Prefs, and
// the two are the same width: the signed reading is the one `tenths` asks for.
static i32 *app_setting_field(const AppSetting *row) {
    return (i32 *)((u8 *)&app.prefs + row->offset);
}

static String8 app_setting_number(Arena *arena, const AppSetting *row, i32 value) {
    String8 number = row->tenths ? app_num_tenths(arena, value)
                                 : app_num_u64(arena, (u64)(u32)value);
    if (row->unit == 0) { return number; }
    return str8f(arena, app_str_c((Str)row->unit), number);
}

static void app_setting_row(const AppSetting *row) {
    Arena *frame = ui_frame_arena();
    if (row->kind == AppSettingKind_Section) {
        app_settings_section(app_str((Str)row->label));
        return;
    }
    i32 *field = app_setting_field(row);
    String8 id = str8f(frame, "s%u", row->offset);
    b32 changed = 0;
    AppSettingsRow(app_str((Str)row->label)) {
        switch (row->kind) {
            case AppSettingKind_Choice:
            case AppSettingKind_Modes: {
                for (u32 i = 0; i < row->count; i += 1) {
                    String8 name = (row->kind == AppSettingKind_Modes)
                                           ? str8_cstr(app_mode_names[i])
                                           : app_str((Str)(row->first + i));
                    if (app_settings_choice(name, str8f(frame, "%S%u", id, i),
                                            *field == (i32)i)) {
                        *field = (i32)i;
                        changed = 1;
                    }
                }
            } break;
            case AppSettingKind_Toggle: {
                changed = app_settings_toggle_button(id, field);
            } break;
            default: {
                changed = app_settings_stepper_i32(id, app_setting_number(frame, row, *field),
                                                   field, row->step, row->low, row->high);
            } break;
        }
        if (row->hint != 0) { ui_tooltip(app_str((Str)row->hint)); }
    }
    if (!changed) { return; }
    app.prefs_dirty = 1;
    // The two live rows are the whole point of "no restart": the language is a
    // lookup and the theme is a set, so the next frame is already the new one.
    if (row->live) {
        app_settings_apply_lang();
        app_settings_apply_theme();
    }
}

static String8 app_settings_megabytes(Arena *arena, u64 bytes) {
    return str8f(arena, app_str_c(Str_SettingsMb), app_num_u64(arena, bytes / MB(1)));
}

// What the two caches actually hold right now, and the button that empties
// them. Not a table row: it reads the disk and it has a verb on it.
static void app_settings_cache_clear(void) {
    const UI_Theme *theme = ui_theme();
    Arena *frame = ui_frame_arena();
    u64 used = app_settings.transcode_bytes + app_settings.covers_bytes;
    // C-T075: "1 Mo" alone was a label with no subject. The row says what the
    // number measures, and the button says what it does about it.
    AppSettingsRow(str8f(frame, app_str_c(Str_SettingsCacheUsed),
                         app_settings_megabytes(frame, used))) {
        if (ui_button(str8f(frame, "%S###sclr", app_str(Str_SettingsCacheClear))).clicked) {
            app_settings_clear_caches();
        }
        ui_tooltip(app_str(Str_SettingsCacheClearHint));
        if (app_settings.cleared) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            ui_label_styled(UI_FontStyle_Caption, theme->success,
                            app_str(Str_SettingsCacheCleared));
        }
    }
}

static void app_settings_library(void) {
    const UI_Theme *theme = ui_theme();
    Arena *frame = ui_frame_arena();
    u32 remove = app.prefs.folder_count;  // no folder asked to go
    for (u32 i = 0; i < app.prefs.folder_count; i += 1) {
        // T-075: the label belongs to the list, not to each of its lines - it
        // was written once per folder, five times over for five folders.
        AppSettingsRow(i == 0 ? app_str(Str_SettingsFolders) : str8_lit("")) {
            UI_PrefWidth(ui_pct(1.0f, 0.0f))
            UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
                ui_label_styled(UI_FontStyle_Caption, theme->fg_primary,
                                prefs_folder(&app.prefs, i));
            }
            if (ui_button(str8f(frame, "%S###sfrm%u", app_str(Str_SettingsFolderRemove), i))
                        .clicked) {
                remove = i;
            }
        }
    }
    if (remove < app.prefs.folder_count) {
        prefs_remove_folder(&app.prefs, remove);
        app.prefs_dirty = 1;
    }
    AppSettingsRow(app.prefs.folder_count == 0 ? app_str(Str_SettingsFolderNone)
                                               : str8_lit("")) {
        if (app.prefs.folder_count >= PREFS_MAX_FOLDERS) {
            ui_label_styled(UI_FontStyle_Caption, theme->fg_muted,
                            str8f(frame, app_str_c(Str_SettingsFolderFull),
                                  app_count(frame, Str_FolderCountOne, Str_FolderCountMany,
                                            PREFS_MAX_FOLDERS)));
        } else if (ui_button(str8f(frame, "%S###sfadd", app_str(Str_SettingsFolderAdd)))
                           .clicked) {
            String8 folder = os_dialog_pick_folder(frame, app_str(Str_SettingsFolderAdd));
            if (folder.size != 0 && prefs_add_folder(&app.prefs, folder)) {
                app.prefs_dirty = 1;
                if (!app.scan_active) {
                    app_scan_folder(folder);
                    app.scan_folder = app.prefs.folder_count;
                }
            }
        }
    }
    AppSettingsRow(app_str(Str_SettingsThumbnails)) {
        if (app_settings_toggle_button(str8_lit("sthu"), &app.prefs.thumbnails)) {
            app.prefs_dirty = 1;
        }
    }
}


// The card both overlays are: a scrim that eats the clicks that miss, a
// rounded panel centred on it, and one padded column inside (MI-43). The two
// callers differ by four values and a body, which is why there is one of these
// and not two.
typedef void (*AppSettingsBody)(void);

static void app_settings_overlay(String8 id, f32 width_dp, f32 top_dp, Str title, b32 closable,
                                 AppSettingsBody body) {
    const UI_Theme *theme = ui_theme();
    V2 viewport = ui_viewport();
    f32 width = min_f32(ui_dp(width_dp), viewport.x - ui_dp(32.0f));
    Arena *frame = ui_frame_arena();

    ui_push_layer(UI_Layer_Popup);
    UI_FixedX(0.0f) UI_FixedY(0.0f)
    UI_PrefWidth(ui_px(viewport.x, 1.0f))
    UI_PrefHeight(ui_px(viewport.y, 1.0f))
    UI_BgColor(r_rgba(0, 0, 0, theme->dark ? 115 : 60)) {
        UI_Box *scrim = ui_build_box(UI_FloatingX | UI_FloatingY | UI_DrawBackground |
                                             UI_Clickable,
                                     str8f(frame, "###%Sscrim", id));
        if (ui_signal(scrim).clicked) {
            if (closable) { app_settings_close(); } else { app_settings_keys_toggle(); }
        }
    }

    UI_Box *card = 0;
    UI_FixedX(round_f32((viewport.x - width) * 0.5f))
    UI_FixedY(ui_dp(top_dp))
    UI_PrefWidth(ui_px(width, 1.0f))
    UI_PrefHeight(ui_children_sum(1.0f))
    UI_ChildLayoutAxis(Axis2_Y)
    UI_BgColor(theme->panel)
    UI_BorderColor(theme->border_subtle)
    UI_BorderThickness(ui_dp(theme->border))
    UI_CornerRadius(ui_dp(8.0f)) {
        card = ui_build_box(UI_FloatingX | UI_FloatingY | UI_DrawBackground | UI_DrawBorder |
                                    UI_DrawDropShadow | UI_Clickable,
                            str8f(frame, "###%Scard", id));
    }
    UI_Parent(card)
    UI_TextPadding(ui_dp(theme->space[UI_Space_16])) {
        UI_PrefWidth(ui_pct(1.0f, 0.0f))
        UI_PrefHeight(ui_px(ui_dp(theme->row_comfortable), 1.0f))
        UI_ChildLayoutAxis(Axis2_X) {
            UI_Box *header = ui_build_box_from_key(0, 0);
            UI_Parent(header) {
                ui_label_styled(UI_FontStyle_Heading, theme->fg_primary, app_str(title));
                ui_spacer(ui_pct(1.0f, 0.0f));
                if (closable) {
                    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f)) {
                        if (ui_button_icon(R_Icon_Cross, str8_lit("###sclose")).clicked) {
                            app_settings_close();
                        }
                        ui_tooltip(app_str(Str_SettingsClose));
                    }
                    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
                }
            }
        }
        UI_PrefWidth(ui_pct(1.0f, 0.0f))
        UI_PrefHeight(ui_children_sum(1.0f))
        UI_ChildLayoutAxis(Axis2_X) {
            UI_Box *body_row = ui_build_box_from_key(0, 0);
            UI_Parent(body_row) {
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_16]), 1.0f));
                UI_PrefWidth(ui_pct(1.0f, 0.0f))
                UI_PrefHeight(ui_children_sum(1.0f))
                UI_ChildLayoutAxis(Axis2_Y) {
                    UI_Box *column = ui_build_box_from_key(0, 0);
                    UI_Parent(column) {
                        body();
                        ui_spacer(ui_px(ui_dp(theme->space[UI_Space_16]), 1.0f));
                    }
                }
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_16]), 1.0f));
            }
        }
    }
    ui_pop_layer();
}

// The four sections of the preferences, in the order s14 puts them in.
static void app_settings_body(void) {
    if (!app_settings.sizes_known) { app_settings_measure_caches(); }
    for (u32 i = 0; i < ArrayCount(app_setting_rows); i += 1) {
        app_setting_row(&app_setting_rows[i]);
    }
    app_settings_cache_clear();
    app_settings_section(app_str(Str_SettingsLibrary));
    app_settings_library();
}

// --- the keyboard help --------------------------------------------------------
// One row per shortcut, straight out of app_shortcuts[]: the overlay cannot
// drift from the dispatch because there is nothing to drift from.
static void app_settings_keys_section(AppShortcutContext context, Str title) {
    const UI_Theme *theme = ui_theme();
    Arena *frame = ui_frame_arena();
    const AppShortcut *table = app_shortcuts();
    b32 header_done = 0;
    for (u32 i = 0; i < app_shortcut_count(); i += 1) {
        if (table[i].context != (u8)context) { continue; }
        if (!header_done) {
            app_settings_section(app_str(title));
            header_done = 1;
        }
        UI_PrefWidth(ui_pct(1.0f, 0.0f))
        UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
        UI_ChildLayoutAxis(Axis2_X) {
            UI_Box *row = ui_build_box_from_key(0, 0);
            UI_Parent(row) {
                UI_PrefWidth(ui_px(ui_dp(APP_SETTINGS_KEYS_COL_DP), 1.0f))
                UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
                    ui_label_styled(UI_FontStyle_Ui, theme->accent,
                                    app_shortcut_keys(frame, &table[i]));
                }
                UI_PrefWidth(ui_pct(1.0f, 0.0f))
                UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
                    ui_label_styled(UI_FontStyle_Ui, theme->fg_primary,
                                    app_str((Str)table[i].label));
                }
            }
        }
    }
}

// The global rows, then the ones of the panel that owns the keyboard. With
// nothing focused there is no current context, so all four are listed.
static void app_settings_keys_body(void) {
    AppShortcutContext context = app_settings_context();
    b32 all = (context == AppShortcutContext_Global);
    app_settings_keys_section(AppShortcutContext_Global, Str_KeysContextGlobal);
    if (all || context == AppShortcutContext_Library) {
        app_settings_keys_section(AppShortcutContext_Library, Str_KeysContextLibrary);
    }
    if (all || context == AppShortcutContext_Plan) {
        app_settings_keys_section(AppShortcutContext_Plan, Str_KeysContextPlan);
    }
    if (all || context == AppShortcutContext_Disc) {
        app_settings_keys_section(AppShortcutContext_Disc, Str_KeysContextDisc);
    }
}


// The status bar hint. Four rows of the table, formatted the way the bar has
// always shown them - which is the point: one table, three readers (T-072).
String8 app_shortcut_status_hint(Arena *arena) {
    AppAction wanted[4] = {AppAction_Search, AppAction_SelectAll, AppAction_AddToPlan,
                           AppAction_KeyboardHelp};
    const AppShortcut *table = app_shortcuts();
    String8List parts;
    StructZero(&parts);
    for (u32 w = 0; w < ArrayCount(wanted); w += 1) {
        for (u32 i = 0; i < app_shortcut_count(); i += 1) {
            if (table[i].action != (u16)wanted[w]) { continue; }
            str8_list_push(arena, &parts,
                           str8f(arena, "%S %S", app_shortcut_keys(arena, &table[i]),
                                 app_str((Str)table[i].label)));
            break;
        }
    }
    return str8_list_join(arena, &parts, str8_lit(" · "));
}

// --- the frame ---------------------------------------------------------------

// Read once at startup: the language, then the theme, then the two capture
// flags. The order matters for nothing but the reader.
void app_settings_init(OsWindow window) {
    app_settings_window = window;
    app_settings_apply_lang();
    app_settings_apply_theme();

    ArenaTemp scratch = scratch_begin(0, 0);
    String8 command_line = os_command_line(scratch.arena);
    if (str8_find(command_line, str8_lit("--settings"), 0) < command_line.size) {
        app_settings_open();
    }
    if (str8_find(command_line, str8_lit("--keys"), 0) < command_line.size) {
        app_settings_keys_toggle();
    }
    // The theme, for the captures of the Livraison and for nothing else: it
    // does not touch the preferences, so the next launch is unchanged.
    if (str8_find(command_line, str8_lit("--theme-light"), 0) < command_line.size) {
        app.prefs.theme = (u32)UI_ThemeChoice_Light;
        app_settings_apply_theme();
    }
    scratch_end(scratch);
}

void app_settings_ui(void) {
    if (!app_settings.open && !app_settings.keys_open) { return; }
    // Escape closes the topmost overlay and nothing below it.
    if (ui_escape_pressed()) {
        if (app_settings.keys_open) {
            app_settings_keys_toggle();
        } else {
            app_settings_close();
        }
        return;
    }
    if (app_settings.open) {
        app_settings_overlay(str8_lit("settings"), APP_SETTINGS_WIDTH_DP, 24.0f,
                             Str_SettingsTitle, 1, app_settings_body);
    }
    if (app_settings.keys_open) {
        app_settings_overlay(str8_lit("keys"), APP_SETTINGS_KEYS_WIDTH_DP, 8.0f, Str_KeysTitle,
                             0, app_settings_keys_body);
    }
}

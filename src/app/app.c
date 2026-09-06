// app.c - the T-007 demo: three panels separated by splitters, a search field
// filtering a generated library of 100 000 tracks, a virtualized list with
// multiple selection, a context menu, tooltips, and a status bar that shows the
// number of boxes the frame actually built.
//
// The loop still only wakes on an event or while something animates (ADR-004).

#define APP_MAX_EVENTS 256
#define APP_LIBRARY_COUNT 100000
#define APP_PLAN_MAX 64

typedef struct AppLibrary {
    u32 count;
    String8 *title;
    String8 *artist;
    u32 *duration_s;
    u8 *mode;  // UI_Mode
} AppLibrary;

typedef struct AppDemo {
    AppLibrary generated;
    u32 *filtered;  // indices into the library, the result of the search
    u32 filtered_count;
    u8 query[UI_TEXT_INPUT_CAP];
    u32 query_size;

    UI_TextInput search;
    UI_List list;
    UI_Splitter library_split;
    UI_Splitter disc_split;
    UI_ContextMenu menu;

    u32 plan[APP_PLAN_MAX];
    u32 plan_count;
    UI_Mode plan_mode;
    b32 initialized;

    // --- the real library, filled by the scan (T-010) ----------------------
    Library library;
    LibEventQueue events;
    LibScan scan;
    Arena *permanent;
    u32 *rows;        // live TrackIds, compacted: the row index of the list
    u32 row_count;
    b32 scanned;      // a folder was added: the panel shows the library, not the demo
    b32 scan_active;
    u32 scan_files, scan_dirs_done, scan_dirs_total;
    u64 scan_us;
    String8 root;
} AppDemo;

global AppDemo app_demo;

// --- the fake library ------------------------------------------------------
static const char *app_words_a[] = {"Nuit",   "Silence", "Orage",  "Cristal", "Fugue",
                                    "Maree",  "Prisme",  "Verre",  "Cendre",  "Aurore",
                                    "Spirale", "Echo",   "Lueur",  "Derive",  "Fracture",
                                    "Ivoire"};
static const char *app_words_b[] = {"electrique", "de janvier", "en mineur", "lointain",
                                    "au ralenti", "de sel",     "obscur",    "de papier",
                                    "sans fin",   "du nord",    "en boucle", "de cuivre"};
static const char *app_artists[] = {
    "Ryuichi Sakamoto", "Boards of Canada", "Autechre",   "Aphex Twin", "Jan Jelinek",
    "Susumu Yokota",    "Oval",             "Fennesz",    "Nala Sinephro", "Cornelius",
    "Loscil",           "Tim Hecker",       "Grouper",    "Biosphere",  "Hiroshi Yoshimura",
    "Midori Takada",    "Colleen",          "Jonny Nash", "Sofie Birch", "Yasuaki Shimizu"};

md_inline u32 app_mix(u32 x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static void app_library_build(Arena *arena, AppLibrary *library, u32 count) {
    library->count = count;
    library->title = push_array(arena, String8, count);
    library->artist = push_array(arena, String8, count);
    library->duration_s = push_array(arena, u32, count);
    library->mode = push_array(arena, u8, count);
    for (u32 i = 0; i < count; i += 1) {
        u32 h = app_mix(i + 1);
        library->title[i] = str8f(arena, "%s %s %u", app_words_a[h % ArrayCount(app_words_a)],
                                  app_words_b[(h >> 8) % ArrayCount(app_words_b)],
                                  (h >> 16) % 900 + 100);
        library->artist[i] = str8_cstr(app_artists[(h >> 5) % ArrayCount(app_artists)]);
        library->duration_s[i] = 95 + (h >> 3) % 420;
        library->mode[i] = (u8)((h >> 21) % UI_Mode_COUNT);
    }
}

// The list, the plan and the gauge read a row through this and nothing else,
// so the same three panels display the generated demo or a scanned folder.
typedef struct AppTrack {
    String8 title;
    String8 artist;
    String8 album;
    u32 duration_s;
    u8 mode;
} AppTrack;

static AppTrack app_track(u32 index) {
    AppTrack track;
    if (app_demo.scanned) {
        Library *lib = &app_demo.library;
        // The tags (T-011), which already carry their own fallbacks: a file
        // with no tag at all still shows its name and its two parent folders.
        track.title = lib_string(&lib->strings, lib->title_id[index]);
        track.artist = lib_string(&lib->strings, lib->artist_id[index]);
        track.album = lib_string(&lib->strings, lib->album_id[index]);
        track.duration_s = lib->duration_ms[index] / 1000;
        if (track.title.size == 0) {
            // The tag job has not landed yet: the path is all we know.
            String8 path = lib_track_path(lib, index);
            track.title = os_path_filename(path);
            String8 folder = os_path_parent(path);
            if (folder.size > app_demo.root.size && str8_starts_with(folder, app_demo.root)) {
                folder = str8_skip(folder, app_demo.root.size + 1);
            } else if (str8_eq(folder, app_demo.root)) {
                folder = os_path_filename(folder);
            }
            track.artist = folder;
        }
        track.mode = UI_Mode_SP;
    } else {
        track.title = app_demo.generated.title[index];
        track.artist = app_demo.generated.artist[index];
        track.album = str8(0, 0);
        track.duration_s = app_demo.generated.duration_s[index];
        track.mode = app_demo.generated.mode[index];
    }
    return track;
}

md_inline u32 app_source_count(void) {
    return app_demo.scanned ? app_demo.row_count : app_demo.generated.count;
}

md_inline u32 app_source_index(u32 i) {
    return app_demo.scanned ? app_demo.rows[i] : i;
}

md_inline u8 app_lower(u8 c) { return (c >= 'A' && c <= 'Z') ? (u8)(c + 32) : c; }

static b32 app_contains_ci(String8 haystack, String8 needle) {
    if (needle.size == 0) { return 1; }
    if (needle.size > haystack.size) { return 0; }
    u64 last = haystack.size - needle.size;
    for (u64 i = 0; i <= last; i += 1) {
        u64 j = 0;
        while (j < needle.size && app_lower(haystack.str[i + j]) == app_lower(needle.str[j])) {
            j += 1;
        }
        if (j == needle.size) { return 1; }
    }
    return 0;
}

static void app_filter(void) {
    String8 query = str8(app_demo.query, app_demo.query_size);
    u32 source_count = app_source_count();
    u32 count = 0;
    for (u32 i = 0; i < source_count; i += 1) {
        u32 index = app_source_index(i);
        AppTrack track = app_track(index);
        if (app_contains_ci(track.title, query) || app_contains_ci(track.artist, query) ||
            app_contains_ci(track.album, query)) {
            app_demo.filtered[count] = index;
            count += 1;
        }
    }
    app_demo.filtered_count = count;
    ui_list_select_clear(&app_demo.list);
    app_demo.list.cursor = 0;
    app_demo.list.anchor = 0;
    app_demo.list.scroll = 0.0f;
}

static void app_plan_add(u32 track) {
    if (app_demo.plan_count >= APP_PLAN_MAX) { return; }
    app_demo.plan[app_demo.plan_count] = track;
    app_demo.plan_count += 1;
}

static void app_plan_add_selection(void) {
    UI_List *list = &app_demo.list;
    for (u64 i = 0; i < app_demo.filtered_count; i += 1) {
        if (ui_list_selected(list, i)) { app_plan_add(app_demo.filtered[i]); }
    }
}

// --- the scan (T-010) ------------------------------------------------------
// The rows of the list are the live tracks, compacted once per merge: the SoA
// keeps tombstones, the list must not show them.
static void app_rows_rebuild(void) {
    Library *lib = &app_demo.library;
    u32 count = 0;
    for (TrackId id = 0; id < lib->count && count < APP_LIBRARY_COUNT; id += 1) {
        if (lib->flags[id] & LibTrackFlag_Live) {
            app_demo.rows[count] = id;
            count += 1;
        }
    }
    app_demo.row_count = count;
}

static void app_scan_folder(String8 folder) {
    if (folder.size && lib_scan_begin(&app_demo.scan, &app_demo.library, &app_demo.events,
                                      folder)) {
        app_demo.scan_active = 1;
        app_demo.scan_files = 0;
        app_demo.scan_dirs_done = 0;
        app_demo.scan_dirs_total = 0;
        app_demo.root = str8_copy(app_demo.permanent, folder);
    }
}

static void app_scan_start(void) {
    ArenaTemp scratch = scratch_begin(0, 0);
    app_scan_folder(os_dialog_pick_folder(scratch.arena,
                                          str8_lit("Ajouter un dossier de musique")));
    scratch_end(scratch);
}

// "--scan <folder>": the same path the button takes, without the dialog. It is
// how the demo is captured and how a scan is reproduced from a script.
static void app_scan_from_command_line(void) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 args = os_command_line(scratch.arena);
    String8 flag = str8_lit("--scan ");
    u64 at = str8_find(args, flag, 0);
    if (at != args.size) {
        String8 folder = str8_trim(str8_skip(args, at + flag.size));
        if (folder.size && folder.str[0] == '"') {
            folder = str8_skip(folder, 1);
            u64 end = str8_find(folder, str8_lit("\""), 0);
            folder = str8_prefix(folder, end);
        }
        app_scan_folder(folder);
    }
    scratch_end(scratch);
}

// Polled once per frame: it merges what the workers published and returns at
// once, so the frame that starts a 50 000 file scan renders like any other.
static void app_scan_tick(void) {
    if (!app_demo.scan_active) { return; }
    lib_scan_update(&app_demo.scan);
    LibEvent event;
    b32 done = 0;
    while (lib_events_next(&app_demo.events, &event)) {
        if (event.kind == LibEvent_ScanProgress || event.kind == LibEvent_ScanDone) {
            app_demo.scan_files = event.files_seen;
            app_demo.scan_dirs_done = event.dirs_done;
            app_demo.scan_dirs_total = event.dirs_total;
        }
        if (event.kind == LibEvent_ScanDone) { done = 1; }
    }
    if (!done) { return; }
    app_demo.scan_us = app_demo.scan.end_us - app_demo.scan.start_us;
    app_demo.scan_active = 0;
    app_demo.scanned = app_demo.library.live_count > 0;
    if (app_demo.scanned) {
        // Paths need more room than the generated titles did.
        f32 wanted = ui_dp(640.0f);
        app_demo.library_split.size = Max(app_demo.library_split.size, wanted);
    }
    lib_scan_end(&app_demo.scan);
    app_rows_rebuild();
    app_demo.plan_count = 0;
    app_filter();
}

// --- small building blocks -------------------------------------------------
static String8 app_duration(u32 seconds) {
    return str8f(ui_frame_arena(), "%u:%02u", seconds / 60, seconds % 60);
}

md_inline f32 app_cell_padding(void) { return ui_dp(ui_theme()->space[UI_Space_8]); }

// One cell of a list row: a single box, whatever the alignment.
static void app_cell(UI_Size width, String8 text, u32 color, u32 text_flags, UI_TextAlign align) {
    UI_PrefWidth(width)
    UI_PrefHeight(ui_pct(1.0f, 1.0f))
    UI_TextColor(color)
    UI_TextFlags(text_flags)
    UI_TextAlign((u32)align)
    UI_TextPadding(app_cell_padding()) {
        UI_Box *box = ui_build_box_from_key(UI_DrawText, 0);
        box->display_string = text;
    }
}

// Durations, indices and counters: right aligned, tabular figures, so the
// digits line up down the column (research/02 s10.1).
static void app_cell_number(f32 width, String8 text, u32 color) {
    app_cell(ui_px(width, 1.0f), text, color, UI_TextFlag_TabularNumbers, UI_TextAlign_Right);
}

static void app_column_header(String8 title, UI_Size width, UI_TextAlign align) {
    const UI_Theme *theme = ui_theme();
    UI_Font(ui_font(UI_FontStyle_Caption)) {
        app_cell(width, title, theme->fg_disabled, 0, align);
    }
}

// A panel: a titled column with a 1 px border on its inner side.
static UI_Box *app_panel_begin(String8 id, UI_Size width, String8 title, String8 subtitle) {
    const UI_Theme *theme = ui_theme();
    UI_Box *panel = 0;
    UI_PrefWidth(width)
    UI_PrefHeight(ui_pct(1.0f, 1.0f))
    UI_ChildLayoutAxis(Axis2_Y)
    UI_BgColor(theme->panel) {
        panel = ui_build_box(UI_DrawBackground | UI_Clip, id);
    }
    ui_push_parent(panel);

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_comfortable), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *header = ui_build_box_from_key(0, 0);
        UI_Parent(header) UI_TextPadding(ui_dp(theme->space[UI_Space_12])) {
            ui_label_styled(UI_FontStyle_Emphasis, theme->fg_primary, title);
            ui_spacer(ui_pct(1.0f, 0.0f));
            ui_label_styled(UI_FontStyle_Caption, theme->fg_muted, subtitle);
        }
    }
    ui_separator();
    return panel;
}

md_inline void app_panel_end(void) { ui_pop_parent(); }

// --- the three panels ------------------------------------------------------
static void app_library_panel(f32 width) {
    const UI_Theme *theme = ui_theme();
    UI_List *list = &app_demo.list;
    String8 count_label =
        app_demo.scan_active
            ? str8f(ui_frame_arena(), "scan : %u fichiers, %u / %u dossiers", app_demo.scan_files,
                    app_demo.scan_dirs_done, app_demo.scan_dirs_total)
            : str8f(ui_frame_arena(), "%u / %u", app_demo.filtered_count, app_source_count());
    app_panel_begin(str8_lit("###library"), ui_px(width, 1.0f), str8_lit("Bibliotheque"),
                    count_label);

    // Search field.
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_comfortable), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *bar = ui_build_box_from_key(0, 0);
        UI_Parent(bar) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f)) {
                ui_text_input(&app_demo.search, str8_lit("Rechercher un titre, un artiste"));
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
        }
    }
    if (app_demo.search.changed) {
        String8 query = ui_text_input_string(&app_demo.search);
        mem_copy(app_demo.query, query.str, query.size);
        app_demo.query_size = (u32)query.size;
        app_filter();
    }

    // Column headers.
    f32 artist_width = ui_dp(180.0f);
    f32 album_width = app_demo.scanned ? ui_dp(180.0f) : 0.0f;
    f32 duration_width = ui_dp(64.0f);
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->panel) {
        UI_Box *header = ui_build_box_from_key(UI_DrawBackground, 0);
        UI_Parent(header) {
            app_column_header(str8_lit("TITRE"), ui_pct(1.0f, 0.0f), UI_TextAlign_Left);
            app_column_header(str8_lit("ARTISTE"), ui_px(artist_width, 1.0f), UI_TextAlign_Left);
            if (app_demo.scanned) {
                app_column_header(str8_lit("ALBUM"), ui_px(album_width, 1.0f), UI_TextAlign_Left);
            }
            app_column_header(str8_lit("DUREE"), ui_px(duration_width, 1.0f), UI_TextAlign_Right);
        }
    }
    ui_separator();

    // The list itself: 100 000 rows, only the visible ones become boxes.
    ui_list_begin(list, app_demo.filtered_count, ui_dp(theme->row_compact));
    UI_ListEachRow(list, i) {
        AppTrack track = app_track(app_demo.filtered[i]);
        ui_list_row_begin(list, i);
        b32 selected = ui_list_selected(list, i);
        u32 secondary = selected ? theme->fg_primary : theme->fg_secondary;
        app_cell(ui_pct(1.0f, 0.0f), track.title, theme->fg_primary, 0, UI_TextAlign_Left);
        app_cell(ui_px(artist_width, 1.0f), track.artist, secondary, 0, UI_TextAlign_Left);
        if (app_demo.scanned) {
            app_cell(ui_px(album_width, 1.0f), track.album, secondary, 0, UI_TextAlign_Left);
        }
        app_cell_number(duration_width, app_duration(track.duration_s), secondary);
        ui_list_row_end(list);
    }
    ui_list_end(list);

    if (list->context) {
        ui_context_menu_open(&app_demo.menu, list->context_pos, list->context_row);
    }
    if (list->activated) { app_plan_add_selection(); }
    app_panel_end();
}

static void app_plan_panel(void) {
    const UI_Theme *theme = ui_theme();
    u32 total = 0;
    for (u32 i = 0; i < app_demo.plan_count; i += 1) {
        total += app_track(app_demo.plan[i]).duration_s;
    }
    String8 subtitle = str8f(ui_frame_arena(), "%u pistes - %S", app_demo.plan_count,
                             app_duration(total));
    app_panel_begin(str8_lit("###plan"), ui_pct(1.0f, 0.0f), str8_lit("Plan"), subtitle);

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_pct(1.0f, 0.0f))
    UI_ChildLayoutAxis(Axis2_Y)
    UI_BgColor(theme->surface) {
        UI_Box *body = ui_build_box_from_key(UI_DrawBackground | UI_Clip, 0);
        UI_Parent(body) {
            if (app_demo.plan_count == 0) {
                UI_PrefHeight(ui_px(ui_dp(theme->row_comfortable), 1.0f))
                UI_TextPadding(ui_dp(theme->space[UI_Space_12])) {
                    ui_label_styled(UI_FontStyle_Ui, theme->fg_muted,
                                    str8_lit("Entree ou double clic ajoute la selection"));
                }
            }
            for (u32 i = 0; i < app_demo.plan_count; i += 1) {
                AppTrack track = app_track(app_demo.plan[i]);
                UI_Seed(hash64_mix((u64)i + 1))
                UI_PrefWidth(ui_pct(1.0f, 0.0f))
                UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
                UI_ChildLayoutAxis(Axis2_X)
                UI_BgColor(theme->surface) {
                    UI_Box *row = ui_build_box(UI_Clickable | UI_DrawBackground,
                                               str8_lit("###planrow"));
                    UI_Parent(row) {
                        app_cell_number(ui_dp(32.0f), str8f(ui_frame_arena(), "%u", i + 1),
                                        theme->fg_muted);
                        // The mode pastille: colour carries meaning, only here.
                        UI_PrefWidth(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f))
                        UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
                            UI_Box *cell = ui_build_box_from_key(0, 0);
                            UI_Parent(cell)
                            UI_FixedY(ui_dp(7.0f))
                            UI_PrefWidth(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f))
                            UI_PrefHeight(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f))
                            UI_CornerRadius(ui_dp(4.0f))
                            UI_BgColor(theme->mode[track.mode]) {
                                ui_build_box_from_key(UI_FloatingY | UI_DrawBackground, 0);
                            }
                        }
                        app_cell(ui_pct(1.0f, 0.0f), track.title, theme->fg_primary, 0,
                                 UI_TextAlign_Left);
                        app_cell_number(ui_dp(64.0f), app_duration(track.duration_s),
                                        theme->fg_secondary);
                    }
                }
            }
        }
    }
    app_panel_end();
}

// A capacity gauge: one segment per planned track, coloured by its mode.
static void app_disc_panel(f32 width) {
    const UI_Theme *theme = ui_theme();
    f32 capacity_s = 80.0f * 60.0f;
    u32 used = 0;
    for (u32 i = 0; i < app_demo.plan_count; i += 1) {
        used += app_track(app_demo.plan[i]).duration_s;
    }
    String8 subtitle = str8_lit("MZ-N505");
    app_panel_begin(str8_lit("###disc"), ui_px(width, 1.0f), str8_lit("Disque"), subtitle);

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_pct(1.0f, 0.0f))
    UI_ChildLayoutAxis(Axis2_Y)
    UI_BgColor(theme->surface) {
        UI_Box *body = ui_build_box_from_key(UI_DrawBackground | UI_Clip, 0);
        UI_Parent(body) UI_TextPadding(ui_dp(theme->space[UI_Space_12])) {
            ui_label_styled(UI_FontStyle_Emphasis, theme->fg_primary,
                            str8f(ui_frame_arena(), "%S / 80:00", app_duration(used)));

            // The gauge: free space in control, then one segment per track.
            UI_PrefWidth(ui_pct(1.0f, 0.0f))
            UI_PrefHeight(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f))
            UI_ChildLayoutAxis(Axis2_X)
            UI_BgColor(theme->control)
            UI_CornerRadius(ui_dp(theme->space[UI_Space_2])) {
                UI_Box *gauge = ui_build_box_from_key(UI_DrawBackground, 0);
                UI_Parent(gauge) UI_CornerRadius(0.0f) {
                    for (u32 i = 0; i < app_demo.plan_count; i += 1) {
                        AppTrack track = app_track(app_demo.plan[i]);
                        f32 fraction = (f32)track.duration_s / capacity_s;
                        UI_PrefWidth(ui_pct(fraction, 0.0f))
                        UI_PrefHeight(ui_pct(1.0f, 1.0f))
                        UI_BgColor(theme->mode[track.mode]) {
                            ui_build_box_from_key(UI_DrawBackground, 0);
                        }
                    }
                }
            }

            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            static const char *mode_names[UI_Mode_COUNT] = {"SP", "Mono", "LP2", "LP4"};
            for (u32 mode = 0; mode < UI_Mode_COUNT; mode += 1) {
                UI_PrefWidth(ui_pct(1.0f, 0.0f))
                UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
                UI_ChildLayoutAxis(Axis2_X) {
                    UI_Box *row = ui_build_box_from_key(0, 0);
                    UI_Parent(row) {
                        UI_PrefWidth(ui_px(ui_dp(theme->space[UI_Space_24]), 1.0f))
                        UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
                            UI_Box *cell = ui_build_box_from_key(0, 0);
                            UI_Parent(cell)
                            UI_FixedX(ui_dp(theme->space[UI_Space_12]))
                            UI_FixedY(ui_dp(7.0f))
                            UI_PrefWidth(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f))
                            UI_PrefHeight(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f))
                            UI_CornerRadius(ui_dp(4.0f))
                            UI_BgColor(theme->mode[mode]) {
                                ui_build_box_from_key(UI_FloatingX | UI_FloatingY |
                                                          UI_DrawBackground,
                                                      0);
                            }
                        }
                        app_cell(ui_text_size(app_cell_padding(), 1.0f),
                                 str8_cstr(mode_names[mode]), theme->fg_secondary, 0,
                                 UI_TextAlign_Left);
                    }
                }
            }

            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
            UI_PrefWidth(ui_pct(1.0f, 0.0f))
            UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
            UI_ChildLayoutAxis(Axis2_X) {
                UI_Box *row = ui_build_box_from_key(0, 0);
                UI_Parent(row) {
                    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
                    if (ui_button_primary(str8_lit("Graver###burn")).clicked) {
                        app_demo.plan_count = 0;
                    }
                    ui_tooltip(str8_lit("Ecrit le plan sur le disque insere"));
                    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
                    if (ui_button(str8_lit("Vider###clear")).clicked) { app_demo.plan_count = 0; }
                    ui_tooltip(str8_lit("Retire toutes les pistes du plan"));
                }
            }
        }
    }
    app_panel_end();
}

static void app_toolbar(void) {
    const UI_Theme *theme = ui_theme();
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_comfortable), 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->panel) {
        UI_Box *bar = ui_build_box_from_key(UI_DrawBackground, 0);
        UI_Parent(bar) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f)) {
                if (ui_button_icon(R_Icon_Disc, str8_lit("###device")).clicked) {}
                ui_tooltip(str8_lit("Rafraichir l'appareil"));
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
                if (ui_button_icon(R_Icon_Play, str8_lit("###preview")).clicked) {}
                ui_tooltip(str8_lit("Preecouter la selection"));
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
                if (app_demo.scan_active) {
                    if (ui_button(str8_lit("Annuler le scan###folder")).clicked) {
                        lib_scan_cancel(&app_demo.scan);
                    }
                    ui_tooltip(str8_lit("Le scan s'arrete a la fin du dossier courant"));
                } else if (ui_button(str8_lit("Ajouter un dossier###folder")).clicked) {
                    app_scan_start();
                }
                ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
                if (ui_button(str8_lit("Ajouter au plan###add")).clicked) {
                    app_plan_add_selection();
                }
                ui_tooltip(str8_lit("Entree ajoute aussi la selection"));
            }
            ui_spacer(ui_pct(1.0f, 0.0f));
            UI_TextPadding(ui_dp(theme->space[UI_Space_12])) {
                ui_label_styled(UI_FontStyle_Caption, theme->fg_muted,
                                str8f(ui_frame_arena(), "%llu selectionnees",
                                      ui_list_selected_count(&app_demo.list)));
            }
        }
    }
}

static void app_status_bar(void) {
    const UI_Theme *theme = ui_theme();
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->panel)
    UI_Font(ui_font(UI_FontStyle_Caption))
    UI_TextPadding(ui_dp(theme->space[UI_Space_12])) {
        UI_Box *bar = ui_build_box_from_key(UI_DrawBackground, 0);
        UI_Parent(bar) {
            ui_label_styled(UI_FontStyle_Caption, theme->fg_muted,
                            str8f(ui_frame_arena(),
                                  "%u pistes : %llu boxes pour %llu lignes visibles - "
                                  "%llu boxes dans la frame",
                                  app_source_count(), app_demo.list.box_count,
                                  app_demo.list.visible_count, ui_frame_box_count()));
            ui_spacer(ui_pct(1.0f, 0.0f));
            ui_label_styled(UI_FontStyle_Caption, theme->fg_muted,
                            str8_lit("Tab navigue - Ctrl+A tout selectionner - clic droit menu"));
        }
    }
}

static void app_build_ui(f32 scale) {
    const UI_Theme *theme = ui_theme();
    Unused(scale);
    UI_Box *root = ui_root(UI_Layer_Content);
    root->flags |= UI_DrawBackground;
    root->bg_color = theme->canvas;
    root->child_layout_axis = Axis2_Y;

    UI_Parent(root) {
        app_toolbar();
        ui_separator();

        UI_PrefWidth(ui_pct(1.0f, 0.0f))
        UI_PrefHeight(ui_pct(1.0f, 0.0f))
        UI_ChildLayoutAxis(Axis2_X) {
            UI_Box *body = ui_build_box_from_key(0, 0);
            UI_Parent(body) {
                // The body rect is last frame's, which is what the splitters
                // were dragged against anyway.
                f32 total = rect_width(body->rect);
                f32 library_width = ui_splitter_update(&app_demo.library_split, Axis2_X, total);
                f32 disc_width = ui_splitter_update(&app_demo.disc_split, Axis2_X, total);
                app_library_panel(library_width);
                ui_splitter(&app_demo.library_split, Axis2_X);
                app_plan_panel();
                ui_splitter(&app_demo.disc_split, Axis2_X);
                app_disc_panel(disc_width);
            }
        }
        ui_separator();
        app_status_bar();
    }

    if (ui_context_menu_begin(&app_demo.menu)) {
        u64 row = app_demo.menu.payload;
        if (ui_context_menu_item(&app_demo.menu, str8_lit("Ajouter au plan"))) {
            app_plan_add_selection();
        }
        if (ui_context_menu_item(&app_demo.menu, str8_lit("Ajouter cette piste"))) {
            if (row < app_demo.filtered_count) { app_plan_add(app_demo.filtered[row]); }
        }
        ui_context_menu_separator(&app_demo.menu);
        if (ui_context_menu_item(&app_demo.menu, str8_lit("Tout selectionner"))) {
            ui_list_select_all(&app_demo.list);
        }
        if (ui_context_menu_item(&app_demo.menu, str8_lit("Deselectionner"))) {
            ui_list_select_clear(&app_demo.list);
        }
        ui_context_menu_end(&app_demo.menu);
    }
}

static void app_init(Arena *permanent, f32 scale) {
    app_demo.permanent = permanent;
    UI_Theme theme;
    ui_theme_dark(&theme);
    ui_theme_set(&theme);

    app_library_build(permanent, &app_demo.generated, APP_LIBRARY_COUNT);
    app_demo.filtered = push_array(permanent, u32, APP_LIBRARY_COUNT);
    app_demo.rows = push_array(permanent, u32, APP_LIBRARY_COUNT);
    // Two arenas: the columns and the hash slots on one, the interned strings
    // on the other, so the string bytes stay contiguous while the SoA doubles.
    lib_init(&app_demo.library, arena_alloc(MB(512)), arena_alloc(MB(512)));
    u64 selection_words = (APP_LIBRARY_COUNT + 63) / 64;
    ui_list_init(&app_demo.list, push_array_zero(permanent, u64, selection_words),
                 selection_words);
    ui_text_input_init(&app_demo.search, str8_lit(""));
    ui_splitter_init(&app_demo.library_split, 420.0f * scale, 220.0f * scale, 420.0f * scale);
    ui_splitter_init(&app_demo.disc_split, 260.0f * scale, 200.0f * scale, 320.0f * scale);
    app_demo.disc_split.measures_trailing = 1;
    app_filter();
    app_scan_from_command_line();
    app_demo.initialized = 1;
}

static void app_run(void) {
    Arena *permanent = arena_alloc(MB(256));
    Arena *frame_arena = arena_alloc(MB(64));
    os_events_set_frame_arena(frame_arena);
    jobs_init(0);  // one worker per logical core minus this thread
    ui_debug_overlay_set_arenas(permanent, frame_arena);
    OsWindow window = os_window_create(str8_lit("minidisk"), 1200, 720);

    if (!os_gl_init(window) || !r_init(permanent)) {
        os_debug_print(str8_lit("minidisk: OpenGL 3.3 core is required, aborting\n"));
        os_exit(2);
    }
    f32 scale = os_window_dpi_scale(window);
    r_icons_build(frame_arena, (u32)(16.0f * scale));
    if (!os_font_init() || !ui_fonts_build(scale)) {
        os_debug_print(str8_lit("minidisk: no usable system font, aborting\n"));
        os_exit(2);
    }
    ui_text_init(permanent);
    ui_init(permanent);
    app_init(permanent, scale);

    // 256 OsEvent is 24 KB: on the arena, not on the stack (C6262).
    OsEvent *events = push_array(permanent, OsEvent, APP_MAX_EVENTS);
    u64 last_us = os_time_now_us();
    b32 running = 1;
    while (running) {
        // A running scan is the only thing besides an animation that makes the
        // loop wake on its own: at rest the timeout is still infinite (P-005).
        b32 busy = ui_animating() || app_demo.scan_active;
        os_events_pump(1, busy ? 16000 : OS_TIMEOUT_INFINITE);
        app_scan_tick();

        u64 event_count = 0;
        OsEvent event;
        while (os_event_next(&event)) {
            if (event.kind == OsEvent_Close) { running = 0; }
            if (event.kind == OsEvent_KeyDown && event.key == OsKey_F11) {
                ui_debug_overlay_toggle();
            }
            if (event.kind == OsEvent_DpiChanged) {
                r_atlas_reset();
                ui_text_reset();
                scale = event.dpi_scale;
                r_icons_build(frame_arena, (u32)(16.0f * scale));
                ui_fonts_build(scale);
            }
            if (event_count < APP_MAX_EVENTS) {
                events[event_count] = event;
                event_count += 1;
            }
            os_request_redraw();
        }
        if (!running) { break; }

        u64 now_us = os_time_now_us();
        f32 dt = (f32)(now_us - last_us) * 0.000001f;
        last_us = now_us;

        if (os_redraw_requested() || ui_animating() || app_demo.scan_active) {
            V2 size = os_window_get_size(window);
            r_begin_frame(frame_arena, size.x, size.y, scale);
            r_clear(ui_theme()->canvas);
            ui_begin(frame_arena, events, event_count, dt, size, scale);
            app_build_ui(scale);
            ui_debug_overlay_build();
            ui_widgets_end_frame();
            ui_end();
            r_end_frame();
            ui_debug_overlay_end_frame();
        }
        arena_clear(frame_arena);
    }

    jobs_shutdown();
    os_font_shutdown();
    r_shutdown();
    os_gl_shutdown();
    os_window_destroy(window);
    arena_release(frame_arena);
    arena_release(permanent);
}

// view_library.c - the library panel: a search field, a collapsible
// Artist | Album browser, a header row whose columns sort and resize, and a
// virtualized list bound to the sorted index (T-013, research/02 s8.3, s10).
//
// The list holds no data of its own. `app_rows()` is the search's own buffer of
// track ids, in the current sort order; a row reads the fields it draws out of
// the SoA and forgets them. 100 000 rows therefore cost the ids and the thirty
// boxes that are on screen.
#include "app_state.h"

// --- cells, shared by the three panels ---------------------------------------
// --- shared by the plan panel and the disc panel -----------------------------
// The mode names and the one colour helper both views need. They live here
// because view_library.c is the first of the three in the unity build: the plan
// panel prints them beside a planned track and the disc panel beside a track
// that is already on the disc, and they must be the same words and the same
// green in both (T-021).
static const char *app_mode_names[PlanCapMode_COUNT] = {"SP", "MONO", "LP2", "LP4"};
// research/02 s9.9: the initial a wide enough gauge segment carries, so the
// mode is never told by the colour alone.
static const char *app_mode_initials[PlanCapMode_COUNT] = {"S", "M", "2", "4"};

// Premultiplied RGBA8 in, premultiplied out. The theme's mode colours are
// opaque, which is the only case this is asked for.
static u32 app_color_lighten(u32 color, f32 amount) {
    u32 red = color & 0xFF, green = (color >> 8) & 0xFF, blue = (color >> 16) & 0xFF;
    red = (u32)((f32)red + (255.0f - (f32)red) * amount);
    green = (u32)((f32)green + (255.0f - (f32)green) * amount);
    blue = (u32)((f32)blue + (255.0f - (f32)blue) * amount);
    return r_rgba((u8)red, (u8)green, (u8)blue, (u8)((color >> 24) & 0xFF));
}

static String8 app_duration(u32 seconds) {
    return str8f(ui_frame_arena(), "%u:%02u", seconds / 60, seconds % 60);
}

md_inline f32 app_cell_padding(void) { return ui_dp(ui_theme()->space[UI_Space_8]); }

// The cover column is not part of the column model: it is never sorted, never
// resized and never dropped, so it lives beside it.
md_inline f32 app_thumb_column_width(void) { return ui_dp((f32)LIB_COVER_SMALL + 8.0f); }
md_inline f32 app_row_height(void) {
    return app.prefs.thumbnails ? ui_dp((f32)LIB_COVER_SMALL + 8.0f)
                                : ui_dp(ui_theme()->row_compact);
}

// One cell of a row: a single box, whatever the alignment.
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

// Durations, indices, years and counters: right aligned, tabular figures, so
// the digits line up down the column (research/02 s10.3).
static void app_cell_number(f32 width, String8 text, u32 color) {
    app_cell(ui_px(width, 1.0f), text, color, UI_TextFlag_TabularNumbers, UI_TextAlign_Right);
}

// The subtitle box of the panel built last, so a caller can hang a tooltip on
// it without app_panel_begin growing a parameter that three panels out of four
// would pass 0 for (T-071).
global UI_Box *app_panel_subtitle_box;

// A panel: a titled column with its own background.
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
            // Clickable only so that it takes part in the hit test: a tooltip
            // has to know whether the pointer is on it.
            UI_PrefWidth(ui_text_size(ui_top_text_padding(), 1.0f))
            UI_PrefHeight(ui_pct(1.0f, 1.0f))
            UI_Font(ui_font(UI_FontStyle_Caption))
            UI_TextColor(theme->fg_muted) {
                app_panel_subtitle_box =
                    ui_build_box(UI_DrawText | UI_Clickable,
                                 str8f(ui_frame_arena(), "%S_subtitle", id));
                app_panel_subtitle_box->display_string = subtitle;
            }
        }
    }
    ui_separator();
    return panel;
}

md_inline void app_panel_end(void) { ui_pop_parent(); }

// --- the column model ---------------------------------------------------------
StaticAssert(Str_ColumnTitle == Str_ColumnIndex + AppColumn_Title, app_column_strings_aligned);
StaticAssert(Str_ColumnAdded == Str_ColumnIndex + AppColumn_Added, app_column_strings_complete);

#define APP_COLUMN_NOT_SORTABLE U32_MAX

static u32 app_column_sort(AppColumn column) {
    switch (column) {
        case AppColumn_Title:    return LibSort_Title;
        case AppColumn_Artist:   return LibSort_Artist;
        case AppColumn_Album:    return LibSort_Album;
        case AppColumn_Duration: return LibSort_Duration;
        case AppColumn_Added:    return LibSort_Added;
        default:                 return APP_COLUMN_NOT_SORTABLE;
    }
}

// Title takes whatever is left; every other column is the width the user gave
// it. That is why only the others carry a resize grip.
md_inline b32 app_column_flexible(AppColumn column) { return column == AppColumn_Title; }

md_inline UI_Size app_column_width(AppColumn column) {
    return ui_px(app.column_px[column], 1.0f);
}

md_inline UI_TextAlign app_column_align(AppColumn column) {
    b32 right = (column == AppColumn_Index || column == AppColumn_Duration ||
                 column == AppColumn_Year || column == AppColumn_Added);
    return right ? UI_TextAlign_Right : UI_TextAlign_Left;
}

// The column drawn at `slot` of the header row; AppColumn_COUNT when the
// preferences left that slot empty, which prefs_parse already made impossible.
static AppColumn app_column_at(u32 slot) {
    for (u32 i = 0; i < AppColumn_COUNT; i += 1) {
        if (app.prefs.columns[i].order == slot) { return (AppColumn)i; }
    }
    return AppColumn_COUNT;
}

// A title narrower than this says nothing, so it is the one width the panel
// refuses to give away.
#define APP_TITLE_MIN_DP 120.0f

// When the panel is too narrow for every column, columns leave in this order.
// It is the reverse of how much they say about a track, and it is what
// research/02 s8.7 asks for (Album goes first under 1400 px). The preferences
// are not touched: this is the panel being narrow, not the user hiding a column.
static const AppColumn app_column_drop_order[] = {
    AppColumn_Added, AppColumn_Year, AppColumn_Format, AppColumn_Album, AppColumn_Duration,
    AppColumn_Index,
};

// What each column really gets this frame. The widths the user dragged are used
// as they are while the panel is wide enough for them; when it is not, columns
// are dropped from the right of that list, and only then does the rest shrink.
static void app_columns_measure(f32 panel_width) {
    f32 available = max_f32(panel_width - ui_dp(ui_theme()->scrollbar_width), 0.0f);
    if (app.prefs.thumbnails) { available = max_f32(available - app_thumb_column_width(), 0.0f); }
    f32 title_min = ui_dp(APP_TITLE_MIN_DP);
    f32 fixed = 0.0f;
    for (u32 i = 0; i < AppColumn_COUNT; i += 1) {
        b32 counts = app.prefs.columns[i].visible && !app_column_flexible((AppColumn)i);
        app.column_px[i] = counts ? ui_dp(app.prefs.columns[i].width) : 0.0f;
        fixed += app.column_px[i];
    }
    for (u32 i = 0; i < ArrayCount(app_column_drop_order) && available - fixed < title_min;
         i += 1) {
        AppColumn column = app_column_drop_order[i];
        fixed -= app.column_px[column];
        app.column_px[column] = 0.0f;
    }
    f32 title = available - fixed;
    if (title < title_min && fixed > 0.0f) {
        f32 factor = max_f32(available - title_min, 0.0f) / fixed;
        for (u32 i = 0; i < AppColumn_COUNT; i += 1) { app.column_px[i] *= factor; }
        title = title_min;
    }
    app.column_px[AppColumn_Title] = title;
}

// One header: clickable when the index can sort on it, and carrying a grip on
// its right edge that drags its width. The width lands in the preferences, so
// it is still there at the next launch.
static void app_column_header(AppColumn column) {
    const UI_Theme *theme = ui_theme();
    u32 sort = app_column_sort(column);
    b32 sortable = app.index_ready && sort != APP_COLUMN_NOT_SORTABLE;
    b32 active = sortable && app.prefs.sort_column == sort;
    String8 label = app_str((Str)(Str_ColumnIndex + column));
    if (active) {
        label = str8f(ui_frame_arena(), "%S %s", label,
                      app.prefs.sort_desc ? "\xE2\x96\xBE" : "\xE2\x96\xB4");
    }

    UI_Box *box = 0;
    UI_Font(ui_font(UI_FontStyle_Caption))
    UI_PrefWidth(app_column_width(column))
    UI_PrefHeight(ui_pct(1.0f, 1.0f))
    UI_TextColor(active ? theme->fg_primary : theme->fg_disabled)
    UI_TextAlign((u32)app_column_align(column))
    UI_TextPadding(app_cell_padding()) {
        box = ui_build_box(UI_DrawText | UI_Clickable,
                           str8f(ui_frame_arena(), "###head%u", column));
        box->display_string = label;
    }
    UI_Signal head = ui_signal(box);
    if (sortable && head.clicked) { app_sort_by(sort); }
    // Right click anywhere on the header row: which columns are shown.
    if (head.right_clicked) { ui_context_menu_open(&app.header_menu, ui_mouse(), 0); }
    if (app_column_flexible(column)) { return; }

    // The grip floats over the right edge of the header, so the header row and
    // the list rows keep exactly the same cell widths.
    f32 grip = ui_dp(6.0f);
    UI_Box *handle = 0;
    UI_Parent(box)
    UI_FixedX(rect_width(box->rect) - grip * 0.5f)
    UI_FixedY(0.0f)
    UI_PrefWidth(ui_px(grip, 1.0f))
    UI_PrefHeight(ui_pct(1.0f, 1.0f))
    UI_BgColor(theme->border_hover)
    UI_CornerRadius(0.0f) {
        handle = ui_build_box(UI_Clickable | UI_FloatingX | UI_FloatingY,
                              str8f(ui_frame_arena(), "###grip%u", column));
    }
    UI_Signal signal = ui_signal(handle);
    if (signal.hovering || signal.dragging) {
        ui_cursor_request(OsCursor_ResizeH);
        handle->flags |= UI_DrawBackground;
    }
    if (signal.pressed) {
        app.header_drag = (u32)column + 1;
        app.header_drag_origin = app.prefs.columns[column].width;
    }
    if (signal.dragging && app.header_drag == (u32)column + 1) {
        f32 width = app.header_drag_origin + signal.drag_delta.x / ui_dpi_scale();
        app.prefs.columns[column].width = clamp_f32(width, PREFS_COLUMN_MIN, PREFS_COLUMN_MAX);
        app.prefs_dirty = 1;
    }
    if (signal.double_clicked) { app.header_drag = 0; }
}


// --- covers ------------------------------------------------------------------
// The view states what it wants and takes what is there: a thumbnail that is
// not decoded yet is a request and an empty cell, never a wait. The uploads are
// capped per frame so a scroll over a cold library stays a scroll - at 16 per
// frame a full screen of rows is complete in two frames.
#define APP_COVER_UPLOADS_PER_FRAME 16

static u32 app_cover_uploads;

void app_covers_begin_frame(void) { app_cover_uploads = 0; }

b32 app_cover_rect(TrackId id, u32 size, R_AtlasRect *out) {
    Library *lib = &app.library;
    String8 path = lib_track_path(lib, id);
    u64 key = lib_cover_key(lib->cover_hash[id], os_path_parent(path));
    if (key == 0) { return 0; }
    if (r_thumbs_lookup(key, size, out)) { return 1; }

    u32 state = lib_covers_state(&app.covers, key);
    if (state == LibCoverState_Missing) {
        // A cover already in the disk cache is ready the instant it is asked
        // for, so the state is read again: the row draws in this very frame
        // rather than waiting for whatever wakes the loop next.
        lib_covers_request(&app.covers, key, path, lib->size[id]);
        state = lib_covers_state(&app.covers, key);
    }
    if (state != LibCoverState_Ready) { return 0; }
    if (app_cover_uploads >= APP_COVER_UPLOADS_PER_FRAME) {
        os_request_redraw();  // the rest of them lands in the next frame
        return 0;
    }
    OsFileMap map;
    if (!lib_covers_open(&app.covers, key, &map)) { return 0; }
    *out = r_thumbs_add(key, size, lib_cover_pixels(&map, size));
    os_file_unmap(&map);
    app_cover_uploads += 1;
    return 1;
}

// The cover cell of a row: the image when it is there, the empty plate of the
// theme when it is not, so the column never jumps as thumbnails arrive.
static void app_cell_cover(f32 width, TrackId id) {
    const UI_Theme *theme = ui_theme();
    f32 side = ui_dp((f32)LIB_COVER_SMALL);
    R_AtlasRect thumb;
    StructZero(&thumb);
    b32 ready = app_cover_rect(id, LIB_COVER_SMALL, &thumb);
    UI_PrefWidth(ui_px(width, 1.0f))
    UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
        UI_Box *cell = ui_build_box_from_key(0, 0);
        UI_Parent(cell)
        UI_FixedX(round_f32((width - side) * 0.5f))
        UI_FixedY(round_f32((app_row_height() - side) * 0.5f))
        UI_PrefWidth(ui_px(side, 1.0f))
        UI_PrefHeight(ui_px(side, 1.0f))
        UI_BgColor(theme->control)
        UI_CornerRadius(ui_dp(3.0f)) {
            UI_Flags flags = UI_FloatingX | UI_FloatingY |
                             (ready ? (UI_Flags)UI_DrawImage : (UI_Flags)UI_DrawBackground);
            UI_Box *image = ui_build_box_from_key(flags, 0);
            if (ready) {
                image->image_x = thumb.x;
                image->image_y = thumb.y;
                image->image_size = thumb.width;
            }
        }
    }
}

// --- one row of the list -------------------------------------------------------
static const char *app_codec_names[LibCodec_COUNT] = {
    "", "MP3", "FLAC", "WAV", "AIFF", "OGG", "M4A", "AAC", "ALAC", "WMA", "OPUS",
};

// A codec badge, "FLAC 44.1": the only place the row leaves the text baseline.
static void app_cell_format(f32 width, u8 codec, u32 sample_rate) {
    const UI_Theme *theme = ui_theme();
    if (codec == (u8)LibCodec_Unknown) {
        app_cell(ui_px(width, 1.0f), str8_lit(""), theme->fg_muted, 0, UI_TextAlign_Left);
        return;
    }
    String8 text = (sample_rate != 0)
                       ? str8f(ui_frame_arena(), "%s %u.%u", app_codec_names[codec],
                               sample_rate / 1000, (sample_rate % 1000) / 100)
                       : str8_cstr(app_codec_names[codec]);
    f32 badge_height = ui_dp(15.0f);
    f32 row_height = app_row_height();
    UI_PrefWidth(ui_px(width, 1.0f))
    UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
        UI_Box *cell = ui_build_box_from_key(0, 0);
        UI_Parent(cell)
        UI_Font(ui_font(UI_FontStyle_Caption))
        UI_FixedX(ui_dp(theme->space[UI_Space_6]))
        UI_FixedY(round_f32((row_height - badge_height) * 0.5f))
        UI_PrefWidth(ui_px(width - ui_dp(theme->space[UI_Space_12]), 1.0f))
        UI_PrefHeight(ui_px(badge_height, 1.0f))
        UI_BgColor(theme->control)
        UI_TextColor(theme->fg_secondary)
        UI_TextAlign(UI_TextAlign_Center)
        UI_TextPadding(0.0f)
        UI_CornerRadius(ui_dp(3.0f)) {
            UI_Box *badge = ui_build_box_from_key(
                UI_FloatingX | UI_FloatingY | UI_DrawBackground | UI_DrawText, 0);
            badge->display_string = text;
        }
    }
}

// Unix microseconds -> "2026-09-07". Civil calendar from days, no CRT, no table.
static String8 app_date(u64 mtime_us) {
    if (mtime_us == 0) { return str8_lit(""); }
    i64 z = (i64)(mtime_us / 86400000000ull) + 719468;
    i64 era = (z >= 0 ? z : z - 146096) / 146097;
    i64 day_of_era = z - era * 146097;
    i64 year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    i64 year = year_of_era + era * 400;
    i64 day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    i64 mp = (5 * day_of_year + 2) / 153;
    i64 day = day_of_year - (153 * mp + 2) / 5 + 1;
    i64 month = mp + ((mp < 10) ? 3 : -9);
    if (month <= 2) { year += 1; }
    return str8f(ui_frame_arena(), "%u-%02u-%02u", (u32)year, (u32)month, (u32)day);
}

static void app_library_row(u64 row, TrackId id, b32 selected) {
    const UI_Theme *theme = ui_theme();
    AppTrack track = app_track(id);
    u32 secondary = selected ? theme->fg_primary : theme->fg_secondary;
    if (app.prefs.thumbnails) { app_cell_cover(app_thumb_column_width(), id); }
    for (u32 slot = 0; slot < AppColumn_COUNT; slot += 1) {
        AppColumn column = app_column_at(slot);
        if (app.column_px[column] <= 0.0f) { continue; }
        UI_Size width = app_column_width(column);
        switch (column) {
            case AppColumn_Index: {
                app_cell(width, str8f(ui_frame_arena(), "%llu", row + 1), theme->fg_muted,
                         UI_TextFlag_TabularNumbers, UI_TextAlign_Right);
            } break;
            case AppColumn_Title: {
                app_cell(width, track.title, theme->fg_primary, 0, UI_TextAlign_Left);
            } break;
            case AppColumn_Artist: {
                app_cell(width, track.artist, secondary, 0, UI_TextAlign_Left);
            } break;
            case AppColumn_Album: {
                app_cell(width, track.album, secondary, 0, UI_TextAlign_Left);
            } break;
            case AppColumn_Duration: {
                app_cell(width, app_duration(track.duration_s), secondary,
                         UI_TextFlag_TabularNumbers, UI_TextAlign_Right);
            } break;
            case AppColumn_Format: {
                app_cell_format(app.column_px[column], track.codec, track.sample_rate);
            } break;
            case AppColumn_Year: {
                String8 year = track.year ? str8f(ui_frame_arena(), "%u", track.year)
                                          : str8_lit("");
                app_cell(width, year, secondary, UI_TextFlag_TabularNumbers, UI_TextAlign_Right);
            } break;
            case AppColumn_Added: {
                app_cell(width, app_date(track.mtime_us), theme->fg_muted,
                         UI_TextFlag_TabularNumbers, UI_TextAlign_Right);
            } break;
            default: break;
        }
    }
}

// --- the Artist -> Album column browser -----------------------------------------
typedef struct AppBrowserClick {
    b32 hit;
    u32 group;  // LIB_GROUP_ALL when the "everything" row was clicked
} AppBrowserClick;

// One column: a virtualized list whose row 0 stands for "no filter", then one
// row per group with the number of tracks behind it.
static AppBrowserClick app_browser_column(UI_List *list, String8 id, String8 title,
                                          String8 all_label, u32 all_count,
                                          const LibGroup *groups, u32 count, u32 selected) {
    const UI_Theme *theme = ui_theme();
    AppBrowserClick result;
    result.hit = 0;
    result.group = LIB_GROUP_ALL;
    f32 row_height = ui_dp(theme->row_compact);
    f32 count_width = ui_dp(32.0f);
    list->cursor = (selected == LIB_GROUP_ALL) ? 0 : selected + 1;
    list->has_cursor = 1;

    UI_PrefWidth(ui_pct(0.5f, 0.0f))
    UI_PrefHeight(ui_pct(1.0f, 1.0f))
    UI_ChildLayoutAxis(Axis2_Y) {
        UI_Box *column = ui_build_box(0, id);
        UI_Parent(column) {
            UI_PrefWidth(ui_pct(1.0f, 0.0f))
            UI_PrefHeight(ui_px(row_height, 1.0f))
            UI_ChildLayoutAxis(Axis2_X)
            UI_BgColor(theme->panel) {
                UI_Box *head = ui_build_box_from_key(UI_DrawBackground, 0);
                UI_Parent(head)
                UI_Font(ui_font(UI_FontStyle_Caption)) {
                    app_cell(ui_pct(1.0f, 0.0f), title, theme->fg_disabled, 0, UI_TextAlign_Left);
                }
            }
            ui_separator();
            ui_list_begin(list, (u64)count + 1, row_height);
            UI_ListEachRow(list, i) {
                UI_Signal signal = ui_list_row_begin(list, i);
                b32 selected_row = ui_list_selected(list, i);
                u32 color = selected_row ? theme->fg_primary : theme->fg_secondary;
                String8 name = all_label;
                u32 tracks = all_count;
                if (i != 0) {
                    name = lib_string(&app.library.strings, groups[i - 1].name);
                    tracks = groups[i - 1].count;
                    if (name.size == 0) { name = app_str(Str_BrowserUnnamed); }
                }
                app_cell(ui_pct(1.0f, 0.0f), name, color, 0, UI_TextAlign_Left);
                app_cell_number(count_width, str8f(ui_frame_arena(), "%u", tracks),
                                theme->fg_muted);
                // L1: the counter used to end two pixels from the edge, under
                // the scrollbar. The gutter is part of the row, not of the
                // number.
                ui_spacer(ui_px(ui_dp(theme->scrollbar_width_hover), 1.0f));
                ui_list_row_end(list);
                if (signal.clicked) {
                    result.hit = 1;
                    result.group = (i == 0) ? LIB_GROUP_ALL : (u32)(i - 1);
                }
            }
            ui_list_end(list);
        }
    }
    return result;
}

// The height of the library panel last frame: what the two vertical splitters
// clamp against. Zero on the first frame, which ui_splitter_update accepts.
static f32 app_library_panel_h;

// A vertical splitter dragged to a new height is a preference: stored in dp so
// a DPI change does not shrink it (same rule as the window size).
static void app_split_remember(UI_Splitter *split, f32 before, u32 *pref) {
    if (split->size == before) { return; }
    *pref = (u32)(split->size / ui_dpi_scale() + 0.5f);
    app.prefs_dirty = 1;
}

// What the column spends whatever the two handles are dragged to: the panel
// header, the search row, the two collapse bars, the column headers, their four
// separators and the two handles. What is left over is what the browser, the
// track list and the detail panel share.
static f32 app_library_chrome_h(void) {
    const UI_Theme *theme = ui_theme();
    return ui_dp(2.0f * theme->row_comfortable + 2.0f * theme->row_standard +
                 theme->row_compact + 4.0f + 2.0f * theme->splitter_size);
}

// The two vertical handles clamped *together*: each one alone knows nothing of
// the other, so 190 dp of browser over 230 dp of detail left the track list its
// header and nothing else in a 700 dp window. The list keeps APP_MIN_LIST_DP;
// the detail gives way first, then the browser, each down to its own minimum. A
// zone folded away asks for nothing and gets nothing (T-075 revue).
static void app_library_split_fit(f32 *browser_h, f32 *detail_h) {
    f32 scale = ui_dpi_scale();
    f32 min_browser = app.prefs.browser_collapsed ? 0.0f : APP_MIN_BROWSER_DP * scale;
    f32 min_detail = app.prefs.detail_collapsed ? 0.0f : APP_MIN_DETAIL_DP * scale;
    ui_split_fit_middle(app_library_panel_h - app_library_chrome_h(), APP_MIN_LIST_DP * scale,
                        browser_h, min_browser, detail_h, min_detail);
}

static void app_browser_panel(void) {
    const UI_Theme *theme = ui_theme();
    LibBrowser *browser = &app.browser;

    // The collapse bar stays even when the columns are folded away: it is the
    // only way back, and it says which filter is still applied.
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->panel) {
        UI_Box *bar = ui_build_box_from_key(UI_DrawBackground, 0);
        UI_Parent(bar) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
            UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f)) {
                R_Icon icon = app.prefs.browser_collapsed ? R_Icon_ChevronRight : R_Icon_ChevronDown;
                if (ui_button_icon(icon, str8_lit("###browsertoggle")).clicked) {
                    app.prefs.browser_collapsed = !app.prefs.browser_collapsed;
                    app.prefs_dirty = 1;
                }
                ui_tooltip(app_str(app.prefs.browser_collapsed ? Str_BrowserExpand
                                                               : Str_BrowserCollapse));
            }
            UI_Font(ui_font(UI_FontStyle_Caption))
            UI_TextPadding(app_cell_padding()) {
                String8 label = str8f(ui_frame_arena(), "%S | %S", app_str(Str_BrowserArtists),
                                      app_str(Str_BrowserAlbums));
                ui_label_styled(UI_FontStyle_Caption, theme->fg_disabled, label);
            }
        }
    }
    ui_separator();
    if (app.prefs.browser_collapsed) { return; }

    // "every album" counts the tracks of the artist column's selection, which
    // is the whole library when no artist is picked.
    u32 album_total = (browser->selected_artist < browser->artist_count)
                          ? browser->artists[browser->selected_artist].count
                          : app.index.live_count;
    f32 before = app.browser_split.size;
    f32 browser_h = ui_splitter_update(&app.browser_split, Axis2_Y, app_library_panel_h);
    app_split_remember(&app.browser_split, before, &app.prefs.browser_height);
    f32 detail_h = app.prefs.detail_collapsed ? 0.0f : app.detail_split.size;
    app_library_split_fit(&browser_h, &detail_h);
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(browser_h, 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->surface) {
        UI_Box *row = ui_build_box(UI_DrawBackground | UI_Clip, str8_lit("###browser"));
        UI_Parent(row) {
            AppBrowserClick artist = app_browser_column(
                &app.artist_list, str8_lit("###artists"), app_str(Str_BrowserArtists),
                app_str(Str_BrowserAllArtists), app.index.live_count, browser->artists,
                browser->artist_count, browser->selected_artist);
            ui_separator();
            AppBrowserClick album = app_browser_column(
                &app.album_list, str8_lit("###albums"), app_str(Str_BrowserAlbums),
                app_str(Str_BrowserAllAlbums), album_total, browser->albums,
                browser->album_count, browser->selected_album);
            if (artist.hit) {
                lib_browser_select_artist(browser, &app.index, artist.group);
                app_filter();
            } else if (album.hit) {
                lib_browser_select_album(browser, album.group);
                app_filter();
            }
        }
    }
    // The handle is the separator: drag it to give the columns more rows.
    ui_splitter(&app.browser_split, Axis2_Y);
}

// --- the three states that are not a list ----------------------------------------
// A centred stack of text with one action underneath: the shape every empty
// state of the app takes (research/02 s8.5.4 - an empty state always proposes).
static void app_centered_begin(UI_Box **out_body) {
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_pct(1.0f, 0.0f))
    UI_ChildLayoutAxis(Axis2_Y)
    UI_BgColor(ui_theme()->surface) {
        *out_body = ui_build_box_from_key(UI_DrawBackground | UI_Clip, 0);
    }
    ui_push_parent(*out_body);
    ui_spacer(ui_pct(0.34f, 0.0f));
}

static void app_centered_line(UI_FontStyle style, u32 color, String8 text) {
    const UI_Theme *theme = ui_theme();
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_TextAlign(UI_TextAlign_Center)
    UI_Font(ui_font(style))
    UI_TextColor(color)
    UI_TextPadding(ui_dp(theme->space[UI_Space_12])) {
        UI_Box *box = ui_build_box_from_key(UI_DrawText, 0);
        box->display_string = text;
    }
}

static void app_library_empty_state(void) {
    const UI_Theme *theme = ui_theme();
    UI_Box *body = 0;
    app_centered_begin(&body);
    app_centered_line(UI_FontStyle_Heading, theme->fg_primary, app_str(Str_LibraryEmptyTitle));
    app_centered_line(UI_FontStyle_Ui, theme->fg_secondary, app_str(Str_LibraryEmptyBody));
    app_centered_line(UI_FontStyle_Ui, theme->fg_secondary, app_str(Str_LibraryEmptyBody2));
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_16]), 1.0f));

    // The action, big enough to be the obvious thing to do in an empty panel.
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_comfortable), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *row = ui_build_box_from_key(0, 0);
        UI_Parent(row) {
            ui_spacer(ui_pct(1.0f, 0.0f));
            UI_PrefHeight(ui_px(ui_dp(36.0f), 1.0f))
            UI_Font(ui_font(UI_FontStyle_Emphasis)) {
                if (ui_button_primary(str8f(ui_frame_arena(), "%S###addfolder",
                                            app_str(Str_LibraryEmptyAction)))
                        .clicked) {
                    app_scan_start();
                }
            }
            ui_spacer(ui_pct(1.0f, 0.0f));
        }
    }
    ui_spacer(ui_px(ui_dp(theme->space[UI_Space_12]), 1.0f));
    app_centered_line(UI_FontStyle_Caption, theme->fg_muted, app_str(Str_LibraryEmptyDrop));
    ui_pop_parent();
}

static void app_library_no_results(void) {
    const UI_Theme *theme = ui_theme();
    UI_Box *body = 0;
    app_centered_begin(&body);
    String8 title = str8f(ui_frame_arena(), app_str_c(Str_LibraryNoResultTitle),
                          str8(app.query, app.query_size));
    app_centered_line(UI_FontStyle_Emphasis, theme->fg_primary, title);
    app_centered_line(UI_FontStyle_Ui, theme->fg_muted, app_str(Str_LibraryNoResultBody));
    ui_pop_parent();
}

// A 2 dp bar at the top of the list while a scan runs: it says "something is
// happening here" without taking a pixel from the rows (research/02 s8.5.4).
static void app_scan_progress(void) {
    const UI_Theme *theme = ui_theme();
    f32 fraction = (app.scan_dirs_total != 0)
                       ? (f32)app.scan_dirs_done / (f32)app.scan_dirs_total
                       : 0.0f;
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->space[UI_Space_2]), 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->control)
    UI_CornerRadius(0.0f) {
        UI_Box *track = ui_build_box_from_key(UI_DrawBackground, 0);
        UI_Parent(track)
        UI_PrefWidth(ui_pct(clamp_f32(fraction, 0.02f, 1.0f), 1.0f))
        UI_PrefHeight(ui_pct(1.0f, 1.0f))
        UI_BgColor(theme->accent) {
            ui_build_box_from_key(UI_DrawBackground, 0);
        }
    }
}


// --- the detail panel ---------------------------------------------------------
// The bottom of the library panel, collapsible: the 256 px cover and the facts
// a track has. It reads the cursor row, which is where the keyboard and the
// last click agree the selection is.
static String8 app_bytes_human(u64 bytes) {
    // Under a megabyte the decimal says nothing: kilobytes do.
    if (bytes < MB(1)) {
        return str8f(ui_frame_arena(), app_str_c(Str_DetailSizeKb), (u32)((bytes + 512) >> 10));
    }
    u64 tenths = (bytes * 10 + (1 << 19)) >> 20;  // MiB, one decimal, rounded
    return str8f(ui_frame_arena(), app_str_c(Str_DetailSize), (u32)(tenths / 10),
                 (u32)(tenths % 10));
}

// A path shown whole at both ends: the drive says where, the file name says
// what, and the middle is what goes. Binary search on how much each end keeps.
static String8 app_path_ellipsis(String8 path, OsFont font, f32 max_width) {
    if (path.size == 0 || ui_text_width(font, path, 0) <= max_width) { return path; }
    u64 low = 0;
    u64 high = path.size / 2;
    String8 best = str8_lit("...");
    while (low <= high) {
        u64 keep = (low + high) / 2;
        u64 head = keep;
        u64 tail = keep;
        // Never cut a codepoint in half: back up to the lead byte.
        while (head > 0 && (path.str[head] & 0xC0) == 0x80) { head -= 1; }
        while (tail > 0 && (path.str[path.size - tail] & 0xC0) == 0x80) { tail -= 1; }
        String8 candidate = str8f(ui_frame_arena(), "%S...%S", str8_prefix(path, head),
                                  str8_skip(path, path.size - tail));
        if (ui_text_width(font, candidate, 0) <= max_width) {
            best = candidate;
            low = keep + 1;
        } else {
            if (keep == 0) { break; }
            high = keep - 1;
        }
    }
    return best;
}

static void app_detail_line(String8 text, UI_FontStyle style, u32 color) {
    const UI_Theme *theme = ui_theme();
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
    UI_Font(ui_font(style))
    UI_TextColor(color)
    UI_TextPadding(0.0f) {
        UI_Box *box = ui_build_box_from_key(UI_DrawText, 0);
        box->display_string = text;
    }
}

static void app_detail_facts(UI_Box *facts, TrackId id, f32 padding) {
    const UI_Theme *theme = ui_theme();
    AppTrack track = app_track(id);
    UI_Parent(facts) {
        ui_spacer(ui_px(padding, 1.0f));
        app_detail_line(track.title, UI_FontStyle_Heading, theme->fg_primary);
        app_detail_line(track.artist, UI_FontStyle_Emphasis, theme->fg_secondary);
        String8 album = track.year ? str8f(ui_frame_arena(), "%S (%u)", track.album, track.year)
                                   : track.album;
        app_detail_line(album, UI_FontStyle_Ui, theme->fg_secondary);
        String8 format = str8f(ui_frame_arena(), "%s %u.%u kHz \xC2\xB7 %S \xC2\xB7 %S",
                               app_codec_names[track.codec], track.sample_rate / 1000,
                               (track.sample_rate % 1000) / 100, app_duration(track.duration_s),
                               app_bytes_human(app.library.size[id]));
        app_detail_line(format, UI_FontStyle_Caption, theme->fg_muted);
        f32 text_width = max_f32(rect_width(facts->rect) - padding, ui_dp(80.0f));
        String8 path = app_path_ellipsis(lib_track_path(&app.library, id),
                                         ui_font(UI_FontStyle_Caption), text_width);
        app_detail_line(path, UI_FontStyle_Caption, theme->fg_disabled);
    }
}

static void app_detail_panel(f32 width) {
    const UI_Theme *theme = ui_theme();
    // Collapsed: a plain separator and the toggle bar. Open: the handle sits
    // between the list and the bar, and its drag sets the panel height.
    f32 detail_h = app.detail_split.size;
    if (app.prefs.detail_collapsed) {
        ui_separator();
    } else {
        f32 before = app.detail_split.size;
        detail_h = ui_splitter_update(&app.detail_split, Axis2_Y, app_library_panel_h);
        app_split_remember(&app.detail_split, before, &app.prefs.detail_height);
        f32 browser_h = app.prefs.browser_collapsed ? 0.0f : app.browser_split.size;
        app_library_split_fit(&browser_h, &detail_h);
        ui_splitter(&app.detail_split, Axis2_Y);
    }
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->panel) {
        UI_Box *bar = ui_build_box_from_key(UI_DrawBackground, 0);
        UI_Parent(bar) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
            UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f)) {
                R_Icon icon = app.prefs.detail_collapsed ? R_Icon_ChevronRight : R_Icon_ChevronDown;
                if (ui_button_icon(icon, str8_lit("###detailtoggle")).clicked) {
                    app.prefs.detail_collapsed = !app.prefs.detail_collapsed;
                    app.prefs_dirty = 1;
                }
                ui_tooltip(app_str(app.prefs.detail_collapsed ? Str_DetailShow : Str_DetailHide));
            }
            UI_Font(ui_font(UI_FontStyle_Caption))
            UI_TextPadding(app_cell_padding()) {
                ui_label_styled(UI_FontStyle_Caption, theme->fg_disabled,
                                app_str(Str_DetailTitle));
            }
        }
    }
    if (app.prefs.detail_collapsed) { return; }

    ui_separator();
    f32 padding = ui_dp(theme->space[UI_Space_12]);
    // 256 *physical* pixels, not 256 dp: that is the size the cache holds, and
    // one texel per pixel is the only way an image is ever sharp. A narrow
    // panel shrinks it rather than pushing the facts off screen.
    // ...and so does a short panel: the cover shrinks to the height the user
    // dragged, never below 64 px.
    // C2: 250 px of cover over a list showing two tracks was the wrong half of
    // the panel. 160 is the size the detail block is now laid out for; a wider
    // panel does not grow it any further.
    f32 side = min_f32(ui_dp(160.0f),
                       max_f32(min_f32(width * 0.4f - padding * 2.0f, detail_h - padding * 2.0f),
                               64.0f));
    // Nothing clicked yet: the first row of the current sort is what the panel
    // is about, which is also what the user is looking at.
    b32 has_row = app.index_ready && app_row_count() > 0;
    u64 row = (app.list.has_cursor && app.list.cursor < app_row_count()) ? app.list.cursor : 0;
    TrackId id = has_row ? app_rows()[row] : LIB_TRACK_NONE;
    R_AtlasRect thumb;
    StructZero(&thumb);
    b32 ready = has_row && app_cover_rect(id, LIB_COVER_LARGE, &thumb);

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(detail_h, 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->surface) {
        UI_Box *body = ui_build_box(UI_DrawBackground | UI_Clip, str8_lit("###detail"));
        UI_Parent(body) {
            ui_spacer(ui_px(padding, 1.0f));
            UI_PrefWidth(ui_px(side, 1.0f))
            UI_PrefHeight(ui_pct(1.0f, 1.0f)) {
                UI_Box *slot = ui_build_box_from_key(0, 0);
                UI_Parent(slot)
                UI_FixedY(padding)
                UI_PrefWidth(ui_px(side, 1.0f))
                UI_PrefHeight(ui_px(side, 1.0f))
                UI_BgColor(theme->control)
                UI_TextColor(theme->fg_muted)
                UI_TextAlign(UI_TextAlign_Center)
                UI_Font(ui_font(UI_FontStyle_Caption))
                UI_CornerRadius(ui_dp(4.0f)) {
                    UI_Flags flags =
                        UI_FloatingY | (ready ? (UI_Flags)UI_DrawImage
                                              : (UI_Flags)(UI_DrawBackground | UI_DrawText));
                    UI_Box *image = ui_build_box_from_key(flags, 0);
                    if (ready) {
                        image->image_x = thumb.x;
                        image->image_y = thumb.y;
                        image->image_size = thumb.width;
                    } else {
                        image->display_string =
                            app_str(has_row ? Str_DetailNoCover : Str_DetailEmpty);
                    }
                }
            }
            ui_spacer(ui_px(padding, 1.0f));
            UI_PrefWidth(ui_pct(1.0f, 0.0f))
            UI_PrefHeight(ui_pct(1.0f, 1.0f))
            UI_ChildLayoutAxis(Axis2_Y) {
                UI_Box *facts = ui_build_box(UI_Clip, str8_lit("###detailfacts"));
                if (has_row) { app_detail_facts(facts, id, padding); }
            }
        }
    }
}

// While the Explorer holds a drag over the panel, the panel says so: an accent
// ring and the one sentence that tells the user what letting go does.
static void app_drop_overlay(UI_Box *panel) {
    const UI_Theme *theme = ui_theme();
    if (!app.drag_active || !rect_contains(panel->rect, app.drag_pos)) { return; }
    // Built last inside the panel: within a layer the order of the calls is the
    // order of the drawing, so the ring lands over the rows without a layer.
    UI_Parent(panel)
    UI_FixedX(0.0f)
    UI_FixedY(0.0f)
    UI_PrefWidth(ui_px(rect_width(panel->rect), 1.0f))
    UI_PrefHeight(ui_px(rect_height(panel->rect), 1.0f))
    UI_BorderColor(theme->accent)
    UI_BorderThickness(ui_dp(2.0f))
    UI_TextColor(theme->fg_primary)
    UI_TextAlign(UI_TextAlign_Center)
    UI_Font(ui_font(UI_FontStyle_Emphasis))
    UI_TextPadding(0.0f) {
        UI_Box *ring = ui_build_box(UI_FloatingX | UI_FloatingY | UI_DrawBorder | UI_DrawText,
                                    str8_lit("###droptarget"));
        ring->display_string = app_str(Str_DropHint);
    }
}

// --- the panel -------------------------------------------------------------------
void app_library_panel(f32 width) {
    const UI_Theme *theme = ui_theme();
    UI_List *list = &app.list;

    // Ctrl+F focuses the field, Escape empties it. Read before the field is
    // built, so a clear takes effect in the very frame it was asked for.
    b32 focus_search = 0;
    for (u32 i = 0; i < ui_key_event_count(); i += 1) {
        UI_KeyEvent event = ui_key_event(i);
        if (event.key == OsKey_F && (event.modifiers & OsMod_Ctrl)) { focus_search = 1; }
    }
    if (ui_escape_pressed() && app.query_size != 0) {
        ui_text_input_init(&app.search, str8_lit(""));
        app_query_set(str8(0, 0));
    }

    app_columns_measure(width);
    String8 subtitle =
        app.scan_active
            ? str8f(ui_frame_arena(), app_str_c(Str_LibraryScanning), app.scan_files,
                    app.scan_dirs_done, app.scan_dirs_total)
            : str8f(ui_frame_arena(), app_str_c(Str_LibraryCount), app_row_count(),
                    app_track_count());
    UI_Box *panel = app_panel_begin(str8_lit("###library"), ui_px(width, 1.0f),
                                    app_str(Str_LibraryTitle), subtitle);
    app_library_panel_h = rect_height(panel->rect);

    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_comfortable), 1.0f))
    UI_ChildLayoutAxis(Axis2_X) {
        UI_Box *bar = ui_build_box_from_key(0, 0);
        UI_Parent(bar) {
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
            UI_PrefHeight(ui_px(ui_dp(theme->row_standard), 1.0f)) {
                UI_Signal signal =
                    ui_text_input(&app.search, app_str(Str_LibrarySearchPlaceholder));
                if (focus_search) {
                    ui_set_focus(signal.box->key, 1);
                    ui_text_input_select_all(&app.search);
                }
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_8]), 1.0f));
        }
    }
    if (app.search.changed) { app_query_set(ui_text_input_string(&app.search)); }
    ui_separator();

    b32 empty = (app.library.live_count == 0 && !app.scan_active);
    if (!empty) { app_browser_panel(); }

    if (empty) {
        app_library_empty_state();
        app_drop_overlay(panel);
        app_panel_end();
        return;
    }

    // Column headers, in the order the preferences remember.
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(theme->row_compact), 1.0f))
    UI_ChildLayoutAxis(Axis2_X)
    UI_BgColor(theme->panel) {
        UI_Box *header = ui_build_box_from_key(UI_DrawBackground, 0);
        UI_Parent(header) {
            if (app.prefs.thumbnails) { ui_spacer(ui_px(app_thumb_column_width(), 1.0f)); }
            for (u32 slot = 0; slot < AppColumn_COUNT; slot += 1) {
                AppColumn column = app_column_at(slot);
                if (app.column_px[column] > 0.0f) { app_column_header(column); }
            }
        }
    }
    ui_separator();
    if (app.scan_active) { app_scan_progress(); }

    if (app_row_count() == 0 && app.query_size != 0) {
        app_library_no_results();
        app_detail_panel(width);
        app_drop_overlay(panel);
        app_panel_end();
        return;
    }

    // The list itself: only the visible rows become boxes.
    const u32 *rows = app_rows();
    ui_list_begin(list, app_row_count(), app_row_height());
    UI_ListEachRow(list, i) {
        UI_Signal signal = ui_list_row_begin(list, i);
        app_library_row(i, rows[i], ui_list_selected(list, i));
        ui_list_row_end(list);
        // A press that travels is a drag towards the plan: the selection is
        // already the one the press made, so the drop has nothing to decide.
        if (signal.dragging && !app.lib_drag &&
            abs_f32(signal.drag_delta.x) + abs_f32(signal.drag_delta.y) > ui_dp(6.0f)) {
            app.lib_drag = 1;
        }
    }
    ui_list_end(list);
    if (app.lib_drag) {
        app.lib_drag_pos = ui_mouse();
        ui_request_animation();
        ui_cursor_request(app_plan_hovered(app.lib_drag_pos) ? OsCursor_Hand
                                                             : OsCursor_Forbidden);
        // The button came up somewhere: over the plan it adds, anywhere else it
        // was a change of mind and costs nothing.
        if (ui_active_key() == 0) {
            app.lib_drag = 0;
            if (app_plan_hovered(app.lib_drag_pos)) { app_plan_drop_rows(0); }
        }
    }

    if (list->context) { ui_context_menu_open(&app.menu, list->context_pos, list->context_row); }
    if (list->activated) { app_plan_add_selection(); }
    app_detail_panel(width);
    app_drop_overlay(panel);
    app_panel_end();
}

// Which columns the list shows. Title is not in the list: a library row with no
// title is not a row, and the width model gives it whatever is left anyway.
static void app_header_menu(void) {
    if (!ui_context_menu_begin(&app.header_menu)) { return; }
    {
        String8 label = str8f(ui_frame_arena(), "%s %S", app.prefs.thumbnails ? "â" : "Â ",
                              app_str(Str_MenuThumbnails));
        if (ui_context_menu_item(&app.header_menu, label)) {
            app.prefs.thumbnails = !app.prefs.thumbnails;
            app.prefs_dirty = 1;
        }
    }
    ui_context_menu_separator(&app.header_menu);
    for (u32 slot = 0; slot < AppColumn_COUNT; slot += 1) {
        AppColumn column = app_column_at(slot);
        if (app_column_flexible(column)) { continue; }
        String8 label = str8f(ui_frame_arena(), "%s %S",
                              app.prefs.columns[column].visible ? "â" : "Â ",
                              app_str((Str)(Str_ColumnIndex + column)));
        if (ui_context_menu_item(&app.header_menu, label)) {
            app.prefs.columns[column].visible = !app.prefs.columns[column].visible;
            app.prefs_dirty = 1;
        }
    }
    ui_context_menu_end(&app.header_menu);
}

void app_library_context_menu(void) {
    app_header_menu();
    if (!ui_context_menu_begin(&app.menu)) { return; }
    u64 row = app.menu.payload;
    if (ui_context_menu_item(&app.menu, app_str(Str_MenuAddSelection))) {
        app_plan_add_selection();
    }
    if (ui_context_menu_item(&app.menu, app_str(Str_MenuAddTrack))) {
        if (row < app_row_count()) { app_plan_add(app_rows()[row]); }
    }
    ui_context_menu_separator(&app.menu);
    if (ui_context_menu_item(&app.menu, app_str(Str_MenuSelectAll))) {
        ui_list_select_all(&app.list);
    }
    if (ui_context_menu_item(&app.menu, app_str(Str_MenuSelectNone))) {
        ui_list_select_clear(&app.list);
    }
    ui_context_menu_end(&app.menu);
}

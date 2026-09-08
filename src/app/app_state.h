// app_state.h - everything the application remembers between two frames, in one
// struct, separated from the frame loop (T-013). app.c owns the loop and the
// panels; view_library.c draws the library over this state; nothing here talks
// to GL or to Win32.
//
// One rule holds the whole view together: the list never copies a track. It
// reads an id out of the search result buffer and pulls the fields it needs out
// of the SoA, so 100 000 rows cost one array of ids and nothing else.
#ifndef APP_STATE_H
#define APP_STATE_H

#include "../base/base.h"
#include "../core/library/lib_cache.h"
#include "../core/library/lib_covers.h"
#include "../core/library/lib_events.h"
#include "../core/library/lib_index.h"
#include "../core/library/lib_model.h"
#include "../core/library/lib_scan.h"
#include "../core/library/lib_search.h"
#include "../core/plan/plan_capacity.h"
#include "../core/plan/plan_file.h"
#include "../core/plan/plan_toc.h"
#include "../core/plan/plan_model.h"
#include "../core/netmd/netmd_device.h"
#include "../ui/ui_widgets.h"
#include "app_shortcuts.h"
#include "plan_view.h"
#include "prefs.h"
#include "strings.h"

// The buffers the search, the browser and the selection are sized for once.
// A library bigger than this is a dimensioning bug, not a runtime error.
#define APP_TRACK_MAX 300000

// Minimum widths of the three panels, in dp. Their sum plus the two handles is
// what "the three panels stay visible" means; below it the window cannot go.
// T-075 (C1): the disc panel could not hold its own commands at 200 dp and the
// plan cut three column headers at 280; both minima are taken on the library,
// which is the panel that had room to spare.
#define APP_MIN_LIBRARY_DP 300.0f
#define APP_MIN_PLAN_DP    400.0f
#define APP_MIN_DISC_DP    360.0f
#define APP_MIN_BROWSER_DP  80.0f  // the artist/album columns, two rows at least
#define APP_MIN_DETAIL_DP   96.0f  // the detail panel: a small cover and three lines
#define APP_MIN_LIST_DP    160.0f  // what the track list keeps whatever is dragged

// What one row shows. Built on the stack from the SoA, never stored.
typedef struct AppTrack {
    String8 title;
    String8 artist;
    String8 album;
    u32 duration_s;
    u32 sample_rate;
    u64 mtime_us;
    u16 year;
    u8 codec;
    u8 mode;  // UI_Mode, SP until the plan decides otherwise (T-030)
} AppTrack;

typedef struct AppState {
    Arena *permanent;
    Arena *index_arena;  // the index owns it: lib_index_build clears it

    // --- the core ----------------------------------------------------------
    Library library;
    LibEventQueue events;
    LibScan scan;
    LibIndex index;
    LibBrowser browser;
    LibSearch finder;
    LibCovers covers;

    // --- widgets whose state outlives the frame -----------------------------
    UI_TextInput search;
    UI_List list;
    UI_List artist_list;
    UI_List album_list;
    UI_Splitter library_split;
    UI_Splitter disc_split;
    // Vertical, inside the library panel: the browser columns above the list
    // and the detail panel below it. Heights, in physical pixels like the others.
    UI_Splitter browser_split;
    UI_Splitter detail_split;
    UI_ContextMenu menu;

    // --- the plan (T-030) ----------------------------------------------------
    // The document itself, with the two arenas it owns: opening a file empties
    // them, so nothing else may ever push into them.
    Plan plan;
    // Every durable write of the plan goes through it, autosave and Save alike:
    // the frame thread only encodes, a job does the disk (P-009, T-073).
    PlanSaver plan_saver;
    String8 plan_autosave_path;
    String8 plan_path;  // what Save writes to; empty until the first Save as
    u32 plan_disc;      // the tab the view edits, an index into plan.discs
    u32 plan_revision;  // the revision the header numbers were computed at
    // What the gauge and the header show (T-031). Recomputed only when the
    // document changes: a frame that draws the same plan reads the same numbers.
    PlanCapacity capacity;
    PlanTocBudget toc;

    // --- the plan view (T-032) -----------------------------------------------
    // The list, its selection bitset and the two fields that edit text in
    // place. The bitset is 254 bits, so it lives in the struct: a plan is
    // bounded and a pointer to four words would only add an indirection.
    UI_List plan_list;
    u64 plan_selection[(PLAN_ENTRY_MAX + 63) / 64];
    UI_TextInput plan_rename;   // the inline title editor
    u32 plan_rename_row;        // the row it edits, + 1; 0 when it is closed
    UI_TextInput plan_disc_title;
    b32 plan_disc_title_open;   // the header field is live and owns its text
    u32 plan_group_editing;     // the group header being renamed, + 1
    UI_TextInput plan_group_name;
    UI_ContextMenu plan_menu;
    u32 plan_group_collapsed;   // bit i: group i is folded away

    // A reorder in flight: the row that was picked up and the gap the
    // insertion line is sitting in. One plan_move is issued, at the drop.
    b32 plan_drag;
    u32 plan_drag_row;
    u32 plan_drag_target;  // 0..entry_count, the gap before that row
    V2 plan_drag_pos;

    // The gauge. Two layouts and a parameter between them: a change of plan
    // does not jump, it slides over 120 ms (research/02 s9, MI-09/MI-11).
    PlanGaugeLayout gauge;
    PlanGaugeLayout gauge_from;
    f32 gauge_t;           // 0 at the start of the slide, 1 once it landed
    f32 gauge_width;       // the width both layouts were computed for
    u32 gauge_revision;    // the plan revision `gauge` was laid out at
    u32 gauge_hover;       // segment under the pointer, + 1; 0 when none
    b32 gauge_hover_free;  // the pointer is over the free zone
    f32 gauge_hover_x;     // where it is, for the playhead

    // --- preferences and the paths they live at ------------------------------
    Prefs prefs;
    String8 prefs_path;
    String8 cache_path;
    String8 cache_dir;
    b32 prefs_dirty;

    // --- the scan -----------------------------------------------------------
    String8 root;      // the folder of the running or last scan
    b32 scan_active;
    u32 scan_folder;   // next entry of prefs.folder to scan
    u32 scan_files, scan_dirs_done, scan_dirs_total;

    // --- the index and the search --------------------------------------------
    b32 index_ready;
    u64 index_at_us;   // throttles the rebuilds a live rescan asks for
    u8 query[UI_TEXT_INPUT_CAP];
    u32 query_size;

    // --- the library view ----------------------------------------------------
    // Column widths in physical pixels, recomputed once per frame from the
    // preferences and the panel width, so the header row and every list row
    // agree to the pixel and nothing is ever clipped away.
    f32 column_px[AppColumn_COUNT];
    f32 split_scale;         // the DPI scale the splitter sizes are expressed in
    UI_ContextMenu header_menu;
    u32 header_drag;         // AppColumn being resized, + 1; 0 when idle
    f32 header_drag_origin;  // its width in dp when the drag started

    // --- the drag from the Explorer (T-014) ----------------------------------
    // A drag in flight and where its cursor is, so the library panel can say
    // "here" before the user lets go. Cleared by DragLeave and by the drop.
    b32 drag_active;
    V2 drag_pos;

    // The drag that goes the other way (T-032): a row picked up in the library
    // and carried over the plan. It is not an OLE drag - it never leaves the
    // window - so it is two fields and the mouse.
    b32 lib_drag;
    V2 lib_drag_pos;
} AppState;

// One instance, named by everything above app/: the unity build defines it in
// app_state.c and every view reads it directly.
extern AppState app;

// Preferences first and alone: the window is created at the size they remember,
// so nothing else may exist yet when this runs.
void app_prefs_init(Arena *permanent);
void app_init(Arena *permanent, f32 scale);
void app_shutdown(void);

// The rows the list shows: track ids in the current sort order, filtered by the
// browser and by the query. Owned by the search, never copied.
md_inline const u32 *app_rows(void) { return app.finder.results; }
md_inline u32 app_row_count(void) { return app.index_ready ? app.finder.result_count : 0; }
md_inline u32 app_track_count(void) { return app.library.live_count; }

AppTrack app_track(TrackId id);
void     app_filter(void);
void     app_sort_by(u32 column);  // LibSortColumn; toggles the direction
void     app_index_rebuild(void);
void     app_scan_start(void);     // asks for a folder, then scans it
void     app_scan_folder(String8 folder);
void     app_scan_tick(void);
void     app_plan_add(TrackId id);
void     app_plan_add_selection(void);
void     app_plan_clear(void);  // one Remove per entry, so a clear is undoable
void     app_plan_tick(void);   // drains the plan events, autosaves every 5 s
void     app_query_set(String8 query);

// The disc the view is on. Every disc of the plan is a tab, and the active one
// is what the list, the gauge and the TOC bar are all about.
md_inline PlanDisc *app_plan_disc(void) {
    Assert(app.plan_disc < app.plan.disc_count);
    return &app.plan.discs[app.plan_disc];
}

// The clusters and the title cells of the current disc (T-031). Cheap enough to
// call every frame: it only recomputes when the revision moved.
void app_plan_recompute(void);
md_inline void app_plan_sync(void) {
    if (app.plan_revision != app.plan.revision) { app_plan_recompute(); }
}
md_inline u32 app_plan_count(void) { return app_plan_disc()->entry_count; }

// The two hooks the plan header uses for the save (T-073), so that view_plan.c
// says what it wants and not how it is done.
// Save and Save as: the same path as the autosave, off the frame thread. It
// returns 1 when the snapshot was handed over - the dirty flag goes then, and
// the indicator carries the rest of the story.
md_inline b32 app_plan_save_to(String8 path) {
    return plan_save_async(&app.plan_saver, &app.plan, path);
}
// The colour of the dot: amber while something is unsaved or being written,
// red when the last write failed, green when the document is on disk.
md_inline u32 app_plan_save_color(const UI_Theme *theme) {
    PlanSaveState state = plan_save_state(&app.plan_saver);
    if (state == PlanSave_Failed) { return theme->danger; }
    if (app.plan.dirty || state == PlanSave_Running) { return theme->warning; }
    return theme->success;
}

// view_plan.c
void app_plan_panel(void);
void app_disc_panel(f32 width);
// The 12 px variant of the gauge, for the status bar (research/02 s9.10).
void app_plan_gauge_compact(f32 width);
void app_plan_context_menu(void);
// The drop of a library drag inside the plan panel: the rows land at `row`.
void app_plan_drop_rows(u32 row);
// Is the pointer over the plan list right now? The library drag asks, to know
// whether letting go adds to the plan or to the library.
b32 app_plan_hovered(V2 pos);

// view_transfer.c (T-043): the burn as a visible step. The Disc panel calls
// app_transfer_bar in place of the T-042 button, and the frame loop asks the
// other three whether it may sleep, tick or close.
void app_transfer_init(void);
void app_transfer_tick(void);
void app_transfer_bar(f32 panel_width);
b32  app_transfer_takes_over(void);
void app_transfer_open(void);
b32  app_transfer_busy(void);
b32  app_transfer_can_close(void);
void app_transfer_close_blocked(void);
void app_transfer_device_event(const NetmdEvent *event);

// view_settings.c (T-072): the preferences panel, the keyboard help overlay and
// the two things the rest of the app asks them.
void app_settings_init(OsWindow window);  // language and theme, before frame 1
void app_settings_ui(void);               // both overlays, on the popup layer
void app_settings_open(void);
void app_settings_close(void);
void app_settings_toggle(void);
void app_settings_keys_toggle(void);
b32  app_settings_overlay_open(void);
void app_settings_system_theme_changed(void);  // WM_SETTINGCHANGE
AppShortcutContext app_settings_context(void);
String8 app_shortcut_status_hint(Arena *arena);

// app.c (T-075): one row of buttons - space_12 left and right, space_4 above
// and below, space_4 between two buttons, and a wrap onto a second line when
// the line is full. Returns the row, to be pushed as the parent.
UI_Box *app_button_row(void);

// view_library.c
void app_library_panel(f32 width);
// The thumbnail of a track, uploaded into the atlas the frame it is ready.
// 0 when nothing can be drawn yet; the request has been made either way.
b32 app_cover_rect(TrackId id, u32 size, R_AtlasRect *out);
void app_covers_begin_frame(void);
void app_library_context_menu(void);

#endif // APP_STATE_H

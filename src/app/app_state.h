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
#include "../ui/ui_widgets.h"
#include "prefs.h"
#include "strings.h"

// The buffers the search, the browser and the selection are sized for once.
// A library bigger than this is a dimensioning bug, not a runtime error.
#define APP_TRACK_MAX 300000

// Minimum widths of the three panels, in dp. Their sum plus the two handles is
// what "the three panels stay visible" means; below it the window cannot go.
#define APP_MIN_LIBRARY_DP 300.0f
#define APP_MIN_PLAN_DP    280.0f
#define APP_MIN_DISC_DP    200.0f

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
    UI_ContextMenu menu;

    // --- the plan (T-030) ----------------------------------------------------
    // The document itself, with the two arenas it owns: opening a file empties
    // them, so nothing else may ever push into them.
    Plan plan;
    String8 plan_autosave_path;
    u32 plan_cursor;    // the row Delete acts on; the real view is T-032
    u32 plan_revision;  // the revision the header numbers were computed at
    // What the gauge and the header show (T-031). Recomputed only when the
    // document changes: a frame that draws the same plan reads the same numbers.
    PlanCapacity capacity;
    PlanTocBudget toc;

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

// The plan is a single disc until T-032 gives the panel its multi disc view.
md_inline PlanDisc *app_plan_disc(void) { return &app.plan.discs[0]; }

// The clusters and the title cells of the current disc (T-031). Cheap enough to
// call every frame: it only recomputes when the revision moved.
void app_plan_recompute(void);
md_inline void app_plan_sync(void) {
    if (app.plan_revision != app.plan.revision) { app_plan_recompute(); }
}
md_inline u32 app_plan_count(void) { return app.plan.discs[0].entry_count; }

// view_library.c
void app_library_panel(f32 width);
// The thumbnail of a track, uploaded into the atlas the frame it is ready.
// 0 when nothing can be drawn yet; the request has been made either way.
b32 app_cover_rect(TrackId id, u32 size, R_AtlasRect *out);
void app_covers_begin_frame(void);
void app_library_context_menu(void);

#endif // APP_STATE_H

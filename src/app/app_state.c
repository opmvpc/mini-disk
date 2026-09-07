// app_state.c - the state machine behind the three panels: preferences in,
// cache in, scan, index, search, plan. No box is built here.
#include "app_state.h"

AppState app;

// --- one row of the library -------------------------------------------------
AppTrack app_track(TrackId id) {
    Library *lib = &app.library;
    Assert(lib_track_live(lib, id));
    AppTrack track;
    // The tags (T-011) already carry their own fallbacks; the one case left is
    // a file the tag wave has not reached yet, which still has its name.
    track.title = lib_string(&lib->strings, lib->title_id[id]);
    track.artist = lib_string(&lib->strings, lib->artist_id[id]);
    track.album = lib_string(&lib->strings, lib->album_id[id]);
    track.duration_s = lib->duration_ms[id] / 1000;
    track.sample_rate = lib->sample_rate[id];
    track.mtime_us = lib->mtime_us[id];
    track.year = lib->year[id];
    track.codec = lib->codec[id];
    track.mode = UI_Mode_SP;
    if (track.title.size == 0) {
        String8 path = lib_track_path(lib, id);
        track.title = os_path_filename(path);
        String8 folder = os_path_parent(path);
        if (folder.size > app.root.size && str8_starts_with(folder, app.root)) {
            folder = str8_skip(folder, app.root.size + 1);
        } else if (str8_eq(folder, app.root)) {
            folder = os_path_filename(folder);
        }
        track.artist = folder;
    }
    return track;
}

// --- the plan's numbers (T-031) ----------------------------------------------
// Clusters, not seconds: a plan that sums to 79:58 of audio can still be 80:04
// of disc (ADR-011 D2). The title budget is measured in the same pass because
// both answers come from the same walk over the entries.
void app_plan_recompute(void) {
    plan_capacity_compute(app_plan_disc(), &app.capacity);
    plan_toc_budget(&app.plan, &app.library, 0, &app.toc);
    app.plan_revision = app.plan.revision;
}

// --- search, sort, browser --------------------------------------------------
void app_filter(void) {
    if (!app.index_ready) {
        app.finder.result_count = 0;
    } else {
        lib_search_set_order(&app.finder, (LibSortColumn)app.prefs.sort_column,
                             app.prefs.sort_desc);
        lib_search_set_filter(&app.finder, lib_browser_artist_filter(&app.browser),
                              lib_browser_album_filter(&app.browser));
        lib_search_run(&app.finder, &app.index, str8(app.query, app.query_size));
    }
    ui_list_select_clear(&app.list);
    app.list.cursor = 0;
    app.list.anchor = 0;
    app.list.scroll = 0.0f;
}

void app_query_set(String8 query) {
    query = str8_prefix(query, Min(query.size, (u64)UI_TEXT_INPUT_CAP - 1));
    mem_copy(app.query, query.str, query.size);
    app.query_size = (u32)query.size;
    app_filter();
}

void app_sort_by(u32 column) {
    Assert(column < LibSort_COUNT);
    if (app.prefs.sort_column == column) {
        app.prefs.sort_desc = !app.prefs.sort_desc;
    } else {
        app.prefs.sort_column = column;
        app.prefs.sort_desc = 0;
    }
    app.prefs_dirty = 1;
    app_filter();
}

// One rebuild covers everything the panel reads: the sorted views, the artist
// and album columns, and the buffers the next keystroke refines.
void app_index_rebuild(void) {
    // The buffers were sized once; a library past that is a dimensioning bug.
    AssertAlways(app.library.live_count <= APP_TRACK_MAX);
    lib_index_build(&app.index, &app.library, app.index_arena, 1);
    lib_browser_build(&app.browser, &app.index);
    lib_search_invalidate(&app.finder);
    app.index_at_us = os_time_now_us();
    app.index_ready = app.library.live_count > 0;
    app_filter();
}

// --- the plan (T-030) --------------------------------------------------------
// Every one of these goes through a command, so everything the panel does -
// adding a selection of forty tracks included - is one Ctrl+Z away.
void app_plan_add(TrackId id) { plan_add_track(&app.plan, &app.library, 0, id); }

void app_plan_clear(void) {
    while (app_plan_count() != 0) { plan_remove(&app.plan, 0, 0); }
    app.plan_cursor = 0;
}

// Once per frame: the events tell the panel it must redraw, and the autosave
// writes at most every five seconds and only when something changed (B-01).
void app_plan_tick(void) {
    PlanEvent event;
    b32 changed = 0;
    while (plan_events_next(&app.plan.events, &event)) { changed = 1; }
    if (changed) {
        u32 count = app_plan_count();
        if (app.plan_cursor >= count) { app.plan_cursor = count ? count - 1 : 0; }
        os_request_redraw();
    }
    plan_autosave_tick(&app.plan, app.plan_autosave_path, os_time_now_us());
}

void app_plan_add_selection(void) {
    const u32 *rows = app_rows();
    u32 count = app_row_count();
    for (u32 i = 0; i < count; i += 1) {
        if (ui_list_selected(&app.list, i)) { app_plan_add(rows[i]); }
    }
}

// --- the scan ----------------------------------------------------------------
void app_scan_folder(String8 folder) {
    if (folder.size == 0 || app.scan_active) { return; }
    if (!lib_scan_begin(&app.scan, &app.library, &app.events, folder)) { return; }
    app.scan_active = 1;
    app.scan_files = 0;
    app.scan_dirs_done = 0;
    app.scan_dirs_total = 0;
    app.root = str8_copy(app.permanent, folder);
}

void app_scan_start(void) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 folder = os_dialog_pick_folder(scratch.arena, app_str(Str_LibraryEmptyAction));
    if (folder.size != 0) {
        if (prefs_add_folder(&app.prefs, folder)) { app.prefs_dirty = 1; }
        app_scan_folder(folder);
    }
    scratch_end(scratch);
}

// Polled once per frame: it merges what the workers published and returns at
// once, so the frame that starts a 50 000 file scan renders like any other.
void app_scan_tick(void) {
    if (!app.scan_active) {
        // Folders are scanned one after the other, so a second library folder
        // starts as soon as the first is merged.
        if (app.scan_folder < app.prefs.folder_count) {
            String8 folder = prefs_folder(&app.prefs, app.scan_folder);
            app.scan_folder += 1;
            app_scan_folder(folder);
        }
        return;
    }
    lib_scan_update(&app.scan);
    LibEvent event;
    b32 done = 0;
    b32 changed = 0;
    while (lib_events_next(&app.events, &event)) {
        if (event.kind == LibEvent_ScanProgress || event.kind == LibEvent_ScanDone) {
            app.scan_files = event.files_seen;
            app.scan_dirs_done = event.dirs_done;
            app.scan_dirs_total = event.dirs_total;
        }
        if (event.kind == LibEvent_TracksAdded || event.kind == LibEvent_TracksRemoved ||
            event.kind == LibEvent_TracksTagged) {
            changed = 1;
        }
        if (event.kind == LibEvent_ScanDone) { done = 1; }
    }
    // Only the diffs reach the panel: a rescan over an unchanged library pushes
    // nothing, and a rescan that does is folded in at most four times a second
    // rather than once per merged batch.
    if (!done && changed && app.index_ready && os_time_now_us() - app.index_at_us > 250000) {
        app_index_rebuild();
    }
    if (!done) { return; }
    app.scan_active = 0;
    lib_scan_end(&app.scan);
    app_index_rebuild();
    // A rescan renumbers the library; the plan is re-resolved by path rather
    // than thrown away, which is the whole point of storing both (ADR-011 D1).
    plan_resolve(&app.plan, &app.library);
    // The cache is reconstructible: writing it is best effort, and a failure
    // costs the next launch a rescan and nothing else.
    lib_cache_save(&app.library, app.cache_path, app.root);
}

// --- start up ----------------------------------------------------------------
static String8 app_cache_dir(Arena *arena) {
    ArenaTemp scratch = scratch_begin(&arena, 1);
    String8 folder = os_known_folder(scratch.arena, OsKnownFolder_LocalAppData);
    if (folder.size == 0) { folder = os_known_folder(scratch.arena, OsKnownFolder_Temp); }
    String8 dir = os_path_join(arena, folder, str8_lit("minidisk"));
    os_dir_create(dir);
    scratch_end(scratch);
    return dir;
}

// "--scan <folder>" and "--query <text>": the same paths the button and the
// field take, without the dialog and without anyone typing. It is how the demo
// is captured and how a session is reproduced from a script.
static String8 app_command_line_value(Arena *arena, String8 flag) {
    String8 args = os_command_line(arena);
    u64 at = str8_find(args, flag, 0);
    if (at == args.size) { return str8(0, 0); }
    String8 value = str8_trim(str8_skip(args, at + flag.size));
    if (value.size != 0 && value.str[0] == '"') {
        value = str8_skip(value, 1);
        value = str8_prefix(value, str8_find(value, str8_lit("\""), 0));
    }
    return value;
}

void app_prefs_init(Arena *permanent) {
    app.prefs_path = prefs_path(permanent);
    ArenaTemp scratch = scratch_begin(&permanent, 1);
    prefs_load(&app.prefs, app.prefs_path, scratch.arena);
    scratch_end(scratch);
}

void app_init(Arena *permanent, f32 scale) {
    app.permanent = permanent;
    Assert(app.prefs_path.size != 0);  // app_prefs_init ran first

    // Two arenas: the columns and the hash slots on one, the interned strings
    // on the other, so the string bytes stay contiguous while the SoA doubles.
    lib_init(&app.library, arena_alloc(MB(512)), arena_alloc(MB(512)));
    app.index_arena = arena_alloc(GB(2));
    lib_browser_init(&app.browser, permanent, APP_TRACK_MAX);
    lib_search_init(&app.finder, permanent, APP_TRACK_MAX);
    app.cache_dir = app_cache_dir(permanent);
    // Two arenas of its own: opening a plan empties them, so nothing else may
    // ever push into them.
    plan_init(&app.plan, arena_alloc(MB(16)), arena_alloc(MB(16)));
    app_plan_recompute();  // the gauge has real numbers from the first frame
    app.plan_autosave_path = plan_autosave_path(permanent, app.cache_dir);
    app.cache_path = os_path_join(permanent, app.cache_dir, str8_lit("library.mdlib"));
    lib_covers_init(&app.covers, permanent, app.cache_dir);

    u64 selection_words = (APP_TRACK_MAX + 63) / 64;
    ui_list_init(&app.list, push_array_zero(permanent, u64, selection_words), selection_words);
    ui_list_init(&app.artist_list, 0, 0);
    ui_list_init(&app.album_list, 0, 0);
    ui_text_input_init(&app.search, str8_lit(""));

    // Three panels that all stay on screen down to 1024 x 640 logical: the disc
    // is measured from the right, the library from the left, and each splitter
    // is clamped against what the other two panels need (see app_body).
    ui_splitter_init(&app.library_split, 720.0f * scale, APP_MIN_LIBRARY_DP * scale,
                     (APP_MIN_PLAN_DP + APP_MIN_DISC_DP) * scale);
    ui_splitter_init(&app.disc_split, 240.0f * scale, APP_MIN_DISC_DP * scale,
                     (APP_MIN_LIBRARY_DP + APP_MIN_PLAN_DP) * scale);
    app.disc_split.measures_trailing = 1;

    // Startup: the cache first, so the library is on screen before the disk is
    // touched; then a rescan in the background that pushes only its diffs.
    String8 cached_root = str8(0, 0);
    if (lib_cache_load(&app.library, app.cache_path, permanent, &cached_root) == LibCache_Ok) {
        app.root = cached_root;
        app_index_rebuild();
    }

    // Recovery: whatever the last autosave caught is on screen at startup, its
    // entries re-resolved against the library we just brought back. A plan that
    // fails to load is left on disk untouched - it is not reconstructible.
    if (plan_load(&app.plan, app.plan_autosave_path) == PlanFile_Ok) {
        plan_resolve(&app.plan, &app.library);
    }

    ArenaTemp scratch = scratch_begin(&permanent, 1);
    String8 folder = app_command_line_value(scratch.arena, str8_lit("--scan "));
    if (folder.size != 0) {
        if (prefs_add_folder(&app.prefs, folder)) { app.prefs_dirty = 1; }
        app.scan_folder = app.prefs.folder_count;  // the queue starts after it
        app_scan_folder(folder);
    } else if (cached_root.size != 0) {
        if (prefs_add_folder(&app.prefs, cached_root)) { app.prefs_dirty = 1; }
    }
    String8 query = app_command_line_value(scratch.arena, str8_lit("--query "));
    if (query.size != 0) {
        ui_text_input_init(&app.search, query);
        app_query_set(query);
    }
    scratch_end(scratch);
}

void app_shutdown(void) {
    if (app.prefs_dirty) { prefs_save(&app.prefs, app.prefs_path); }
    // The five second window does not apply to a close: whatever is unsaved
    // goes out now, so the next launch opens on it.
    if (app.plan.dirty) { plan_save(&app.plan, app.plan_autosave_path); }
}

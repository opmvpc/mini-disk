// prefs.c - see prefs.h. The whole file is the parsing boundary; everything
// below it trusts the struct.
#include "prefs.h"

// Keys are stable identifiers, never translated: they are written on disk.
static const char *prefs_column_names[AppColumn_COUNT] = {
    "index", "title", "artist", "album", "duration", "format", "year", "added",
};

// Dense by default (ADR-011 D8): the eight columns fit a 720 dp library panel
// with room left for the title, which is the one that must not be cramped.
static const f32 prefs_column_default_width[AppColumn_COUNT] = {
    34.0f, 320.0f, 130.0f, 130.0f, 60.0f, 78.0f, 56.0f, 86.0f,
};

void prefs_defaults(Prefs *prefs) {
    StructZero(prefs);
    for (u32 i = 0; i < AppColumn_COUNT; i += 1) {
        prefs->columns[i].width = prefs_column_default_width[i];
        prefs->columns[i].order = i;
        prefs->columns[i].visible = 1;
    }
    prefs->sort_column = 0;  // LibSort_Title
    prefs->thumbnails = 1;
    prefs->window_width = 1360;
    prefs->window_height = 820;

    // T-072. The language default is the compile time one of strings.h, the
    // theme follows the system, and the audio defaults are what the pipeline
    // already does when nobody has an opinion.
    prefs->lang = 0;
    prefs->theme = 2;  // UI_ThemeChoice_System
    prefs->default_mode = 0;
    prefs->loudness_lufs = PREFS_LOUDNESS_DEF;
    prefs->true_peak_dbtp = PREFS_PEAK_DEF;
    prefs->trim_silence = 0;
    prefs->fade_in_ms = 0;
    prefs->fade_out_ms = 0;
    prefs->gap_ms = 0;
    prefs->cache_transcode_mb = PREFS_CACHE_TRANSCODE_DEF;
    prefs->cache_covers_mb = PREFS_CACHE_COVERS_DEF;
}

String8 prefs_folder(const Prefs *prefs, u32 index) {
    Assert(index < prefs->folder_count);
    return str8((u8 *)prefs->folder[index], prefs->folder_size[index]);
}

b32 prefs_add_folder(Prefs *prefs, String8 folder) {
    if (folder.size == 0 || folder.size >= PREFS_FOLDER_CAP) { return 0; }
    for (u32 i = 0; i < prefs->folder_count; i += 1) {
        if (str8_eq(prefs_folder(prefs, i), folder)) { return 1; }
    }
    if (prefs->folder_count >= PREFS_MAX_FOLDERS) { return 0; }
    u32 slot = prefs->folder_count;
    mem_copy(prefs->folder[slot], folder.str, folder.size);
    prefs->folder_size[slot] = (u16)folder.size;
    prefs->folder_count = slot + 1;
    return 1;
}

void prefs_remove_folder(Prefs *prefs, u32 index) {
    Assert(index < prefs->folder_count);
    for (u32 i = index + 1; i < prefs->folder_count; i += 1) {
        mem_copy(prefs->folder[i - 1], prefs->folder[i], prefs->folder_size[i]);
        prefs->folder_size[i - 1] = prefs->folder_size[i];
    }
    prefs->folder_count -= 1;
    prefs->folder_size[prefs->folder_count] = 0;
}

// --- parsing ---------------------------------------------------------------
static b32 prefs_read_u32(String8 text, u32 *out) {
    if (text.size == 0 || text.size > 10) { return 0; }
    u64 value = 0;
    for (u64 i = 0; i < text.size; i += 1) {
        u8 c = text.str[i];
        if (c < '0' || c > '9') { return 0; }
        value = value * 10 + (u64)(c - '0');
    }
    if (value > U32_MAX) { return 0; }
    *out = (u32)value;
    return 1;
}

static b32 prefs_read_i32(String8 text, i32 *out) {
    b32 negative = (text.size != 0 && text.str[0] == '-');
    u32 magnitude = 0;
    if (!prefs_read_u32(negative ? str8_skip(text, 1) : text, &magnitude)) { return 0; }
    if (magnitude > 0x7FFFFFFFu) { return 0; }
    *out = negative ? -(i32)magnitude : (i32)magnitude;
    return 1;
}

static b32 prefs_read_b32(String8 text, b32 *out) {
    u32 value = 0;
    if (!prefs_read_u32(text, &value) || value > 1) { return 0; }
    *out = (b32)value;
    return 1;
}

// A bounded u32, the shape almost every key of T-072 has. Out of range is
// dropped like anything else the boundary does not understand.
static void prefs_read_u32_range(String8 text, u32 low, u32 high, u32 *out) {
    u32 value = 0;
    if (prefs_read_u32(text, &value) && value >= low && value <= high) { *out = value; }
}

static void prefs_read_i32_range(String8 text, i32 low, i32 high, i32 *out) {
    i32 value = 0;
    if (prefs_read_i32(text, &value) && value >= low && value <= high) { *out = value; }
}

// The word forms are what a human writes into the file by hand, the numbers are
// what an older build wrote. Both are read, neither is guessed (ADR-012).
static void prefs_read_enum(String8 text, const char *const *names, u32 count, u32 *out) {
    for (u32 i = 0; i < count; i += 1) {
        if (str8_eq(text, str8_cstr(names[i]))) {
            *out = i;
            return;
        }
    }
    u32 value = 0;
    if (prefs_read_u32(text, &value) && value < count) { *out = value; }
}

static const char *prefs_lang_names[2] = {"fr", "en"};
static const char *prefs_theme_names[3] = {"dark", "light", "system"};
static const char *prefs_mode_names[4] = {"sp", "mono", "lp2", "lp4"};

static u32 prefs_column_from_name(String8 name) {
    for (u32 i = 0; i < AppColumn_COUNT; i += 1) {
        if (str8_eq(name, str8_cstr(prefs_column_names[i]))) { return i; }
    }
    return AppColumn_COUNT;
}

// One "key=value" line, already trimmed. Anything unknown, malformed or out of
// range is dropped without a word: the default that is already in place wins.
static void prefs_apply_pair(Prefs *prefs, String8 key, String8 value) {
    if (str8_eq(key, str8_lit("folder"))) {
        prefs_add_folder(prefs, value);
        return;
    }
    if (str8_eq(key, str8_lit("sort.column"))) {
        u32 column = 0;
        if (prefs_read_u32(value, &column) && column < 16) { prefs->sort_column = column; }
        return;
    }
    if (str8_eq(key, str8_lit("sort.desc"))) { prefs_read_b32(value, &prefs->sort_desc); return; }
    if (str8_eq(key, str8_lit("browser.collapsed"))) {
        prefs_read_b32(value, &prefs->browser_collapsed);
        return;
    }
    if (str8_eq(key, str8_lit("library.thumbnails"))) {
        prefs_read_b32(value, &prefs->thumbnails);
        return;
    }
    if (str8_eq(key, str8_lit("library.detail_collapsed"))) {
        prefs_read_b32(value, &prefs->detail_collapsed);
        return;
    }
    if (str8_eq(key, str8_lit("library.browser_height"))) {
        u32 h = 0;
        if (prefs_read_u32(value, &h) && h <= PREFS_WINDOW_MAX) { prefs->browser_height = h; }
        return;
    }
    if (str8_eq(key, str8_lit("library.detail_height"))) {
        u32 h = 0;
        if (prefs_read_u32(value, &h) && h <= PREFS_WINDOW_MAX) { prefs->detail_height = h; }
        return;
    }
    if (str8_eq(key, str8_lit("ui.lang"))) {
        prefs_read_enum(value, prefs_lang_names, 2, &prefs->lang);
        return;
    }
    if (str8_eq(key, str8_lit("ui.theme"))) {
        prefs_read_enum(value, prefs_theme_names, 3, &prefs->theme);
        return;
    }
    if (str8_eq(key, str8_lit("plan.default_mode"))) {
        prefs_read_enum(value, prefs_mode_names, 4, &prefs->default_mode);
        return;
    }
    if (str8_eq(key, str8_lit("audio.loudness_lufs"))) {
        prefs_read_i32_range(value, PREFS_LOUDNESS_MIN, PREFS_LOUDNESS_MAX,
                             &prefs->loudness_lufs);
        return;
    }
    if (str8_eq(key, str8_lit("audio.true_peak_dbtp"))) {
        prefs_read_i32_range(value, PREFS_PEAK_MIN, PREFS_PEAK_MAX, &prefs->true_peak_dbtp);
        return;
    }
    if (str8_eq(key, str8_lit("audio.trim_silence"))) {
        prefs_read_b32(value, &prefs->trim_silence);
        return;
    }
    if (str8_eq(key, str8_lit("audio.fade_in_ms"))) {
        prefs_read_u32_range(value, 0, PREFS_FADE_MAX, &prefs->fade_in_ms);
        return;
    }
    if (str8_eq(key, str8_lit("audio.fade_out_ms"))) {
        prefs_read_u32_range(value, 0, PREFS_FADE_MAX, &prefs->fade_out_ms);
        return;
    }
    if (str8_eq(key, str8_lit("audio.gap_ms"))) {
        prefs_read_u32_range(value, 0, PREFS_GAP_MAX, &prefs->gap_ms);
        return;
    }
    if (str8_eq(key, str8_lit("cache.transcode_mb"))) {
        prefs_read_u32_range(value, PREFS_CACHE_TRANSCODE_MIN, PREFS_CACHE_TRANSCODE_MAX,
                             &prefs->cache_transcode_mb);
        return;
    }
    if (str8_eq(key, str8_lit("cache.covers_mb"))) {
        prefs_read_u32_range(value, PREFS_CACHE_COVERS_MIN, PREFS_CACHE_COVERS_MAX,
                             &prefs->cache_covers_mb);
        return;
    }
    if (str8_eq(key, str8_lit("window.x"))) { prefs_read_i32(value, &prefs->window_x); return; }
    if (str8_eq(key, str8_lit("window.y"))) { prefs_read_i32(value, &prefs->window_y); return; }
    if (str8_eq(key, str8_lit("window.placed"))) {
        prefs_read_b32(value, &prefs->window_placed);
        return;
    }
    if (str8_eq(key, str8_lit("window.maximized"))) {
        prefs_read_b32(value, &prefs->window_maximized);
        return;
    }
    b32 is_width = str8_eq(key, str8_lit("window.width"));
    if (is_width || str8_eq(key, str8_lit("window.height"))) {
        u32 size = 0;
        if (!prefs_read_u32(value, &size)) { return; }
        u32 low = is_width ? (u32)PREFS_WINDOW_MIN_W : (u32)PREFS_WINDOW_MIN_H;
        if (size < low) { size = low; }
        if (size > PREFS_WINDOW_MAX) { size = PREFS_WINDOW_MAX; }
        if (is_width) { prefs->window_width = size; } else { prefs->window_height = size; }
        return;
    }
    if (str8_starts_with(key, str8_lit("column."))) {
        String8 rest = str8_skip(key, 7);
        u64 dot = str8_find(rest, str8_lit("."), 0);
        if (dot == rest.size) { return; }
        u32 column = prefs_column_from_name(str8_prefix(rest, dot));
        if (column == AppColumn_COUNT) { return; }
        String8 field = str8_skip(rest, dot + 1);
        PrefsColumn *slot = &prefs->columns[column];
        if (str8_eq(field, str8_lit("width"))) {
            u32 width = 0;
            if (!prefs_read_u32(value, &width)) { return; }
            f32 clamped = clamp_f32((f32)width, PREFS_COLUMN_MIN, PREFS_COLUMN_MAX);
            slot->width = clamped;
        } else if (str8_eq(field, str8_lit("order"))) {
            u32 order = 0;
            if (prefs_read_u32(value, &order) && order < AppColumn_COUNT) { slot->order = order; }
        } else if (str8_eq(field, str8_lit("visible"))) {
            prefs_read_b32(value, &slot->visible);
        }
    }
}

// The order fields only mean something together: a duplicate or a hole would
// hide a column behind another, so a broken set falls back to the natural one.
static void prefs_fix_order(Prefs *prefs) {
    u32 seen = 0;
    for (u32 i = 0; i < AppColumn_COUNT; i += 1) {
        u32 bit = 1u << prefs->columns[i].order;
        if (prefs->columns[i].order >= AppColumn_COUNT || (seen & bit)) {
            for (u32 j = 0; j < AppColumn_COUNT; j += 1) { prefs->columns[j].order = j; }
            return;
        }
        seen |= bit;
    }
}

static String8 prefs_next_line(String8 text, u64 *cursor) {
    u64 start = *cursor;
    u64 end = start;
    while (end < text.size && text.str[end] != '\n') { end += 1; }
    *cursor = (end < text.size) ? end + 1 : text.size;
    return str8_trim(str8_substr(text, start, end - start));
}

b32 prefs_parse(Prefs *prefs, String8 text) {
    prefs_defaults(prefs);

    // A file that does not say which version it is, is not our file. Nothing is
    // migrated and nothing is guessed: the defaults stand.
    b32 versioned = 0;
    u64 cursor = 0;
    while (cursor < text.size) {
        String8 line = prefs_next_line(text, &cursor);
        u32 version = 0;
        if (str8_starts_with(line, str8_lit("version=")) &&
            prefs_read_u32(str8_skip(line, 8), &version) && version == PREFS_VERSION) {
            versioned = 1;
            break;
        }
    }
    if (!versioned) { return 0; }

    cursor = 0;
    while (cursor < text.size) {
        String8 line = prefs_next_line(text, &cursor);
        if (line.size == 0 || line.str[0] == '#') { continue; }
        u64 equal = str8_find(line, str8_lit("="), 0);
        if (equal == line.size) { continue; }
        String8 key = str8_trim(str8_prefix(line, equal));
        String8 value = str8_trim(str8_skip(line, equal + 1));
        if (key.size != 0) { prefs_apply_pair(prefs, key, value); }
    }
    prefs_fix_order(prefs);
    return 1;
}

String8 prefs_serialize(Arena *arena, const Prefs *prefs) {
    String8List lines;
    StructZero(&lines);
    str8_list_push(arena, &lines, str8_lit("# minidisk preferences"));
    str8_list_push(arena, &lines, str8f(arena, "version=%u", PREFS_VERSION));
    for (u32 i = 0; i < prefs->folder_count; i += 1) {
        str8_list_push(arena, &lines, str8f(arena, "folder=%S", prefs_folder(prefs, i)));
    }
    str8_list_push(arena, &lines, str8f(arena, "sort.column=%u", prefs->sort_column));
    str8_list_push(arena, &lines, str8f(arena, "sort.desc=%u", prefs->sort_desc ? 1u : 0u));
    str8_list_push(arena, &lines,
                   str8f(arena, "browser.collapsed=%u", prefs->browser_collapsed ? 1u : 0u));
    str8_list_push(arena, &lines,
                   str8f(arena, "library.thumbnails=%u", prefs->thumbnails ? 1u : 0u));
    str8_list_push(arena, &lines, str8f(arena, "library.detail_collapsed=%u",
                                        prefs->detail_collapsed ? 1u : 0u));
    str8_list_push(arena, &lines,
                   str8f(arena, "library.browser_height=%u", prefs->browser_height));
    str8_list_push(arena, &lines,
                   str8f(arena, "library.detail_height=%u", prefs->detail_height));
    for (u32 i = 0; i < AppColumn_COUNT; i += 1) {
        const PrefsColumn *column = &prefs->columns[i];
        const char *name = prefs_column_names[i];
        str8_list_push(arena, &lines,
                       str8f(arena, "column.%s.width=%u", name, (u32)(column->width + 0.5f)));
        str8_list_push(arena, &lines, str8f(arena, "column.%s.order=%u", name, column->order));
        str8_list_push(arena, &lines,
                       str8f(arena, "column.%s.visible=%u", name, column->visible ? 1u : 0u));
    }
    str8_list_push(arena, &lines, str8f(arena, "window.placed=%u", prefs->window_placed ? 1u : 0u));
    str8_list_push(arena, &lines, str8f(arena, "window.x=%d", prefs->window_x));
    str8_list_push(arena, &lines, str8f(arena, "window.y=%d", prefs->window_y));
    str8_list_push(arena, &lines, str8f(arena, "window.width=%u", prefs->window_width));
    str8_list_push(arena, &lines, str8f(arena, "window.height=%u", prefs->window_height));
    str8_list_push(arena, &lines,
                   str8f(arena, "window.maximized=%u", prefs->window_maximized ? 1u : 0u));
    str8_list_push(arena, &lines, str8f(arena, "ui.lang=%s", prefs_lang_names[prefs->lang]));
    str8_list_push(arena, &lines, str8f(arena, "ui.theme=%s", prefs_theme_names[prefs->theme]));
    str8_list_push(arena, &lines,
                   str8f(arena, "plan.default_mode=%s", prefs_mode_names[prefs->default_mode]));
    str8_list_push(arena, &lines, str8f(arena, "audio.loudness_lufs=%d", prefs->loudness_lufs));
    str8_list_push(arena, &lines, str8f(arena, "audio.true_peak_dbtp=%d", prefs->true_peak_dbtp));
    str8_list_push(arena, &lines,
                   str8f(arena, "audio.trim_silence=%u", prefs->trim_silence ? 1u : 0u));
    str8_list_push(arena, &lines, str8f(arena, "audio.fade_in_ms=%u", prefs->fade_in_ms));
    str8_list_push(arena, &lines, str8f(arena, "audio.fade_out_ms=%u", prefs->fade_out_ms));
    str8_list_push(arena, &lines, str8f(arena, "audio.gap_ms=%u", prefs->gap_ms));
    str8_list_push(arena, &lines,
                   str8f(arena, "cache.transcode_mb=%u", prefs->cache_transcode_mb));
    str8_list_push(arena, &lines, str8f(arena, "cache.covers_mb=%u", prefs->cache_covers_mb));
    str8_list_push(arena, &lines, str8_lit(""));
    return str8_list_join(arena, &lines, str8_lit("\r\n"));
}

// --- the file ---------------------------------------------------------------
String8 prefs_path(Arena *arena) {
    ArenaTemp scratch = scratch_begin(&arena, 1);
    String8 name = str8_lit("minidisk.prefs");
    String8 exe_dir = os_exe_dir(scratch.arena);
    String8 marker = os_path_join(scratch.arena, exe_dir, str8_lit("portable"));
    OsFileInfo info;
    StructZero(&info);
    String8 path;
    if (exe_dir.size != 0 && os_file_stat(marker, &info)) {
        path = os_path_join(arena, exe_dir, name);
    } else {
        String8 root = os_known_folder(scratch.arena, OsKnownFolder_LocalAppData);
        if (root.size == 0) { root = os_known_folder(scratch.arena, OsKnownFolder_Temp); }
        String8 dir = os_path_join(scratch.arena, root, str8_lit("minidisk"));
        os_dir_create(dir);
        path = os_path_join(arena, dir, name);
    }
    scratch_end(scratch);
    return path;
}

b32 prefs_load(Prefs *prefs, String8 path, Arena *scratch) {
    ArenaTemp temp = arena_temp_begin(scratch);
    String8 text = os_file_read_all(temp.arena, path);
    b32 ok = prefs_parse(prefs, text);
    arena_temp_end(temp);
    return ok;
}

b32 prefs_save(const Prefs *prefs, String8 path) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 text = prefs_serialize(scratch.arena, prefs);
    String8 temp_path = str8_cat(scratch.arena, path, str8_lit(".tmp"));
    b32 ok = os_file_write_all(temp_path, text) && os_file_move_replace(temp_path, path);
    scratch_end(scratch);
    return ok;
}

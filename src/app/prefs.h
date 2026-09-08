// prefs.h - the preferences file (ADR-010): text, key=value, one line each,
// written atomically (.tmp then MoveFileEx) into %LOCALAPPDATA%\minidisk\, or
// next to the exe when a file named `portable` sits there.
//
// This is a boundary (ADR-012): prefs_parse starts from the defaults, accepts
// only what it understands, clamps every number it keeps, and refuses a file
// that does not announce its version. Past it, the struct is canonical and no
// one checks a field again.
#ifndef APP_PREFS_H
#define APP_PREFS_H

#include "../base/base.h"
#include "../base/base_arena.h"
#include "../base/base_string.h"
#include "../platform/platform.h"

// The columns of the library list, in their natural order. The persisted order
// is a permutation of this enum, so moving a column never renames a key.
typedef enum AppColumn {
    AppColumn_Index = 0,
    AppColumn_Title,
    AppColumn_Artist,
    AppColumn_Album,
    AppColumn_Duration,
    AppColumn_Format,
    AppColumn_Year,
    AppColumn_Added,
    AppColumn_COUNT
} AppColumn;

#define PREFS_VERSION       1
#define PREFS_MAX_FOLDERS   8
#define PREFS_FOLDER_CAP    512
#define PREFS_COLUMN_MIN    28.0f    // dp, both ends of a resize
#define PREFS_COLUMN_MAX    720.0f
#define PREFS_WINDOW_MIN_W  640      // dp
#define PREFS_WINDOW_MIN_H  400
#define PREFS_WINDOW_MAX    16384

// T-072. Ranges are the *clamps* of the parsing boundary, and the settings
// panel offers exactly the same ones: a value that cannot be typed cannot be
// read either. Loudness and true peak are in tenths of a dB, so the whole file
// stays integers (ADR-010).
#define PREFS_LOUDNESS_MIN  (-230)   // -23,0 LUFS, EBU R128 broadcast
#define PREFS_LOUDNESS_MAX  (-90)    //  -9,0 LUFS
#define PREFS_LOUDNESS_DEF  (-140)   // -14,0 LUFS, the streaming consensus
#define PREFS_PEAK_MIN      (-60)    //  -6,0 dBTP
#define PREFS_PEAK_MAX      (0)
#define PREFS_PEAK_DEF      (-10)    //  -1,0 dBTP
#define PREFS_FADE_MAX      5000     // ms
#define PREFS_GAP_MAX       10000    // ms
#define PREFS_CACHE_TRANSCODE_MIN  64     // MB
#define PREFS_CACHE_TRANSCODE_MAX  65536
#define PREFS_CACHE_TRANSCODE_DEF  2048
#define PREFS_CACHE_COVERS_MIN     16
#define PREFS_CACHE_COVERS_MAX     8192
#define PREFS_CACHE_COVERS_DEF     256

typedef struct PrefsColumn {
    f32 width;    // dp; the Title column stretches and ignores it
    u32 order;    // position in the header row, a permutation over the columns
    b32 visible;
} PrefsColumn;

typedef struct Prefs {
    PrefsColumn columns[AppColumn_COUNT];
    u32 sort_column;  // LibSortColumn; prefs does not depend on core/, so a u32
    b32 sort_desc;
    b32 browser_collapsed;
    b32 thumbnails;       // the 48 px cover column of the library rows (T-014)
    b32 detail_collapsed; // the selection detail panel at the bottom
    u32 browser_height;   // dp, 0 = default: the artist/album columns (vertical splitter)
    u32 detail_height;    // dp, 0 = default: the detail panel

    i32 window_x, window_y;           // physical pixels, workspace coordinates
    u32 window_width, window_height;  // dp, so a DPI change does not shrink it
    b32 window_placed;                // 0: never saved, let the OS decide
    b32 window_maximized;

    u32 folder_count;
    u16 folder_size[PREFS_MAX_FOLDERS];
    u8  folder[PREFS_MAX_FOLDERS][PREFS_FOLDER_CAP];

    // --- T-072: what the preferences panel edits -----------------------------
    // Plain u32/i32 and not the ui/ and core/ enums they mirror: prefs.c is a
    // parsing boundary that depends on base and platform only (ADR-001). The
    // views cast, having clamped here once.
    u32 lang;          // StrLang: 0 fr, 1 en
    u32 theme;         // UI_ThemeChoice: 0 dark, 1 light, 2 system
    u32 default_mode;  // UI_Mode: 0 SP, 1 mono, 2 LP2, 3 LP4
    i32 loudness_lufs; // tenths of a LUFS, negative
    i32 true_peak_dbtp;// tenths of a dBTP, negative or zero
    b32 trim_silence;
    u32 fade_in_ms, fade_out_ms;
    u32 gap_ms;
    u32 cache_transcode_mb;
    u32 cache_covers_mb;
} Prefs;

void    prefs_defaults(Prefs *prefs);
String8 prefs_folder(const Prefs *prefs, u32 index);
// Appends unless it is already there. 0 when the table is full or the path does
// not fit: a domain answer, the caller tells the user.
b32     prefs_add_folder(Prefs *prefs, String8 folder);
// Drops one watched folder and closes the hole. The index is a position in the
// table, so the caller has just read it out of prefs_folder (T-072).
void    prefs_remove_folder(Prefs *prefs, u32 index);

// The two halves of the file, without touching the disk - which is also how the
// tests reach them. prefs_parse always leaves `prefs` usable: it returns 0 when
// the text was unusable and the defaults were kept.
b32     prefs_parse(Prefs *prefs, String8 text);
String8 prefs_serialize(Arena *arena, const Prefs *prefs);

// %LOCALAPPDATA%\minidisk\minidisk.prefs, or <exe folder>\minidisk.prefs when
// <exe folder>\portable exists. The folder is created if needed.
String8 prefs_path(Arena *arena);
b32     prefs_load(Prefs *prefs, String8 path, Arena *scratch);  // 0: defaults
b32     prefs_save(const Prefs *prefs, String8 path);

#endif // APP_PREFS_H

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

    i32 window_x, window_y;           // physical pixels, workspace coordinates
    u32 window_width, window_height;  // dp, so a DPI change does not shrink it
    b32 window_placed;                // 0: never saved, let the OS decide
    b32 window_maximized;

    u32 folder_count;
    u16 folder_size[PREFS_MAX_FOLDERS];
    u8  folder[PREFS_MAX_FOLDERS][PREFS_FOLDER_CAP];
} Prefs;

void    prefs_defaults(Prefs *prefs);
String8 prefs_folder(const Prefs *prefs, u32 index);
// Appends unless it is already there. 0 when the table is full or the path does
// not fit: a domain answer, the caller tells the user.
b32     prefs_add_folder(Prefs *prefs, String8 folder);

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

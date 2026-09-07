// plan_file.h - the plan on disk. Two forms, one document:
//
//   .mdplan      versioned binary, written atomically (.tmp then rename), read
//                through a mapping. This is a boundary (ADR-012): the loader
//                validates the header, every offset, every string id and every
//                group before a byte of it is trusted. A version that does not
//                match is refused, never migrated - unlike the library cache
//                the plan is *not* reconstructible, so it is refused loudly and
//                the file is left exactly as it was.
//   .mdplan.txt  one line per track, tab separated, meant to be read, diffed and
//                pasted into a mail (B-29). Titles are sanitised of tabs and
//                newlines on the way out: it is a readable view, not the master.
//
// The plan owns two arenas; loading clears both, exactly as lib_cache_load does.
#ifndef PLAN_FILE_H
#define PLAN_FILE_H

#include "plan_model.h"

#define PLAN_FILE_VERSION 1
#define PLAN_FILE_STRINGS_MAX MB(4)

typedef struct PlanFileHeader {
    u8 magic[8];  // "MDSKPLN\0"
    u32 version;
    u32 flags;
    u64 file_size;
    u64 disc_count;
    u64 entry_count;  // over every disc, so the entry section is one block
    u64 title;        // StringId of the plan's own title
    u64 strings_offset, strings_size, string_count;
    u64 discs_offset, discs_size;
    u64 entries_offset, entries_size;
    u64 created_us;
    u64 reserved[6];
} PlanFileHeader;
StaticAssert(sizeof(PlanFileHeader) == 160, plan_file_header_layout);

// A disc's record: the groups keep only their names, since first and count are
// derived from the entries' group_id column (plan_groups_refresh).
typedef struct PlanFileDisc {
    u32 title;
    u16 length_min;
    u8 default_mode;
    u8 pad_;
    u32 entry_count;
    u32 group_live;
    u32 group_name[PLAN_GROUP_MAX];
} PlanFileDisc;
StaticAssert(sizeof(PlanFileDisc) == 16 + 4 * PLAN_GROUP_MAX, plan_file_disc_layout);

typedef struct PlanFileEntry {
    u32 track_id;  // a hint: the path is what re-resolves it (plan_resolve)
    u32 path_id;
    u32 title_override;
    u32 duration_ms;
    i16 gain_db;
    u16 fade_in_ms, fade_out_ms, trim_head_ms, trim_tail_ms;
    u8 mode, flags, group_id;
    u8 pad_[3];
} PlanFileEntry;
StaticAssert(sizeof(PlanFileEntry) == 32, plan_file_entry_layout);

typedef enum PlanFileStatus {
    PlanFile_Ok = 0,
    PlanFile_Missing,     // no file, or it cannot be mapped
    PlanFile_BadVersion,  // another build wrote it: refuse, do not migrate
    PlanFile_Corrupt,     // the file exists and lies about itself
    PlanFile_WriteFailed,
} PlanFileStatus;

PlanFileStatus plan_save(const Plan *plan, String8 path);
PlanFileStatus plan_load(Plan *plan, String8 path);
PlanFileStatus plan_export_text(const Plan *plan, String8 path);
PlanFileStatus plan_import_text(Plan *plan, String8 path);

// %LOCALAPPDATA%\minidisk\plans\autosave.mdplan, the directory created on the
// way. `base_dir` is the application's data folder.
String8 plan_autosave_path(Arena *arena, String8 base_dir);
// Called once per frame. Writes when the plan has been dirty for five seconds,
// clears the flag, and returns 1 when it wrote (B-01).
b32 plan_autosave_tick(Plan *plan, String8 path, u64 now_us);

#endif // PLAN_FILE_H

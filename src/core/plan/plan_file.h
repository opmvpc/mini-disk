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
#include "../../base/base_jobs.h"

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

// --- the durable save, off the frame thread (P-009) ------------------------
// The barrier that makes a save durable (FlushFileBuffers, then a rename with
// WRITE_THROUGH) costs about 7 ms on this machine and not one of them is ours:
// it is the disk. What is ours is the encoding, half a millisecond, and that is
// the only part with any business on the frame thread.
//
// So a save is two steps. The frame thread serialises the document into the
// saver's own arena - an immutable snapshot of bytes, so the job never reads
// the live plan and no lock is ever taken on it - and hands that buffer to a
// job. The job writes, flushes, renames, and publishes its verdict in `state`,
// which the plan header reads to show whether the last save is running, done or
// failed. A save asked for while one is in flight is refused, not queued: the
// caller keeps its dirty flag and comes back on the next tick.
typedef enum PlanSaveState {
    PlanSave_Idle = 0,
    PlanSave_Running,
    PlanSave_Done,
    PlanSave_Failed,
} PlanSaveState;

typedef struct PlanSaver {
    Arena *arena;  // the snapshot and the path; cleared before each save
    JobCounter counter;
    volatile u32 state;   // PlanSaveState, written by the job, read by the view
    volatile u32 status;  // the PlanFileStatus the last finished save returned
    String8 path;
    String8 bytes;  // the snapshot the job writes
    u64 frame_us;   // what the last hand-over cost the frame thread
    u64 disk_us;    // what the job spent on the disk, for the record
    u64 saves;      // finished saves, for the tests
} PlanSaver;

void plan_saver_init(PlanSaver *saver, Arena *arena);
// 1 when the snapshot was taken and the job pushed, 0 when a save is still in
// flight. The frame cost (snapshot included) lands in `saver->frame_us`.
b32  plan_save_async(PlanSaver *saver, const Plan *plan, String8 path);
void plan_save_wait(PlanSaver *saver);  // the shutdown path, and the tests
md_inline PlanSaveState plan_save_state(const PlanSaver *saver) {
    return (PlanSaveState)os_atomic_load_u32(&saver->state);
}
// The snapshot on its own: the bytes plan_save writes, and what the bench times.
String8 plan_serialize(Arena *arena, const Plan *plan);

// %LOCALAPPDATA%\minidisk\plans\autosave.mdplan, the directory created on the
// way. `base_dir` is the application's data folder.
String8 plan_autosave_path(Arena *arena, String8 base_dir);
// Called once per frame. Hands the plan to the saver when it has been dirty for
// five seconds, clears the flag, and returns 1 when it did (B-01).
b32 plan_autosave_tick(Plan *plan, PlanSaver *saver, String8 path, u64 now_us);

#endif // PLAN_FILE_H

// plan_model.h - the disc plan: the document the user actually edits (ADR-011
// D1). A plan is a list of discs; a disc is at most 254 entries (the TOC holds
// 255 fragments, research/01 s7.5) plus its groups.
//
// The entries are SoA because every pass over them is a column pass: sum the
// durations for the header, colour one pastille per mode, measure the title
// budget. Nothing here is allocated per entry: a disc is one flat block, and a
// plan is a fixed array of discs, so a command is a memmove and never a malloc.
//
// Every mutation goes through plan_apply (plan_cmd.c): there is no setter that
// bypasses the undo stack, which is what makes B-25 (unlimited undo, bounded
// here at 256) true by construction rather than by discipline.
//
// This module knows base/ and platform.h only, never an OS header (ADR-001).
#ifndef PLAN_MODEL_H
#define PLAN_MODEL_H

#include "../../base/base.h"
#include "../../base/base_arena.h"
#include "../../base/base_string.h"
#include "../../platform/platform.h"
#include "../library/lib_model.h"

#define PLAN_ENTRY_MAX   254  // B-28: the TOC cannot hold more (research/01 s7.5)
#define PLAN_GROUP_MAX   32   // one u32 of liveness, which SplitDisc saves whole
#define PLAN_DISC_MAX    8    // one afternoon of burning; a dimensioning choice
#define PLAN_UNDO_MAX    256  // bounded, ADR-011: the stack is part of the struct
#define PLAN_GROUP_NONE  255
#define PLAN_TITLE_MAX   256  // bytes of one title, before the TOC budget (B-19)

// Keystrokes inside one title fold into a single undo step while they keep
// coming; a pause, or any other command, closes the run (B-25).
#define PLAN_COALESCE_US 800000
#define PLAN_AUTOSAVE_US 5000000

typedef enum PlanMode {
    PlanMode_SP = 0,  // D7: the only one phase 5 can burn
    PlanMode_LP2,
    PlanMode_LP4,
    PlanMode_COUNT
} PlanMode;

typedef enum PlanEntryFlag {
    PlanEntryFlag_Mono    = 1u << 0,  // SP mono: twice the disc, half the channels
    PlanEntryFlag_Missing = 1u << 1,  // neither the id nor the path resolved
} PlanEntryFlag;
#define PLAN_ENTRY_FLAG_MASK (PlanEntryFlag_Mono | PlanEntryFlag_Missing)

// 60 / 74 / 80 minutes of SP (research/01 s7.5). Chosen without a disc in the
// drive (B-10); a disc that is actually inserted overrides it (B-11, T-031).
#define PLAN_LENGTH_DEFAULT 80
md_inline b32 plan_length_valid(u32 minutes) {
    return minutes == 60 || minutes == 74 || minutes == 80;
}

#define PLAN_GAIN_NONE ((i16)-32768)  // no per track gain, in 1/256 dB

// One entry, gathered from the columns. The commands carry this shape, the
// text export writes it, and nothing stores an array of it.
typedef struct PlanEntry {
    TrackId track_id;      // LIB_TRACK_NONE when the library no longer has it
    StringId path_id;      // always written: the path is what re-resolves an id
    StringId title_override;  // 0: use the library title
    u32 duration_ms;       // copied in, so a plan reads standalone
    i16 gain_db;           // 1/256 dB, PLAN_GAIN_NONE when absent
    u16 fade_in_ms, fade_out_ms;    // B-14
    u16 trim_head_ms, trim_tail_ms; // B-13
    u8 mode;               // PlanMode
    u8 flags;              // PlanEntryFlag
    u8 group_id;           // PLAN_GROUP_NONE when the entry belongs to none
} PlanEntry;

// B-17. `first` and `count` are derived from the group_id column by
// plan_groups_refresh, so a move or a removal never has to fix them up: the
// membership travels with the entry, and the range is recomputed after.
typedef struct PlanGroup {
    StringId name;
    u16 first;
    u16 count;
} PlanGroup;

typedef struct PlanDisc {
    StringId title;
    u16 length_min;   // 60 / 74 / 80
    u8 default_mode;  // PlanMode, applied to whatever is added next (B-06)
    u8 pad_;
    u32 entry_count;
    u32 group_live;   // bit i: groups[i] exists
    PlanGroup groups[PLAN_GROUP_MAX];

    // --- SoA ---------------------------------------------------------------
    u32 track_id[PLAN_ENTRY_MAX];
    u32 path_id[PLAN_ENTRY_MAX];
    u32 title_override[PLAN_ENTRY_MAX];
    u32 duration_ms[PLAN_ENTRY_MAX];
    i16 gain_db[PLAN_ENTRY_MAX];
    u16 fade_in_ms[PLAN_ENTRY_MAX];
    u16 fade_out_ms[PLAN_ENTRY_MAX];
    u16 trim_head_ms[PLAN_ENTRY_MAX];
    u16 trim_tail_ms[PLAN_ENTRY_MAX];
    u8 mode[PLAN_ENTRY_MAX];
    u8 flags[PLAN_ENTRY_MAX];
    u8 group_id[PLAN_ENTRY_MAX];
} PlanDisc;

// --- commands ---------------------------------------------------------------
typedef enum PlanCmdKind {
    PlanCmd_None = 0,
    PlanCmd_Add,
    PlanCmd_Remove,
    PlanCmd_Move,
    PlanCmd_SetMode,
    PlanCmd_SetTitle,
    PlanCmd_SetDiscTitle,
    PlanCmd_Group,
    PlanCmd_Ungroup,
    PlanCmd_SetDiscLength,
    PlanCmd_SplitDisc,
    PlanCmd_COUNT
} PlanCmdKind;

// One flat record, no union: the stack is a plain array and a command is 80
// bytes, so 256 of them are 20 KB of the plan struct and never a pointer chase.
// Which fields carry meaning is `kind`'s business, and plan_cmd.c's alone.
typedef struct PlanCmd {
    u8 kind;
    u8 disc;
    u8 group;      // Group / Ungroup: the slot that was taken or freed
    u8 pad_;
    u32 index;     // entry index; Move: from; Group: first; SplitDisc: cut
    u32 aux;       // Move: to; Group / Ungroup: count
    u32 old_value; // StringId, mode|mono, disc length, group live mask
    u32 new_value;
    u64 time_us;   // when it was recorded, for the coalescing window
    PlanEntry entry;  // Add / Remove: the payload, saved when it is applied
} PlanCmd;

// --- events ------------------------------------------------------------------
// Main thread only: the plan is edited by the UI and read by the UI. A ring
// all the same, so a frame that applies twenty commands drains one queue.
typedef enum PlanEventKind {
    PlanEvent_None = 0,
    PlanEvent_Changed,  // one command was applied, undone or redone
    PlanEvent_Loaded,   // the whole document was replaced (open, recovery)
    PlanEvent_COUNT
} PlanEventKind;

typedef struct PlanEvent {
    u32 kind;
    u32 cmd;    // PlanCmdKind
    u32 disc;
    u32 index;
    u32 direction;  // 0 apply, 1 undo, 2 redo
} PlanEvent;

#define PLAN_EVENT_CAPACITY 64  // power of two

typedef struct PlanEventQueue {
    PlanEvent items[PLAN_EVENT_CAPACITY];
    u32 write, read;
} PlanEventQueue;

// --- the document -------------------------------------------------------------
typedef struct Plan {
    Arena *arena;  // the intern table's slots
    Arena *text;   // the interned bytes
    StringTable strings;

    StringId title;
    u32 disc_count;
    PlanDisc discs[PLAN_DISC_MAX];

    // The stack, as three absolute counters over a ring of PLAN_UNDO_MAX:
    // [first, done) can be undone, [done, last) can be redone. Pushing past
    // the capacity drops the oldest command, which then becomes history.
    PlanCmd undo[PLAN_UNDO_MAX];
    u32 first, done, last;
    b32 coalesce_open;  // the last command may still absorb a keystroke

    u32 revision;    // bumped by every change; the view compares and rebuilds
    b32 dirty;       // something changed since the last save
    u64 dirty_us;    // when it first went dirty, for the 5 s autosave
    PlanEventQueue events;
} Plan;

// --- plan_model.c ------------------------------------------------------------
// `arena` holds the intern slots, `text` the interned bytes (they may be the
// same). The plan starts with one empty 80 minute disc: a document, always.
void plan_init(Plan *plan, Arena *arena, Arena *text);
void plan_clear(Plan *plan);  // back to one empty disc, undo stack emptied

md_inline PlanDisc *plan_disc(Plan *plan, u32 disc) {
    Assert(disc < plan->disc_count);
    return &plan->discs[disc];
}
md_inline String8 plan_string(const Plan *plan, StringId id) {
    return lib_string(&plan->strings, id);
}

PlanEntry plan_entry(const PlanDisc *disc, u32 index);
void      plan_entry_store(PlanDisc *disc, u32 index, const PlanEntry *entry);
// Recomputes every live group's first / count from the group_id column.
void      plan_groups_refresh(PlanDisc *disc);
u64       plan_disc_duration_ms(const PlanDisc *disc);
u64       plan_duration_ms(const Plan *plan);
u32       plan_entry_count(const Plan *plan);

// The title a row shows: the override, else the library's, else the file name.
String8 plan_entry_title(const Plan *plan, const Library *lib, u32 disc, u32 index);

// Re-resolution at load time: an entry whose TrackId no longer names its path
// is looked up by path; when that misses too it is flagged Missing and kept.
// Never drops an entry - a plan is not repaired behind the user's back.
u32 plan_resolve(Plan *plan, const Library *lib);  // entries still missing

b32 plan_events_next(PlanEventQueue *queue, PlanEvent *out);

// --- plan_cmd.c ---------------------------------------------------------------
// The one door in. Returns 0 when the command does not apply (a full disc, an
// out of range index, a group that straddles a cut): a refusal, not a failure.
b32 plan_apply(Plan *plan, PlanCmd cmd);
b32 plan_undo(Plan *plan);
b32 plan_redo(Plan *plan);
b32 plan_can_undo(const Plan *plan);
b32 plan_can_redo(const Plan *plan);
// Closes the coalescing window: the next title edit starts its own undo step.
void plan_coalesce_break(Plan *plan);

// The builders. They are the whole vocabulary of the editor.
b32 plan_add(Plan *plan, u32 disc, u32 index, PlanEntry entry);
b32 plan_add_track(Plan *plan, const Library *lib, u32 disc, TrackId id);
b32 plan_remove(Plan *plan, u32 disc, u32 index);
b32 plan_move(Plan *plan, u32 disc, u32 from, u32 to);
b32 plan_set_mode(Plan *plan, u32 disc, u32 index, u32 mode, b32 mono);
b32 plan_set_title(Plan *plan, u32 disc, u32 index, String8 title, u64 now_us);
b32 plan_set_disc_title(Plan *plan, u32 disc, String8 title, u64 now_us);
b32 plan_group(Plan *plan, u32 disc, u32 first, u32 count, String8 name);
b32 plan_ungroup(Plan *plan, u32 disc, u32 group);
b32 plan_set_disc_length(Plan *plan, u32 disc, u32 minutes);
b32 plan_split_disc(Plan *plan, u32 disc, u32 index);

#endif // PLAN_MODEL_H

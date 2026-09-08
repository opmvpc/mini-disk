// netmd_edit.h - changing what is on the disc: titles, order, groups, erasure
// (research/01 s3.9, s3.10, s3.12, s6.2, s6.5, s7.4-7.5).
//
// Two rules shape this whole module, and both come from ADR-011 D4.
//
//   1. Nothing is written before it has been *simulated*. Every edit first
//      produces a DiscDiff out of the DiscLayout already in memory - what the
//      disc looks like now, what it would look like afterwards, what it costs
//      in TOC cells - and that diff is what the confirmation panel shows. A
//      refusal (protected disc, budget overflow, nothing to do) is decided
//      there, with no USB traffic at all.
//   2. The TOC is a magneto-optical structure that is rewritten in place and
//      lives in the device's RAM until the disc is ejected (s6.3). A wrong
//      `oldLen` corrupts it (s3.9, pitfall 9), an identical title crashes some
//      machines (pitfall 10), and every extra rewrite is wear. So: the current
//      title is read back from the device immediately before it is written, an
//      unchanged title is not written at all, and the disc title - which
//      carries the whole group syntax - is compiled once and written once
//      (s6.5).
//
// The cell budget is not recomputed here: it is plan_toc's, the same 255 x 7
// the plan panel shows (ADR-011 D3). A disc read off the device and a disc
// being planned spend from the same pot in the same way, and there is exactly
// one implementation of that arithmetic.
#ifndef NETMD_EDIT_H
#define NETMD_EDIT_H

#include "../plan/plan_toc.h"
#include "netmd_disc.h"

// s3.9 / pitfall 8: the wchar byte says *which* title space is addressed, and
// it is not the same value for a disc and for a track. Confusing the two writes
// a track title into the disc's space.
#define NETMD_WCHAR_DISC_HALF 0x00u
#define NETMD_WCHAR_DISC_FULL 0x01u
#define NETMD_WCHAR_TRACK_HALF 0x02u
#define NETMD_WCHAR_TRACK_FULL 0x03u

// s6.2, the two mandatory waits of an edit session: the device is writing its
// TOC and answers REJECTED to whatever arrives during that time.
#define NETMD_EDIT_HOLD_MS 100
#define NETMD_EDIT_RELIST_HOLD_MS 500

// Sharp renames the *disc* through audioUTOC1TD, not discTitleTD (s3.9 pt 2).
#define NETMD_VID_SHARP 0x04DDu

// --- text (s3.11, write direction) -------------------------------------------
// UTF-8 -> the Shift-JIS bytes the TOC holds. The input is sanitized first with
// plan_toc_sanitize, so what comes out is ASCII 0x20..0x7E and half-width
// katakana and nothing else - which makes the encoding arithmetic rather than a
// table, and makes what we count, what we preview and what the device shows the
// same three things. Returns the bytes written.
u64 netmd_utf8_to_sjis(String8 in, u8 *out, u64 cap);

// --- what one edit asks for ---------------------------------------------------
typedef enum NetmdEditKind {
    NetmdEditKind_None = 0,
    NetmdEditKind_RenameDisc,     // `title`
    NetmdEditKind_RenameTrack,    // `track`, `title`
    NetmdEditKind_MoveTrack,      // `track` -> `dest`
    NetmdEditKind_EraseTracks,    // `mask`
    NetmdEditKind_EraseDisc,
    NetmdEditKind_CreateGroup,    // `mask` (a contiguous run), `title`
    NetmdEditKind_DissolveGroup,  // `track` = the group index
    NetmdEditKind_COUNT
} NetmdEditKind;

// Why an edit will not happen. Every one of these is a domain answer with a
// sentence behind it, not a failure: the panel says which one and why.
typedef enum NetmdEditRefusal {
    NetmdEditRefusal_None = 0,
    NetmdEditRefusal_NoDisc,
    NetmdEditRefusal_Protected,       // the tab is closed, or the disc is pre-mastered (s7.5)
    NetmdEditRefusal_TrackProtected,  // flags == 0x03, checked out by SonicStage (s7.5)
    NetmdEditRefusal_Budget,          // 255 cells of 7 characters (s7.4)
    NetmdEditRefusal_Nothing,         // the disc already says exactly this
    NetmdEditRefusal_Range,           // no such track, or no such group
    NetmdEditRefusal_Grouped,         // a track belongs to one group at most (s3.10)
    NetmdEditRefusal_Backup,          // the TOC backup could not be written, so nothing is (D4)
    NetmdEditRefusal_COUNT
} NetmdEditRefusal;

// 255 tracks, one bit each.
#define NETMD_MASK_WORDS 8

typedef struct NetmdEditRequest {
    u32 kind;
    u32 track;  // rename target, move source, or group index
    u32 dest;   // move destination
    u32 title_size;
    u8 title[NETMD_TITLE_MAX];
    u32 mask[NETMD_MASK_WORDS];
} NetmdEditRequest;

md_inline void netmd_mask_set(u32 *mask, u32 index) { mask[index >> 5] |= 1u << (index & 31u); }
md_inline b32 netmd_mask_get(const u32 *mask, u32 index) {
    return (b32)((mask[index >> 5] >> (index & 31u)) & 1u);
}
u32 netmd_mask_count(const u32 *mask, u32 track_count);

// --- the simulation (D4) -------------------------------------------------------
// Everything the confirmation panel needs. `before` / `after` hold the one piece
// of text the edit changes (a title, or the compiled disc title when groups
// move), so the panel can put them side by side without knowing what kind of
// edit it is looking at.
typedef struct DiscDiff {
    u32 kind;
    u32 refusal;
    b32 allowed;
    u32 changed;  // tracks the edit touches: "Erase 3 tracks"
    u32 tracks_before, tracks_after;
    u32 groups_before, groups_after;
    u32 cells_before, cells_after;
    u32 cells_free_after;
    u32 chars_free_after;
    u32 writes;  // TOC rewrites the sequence will cost, s6.5
    // P-014: how many tracks of the selection this session wrote itself. The
    // device still reports them protected because it has not flushed its TOC
    // (s6.3), which is not the same thing as "checked out by SonicStage". The
    // edit is allowed and the panel warns instead of refusing; if the device
    // really means it, it answers REJECTED, and that path already exists.
    u32 written_here;
    u16 before_size, after_size;
    u8 before[NETMD_DISC_TITLE_MAX];
    u8 after[NETMD_DISC_TITLE_MAX];
} DiscDiff;

// The cells `layout` spends, and the raw disc title it compiles into (the one
// that would be written). `raw_out` may be 0.
u32 netmd_layout_cells(const DiscLayout *layout, u8 *raw_out, u64 raw_cap, u64 *raw_size_out);

// No USB, no allocation, no side effect: `after` is what the device would hold,
// `out` is what the user is asked to confirm. `after` may be 0 when only the
// diff is wanted.
void netmd_edit_simulate(const DiscLayout *before, const NetmdEditRequest *request,
                         DiscLayout *after, DiscDiff *out);

// --- the writes (s3.9, s3.12) --------------------------------------------------
// Each one reads the current title back first and returns without writing when
// it already says that (`out_written` = 0). `title` is UTF-8 and is sanitized
// on the way down.
u32 netmd_set_disc_title(NetmdSession *session, Arena *arena, String8 title, b32 *out_written);
u32 netmd_set_track_title(NetmdSession *session, Arena *arena, u32 track, String8 title,
                          b32 *out_written);
u32 netmd_move_track(NetmdSession *session, Arena *arena, u32 from, u32 to);
u32 netmd_erase_track(NetmdSession *session, Arena *arena, u32 track);
u32 netmd_erase_disc(NetmdSession *session, Arena *arena);

// The whole sequence for one request, in the order of s6.5: acquire, the track
// level changes, then one single disc title write carrying every group, then
// release - on every path, including the failing ones. `after` is the layout
// netmd_edit_simulate produced; `out_writes` counts the TOC rewrites made.
u32 netmd_edit_apply(NetmdSession *session, Arena *arena, const DiscLayout *after,
                     const NetmdEditRequest *request, u32 *out_writes);

#endif  // NETMD_EDIT_H

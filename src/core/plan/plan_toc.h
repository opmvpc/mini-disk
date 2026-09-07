// plan_toc.h - the title budget (ADR-011 D3). The TOC holds 255 usable cells of
// 7 characters, and the disc title, the group syntax encoded inside it and every
// track title all spend from that one pot (research/01 s7.4). A non-SP track
// costs a cell even with no title at all, because the device prefixes "LP:" on
// its own.
//
// Titles are sanitized towards what the TOC can hold before anything is counted:
// the tables are generated from Unicode by tools/gen_charset_tables.py (ADR-008,
// never copied from another implementation) into plan_charset.h. Accents fold to
// ASCII, kana fold to half-width katakana, and what neither can represent is
// dropped - so what we count and what we preview is what the device will show.
//
// Shortening only ever happens on overflow, in a fixed order, and the caller can
// see exactly which step fired: a silent truncation is the surprise this module
// exists to avoid.
#ifndef PLAN_TOC_H
#define PLAN_TOC_H

#include "plan_model.h"

#define PLAN_TOC_CELLS       255u  // 256 in the TOC, one reserved
#define PLAN_TOC_CELL_CHARS  7u
#define PLAN_TOC_CHARS_MAX   (PLAN_TOC_CELLS * PLAN_TOC_CELL_CHARS)  // 1785
#define PLAN_TOC_RAW_MAX     2048u  // the compiled disc title, group syntax included

md_inline u32 plan_toc_cells_for_chars(u32 chars) {
    return (chars + PLAN_TOC_CELL_CHARS - 1) / PLAN_TOC_CELL_CHARS;
}

// --- sanitize ----------------------------------------------------------------
// UTF-8 in, UTF-8 out, but only characters the TOC can hold: ASCII 0x20..0x7E
// and half-width katakana. Full-width ASCII is folded down, accents are folded,
// kana are folded to half-width (a voiced kana becoming two characters, which is
// why it costs two), everything else is dropped. Returns the bytes written;
// `chars_out` receives the character count, which is the budget unit.
u64 plan_toc_sanitize(String8 in, u8 *out, u64 cap, u32 *chars_out);
// Characters of an already sanitized string (its codepoints).
u32 plan_toc_halfwidth_len(String8 sanitized);
// Cells one title costs. `non_sp`: LP2/LP4, which cost a cell even when empty.
u32 plan_toc_cells_for_title(String8 title, b32 non_sp);

// --- shortening ---------------------------------------------------------------
// Ordered, deterministic, and applied only when the sanitized title is longer
// than `max_chars` (0 = no limit). Each step is tried in turn and the first one
// that brings the title inside the budget wins.
typedef enum PlanShorten {
    PlanShorten_Feat      = 1u << 0,  // "(feat. X)", " feat. X", " ft. X"
    PlanShorten_Brackets  = 1u << 1,  // "(Remastered)", "[Live]", "(2011 Remaster)"
    PlanShorten_Artist    = 1u << 2,  // "Artist - Title": the artist gives way first
    PlanShorten_Title     = 1u << 3   // last resort: a hard cut
} PlanShorten;

typedef struct PlanTitlePreview {
    u8 text[PLAN_TITLE_MAX];  // the title exactly as it will be written
    u64 size;
    u32 chars;
    u32 cells;
    u32 applied;   // the PlanShorten steps that fired, none when it already fit
    b32 truncated;  // PlanShorten_Title had to cut into the title itself
} PlanTitlePreview;

void plan_toc_preview(String8 title, u32 max_chars, PlanTitlePreview *out);

// --- the disc budget -----------------------------------------------------------
typedef struct PlanTocBudget {
    u32 cells_total;   // PLAN_TOC_CELLS
    u32 cells_disc;    // the disc title with as much group syntax as fitted
    u32 cells_tracks;
    u32 cells_used;
    u32 cells_free;
    u32 chars_free;    // what the UI shows: "340 characters left on this disc"
    b32 overflow;      // the track titles alone do not fit
    u32 groups_total;
    u32 groups_kept;   // groups dropped by the budget, in order (research/01 s7.4)
    u64 raw_size;
    u8 raw[PLAN_TOC_RAW_MAX];   // "0;Album//1-4;Side A//5-9;Side B//"
    u32 cells[PLAN_ENTRY_MAX];  // per track, for the TOC bar of the gauge
} PlanTocBudget;

void plan_toc_budget(const Plan *plan, const Library *lib, u32 disc_index, PlanTocBudget *out);

// One group as the compiler sees it. The plan has its own group struct and so
// does a disc read off the device (T-022), but the string they compile into is
// the same one: it is written once, here, and both sides hand it this.
typedef struct PlanTocGroup {
    u32 first;  // 0-based track index
    u32 count;
    String8 name;  // already sanitized or not: the compiler sanitizes anyway
} PlanTocGroup;

// "0;Album//1-4;Side A//5-9;Side B//" (research/01 s3.10). `groups` must be
// sorted by `first`. Returns the bytes written.
u64 plan_toc_compile_raw(String8 title, const PlanTocGroup *groups, u32 group_count,
                         u32 cells_budget, u8 *out, u64 cap, u32 *groups_kept);

// The disc title compiled into its raw form, dropping the groups that do not
// fit rather than failing whole. Returns the bytes written.
u64 plan_toc_compile_disc_title(const Plan *plan, const PlanDisc *disc, u32 cells_budget, u8 *out,
                                u64 cap, u32 *groups_kept);

// --- automatic titling (D3) -----------------------------------------------------
typedef enum PlanTitleTemplate {
    PlanTitleTemplate_Title = 0,        // {title}
    PlanTitleTemplate_ArtistTitle,      // {artist} - {title}
    PlanTitleTemplate_NumberTitle,      // {n}. {title}
    PlanTitleTemplate_COUNT
} PlanTitleTemplate;

// Fills `out` with the rendered template (not yet sanitized). Returns the bytes.
u64 plan_toc_format(u32 template_id, String8 artist, String8 title, u32 number, u8 *out, u64 cap);

// The disc title we propose: the album every entry shares, else the artist they
// share, else the album that dominates (more than half of them), else empty.
String8 plan_toc_propose_disc_title(const Plan *plan, const Library *lib, u32 disc_index);

typedef struct PlanProposedGroup {
    u32 first;
    u32 count;
    String8 name;
} PlanProposedGroup;

// One group per run of consecutive entries sharing an album, runs of one left
// ungrouped. Returns how many were written into `out`.
u32 plan_toc_propose_groups(const Plan *plan, const Library *lib, u32 disc_index,
                            PlanProposedGroup *out, u32 cap);

#endif  // PLAN_TOC_H

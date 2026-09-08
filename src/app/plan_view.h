// plan_view.h - the plan view with no pixel in it: the geometry of the capacity
// gauge and the mapping from a gesture to a plan command (T-032).
//
// Everything a test would otherwise have to drive a window to reach lives here:
// where a segment starts, how wide its hatched tail is, which entry a drop
// lands on, how many tracks of a selection still fit, what quota an automatic
// shortening gives each title. view_plan.c draws what this computes and applies
// what it decides, and holds nothing else of its own.
//
// No UI, no GL, no allocation: `out` is a struct the caller owns, reused across
// frames, and the two loops here are the whole cost of the gauge.
#ifndef PLAN_VIEW_H
#define PLAN_VIEW_H

#include "../base/base.h"
#include "../core/plan/plan_capacity.h"
#include "../core/plan/plan_toc.h"

// --- the vertical rhythm of research/02 s9.2, in dp --------------------------
// The bands, top to bottom. Whole dp and not floats, so that the assert below
// can add them up at compile time: changing one without changing the total is
// then an error and not a drawing that is one pixel off.
//
// s9.2 states 56 px and then lists bands that add up to 76; the figure beside
// the table is the one that fits, so these are the figure's numbers (4 px
// margins, a 12 px scale band and a 12 px readout) and the total is the 56 px
// the section and the ticket both ask for.
#define PLAN_GAUGE_MARGIN_DP      4
#define PLAN_GAUGE_BAR_DP        14
#define PLAN_GAUGE_BAR_GAP_DP     2
#define PLAN_GAUGE_TICK_DP        4
#define PLAN_GAUGE_SCALE_DP      12
#define PLAN_GAUGE_SCALE_GAP_DP   2
#define PLAN_GAUGE_READOUT_DP    12
#define PLAN_GAUGE_TOC_GAP_DP     2
#define PLAN_GAUGE_TOC_DP         4
#define PLAN_GAUGE_HEIGHT_DP     56
#define PLAN_GAUGE_SIDE_DP       12  // the gauge is inset by this much
StaticAssert(PLAN_GAUGE_MARGIN_DP + PLAN_GAUGE_BAR_DP + PLAN_GAUGE_BAR_GAP_DP +
                     PLAN_GAUGE_TICK_DP + PLAN_GAUGE_SCALE_DP + PLAN_GAUGE_SCALE_GAP_DP +
                     PLAN_GAUGE_READOUT_DP + PLAN_GAUGE_TOC_GAP_DP + PLAN_GAUGE_TOC_DP ==
                 PLAN_GAUGE_HEIGHT_DP,
             plan_gauge_bands_sum_to_56);

// The compact variant of s9.10: one bar, the readout beside it, nothing else.
#define PLAN_GAUGE_COMPACT_DP      12
#define PLAN_GAUGE_COMPACT_BAR_DP   8

// s9.3: under two pixels a segment is not a segment, so consecutive entries are
// merged into one and the tooltip lists them ("3 tracks, 0:38").
#define PLAN_GAUGE_SEGMENT_MIN_PX 2
// s9.9: from this width on, a segment carries the initial of its mode.
#define PLAN_GAUGE_INITIAL_MIN_PX 16
// s9.4: the minor ticks (every 2 min) go away on a narrow gauge.
#define PLAN_GAUGE_MINOR_MIN_PX 400
// s9.3: a padding that rounds to nothing is still shown, one pixel of it.
#define PLAN_GAUGE_HATCH_MIN_PX 1

// One drawn segment. `first`/`count` are the entries it stands for: exactly one
// unless they were too thin to be told apart.
typedef struct PlanGaugeSegment {
    f32 x;        // left edge, physical pixels from the left of the bar
    f32 width;    // the whole segment, hatched tail included
    f32 hatch_x;  // where the wasted end of the last cluster starts
    u32 first;
    u32 count;
    u32 clusters;
    u8 mode;       // PlanCapMode: the colour
    u8 alternate;  // draw the +6 % luminance variant (s9.3)
    u8 fit;        // PlanFit of the first entry it covers
} PlanGaugeSegment;

typedef struct PlanGaugeLayout {
    f32 width;       // the bar the layout was computed for
    f32 used_width;  // where the free zone begins, == width once it overflows
    f32 overflow_x;  // where the red zone begins, == width while it fits
    b32 overflow;
    u32 count;
    PlanGaugeSegment segments[PLAN_ENTRY_MAX];
} PlanGaugeLayout;

// Pure. Segment edges are computed from the *running* cluster total rather than
// from each entry's own width, so the rounding error never accumulates: the
// widths sum to the used width exactly and the order is the plan's order.
void plan_gauge_layout(const PlanCapacity *capacity, f32 width, PlanGaugeLayout *out);

// The segment under `x` (a position in the bar), `count` when there is none:
// the hover playhead and the click that selects a track both ask this.
u32 plan_gauge_segment_at(const PlanGaugeLayout *layout, f32 x);
// The disc position `x` stands for, in milliseconds of the reference mode.
u64 plan_gauge_time_at(const PlanCapacity *capacity, const PlanGaugeLayout *layout, f32 x);

// --- the one duration the plan is said in (T-071) ----------------------------
// Clusters x 2 s, the link cluster of T-045 included: what the *disc* is spent,
// which is what the gauge reads. The per mode billed sum (`PlanCapacity.billed_ms`)
// answers a different question - what the tracks cost in their own modes - and
// two different numbers for "how long is this plan", side by side, are two
// numbers the user has to reconcile. So it goes in a tooltip and this one goes
// everywhere else: header, status bar, pre-flight.
md_inline u64 plan_disc_ms(u32 clusters) { return (u64)clusters * PLAN_CLUSTER_SP_MS; }
md_inline u64 plan_disc_used_ms(const PlanCapacity *capacity) {
    return plan_disc_ms(capacity->used_clusters);
}
md_inline u64 plan_disc_total_ms(const PlanCapacity *capacity) {
    return plan_disc_ms(capacity->capacity_clusters);
}

// The mode of the plan, in the sense of s9.4: the one most of it is written in,
// which is what the scale is graduated in. SP when the disc is empty.
u32 plan_gauge_reference_mode(const PlanCapacity *capacity);

// --- gestures ----------------------------------------------------------------
typedef struct PlanModeStep {
    u32 mode;  // PlanMode
    b32 mono;
} PlanModeStep;

// The badge cycles SP -> LP2 -> LP4 -> SP mono -> SP (T-032). One click, one
// step, and four clicks are the identity.
PlanModeStep plan_mode_cycle(u32 mode, b32 mono);
// A drop between rows: `insert_before` is the gap the insertion line sits in,
// 0 to count. plan_move takes an index in the list *after* the entry left it,
// so a row dragged downwards lands one short of the gap it was dropped in.
// Returns `from` when the drop changes nothing, which plan_move refuses.
u32 plan_drop_index(u32 from, u32 insert_before);

// How many of `durations` still fit, walked in order and stopping at the first
// one that does not (B-08, "fill the remaining space with the selection").
// `used_clusters` is the disc's, and it is advanced as the walk goes.
u32 plan_fill_count(const PlanCapacity *capacity, const u32 *durations, u32 count, u32 cap_mode);

// The per title character quota of "shorten automatically" (s9.7): titles under
// it are untouched, titles over it are cut to it, and the quota is the largest
// one whose result still fits in `cells_budget` cells. The budget is counted in
// cells and not in characters because that is what the TOC actually spends: a
// title of eight characters costs the same two cells as one of fourteen.
// `non_sp` is one byte per title, non zero for LP2/LP4, which cost a cell even
// when empty. Returns 0 when no quota fits, which is the caller's signal that
// shortening alone cannot save this disc.
u32 plan_shorten_quota(const u32 *chars, const u8 *non_sp, u32 count, u32 cells_budget);

// --- the gestures that touch more than one entry -------------------------------
// Each of these is one undo step: the user did one thing. They take the entries
// in plan order and the view is what decides which ones those are. No UI, so a
// test drives them exactly as a click does.
u32 plan_remove_entries(Plan *plan, u32 disc, const u32 *entries, u32 count);
u32 plan_set_mode_entries(Plan *plan, u32 disc, const u32 *entries, u32 count, PlanModeStep step);
// The automatic shortening of s9.7: one quota, applied to the titles that are
// over it, as title overrides. Returns the quota it used, 0 when it gave up.
u32 plan_shorten_apply(Plan *plan, const Library *lib, u32 disc, u32 cells_budget);

#endif  // PLAN_VIEW_H

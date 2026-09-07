// plan_capacity.h - what actually fits on the disc (ADR-011 D2). A MiniDisc is
// written in clusters, not in seconds: every track is rounded UP to a whole
// cluster, so a plan that sums to 79:58 of audio can still be 80:04 of disc and
// not fit. A gauge that adds durations lies (research/02 s3.1); this module is
// what makes ours true.
//
// Everything here is integer milliseconds and whole clusters. No float, no
// division by a runtime zero, no allocation: one PlanCapacity is filled in one
// pass over the SoA columns, which is what keeps a full recompute of 254 tracks
// under the 50 us the ticket asks for.
//
// core/ only: base + plan_model, never an OS header (ADR-001).
#ifndef PLAN_CAPACITY_H
#define PLAN_CAPACITY_H

#include "plan_model.h"

// The four billing modes. Same order as UI_Mode so the gauge can index its
// colours with a PlanCapMode and nothing has to translate (T-032).
typedef enum PlanCapMode {
    PlanCapMode_SP = 0,
    PlanCapMode_Mono,
    PlanCapMode_LP2,
    PlanCapMode_LP4,
    PlanCapMode_COUNT
} PlanCapMode;

// MD_MODE_TABLE - the one place the cluster arithmetic lives.
//
// A cluster is 36 sectors of 2352 bytes and carries ~2 s of SP stereo audio
// (research/01 s7.3, s7.6). The other modes pack more audio into that same
// cluster: mono halves the channel count, LP2 and LP4 are ATRAC3 at 132 and
// 66 kbit/s, so one cluster carries 4 s, 4 s and 8 s respectively
// (research/02 s3.1). A track therefore costs
//     clusters = ceil(duration_ms / cluster_ms), at least 1.
//
// VALIDATED FOR SP ON 2026-09-07 (T-045, from the seven measurements of T-043
// on a real MZ-N505). The 2 000 ms step of the SP row is confirmed to the
// millisecond; what the table was missing is the fixed per track overhead
// below. The LP2, LP4 and mono cluster sizes are still UNMEASURED, because the
// device only ever encodes SP in v1 - nothing we write goes through those rows,
// so they stay the documented estimate (research/01 s7.2 flags the mono row in
// particular as model dependent: some units do not double the running time).
// The device's own getDiscCapacity still wins whenever a disc is inserted
// (Q-29).
typedef struct PlanModeSpec {
    u32 cluster_ms;   // audio milliseconds one cluster carries in this mode
    u32 kbit_per_s;   // nominal bitrate, for the tooltip and nothing else
    const char *name;
} PlanModeSpec;

extern const PlanModeSpec MD_MODE_TABLE[PlanCapMode_COUNT];

#define PLAN_CLUSTER_SP_MS 2000u  // the unit the disc capacity is counted in

// The fixed cost of *being a track*, on top of the audio. Measured on the real
// MZ-N505 on 2026-09-07: seven SP uploads, one session each, the device's own
// free time read before and after every one of them. Every track cost
// ceil(duration / 2 s) x 2 s **plus 2 007 ms on average** (sigma 80 ms over the
// seven), i.e. exactly one more cluster (T-043, section "MD_MODE_TABLE").
//
// Measured in SP only. It is applied to every mode all the same, and always as
// one *disc* cluster of PLAN_CLUSTER_SP_MS, because what it pays for is the
// link/TOC cluster the disc spends per track - not audio. That is why an LP4
// track costs one 8 000 ms cluster of audio plus one 2 000 ms cluster of disc,
// and why plan_padding_ms deliberately ignores it: no listener ever hears it.
#define PLAN_TRACK_OVERHEAD_CLUSTERS 1u

// 60 / 74 / 80 minutes of SP -> whole clusters. 80 min = 2400 clusters.
md_inline u32 plan_clusters_capacity(u32 minutes) {
    return (minutes * 60u * 1000u) / PLAN_CLUSTER_SP_MS;
}

// The billing mode of an entry: PlanMode plus the Mono flag (SP only).
md_inline u32 plan_cap_mode(u32 mode, b32 mono) {
    if (mode == PlanMode_LP2) { return PlanCapMode_LP2; }
    if (mode == PlanMode_LP4) { return PlanCapMode_LP4; }
    return mono ? PlanCapMode_Mono : PlanCapMode_SP;
}
md_inline u32 plan_cap_mode_of(const PlanDisc *disc, u32 index) {
    return plan_cap_mode(disc->mode[index], (disc->flags[index] & PlanEntryFlag_Mono) != 0);
}

// Clusters a track costs the disc: the audio rounded up to whole clusters, plus
// PLAN_TRACK_OVERHEAD_CLUSTERS. Never below 1 + the overhead: even 40 ms of
// audio takes a whole cluster of its own, which is the whole point of D2.
u32 plan_clusters_for(u32 duration_ms, u32 cap_mode);
// The audio milliseconds that rounding wastes at the end of the track. The
// overhead cluster is NOT in here: it is not wasted audio, it is a cluster of
// the disc the track spends whatever its length. The gauge hatches this and
// only this; the overhead is part of the segment's own width.
u32 plan_padding_ms(u32 duration_ms, u32 cap_mode);

// The disc milliseconds a run of `clusters` carrying `entries` tracks costs:
// the audio clusters at the mode's own rate plus one PLAN_CLUSTER_SP_MS disc
// cluster per track. The gauge's tooltip needs this because an LP4 segment's
// clusters are not all worth 8 000 ms - the link cluster is worth 2 000.
md_inline u64 plan_billed_ms_of(u32 clusters, u32 entries, u32 cap_mode) {
    u32 overhead = entries * PLAN_TRACK_OVERHEAD_CLUSTERS;
    if (overhead > clusters) { overhead = clusters; }
    return (u64)(clusters - overhead) * MD_MODE_TABLE[cap_mode].cluster_ms +
           (u64)overhead * PLAN_CLUSTER_SP_MS;
}

// Per entry state, for the segmented gauge: the track fits whole, straddles the
// end of the disc, or starts past it entirely.
typedef enum PlanFit {
    PlanFit_Fits = 0,
    PlanFit_Partial,
    PlanFit_Overflow
} PlanFit;

typedef struct PlanCapacity {
    u32 length_min;
    u32 capacity_clusters;
    u32 used_clusters;      // the whole plan, overflow included
    u32 free_clusters;      // 0 once it overflows
    u32 overflow_clusters;  // 0 while it fits
    u32 entry_count;
    u32 first_overflow;     // index of the first entry that does not fit whole,
                            // entry_count when everything does
    u64 audio_ms;    // sum of the durations, what a naive gauge would show
    u64 billed_ms;   // what the disc is charged: the audio rounded up to whole
                     // clusters per mode, plus the overhead cluster per track
    u64 padding_ms;  // the audio silence the rounding pays for, and nothing
                     // else: the sum of entry_padding_ms. billed_ms - audio_ms
                     // is this plus entry_count overhead clusters.

    // "What would still fit", one answer per mode: the free clusters spent in
    // that mode. Ambiguous in seconds, which is exactly why it is tri-modal
    // (research/02 s3.1, consequence 3).
    u32 remaining_ms[PlanCapMode_COUNT];
    u32 remaining_entries;  // PLAN_ENTRY_MAX - entry_count

    u32 clusters[PLAN_ENTRY_MAX];  // per entry cost, the gauge's segment widths
    // The two columns the gauge reads and would otherwise have to recompute
    // from the disc while it lays out (T-032): the billing mode carries the
    // colour, the padding carries the hatched tail of the segment. Both fall
    // out of the pass that is already running, so they cost a store each.
    u32 entry_padding_ms[PLAN_ENTRY_MAX];
    u8 entry_mode[PLAN_ENTRY_MAX];  // PlanCapMode
    u8 fit[PLAN_ENTRY_MAX];         // PlanFit
} PlanCapacity;

// One pass, no allocation. `out` may be reused across frames.
void plan_capacity_compute(const PlanDisc *disc, PlanCapacity *out);

// The same question for one candidate track, without building anything: would
// adding `duration_ms` in `cap_mode` still fit in what is left?
md_inline b32 plan_capacity_would_fit(const PlanCapacity *cap, u32 duration_ms, u32 cap_mode) {
    return plan_clusters_for(duration_ms, cap_mode) <= cap->free_clusters;
}

// --- multi disc auto split ---------------------------------------------------
// Spreads the entries of one disc over as many discs as it takes. Two policies,
// both first fit in plan order: FirstFit places entries one by one, KeepAlbums
// places a whole album run at a time and only breaks one up when it cannot fit
// on an empty disc by itself.
typedef enum PlanSplitPolicy {
    PlanSplit_FirstFit = 0,
    PlanSplit_KeepAlbums
} PlanSplitPolicy;

typedef struct PlanSplit {
    u32 disc_count;
    u32 entry_count;
    u8 disc_of[PLAN_ENTRY_MAX];  // which disc each entry lands on
    u32 clusters[PLAN_DISC_MAX];
    u32 counts[PLAN_DISC_MAX];
    u32 unplaced;  // entries left over once PLAN_DISC_MAX discs are full
} PlanSplit;

// `lib` may be null under PlanSplit_FirstFit; KeepAlbums needs it to read the
// album of each entry. `minutes` is the length of every target disc.
void plan_capacity_split(const PlanDisc *disc, const Library *lib, u32 policy, u32 minutes,
                         PlanSplit *out);

#endif  // PLAN_CAPACITY_H

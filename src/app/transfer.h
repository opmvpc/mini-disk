// transfer.h - the burn with no pixel in it (T-043): the pre-flight simulation
// of ADR-011 D4, the per track state machine, and the honest ETA of D9.
//
// The same split as plan_view.h: everything a test would otherwise have to
// drive a window and a device to reach lives here, and view_transfer.c draws
// what this computes. The state machine is fed events, not USB - so a run that
// fails in the middle, is cancelled and then resumed is nine lines of test and
// no hardware.
//
// No UI, no GL, no allocation, and nothing here ever talks to the device: the
// caller translates NetmdEvent into TransferEvent and hands it over.
#ifndef APP_TRANSFER_H
#define APP_TRANSFER_H

#include "../base/base.h"
#include "../core/library/lib_model.h"
#include "../core/netmd/netmd_edit.h"
#include "../core/netmd/netmd_upload.h"
#include "../core/pipeline/pipeline_cache.h"
#include "../core/plan/plan_capacity.h"
#include "../core/plan/plan_model.h"
#include "../core/plan/plan_toc.h"

// --- the pre-flight (D4) ------------------------------------------------------
// What will be written, decided before a single byte moves. The disc the plan
// would land on is read from the device, so the capacity here is the *device's*
// free time and not what MD_MODE_TABLE believes about an empty disc.

typedef enum TransferPolicy {
    // The tracks are appended after what the disc already holds. The default,
    // and the only one that never destroys anything.
    TransferPolicy_Append = 0,
    // The disc is erased first. A separate, separately confirmed action that
    // goes through the T-022 edit path; the pre-flight only says what it would
    // cost, and the transfer never erases anything by itself.
    TransferPolicy_EraseFirst,
    TransferPolicy_COUNT
} TransferPolicy;

typedef enum TransferWarning {
    TransferWarn_MissingTrack = 1u << 0,  // the source file is gone
    TransferWarn_Overflow     = 1u << 1,  // more clusters than the disc has free
    TransferWarn_Protected    = 1u << 2,  // the tab is open, or the disc is pre-mastered
    TransferWarn_DiscNotEmpty = 1u << 3,  // append after existing tracks, or erase first
    TransferWarn_NoDisc       = 1u << 4,
    TransferWarn_TocOverflow  = 1u << 5,  // the titles do not fit in 255 cells
    TransferWarn_Empty        = 1u << 6,  // nothing left to write
    TransferWarn_TitleKept    = 1u << 7,  // the disc already has a title: we keep it
    TransferWarn_Shortened    = 1u << 8,  // at least one title was cut to fit
} TransferWarning;

typedef struct TransferSimEntry {
    u32 entry;        // index into the plan disc
    u32 duration_ms;
    u32 clusters;
    u32 cells;
    u32 shorten;      // the PlanShorten steps that fired on the title
    b32 missing;      // the source file is not there: this one is not written
    b32 fits;         // it still fits in what the disc has left
    u16 title_size;
    u8 title[PLAN_TITLE_MAX];  // exactly what the TOC will hold
} TransferSimEntry;

typedef struct TransferSim {
    u32 policy;    // TransferPolicy
    u32 warnings;  // TransferWarning
    b32 allowed;   // there is at least one writable track and room for it

    u32 count;        // entries of the plan disc, missing ones included
    u32 write_count;  // the ones that will actually be written
    u32 missing_count;
    u64 audio_ms;

    // Clusters, in the SP unit the device counts in (2 s). "Before" is what the
    // disc holds now, "after" what it would hold - both from the device when
    // there is one, from the plan model when there is not.
    u32 clusters_capacity;
    u32 clusters_before;
    u32 clusters_after;
    u32 clusters_needed;
    u64 free_ms_before;
    u64 free_ms_after;

    u32 tracks_before, tracks_after;
    u32 cells_before, cells_after, cells_free_after;
    u32 groups;

    // The disc title, and the one rule that protects the user's disc: it is
    // only ever written onto a disc that has none, or onto one we just erased.
    b32 write_disc_title;
    u16 disc_title_size;
    u8 disc_title[NETMD_DISC_TITLE_MAX];

    TransferSimEntry entries[PLAN_ENTRY_MAX];
} TransferSim;

// Pure. `disc` may be 0 (no device, or no disc): the capacity then comes from
// the plan's own disc length, and TransferWarn_NoDisc is raised.
void transfer_simulate(const Plan *plan, const Library *lib, u32 disc_index,
                       const DiscLayout *disc, u32 policy, TransferSim *out);

// --- the run ---------------------------------------------------------------------

typedef enum TransferTrackState {
    TransferTrack_Pending = 0,   // waiting for its turn
    TransferTrack_Transcoding,   // a pipeline job is rendering it
    TransferTrack_Transcoded,    // the cache holds it: nothing to wait for
    TransferTrack_Sending,       // on the wire
    TransferTrack_Written,       // committed to the disc
    TransferTrack_Titled,        // the title is in the TOC too
    TransferTrack_Failed,
    TransferTrack_Skipped,       // missing source, or cancelled before its turn
    TransferTrack_COUNT
} TransferTrackState;

typedef enum TransferPhase {
    TransferPhase_Idle = 0,
    TransferPhase_Preflight,    // the simulation is on screen, nothing is running
    TransferPhase_Running,
    TransferPhase_Pausing,      // pause asked: it takes effect between two tracks
    TransferPhase_Paused,
    TransferPhase_Cancelling,   // cancel asked: the current track is being stopped
    TransferPhase_Cancelled,
    TransferPhase_Done,
    TransferPhase_Failed,
    TransferPhase_COUNT
} TransferPhase;

typedef enum TransferEventKind {
    TransferEvent_Start = 0,
    TransferEvent_CacheHit,        // `entry` is already rendered
    TransferEvent_TranscodeBegin,
    TransferEvent_TranscodeDone,
    TransferEvent_TranscodeFailed,
    TransferEvent_Progress,        // bytes_done / bytes_total moved
    TransferEvent_TrackDone,       // `entry` is committed and titled
    TransferEvent_UploadDone,
    TransferEvent_UploadError,     // `result` is the NetmdResult
    TransferEvent_PauseRequested,
    TransferEvent_Resume,
    TransferEvent_CancelRequested,
    TransferEvent_COUNT
} TransferEventKind;

typedef struct TransferEvent {
    u32 kind;
    u32 entry;
    u32 result;  // NetmdResult, on UploadError
    u64 bytes_done;
    u64 bytes_total;
    u64 now_us;
} TransferEvent;

// The ETA of D9 and MI-28: a sliding average over the last 30 seconds, never
// the instantaneous rate, and it never goes up.
#define TRANSFER_RATE_SAMPLES 32
#define TRANSFER_RATE_WINDOW_US 30000000ull
// Below this the measured rate is noise and the nominal SP rate is the honest
// answer: 44100 frames a second of four bytes each (research/01 s4.9).
#define TRANSFER_RATE_MIN_BYTES MB(1)
#define TRANSFER_SP_BYTES_PER_S (44100ull * 4ull)
// MI-29: a rate that falls this far below the run's own average, for this long,
// is worth a calm line rather than an alarm.
#define TRANSFER_SLOW_RATIO 60u  // percent of the average
#define TRANSFER_SLOW_US 15000000ull
// The operations log is written once at the end of the run, so it is held in
// one buffer until then: 64 KB is 400 lines, ten times what a full disc needs.
#define TRANSFER_LOG_BYTES KB(64)

typedef struct TransferRateSample {
    u64 us;
    u64 bytes;
} TransferRateSample;

typedef struct Transfer {
    u32 phase;  // TransferPhase
    u32 count;  // tracks in the run
    u8 state[PLAN_ENTRY_MAX];  // TransferTrackState
    u32 result[PLAN_ENTRY_MAX];
    u64 track_bytes[PLAN_ENTRY_MAX];  // what each track will send, from the sim

    u32 current;      // the entry on the wire
    u32 done_count;   // committed
    u32 failed_count;
    u32 transcoded;   // rendered ahead, cache hits included
    u32 cache_hits;

    u64 bytes_done;
    u64 bytes_total;
    u64 started_us;
    u64 last_us;      // the last event's timestamp
    u64 paused_us;    // when the pause started, 0 when running
    u64 paused_total_us;
    u64 finished_us;

    // The rate window, a ring. `eta_s` is what the UI shows and never rises.
    TransferRateSample samples[TRANSFER_RATE_SAMPLES];
    u32 sample_count;
    u32 sample_next;
    u32 eta_s;
    u64 eta_us;      // when eta_s was computed, so it can decay with the clock
    u64 slow_since_us;  // MI-29: since when the rate has been under the ratio
    b32 slow;

    u32 last_result;  // the NetmdResult the run ended with
    u32 log_size;
    u8 log[TRANSFER_LOG_BYTES];
} Transfer;

// `sim` gives the run its length and its per track byte counts. The phase goes
// to Preflight, and nothing moves until TransferEvent_Start.
void transfer_begin(Transfer *transfer, const TransferSim *sim);
// Every transition of the machine. Feeding it the same event twice is not an
// error: the device thread coalesces its progress, and a resume replays.
void transfer_apply(Transfer *transfer, const TransferEvent *event);

// Seconds left at the measured rate, 0 when there is nothing left to send.
// Recomputed by transfer_apply on every Progress; this is the read.
md_inline u32 transfer_eta_s(const Transfer *transfer) { return transfer->eta_s; }
// Bytes a second, over the window. 0 before the run has moved enough to say.
u64 transfer_rate(const Transfer *transfer);
// The announcement made *before* the run starts: SP is written in real time, so
// a plan of 40 minutes takes about 40 minutes (research/01 s6.1).
u32 transfer_expected_s(const TransferSim *sim);
// Wall clock seconds since the start, pauses removed.
u32 transfer_elapsed_s(const Transfer *transfer, u64 now_us);
// 0..1000, in thousandths: the global bar. Per track, `track_bytes` and the
// live byte count of the current one are what the caller has.
u32 transfer_progress_permille(const Transfer *transfer);
// How many tracks are already on the disc and would stay there if the run were
// cancelled right now. This is the sentence the cancel button has to say.
md_inline u32 transfer_written_count(const Transfer *transfer) { return transfer->done_count; }
b32 transfer_active(const Transfer *transfer);
// The transfer is holding the application open (D4): closing mid burn leaves a
// half written TOC, and the app says so rather than doing it.
md_inline b32 transfer_blocks_close(const Transfer *transfer) {
    return transfer->phase == TransferPhase_Running ||
           transfer->phase == TransferPhase_Pausing ||
           transfer->phase == TransferPhase_Cancelling;
}

// One line into the run's log, timestamped with the run's own clock. Truncated
// rather than grown: a log that eats memory during a burn is a worse bug than a
// log that stops at 64 KB.
void transfer_log(Transfer *transfer, u64 now_us, String8 line);

#endif  // APP_TRANSFER_H

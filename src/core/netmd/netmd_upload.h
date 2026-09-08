// netmd_upload.h - burning a plan onto a disc: the orchestration above
// netmd_secure.c (research/01 s4.12, s6.5, ADR-011).
//
// One run writes N tracks and then the disc title. The order is not a
// preference: the TOC is written to the disc at the end of every commit, so
// titling a track before committing it (s4.12) and writing the disc title once
// at the very end (s6.5) is the difference between one TOC cycle and N of them.
//
// Resume is the reason every entry carries its own status. A run that is
// cancelled, or that loses the cable halfway, leaves the tracks it already
// committed on the disc - they are real tracks, the device wrote them, and
// erasing them to "clean up" would destroy the user's work. So the entries that
// finished are marked Done, and calling netmd_upload_run again with the same
// array picks up where it stopped.
//
// Nothing here allocates outside the arenas it is handed, and the only thread
// that ever calls it is the device thread.
#ifndef NETMD_UPLOAD_H
#define NETMD_UPLOAD_H

#include "../../base/base.h"
#include "../../base/base_arena.h"
#include "../pipeline/pipeline.h"
#include "netmd_disc.h"
#include "netmd_secure.h"

#define NETMD_UPLOAD_PATH_MAX 512
// s7.2: a SP cluster is 2 seconds, and a track always costs a whole one. This
// is the SP row of plan_capacity.h's MD_MODE_TABLE, restated here so the check
// against the *device's* free time does not drag the plan model into the
// protocol layer. T-042's acceptance criterion is that the two agree.
#define NETMD_SP_CLUSTER_MS 2000u

typedef enum NetmdUploadStatus {
    NetmdUploadStatus_Pending = 0,
    NetmdUploadStatus_Done,      // committed and titled: a resume skips it
    NetmdUploadStatus_Failed,
    NetmdUploadStatus_Cancelled,
} NetmdUploadStatus;

typedef struct NetmdUploadEntry {
    u32 path_size;
    u8 path[NETMD_UPLOAD_PATH_MAX];
    // The title exactly as it will be written: plan_toc_preview has already
    // sanitized and shortened it, so this layer only encodes it (s3.11).
    u32 title_size;
    u8 title[NETMD_TITLE_MAX];
    u64 duration_ms;
    PipelineConfig config;
    // A source with a length already known short-circuits the render: that is
    // how the tests feed known bytes, and how T-043 will hand over a cache file.
    NetmdAudioSource data;

    // --- filled by the run ---------------------------------------------------
    u32 status;  // NetmdUploadStatus
    u32 result;  // NetmdResult of the failure, when there is one
    u32 track;   // the number the device assigned
    u64 bytes;   // payload bytes actually sent
} NetmdUploadEntry;

// T-043: called on the device thread immediately before entry `index` goes on
// the wire, so a transcode that is still running is waited for *there* and not
// before the run starts. That is what lets the next tracks be rendered while
// the current one uploads. It fills `entry->data`; 0 fails that entry alone.
// A plan whose entries already carry their audio leaves this null.
typedef b32 NetmdUploadPrepareFn(void *user, NetmdUploadEntry *entry, u32 index);

// --- P-014: the tracks this session wrote -------------------------------------
// A track committed a moment ago reads back with `protect` set, because the TOC
// is still in the device's RAM (s6.3) - the same 0x03 SonicStage uses for a
// checked out track. The device cannot tell the two apart for us, so we
// remember what we wrote and say so ourselves.
//
// The re-match is not by index. A rename, a move or an erase renumbers the disc
// between two reads, and an index that has drifted would mark somebody else's
// track. What identifies a track across a re-read is what a re-read reports
// about it: its length to the frame and its title. The position is kept as a
// hint, so the common case - nothing moved - matches on the first candidate.
#define NETMD_WRITTEN_MAX 64

typedef struct NetmdWrittenTrack {
    u32 position;    // where it was last seen, a hint and nothing more
    u32 frames;      // 0 until the first read back resolves it
    u32 title_size;
    u8 title[NETMD_TITLE_MAX];
} NetmdWrittenTrack;

typedef struct NetmdWrittenSet {
    u32 count;
    NetmdWrittenTrack tracks[NETMD_WRITTEN_MAX];
} NetmdWrittenSet;

void netmd_written_reset(NetmdWrittenSet *set);
// Called at commitTrack, with the number the device assigned. The length is not
// known here to the frame - the device rounds it up to a whole cluster - so it
// is left at 0 and taken from the first read back.
void netmd_written_add(NetmdWrittenSet *set, u32 position, String8 title);
// Marks `written_here` on every track of `layout` the set recognizes, and
// updates the set with where each one is now. Returns how many were matched.
u32 netmd_written_apply(NetmdWrittenSet *set, DiscLayout *layout);

typedef struct NetmdUploadPlan {
    NetmdUploadEntry *entries;
    u32 count;
    // P-014. 0 when the caller does not care: the replay tests do not.
    NetmdWrittenSet *written;
    NetmdUploadPrepareFn *prepare;
    void *prepare_user;
    // The compiled disc title, group syntax included - plan_toc_compile_disc_title
    // produced it, this layer only writes it (s3.10).
    u32 disc_title_size;
    u8 disc_title[NETMD_DISC_TITLE_MAX];
    b32 write_disc_title;
    // Entropy injection, and the only reason this field exists: with a fixed
    // host nonce and a fixed packet key, every encrypted byte of a download is
    // fixed, so a transcript of one can be replayed byte for byte. 0 means the
    // real one (os_random_bytes).
    NetmdRandomFn *random;
    void *random_user;
} NetmdUploadPlan;

typedef enum NetmdUploadEvent {
    NetmdUploadEvent_Progress = 0,
    NetmdUploadEvent_TrackDone,
    NetmdUploadEvent_Done,
    NetmdUploadEvent_Error,
} NetmdUploadEvent;

// Everything the UI reads while the transfer runs. Written by the device
// thread, read by the main thread, so every live field is atomic-sized.
typedef struct NetmdUploadState {
    volatile u32 cancel;  // set by the main thread, read between two packets
    volatile u32 active;  // the "a burn is running" flag the app checks on close
    volatile u32 entry;   // which entry is on the wire
    volatile u32 done;    // entries committed in this run
    volatile long long bytes_done;    // payload bytes of the tracks already committed
    volatile long long track_bytes;   // payload bytes of the track on the wire
    volatile long long bytes_total;   // what the run will send in total
    u64 started_us;
    u32 result;  // NetmdResult of the run
} NetmdUploadState;

typedef void NetmdUploadProgressFn(void *user, const NetmdUploadPlan *plan,
                                   const NetmdUploadState *state, u32 event);

md_inline b32 netmd_upload_active(const NetmdUploadState *state) {
    return os_atomic_load_u32((volatile u32 *)&state->active) != 0;
}

// Seconds left, from the bytes still to send. SP runs at 1x real time by
// construction (the device encodes ATRAC1 as it swallows the PCM, s4.9), so the
// nominal rate is 44100 * 4 bytes per second; once a run has moved a megabyte,
// its own measured rate takes over.
u32 netmd_upload_eta_s(const NetmdUploadState *state);

// The whole run. `arena` is the protocol scratch; each track's rendered audio
// gets an arena of its own that is released as soon as the track is committed.
u32 netmd_upload_run(NetmdSession *session, Arena *arena, NetmdUploadPlan *plan,
                     NetmdUploadState *state, NetmdUploadProgressFn *progress, void *user);

// Half-width sanitized UTF-8 (what plan_toc_preview produces) to the bytes the
// TOC stores: ASCII passes through and half-width katakana fold to 0xA1..0xDF,
// which is all Shift-JIS needs for a title this program can produce (s3.11).
u64 netmd_sjis_from_utf8(String8 in, u8 *out, u64 capacity);

// Exposed for the tests: what the run does per track, minus the audio.
u32 netmd_upload_set_track_title(NetmdSession *session, Arena *arena, u32 track, String8 title);
u32 netmd_upload_set_disc_title(NetmdSession *session, Arena *arena, String8 title);

#endif  // NETMD_UPLOAD_H

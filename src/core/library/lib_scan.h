// lib_scan.h - the recursive folder scan: one job per subfolder (T-008), the
// results merged into the library by the main thread through a lock free queue.
//
// The UI never waits for it: lib_scan_update is a poll that merges what the
// workers have published so far and returns at once, and lib_scan_cancel is a
// single store the jobs read at the top of every directory.
#ifndef LIB_SCAN_H
#define LIB_SCAN_H

#include "../../base/base.h"
#include "../../base/base_jobs.h"
#include "../../platform/platform.h"
#include "lib_events.h"
#include "lib_model.h"

// Entries travel in batches: one atomic push per 64 files instead of one per
// file, and the main thread drains a whole directory's worth per pop.
#define LIB_SCAN_BATCH     64
#define LIB_SCAN_RESERVE   MB(256)  // virtual, committed 4 MB at a time
#define LIB_SCAN_COMMIT    MB(4)

typedef enum LibScanState {
    LibScanState_Idle = 0,
    LibScanState_Running,
    LibScanState_Done,
} LibScanState;

typedef struct LibScanEntry {
    String8 path;  // in the scan's own block, alive until lib_scan_end
    u64 size;
    u64 mtime_us;
} LibScanEntry;

typedef struct LibScanBatch {
    u32 next;  // offset + 1 into the block, 0: end of the list
    u32 count;
    LibScanEntry entries[LIB_SCAN_BATCH];
} LibScanBatch;

typedef struct LibScan {
    // --- the block: a lock free bump allocator over reserved pages ---------
    u8 *block;
    u64 block_size;
    volatile u64 block_pos;
    volatile u64 block_committed;
    OsMutex commit_mutex;

    // --- shared with the workers ------------------------------------------
    volatile u32 published;   // Treiber stack head: batch offset + 1
    volatile u32 cancel;
    volatile u32 files_seen;
    volatile u32 dirs_total;
    volatile u32 dirs_done;
    volatile u32 exhausted;   // the block ran out: the scan stops, and says so
    JobCounter counter;

    // --- main thread only --------------------------------------------------
    LibScanState state;
    Library *library;
    LibEventQueue *events;
    String8 root;        // in the block, normalised
    u32 generation;
    u32 added, updated, unchanged, removed;
    u64 start_us, end_us;
    b32 cancelled;
} LibScan;

// `root` is copied into the scan's block. Returns 0 when the root is not a
// readable directory - a domain error, the only one this module has.
b32  lib_scan_begin(LibScan *scan, Library *library, LibEventQueue *events, String8 root);
// Merges what the workers published; returns 1 while the scan is still running.
b32  lib_scan_update(LibScan *scan);
void lib_scan_cancel(LibScan *scan);
void lib_scan_end(LibScan *scan);  // releases the block, after the scan is done

md_inline b32 lib_scan_running(const LibScan *scan) {
    return scan->state == LibScanState_Running;
}

#endif // LIB_SCAN_H

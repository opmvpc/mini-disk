// lib_events.h - the core -> UI queue. A fixed ring, single consumer (the main
// thread), producers that never block: when the ring is full the oldest event
// of the same kind has already told the UI everything it needed, so a dropped
// progress event costs nothing and a dropped terminal event cannot happen (the
// consumer drains once per frame, and a scan pushes at most a few per frame).
//
// The UI reads this and nothing else of the scan's internals.
#ifndef LIB_EVENTS_H
#define LIB_EVENTS_H

#include "../../base/base.h"
#include "../../platform/platform.h"

typedef enum LibEventKind {
    LibEvent_None = 0,
    LibEvent_ScanProgress,   // files_seen, dirs_done, dirs_total
    LibEvent_ScanDone,       // added, updated, removed, cancelled
    LibEvent_TracksAdded,    // count
    LibEvent_TracksRemoved,  // count
    LibEvent_TracksTagged,   // count: tags read and merged (T-011)
    LibEvent_COUNT
} LibEventKind;

typedef struct LibEvent {
    u32 kind;
    u32 files_seen;
    u32 dirs_done;
    u32 dirs_total;
    u32 added;
    u32 updated;
    u32 removed;
    u32 tagged;
    u32 count;
    b32 cancelled;
} LibEvent;

#define LIB_EVENT_CAPACITY 64  // power of two

typedef struct LibEventQueue {
    LibEvent items[LIB_EVENT_CAPACITY];
    volatile u32 write;  // producers
    volatile u32 read;   // the consumer only
} LibEventQueue;

// Drops the event rather than waiting when the ring is full: the UI is never
// the reason a worker stalls, and never the reason it spins either.
md_inline b32 lib_events_push(LibEventQueue *queue, LibEvent event) {
    u32 write = os_atomic_load_u32(&queue->write);
    u32 read = os_atomic_load_u32(&queue->read);
    if (write - read >= LIB_EVENT_CAPACITY) { return 0; }
    mem_copy(&queue->items[write & (LIB_EVENT_CAPACITY - 1)], &event, sizeof(LibEvent));
    os_atomic_store_u32(&queue->write, write + 1);
    return 1;
}

md_inline b32 lib_events_next(LibEventQueue *queue, LibEvent *out) {
    u32 read = os_atomic_load_u32(&queue->read);
    if (read == os_atomic_load_u32(&queue->write)) { return 0; }
    mem_copy(out, &queue->items[read & (LIB_EVENT_CAPACITY - 1)], sizeof(LibEvent));
    os_atomic_store_u32(&queue->read, read + 1);
    return 1;
}

#endif // LIB_EVENTS_H

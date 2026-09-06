// base_jobs.h - the job system: N-1 workers, one bounded MPMC ring, no lock and
// no allocation on the fast path (ADR-001, research/03 s1.9).
//
// A job is a function and a range. A plain job gets [0, 1); jobs_dispatch cuts
// a parallel-for into chunks that are jobs of the very same kind. The thread
// that waits helps: jobs_wait runs jobs out of the ring until its group is
// done, so the main thread is never idle while workers still have work.
#ifndef BASE_JOBS_H
#define BASE_JOBS_H

#include "base.h"
#include "../platform/platform.h"

#define JOBS_QUEUE_CAPACITY 4096  // power of two: the ring masks with it
#define JOBS_MAX_WORKERS    63
// A worker that sleeps between two jobs of the same burst costs a pair of
// syscalls per job (measured: 1.5 us). It therefore stays awake for about
// 30 us of pauses first, looking at the ring every sixteenth of them so seven
// idle workers do not hammer the line the producer is writing. After a burst
// this costs each worker one spin, once - a fifth of a millisecond in total.
#define JOBS_SPIN_COUNT     1024
#define JOBS_SPIN_POLL_MASK 15
// Four chunks per thread: enough slack for the scheduler to balance a range
// whose elements do not all cost the same, few enough to stay in the ring.
#define JOBS_CHUNKS_PER_THREAD 4

typedef void JobFunc(void *data, u64 begin, u64 end);

// Owned by the caller, usually on its stack. It counts the jobs of one group
// that have not finished yet; jobs_wait returns when it reaches zero.
typedef struct JobCounter {
    volatile u32 pending;
} JobCounter;

// worker_count 0: os_cpu_count() - 1, the shape every ticket asks for.
void jobs_init(u32 worker_count);
void jobs_shutdown(void);  // wakes every worker, joins them, releases the pool

u32 jobs_worker_count(void);
u32 jobs_thread_index(void);  // 0 is the main thread, workers are 1..N

// `counter` may be 0 when nobody waits for that job.
void jobs_push(JobCounter *counter, JobFunc *fn, void *data);
void jobs_dispatch(JobCounter *counter, JobFunc *fn, void *data, u64 count);
void jobs_wait(JobCounter *counter);

u32 jobs_pending(void);  // jobs sitting in the ring, for the debug overlay
u32 jobs_busy(void);     // threads inside a job right now

#endif // BASE_JOBS_H

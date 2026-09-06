#include "base_jobs.h"

#include "base_arena.h"
#include "base_string.h"

typedef struct Job {
    JobFunc *fn;
    void *data;
    u64 begin, end;
    JobCounter *counter;
} Job;

// Vyukov's bounded MPMC ring: one sequence number per cell is the whole
// synchronisation. A producer owns a cell when its sequence equals the position
// it claimed, a consumer when it equals that position plus one. No lock, no
// ABA, and the queue is a plain array: nothing is allocated at any point.
typedef struct JobCell {
    volatile u32 sequence;
    u32 pad;
    Job job;
} JobCell;

// One cache line per thread: the busy flag of one worker never invalidates the
// line of another.
typedef struct JobThreadState {
    volatile u32 busy;
    u8 pad[60];
} JobThreadState;

typedef struct JobPool {
    JobCell cells[JOBS_QUEUE_CAPACITY];
    JobThreadState threads[JOBS_MAX_WORKERS + 1];  // [0] is the main thread
    // The two positions are written by every thread on every push and pop:
    // they get a cache line each, or the ring false-shares itself to death.
    volatile u32 enqueue_pos;
    u8 pad0[60];
    volatile u32 dequeue_pos;
    u8 pad1[60];

    volatile u32 sleepers;  // workers blocked on the semaphore
    volatile u32 signaled;  // a wake-up token is already in flight
    volatile u32 quitting;

    u32 worker_count;
    OsThread workers[JOBS_MAX_WORKERS];
    OsSemaphore semaphore;
    b32 initialized;
} JobPool;

global JobPool jobs_pool;
thread_var u32 jobs_tls_thread_index;

u32 jobs_worker_count(void) { return jobs_pool.worker_count; }
u32 jobs_thread_index(void) { return jobs_tls_thread_index; }
// Both counters are derived, never maintained: a shared counter incremented on
// every push and decremented on every pop is one more contended cache line on
// the hot path, and it buys nothing the ring positions do not already say.
u32 jobs_pending(void) {
    u32 queued = os_atomic_load_u32(&jobs_pool.enqueue_pos) -
                 os_atomic_load_u32(&jobs_pool.dequeue_pos);
    return Min(queued, (u32)JOBS_QUEUE_CAPACITY);  // the two loads are not one snapshot
}

u32 jobs_busy(void) {
    u32 busy = 0;
    for (u32 i = 0; i <= jobs_pool.worker_count; i += 1) { busy += jobs_pool.threads[i].busy; }
    return busy;
}

// --- the ring --------------------------------------------------------------

static b32 jobs_queue_push(const Job *job) {
    JobPool *pool = &jobs_pool;
    u32 pos = os_atomic_load_u32(&pool->enqueue_pos);
    for (;;) {
        JobCell *cell = &pool->cells[pos & (JOBS_QUEUE_CAPACITY - 1)];
        i32 diff = (i32)(os_atomic_load_u32(&cell->sequence) - pos);
        if (diff == 0) {
            u32 found = os_atomic_cas_u32(&pool->enqueue_pos, pos, pos + 1);
            if (found == pos) {
                // Field by field through mem_copy: under /GL a struct
                // assignment becomes a memcpy call the no-CRT link refuses.
                mem_copy(&cell->job, job, sizeof(Job));
                os_atomic_store_u32(&cell->sequence, pos + 1);
                return 1;
            }
            pos = found;
        } else if (diff < 0) {
            return 0;  // full: the caller runs the job itself
        } else {
            pos = os_atomic_load_u32(&pool->enqueue_pos);
        }
    }
}

static b32 jobs_queue_pop(Job *out) {
    JobPool *pool = &jobs_pool;
    u32 pos = os_atomic_load_u32(&pool->dequeue_pos);
    for (;;) {
        JobCell *cell = &pool->cells[pos & (JOBS_QUEUE_CAPACITY - 1)];
        i32 diff = (i32)(os_atomic_load_u32(&cell->sequence) - (pos + 1));
        if (diff == 0) {
            u32 found = os_atomic_cas_u32(&pool->dequeue_pos, pos, pos + 1);
            if (found == pos) {
                mem_copy(out, &cell->job, sizeof(Job));
                os_atomic_store_u32(&cell->sequence, pos + JOBS_QUEUE_CAPACITY);
                return 1;
            }
            pos = found;
        } else if (diff < 0) {
            return 0;  // empty
        } else {
            pos = os_atomic_load_u32(&pool->dequeue_pos);
        }
    }
}

static void jobs_run(const Job *job) {
    // A plain store to a line only this thread writes: the overlay reads the
    // flags whenever it likes, and the hot path pays nothing for them.
    volatile u32 *busy = &jobs_pool.threads[jobs_tls_thread_index].busy;
    *busy = 1;
    job->fn(job->data, job->begin, job->end);
    *busy = 0;
    // Last: a waiter that sees the counter reach zero may free `job->data`.
    if (job->counter) { os_atomic_dec_u32(&job->counter->pending); }
}

// One token at a time. Signalling per push costs a syscall per job (measured:
// 1.7 us each, seventeen times the budget); instead the producer wakes a single
// worker, and a worker that takes a job while the ring is still full wakes the
// next one. The chain unrolls the pool in a few hundred nanoseconds and a burst
// of 200 000 jobs costs a handful of syscalls instead of 200 000.
static void jobs_wake_one(void) {
    JobPool *pool = &jobs_pool;
    if (os_atomic_load_u32(&pool->sleepers) == 0) { return; }
    if (os_atomic_cas_u32(&pool->signaled, 0, 1) != 0) { return; }
    os_semaphore_signal(pool->semaphore, 1);
}

// Clearing the flag can only ever cost an extra token (a spurious wake-up that
// finds an empty ring and goes back to sleep); it can never lose one, since a
// token is released only when the flag went from 0 to 1.
static void jobs_took_job(void) {
    os_atomic_store_u32(&jobs_pool.signaled, 0);
    if (jobs_pending() > 0) { jobs_wake_one(); }
}

static void jobs_submit(JobCounter *counter, JobFunc *fn, void *data, u64 begin, u64 end) {
    Job job;
    job.fn = fn;
    job.data = data;
    job.begin = begin;
    job.end = end;
    job.counter = counter;
    if (counter) { os_atomic_inc_u32(&counter->pending); }
    // No pool, or a ring that is full: the submitter is a worker like any
    // other. This is what keeps a nested dispatch from ever deadlocking.
    if (jobs_pool.worker_count == 0 || !jobs_queue_push(&job)) {
        jobs_run(&job);
        return;
    }
    jobs_wake_one();
}

// --- workers ---------------------------------------------------------------

static void jobs_worker(void *data) {
    JobPool *pool = &jobs_pool;
    // The worker is handed its own slot rather than its index: a pointer that
    // subtracts back to the index, and no integer to pointer cast anywhere.
    jobs_tls_thread_index = (u32)((JobThreadState *)data - pool->threads);
    while (!os_atomic_load_u32(&pool->quitting)) {
        Job job;
        if (jobs_queue_pop(&job)) {
            jobs_took_job();
            jobs_run(&job);
            continue;
        }
        b32 got = 0;
        for (u32 spin = 0; spin < JOBS_SPIN_COUNT && !got; spin += 1) {
            os_cpu_pause();
            if ((spin & JOBS_SPIN_POLL_MASK) == 0) { got = jobs_queue_pop(&job); }
        }
        if (got) {
            jobs_took_job();
            jobs_run(&job);
            continue;
        }
        // Announce the sleep *before* the last look at the ring: a producer
        // that pushes after this point reads sleepers > 0 and signals us, so
        // no wake-up can be lost between the look and the wait.
        os_atomic_inc_u32(&pool->sleepers);
        if (jobs_queue_pop(&job)) {
            os_atomic_dec_u32(&pool->sleepers);
            jobs_took_job();
            jobs_run(&job);
            continue;
        }
        if (os_atomic_load_u32(&pool->quitting)) {
            os_atomic_dec_u32(&pool->sleepers);
            break;
        }
        os_semaphore_wait(pool->semaphore);
        os_atomic_dec_u32(&pool->sleepers);
        os_atomic_store_u32(&pool->signaled, 0);  // the token has been consumed
    }
    scratch_thread_release();
}

// --- api -------------------------------------------------------------------

void jobs_init(u32 worker_count) {
    JobPool *pool = &jobs_pool;
    Assert(!pool->initialized);
    if (worker_count == 0) {
        u32 cpus = os_cpu_count();
        worker_count = (cpus > 1) ? cpus - 1 : 0;
    }
    if (worker_count > JOBS_MAX_WORKERS) { worker_count = JOBS_MAX_WORKERS; }

    pool->enqueue_pos = 0;
    pool->dequeue_pos = 0;
    pool->sleepers = 0;
    pool->signaled = 0;
    pool->quitting = 0;
    for (u32 i = 0; i < JOBS_QUEUE_CAPACITY; i += 1) { pool->cells[i].sequence = i; }
    pool->worker_count = worker_count;
    pool->semaphore = os_semaphore_create(0, worker_count + 1);

    ArenaTemp scratch = scratch_begin(0, 0);
    for (u32 i = 0; i < worker_count; i += 1) {
        String8 name = str8f(scratch.arena, "minidisk worker %u", i + 1);
        pool->workers[i] = os_thread_create(jobs_worker, &pool->threads[i + 1], name);
    }
    scratch_end(scratch);
    pool->initialized = 1;
}

void jobs_shutdown(void) {
    JobPool *pool = &jobs_pool;
    Assert(pool->initialized);
    os_atomic_store_u32(&pool->quitting, 1);
    // Exactly one token per worker, and a worker consumes at most one before
    // seeing `quitting` at the top of its loop: nobody is left asleep.
    os_semaphore_signal(pool->semaphore, pool->worker_count);
    for (u32 i = 0; i < pool->worker_count; i += 1) { os_thread_join(pool->workers[i]); }
    os_semaphore_destroy(pool->semaphore);
    pool->worker_count = 0;
    pool->initialized = 0;
}

void jobs_push(JobCounter *counter, JobFunc *fn, void *data) {
    jobs_submit(counter, fn, data, 0, 1);
}

void jobs_dispatch(JobCounter *counter, JobFunc *fn, void *data, u64 count) {
    if (count == 0) { return; }
    u64 chunks = ((u64)jobs_pool.worker_count + 1) * JOBS_CHUNKS_PER_THREAD;
    if (chunks > count) { chunks = count; }
    u64 per_chunk = (count + chunks - 1) / chunks;
    for (u64 begin = 0; begin < count; begin += per_chunk) {
        u64 end = Min(begin + per_chunk, count);
        jobs_submit(counter, fn, data, begin, end);
    }
}

void jobs_wait(JobCounter *counter) {
    u32 spin = 0;
    while (os_atomic_load_u32(&counter->pending) > 0) {
        Job job;
        if (jobs_queue_pop(&job)) {
            jobs_took_job();
            jobs_run(&job);
            spin = 0;
            continue;
        }
        // The ring is empty but our group is not done: the last jobs are
        // running on other cores. Spin briefly, then give the core back.
        spin += 1;
        if (spin < JOBS_SPIN_COUNT) {
            os_cpu_pause();
        } else {
            os_thread_yield();
        }
    }
}

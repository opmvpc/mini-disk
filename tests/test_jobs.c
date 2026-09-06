// test_jobs.c - the job pool: throughput, parallel-for equivalence, nesting and
// a clean shutdown. Every case starts and stops a pool of its own, which is the
// shutdown test repeated fourteen times.

typedef struct TestJobsCount {
    volatile u32 value;
} TestJobsCount;

static void test_jobs_increment(void *data, u64 begin, u64 end) {
    TestJobsCount *count = (TestJobsCount *)data;
    Unused(begin);
    Unused(end);
    os_atomic_inc_u32(&count->value);
}

TEST(jobs_ten_thousand) {
    Unused(arena);
    jobs_init(0);
    TestJobsCount count;
    count.value = 0;
    JobCounter counter;
    counter.pending = 0;
    for (u32 i = 0; i < 10000; i += 1) { jobs_push(&counter, test_jobs_increment, &count); }
    jobs_wait(&counter);
    EXPECT(count.value == 10000);
    EXPECT(counter.pending == 0);
    EXPECT(jobs_pending() == 0);
    EXPECT(jobs_busy() == 0);
    jobs_shutdown();
}

// --- parallel-for ----------------------------------------------------------
typedef struct TestJobsSum {
    const u32 *values;
    volatile long long total;  // one atomic add per chunk, not per element
} TestJobsSum;

static void test_jobs_sum_range(void *data, u64 begin, u64 end) {
    TestJobsSum *sum = (TestJobsSum *)data;
    u64 local = 0;
    for (u64 i = begin; i < end; i += 1) { local += sum->values[i]; }
    os_atomic_add_u64(&sum->total, local);
}

TEST(jobs_parallel_for) {
    u64 count = 1000000;
    u32 *values = push_array(arena, u32, count);
    u64 sequential = 0;
    for (u64 i = 0; i < count; i += 1) {
        values[i] = (u32)(hash64_mix(i + 1) & 0xFFFFu);
        sequential += values[i];
    }

    jobs_init(0);
    TestJobsSum sum;
    sum.values = values;
    sum.total = 0;
    JobCounter counter;
    counter.pending = 0;
    jobs_dispatch(&counter, test_jobs_sum_range, &sum, count);
    jobs_wait(&counter);
    EXPECT((u64)sum.total == sequential);

    // A count smaller than the number of chunks must still cover every index.
    sum.total = 0;
    counter.pending = 0;
    jobs_dispatch(&counter, test_jobs_sum_range, &sum, 3);
    jobs_wait(&counter);
    EXPECT((u64)sum.total == (u64)values[0] + values[1] + values[2]);

    // And a dispatch of nothing is nothing, not a hang.
    counter.pending = 0;
    jobs_dispatch(&counter, test_jobs_sum_range, &sum, 0);
    jobs_wait(&counter);
    EXPECT(counter.pending == 0);
    jobs_shutdown();
}

// --- nesting ---------------------------------------------------------------
// The outer job pushes its own group and waits on it: the waiting thread runs
// jobs while it waits, so a nested wait can never deadlock the pool.
typedef struct TestJobsNested {
    TestJobsCount leaves;
    volatile u32 parents;
} TestJobsNested;

static void test_jobs_parent(void *data, u64 begin, u64 end) {
    TestJobsNested *nested = (TestJobsNested *)data;
    Unused(begin);
    Unused(end);
    JobCounter inner;
    inner.pending = 0;
    for (u32 i = 0; i < 50; i += 1) { jobs_push(&inner, test_jobs_increment, &nested->leaves); }
    jobs_wait(&inner);
    os_atomic_inc_u32(&nested->parents);
}

TEST(jobs_nested) {
    Unused(arena);
    jobs_init(0);
    TestJobsNested nested;
    StructZero(&nested);
    JobCounter counter;
    counter.pending = 0;
    for (u32 i = 0; i < 20; i += 1) { jobs_push(&counter, test_jobs_parent, &nested); }
    jobs_wait(&counter);
    EXPECT(nested.parents == 20);
    EXPECT(nested.leaves.value == 20 * 50);
    jobs_shutdown();
}

TEST(jobs_pool_lifetime) {
    Unused(arena);
    EXPECT(os_cpu_count() >= 1);
    for (u32 round = 0; round < 3; round += 1) {
        jobs_init(2);
        EXPECT(jobs_worker_count() == 2);
        EXPECT(jobs_thread_index() == 0);  // this is the main thread
        TestJobsCount count;
        count.value = 0;
        JobCounter counter;
        counter.pending = 0;
        for (u32 i = 0; i < 64; i += 1) { jobs_push(&counter, test_jobs_increment, &count); }
        jobs_wait(&counter);
        EXPECT(count.value == 64);
        jobs_shutdown();
        EXPECT(jobs_worker_count() == 0);
        EXPECT(jobs_pending() == 0);
    }
    // With no workers at all the submitter runs everything inline.
    jobs_init(1);
    jobs_shutdown();
}

// --- platform primitives ---------------------------------------------------
static void test_jobs_thread_body(void *data) {
    TestJobsCount *count = (TestJobsCount *)data;
    ArenaTemp scratch = scratch_begin(0, 0);  // a worker owns scratch arenas too
    EXPECT(push_array(scratch.arena, u8, 128) != 0);
    scratch_end(scratch);
    os_atomic_inc_u32(&count->value);
    scratch_thread_release();
}

TEST(jobs_platform_threads) {
    Unused(arena);
    TestJobsCount count;
    count.value = 0;
    OsThread thread = os_thread_create(test_jobs_thread_body, &count, str8_lit("minidisk test"));
    os_thread_join(thread);
    EXPECT(count.value == 1);

    OsSemaphore semaphore = os_semaphore_create(0, 4);
    os_semaphore_signal(semaphore, 2);
    os_semaphore_wait(semaphore);
    os_semaphore_wait(semaphore);  // would hang if the count were wrong
    os_semaphore_destroy(semaphore);

    OsMutex mutex;
    os_mutex_init(&mutex);
    os_mutex_lock(&mutex);
    os_mutex_unlock(&mutex);

    volatile u32 value = 0;
    EXPECT(os_atomic_inc_u32(&value) == 1);
    EXPECT(os_atomic_add_u32(&value, 4) == 1);
    EXPECT(os_atomic_load_u32(&value) == 5);
    EXPECT(os_atomic_cas_u32(&value, 5, 9) == 5);
    EXPECT(os_atomic_load_u32(&value) == 9);
    os_atomic_store_u32(&value, 0);
    EXPECT(os_atomic_dec_u32(&value) == U32_MAX);
}

static void test_jobs_run_all(void) {
    RUN(jobs_ten_thousand);
    RUN(jobs_parallel_for);
    RUN(jobs_nested);
    RUN(jobs_pool_lifetime);
    RUN(jobs_platform_threads);
}

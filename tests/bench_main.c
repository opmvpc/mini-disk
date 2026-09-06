// bench_main.c - benchmark skeleton: wall clock (QPC) + cycles (__rdtsc) + bytes.
// One bench for now (mem_copy); every perf critical module adds its own (ADR-012).
#include "../src/base/base.h"
#include "../src/base/base_arena.h"
#include "../src/base/base_string.h"
#include "../src/base/base_math.h"
#include "../src/base/base_hash.h"
#include "../src/platform/platform.h"

#include "../src/base/base_arena.c"
#include "../src/base/base_string.c"
#include "../src/base/base_math.c"
#include "../src/base/base_hash.c"
#include "../src/platform/win32/win32_platform.c"

typedef struct BenchResult {
    const char *name;
    u64 cycles;
    u64 micros;
    u64 bytes;
} BenchResult;

global Arena *bench_arena;

static void bench_print(BenchResult r) {
    u64 mb_per_s = (r.micros != 0) ? (r.bytes / r.micros) : 0;  // bytes/us == MB/s
    f64 bytes_per_cycle = (r.cycles != 0) ? ((f64)r.bytes / (f64)r.cycles) : 0.0;
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena, "%s: %llu us, %llu cycles, %llu MB, %llu MB/s, %f B/cycle\n",
                         r.name, r.micros, r.cycles, r.bytes >> 20, mb_per_s, bytes_per_cycle));
    scratch_end(scratch);
}

static BenchResult bench_mem_copy(void) {
    u64 block_size = MB(16);
    u32 iterations = 16;
    u8 *src = push_array(bench_arena, u8, block_size);
    u8 *dst = push_array(bench_arena, u8, block_size);
    mem_set(src, 0xC3, block_size);
    mem_set(dst, 0, block_size);

    u64 start_us = os_time_now_us();
    u64 start_cycles = __rdtsc();
    for (u32 i = 0; i < iterations; i += 1) { mem_copy(dst, src, block_size); }
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();
    AssertAlways(dst[block_size - 1] == 0xC3);

    BenchResult result;
    result.name = "mem_copy 16 MB x16";
    result.cycles = end_cycles - start_cycles;
    result.micros = end_us - start_us;
    result.bytes = block_size * iterations;
    return result;
}

int main(void) {
    os_init();
    bench_arena = arena_alloc(GB(1));
    os_debug_print(str8_lit("minidisk benches\n"));
    bench_print(bench_mem_copy());
    return 0;
}

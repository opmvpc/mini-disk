// bench_main.c - benchmark skeleton: wall clock (QPC) + cycles (__rdtsc) + bytes.
// One bench for now (mem_copy); every perf critical module adds its own (ADR-012).
#include "../src/base/base.h"
#include "../src/base/base_arena.h"
#include "../src/base/base_string.h"
#include "../src/base/base_math.h"
#include "../src/base/base_hash.h"
#include "../src/platform/platform.h"
#include "../src/ui/r_core.h"
#include "../src/ui/r_backend.h"
#include "../src/ui/r_atlas.h"
#include "../src/ui/r_raster.h"
#include "../src/ui/r_icons.h"

#include "../src/base/base_arena.c"
#include "../src/base/base_string.c"
#include "../src/base/base_math.c"
#include "../src/base/base_hash.c"
#include "../src/platform/win32/win32_platform.c"
#include "../src/ui/r_atlas.c"
#include "../src/ui/r_raster.c"
#include "../src/ui/r_icons.c"
#include "../src/ui/r_core.c"

// The renderer benches measure r_core and r_atlas, not the driver: the back end
// is a stub, exactly as in the tests.
b32  r_backend_init(void) { return 1; }
void r_backend_shutdown(void) {}
R_Vertex *r_backend_map_vertices(u32 quad_count) { Unused(quad_count); return 0; }
void r_backend_draw(const R_Frame *frame) { Unused(frame); }
u32  r_backend_texture_r8(u32 size) { Unused(size); return 1; }
void r_backend_texture_upload_r8(u32 texture, u32 atlas_size, const u8 *pixels, u32 x, u32 y,
                                 u32 width, u32 height) {
    Unused(texture); Unused(atlas_size); Unused(pixels);
    Unused(x); Unused(y); Unused(width); Unused(height);
}

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

// 10 000 rects across four clip zones and two textures: the shape of a dense
// UI frame. Measures the whole path, r_rect to batches, vertices included.
static BenchResult bench_batch_build(void) {
    u32 rect_count = 10000;
    u32 iterations = 64;
    Arena *frame_arena = arena_alloc(MB(256));

    u64 start_us = os_time_now_us();
    u64 start_cycles = __rdtsc();
    u32 batches = 0;
    for (u32 iter = 0; iter < iterations; iter += 1) {
        r_begin_frame(frame_arena, 1920.0f, 1080.0f, 1.0f);
        R_RectParams params;
        StructZero(&params);
        params.color = 0xFF804020u;
        params.corner_radius = 3.0f;
        for (u32 i = 0; i < rect_count; i += 1) {
            if ((i % 2500) == 0) {
                if (i) { r_pop_clip(); }
                f32 clip_x = (f32)(i / 2500) * 400.0f;
                r_push_clip(rect(clip_x, 0.0f, clip_x + 380.0f, 1080.0f));
            }
            f32 x = (f32)((i % 40) * 24);
            f32 y = (f32)((i / 40) * 12 % 1000);
            params.dst = rect(x, y, x + 20.0f, y + 10.0f);
            params.texture = (i & 8) ? 1 : 0;
            r_rect(params);
        }
        r_pop_clip();
        r_end_frame();
        batches = r_draw_call_count();
        arena_clear(frame_arena);
    }
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();
    AssertAlways(batches > 0);
    arena_release(frame_arena);

    BenchResult result;
    result.name = "r_core batch build 10k rects x64";
    result.cycles = end_cycles - start_cycles;
    result.micros = end_us - start_us;
    result.bytes = (u64)rect_count * iterations * 4 * sizeof(R_Vertex);
    return result;
}

// Skyline packing alone: 20 000 glyph sized entries, which is what a full
// latin + kana atlas costs to build.
static BenchResult bench_atlas_skyline(void) {
    u32 count = 20000;
    Arena *atlas_arena = arena_alloc(MB(512));
    r_atlas_init(atlas_arena);
    u8 *pixels = push_array_zero(bench_arena, u8, 40 * 40);

    u64 start_us = os_time_now_us();
    u64 start_cycles = __rdtsc();
    u64 area = 0;
    for (u32 i = 0; i < count; i += 1) {
        u64 noise = hash64_mix(i + 1);
        u32 w = 6 + (u32)(noise % 34);
        u32 h = 8 + (u32)((noise >> 9) % 32);
        R_AtlasRect placed = r_atlas_add(w, h, pixels);
        AssertAlways(placed.width == w);
        area += w * h;
    }
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena, "  skyline: %u entries, %llu px2 packed, atlas %u\n", count,
                         area, r_atlas_size()));
    scratch_end(scratch);
    arena_release(atlas_arena);

    BenchResult result;
    result.name = "r_atlas skyline 20k entries";
    result.cycles = end_cycles - start_cycles;
    result.micros = end_us - start_us;
    result.bytes = area;
    return result;
}

int main(void) {
    os_init();
    bench_arena = arena_alloc(GB(1));
    os_debug_print(str8_lit("minidisk benches\n"));
    bench_print(bench_mem_copy());
    bench_print(bench_batch_build());
    bench_print(bench_atlas_skyline());
    return 0;
}

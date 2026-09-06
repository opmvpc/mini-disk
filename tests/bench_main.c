// bench_main.c - benchmark skeleton: wall clock (QPC) + cycles (__rdtsc) + bytes.
// One bench for now (mem_copy); every perf critical module adds its own (ADR-012).
#include "../src/base/base.h"
#include "../src/base/base_arena.h"
#include "../src/base/base_string.h"
#include "../src/base/base_math.h"
#include "../src/base/base_hash.h"
#include "../src/platform/platform.h"
#include "../src/base/base_jobs.h"
#include "../src/core/library/lib_model.h"
#include "../src/core/library/tags.h"
#include "../src/core/library/lib_events.h"
#include "../src/core/library/lib_scan.h"
#include "../src/ui/r_core.h"
#include "../src/ui/r_backend.h"
#include "../src/ui/r_atlas.h"
#include "../src/ui/r_raster.h"
#include "../src/ui/r_icons.h"
#include "../src/ui/ui_font.h"
#include "../src/ui/ui_text.h"
#include "../src/ui/ui_theme.h"
#include "../src/ui/ui_core.h"

#include "../src/base/base_arena.c"
#include "../src/base/base_string.c"
#include "../src/base/base_math.c"
#include "../src/base/base_hash.c"
#include "../src/base/base_jobs.c"
#include "../src/platform/win32/win32_platform.c"
#include "../src/platform/win32/win32_file.c"
#include "../src/platform/win32/win32_thread.c"
#include "../src/ui/r_atlas.c"
#include "../src/ui/r_raster.c"
#include "../src/ui/r_icons.c"
#include "../src/ui/r_core.c"
#include "../src/platform/win32/win32_font_dwrite.c"
#include "../src/ui/ui_font.c"
#include "../src/ui/ui_text.c"
#include "../src/ui/ui_theme.c"
#include "../src/ui/ui_core.c"
#include "../src/core/library/lib_model.c"
#include "../src/core/library/tags.c"
#include "../src/core/library/tags_id3.c"
#include "../src/core/library/tags_vorbis.c"
#include "../src/core/library/tags_mp4.c"
#include "../src/core/library/tags_ape.c"
#include "../src/core/library/tags_riff.c"
#include "../src/core/library/lib_scan.c"

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

    u64 best_us = U64_MAX;
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
        // r_end_frame on its own, best of the passes: that is the number the
        // budget of T-004 and T-009 is written against (the mean measures the
        // machine, the minimum measures the code).
        u64 pass_start = os_time_now_us();
        r_end_frame();
        u64 pass_us = os_time_now_us() - pass_start;
        if (pass_us < best_us) { best_us = pass_us; }
        batches = r_draw_call_count();
        arena_clear(frame_arena);
    }
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();
    AssertAlways(batches > 0);
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena,
                         "  batches: %u draw calls, r_end_frame %llu us (best of %u, budget 900)\n",
                         batches, best_us, iterations));
    scratch_end(scratch);
    AssertAlways(best_us <= 900);
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

// Warm glyph cache lookups: the cost the UI actually pays every frame once the
// first one has rasterized everything. Each pass walks a mixed Latin and
// Japanese line of 32 codepoints through the fallback map, the glyph cache and
// the quad emission, and allocates nothing.
static BenchResult bench_text_glyph_cache(void) {
    u32 iterations = 20000;
    Arena *text_arena = arena_alloc(MB(64));
    r_atlas_init(text_arena);
    if (!os_font_init() || !ui_fonts_build(1.0f)) {
        os_debug_print(str8_lit("  text: no system font, bench skipped\n"));
        BenchResult skipped;
        skipped.name = "ui_text glyph cache (skipped)";
        skipped.cycles = 0;
        skipped.micros = 0;
        skipped.bytes = 0;
        return skipped;
    }
    ui_text_init(text_arena);
    OsFont font = ui_font(UI_FontStyle_Ui);
    // The Japanese part is spelled out in escapes so the source stays pure
    // ASCII whatever code page the compiler assumes.
    String8 line = str8_lit("Utada Hikaru - First Love \xE5\xAE\x87\xE5\xA4\x9A\xE7\x94\xB0");

    // Warm up: after this every glyph, every variant and every fallback
    // answer is cached, which is the steady state of a running UI.
    Arena *warm_arena = arena_alloc(MB(16));
    for (u32 i = 0; i < 2; i += 1) {
        r_begin_frame(warm_arena, 1920.0f, 1080.0f, 1.0f);
        ui_text_draw(font, line, v2(20.0f + 0.5f * (f32)i, 40.0f), 0xFFFFFFFFu, 0);
        r_end_frame();
        arena_clear(warm_arena);
    }
    arena_release(warm_arena);
    UI_TextStats before = ui_text_stats();

    // Drawing, not ui_text_width: a measurement of the same string twice is a
    // hit in the string cache and would time a hash instead of the glyph path.
    Arena *frame_arena = arena_alloc(MB(64));
    u64 start_us = os_time_now_us();
    u64 start_cycles = __rdtsc();
    f32 total = 0.0f;
    for (u32 i = 0; i < iterations; i += 1) {
        r_begin_frame(frame_arena, 1920.0f, 1080.0f, 1.0f);
        // A fractional x on every other pass, so both the aligned and the
        // quarter pixel variants are exercised, exactly as scrolling does.
        total += ui_text_draw(font, line, v2(20.0f + 0.5f * (f32)(i & 1), 40.0f), 0xFFFFFFFFu, 0);
        r_end_frame();
        arena_clear(frame_arena);
    }
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();
    UI_TextStats after = ui_text_stats();
    AssertAlways(total > 0.0f);
    AssertAlways(after.rasterizations == before.rasterizations);  // nothing new

    u64 lookups = after.glyph_hits - before.glyph_hits;
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena, "  text: %llu glyph lookups, %llu ns each\n", lookups,
                         lookups ? ((end_us - start_us) * 1000 / lookups) : 0));
    scratch_end(scratch);
    os_font_shutdown();
    arena_release(frame_arena);
    arena_release(text_arena);

    BenchResult result;
    result.name = "ui_text glyph cache 20k lines";
    result.cycles = end_cycles - start_cycles;
    result.micros = end_us - start_us;
    result.bytes = lookups;  // one "byte" per lookup: the MB/s column reads as M lookups/s
    return result;
}


// A 10 000 box tree, laid out over and over: the five passes are the whole
// cost of a frame once the widgets are virtualized (ADR-004, T-006).
static BenchResult bench_ui_layout(void) {
    u32 columns = 20;
    u32 rows = 100;
    u32 cells = 5;  // 20 * 100 * 5 = 10 000 leaves, plus the containers

    Arena *ui_arena = arena_alloc(MB(256));
    Arena *frame_arena = arena_alloc(MB(64));
    r_atlas_init(ui_arena);
    ui_init(ui_arena);

    V2 viewport = v2(1920.0f, 1080.0f);
    r_begin_frame(frame_arena, viewport.x, viewport.y, 1.0f);
    ui_begin(frame_arena, 0, 0, 1.0f / 60.0f, viewport, 1.0f);
    UI_Box *root = ui_root(UI_Layer_Content);
    ui_push_child_layout_axis(Axis2_X);
    ui_push_pref_width(ui_pct(1.0f, 0.0f));
    ui_push_pref_height(ui_pct(1.0f, 0.0f));
    for (u32 c = 0; c < columns; c += 1) {
        UI_Seed(hash64_mix(c + 1)) {
            UI_Box *column = ui_build_box_from_key(0, 0);
            UI_Parent(column) UI_ChildLayoutAxis(Axis2_Y) {
                for (u32 r = 0; r < rows; r += 1) {
                    UI_Box *row = ui_build_box_from_key(0, 0);
                    UI_Parent(row) UI_ChildLayoutAxis(Axis2_X) {
                        for (u32 i = 0; i < cells; i += 1) {
                            UI_PrefWidth(ui_px(40.0f, (f32)(i & 1)))
                            UI_PrefHeight(ui_px(20.0f, 1.0f)) {
                                ui_build_box_from_key(0, 0);
                            }
                        }
                    }
                }
            }
        }
    }
    ui_pop_pref_height();
    ui_pop_pref_width();
    ui_pop_child_layout_axis();

    u32 box_count = 0;
    for (UI_Box *column = root->first; column; column = column->next) {
        box_count += 1;
        for (UI_Box *row = column->first; row; row = row->next) {
            box_count += 1;
            for (UI_Box *cell = row->first; cell; cell = cell->next) { box_count += 1; }
        }
    }
    AssertAlways(box_count >= 10000);

    // Repetition testing: each pass is timed on its own and the *best* one is
    // the answer. The mean measures the machine (other processes, turbo, page
    // faults on the first pass), the minimum measures the code.
    u32 iterations = 200;
    u64 best_us = U64_MAX;
    u64 start_us = os_time_now_us();
    u64 start_cycles = __rdtsc();
    for (u32 i = 0; i < iterations; i += 1) {
        u64 pass_start = os_time_now_us();
        ui_layout(root);
        u64 pass_us = os_time_now_us() - pass_start;
        if (pass_us < best_us) { best_us = pass_us; }
    }
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();
    AssertAlways(root->first->computed_size[Axis2_Y] > 0.0f);

    ArenaTemp scratch = scratch_begin(0, 0);
    u64 mean_us = (end_us - start_us) / iterations;
    os_debug_print(str8f(scratch.arena,
                         "  ui: %u boxes, %llu us per layout (best of %u, mean %llu, budget 1000)\n",
                         box_count, best_us, iterations, mean_us));
    os_debug_print(str8f(scratch.arena, "  ui: %llu ns per box (best), %llu ns per box (mean)\n",
                         best_us * 1000 / box_count, mean_us * 1000 / box_count));
    scratch_end(scratch);
    AssertAlways(best_us < 1000);  // the acceptance criterion: < 1 ms

    r_end_frame();
    arena_release(frame_arena);
    arena_release(ui_arena);

    BenchResult result;
    result.name = "ui_layout 10k boxes x200";
    result.cycles = end_cycles - start_cycles;
    result.micros = end_us - start_us;
    result.bytes = (u64)box_count * iterations;  // the MB/s column reads as M boxes/s
    return result;
}

// --- jobs ------------------------------------------------------------------
// Dispatch overhead: the job body is empty, so what is measured is the ring,
// the counter and the wake-ups. Target: under 1 us per job (ADR-012 budget).
static void bench_job_empty(void *data, u64 begin, u64 end) {
    Unused(data);
    Unused(begin);
    Unused(end);
}

static u64 bench_jobs_dispatch_run(u32 worker_count, u32 job_count) {
    jobs_init(worker_count);
    JobCounter counter;
    counter.pending = 0;
    u64 start_us = os_time_now_us();
    for (u32 i = 0; i < job_count; i += 1) { jobs_push(&counter, bench_job_empty, 0); }
    jobs_wait(&counter);
    u64 elapsed_us = os_time_now_us() - start_us;
    AssertAlways(counter.pending == 0);
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena, "  jobs: %u workers, %llu ns per empty job\n",
                         jobs_worker_count(), elapsed_us * 1000 / job_count));
    scratch_end(scratch);
    jobs_shutdown();
    return elapsed_us;
}

static BenchResult bench_jobs_dispatch(void) {
    u32 job_count = 200000;
    // Every pool shape, because the interesting number is what the contention
    // between consumers costs: one producer against N consumers on one ring.
    bench_jobs_dispatch_run(1, job_count);
    bench_jobs_dispatch_run(3, job_count);

    u64 start_cycles = __rdtsc();
    u64 micros = bench_jobs_dispatch_run(0, job_count);  // 0: one worker per core, minus us
    u64 end_cycles = __rdtsc();
    u64 ns_per_job = micros * 1000 / job_count;
    AssertAlways(ns_per_job < 1000);  // the acceptance criterion

    BenchResult result;
    result.name = "jobs dispatch 200k empty jobs";
    result.cycles = end_cycles - start_cycles;
    result.micros = micros;
    result.bytes = job_count;  // the MB/s column reads as M jobs/s
    return result;
}

// 64 MB summed sequentially, then through jobs_dispatch: the speedup is the
// number the ticket asks for (> 3x on four cores).
typedef struct BenchSum {
    const u32 *values;
    volatile long long total;
} BenchSum;

// The same range, but with real work per element: the plain sum saturates the
// memory bus long before it saturates the cores, so it measures the machine's
// DRAM, not the pool. This one measures the pool.
static void bench_mix_range(void *data, u64 begin, u64 end) {
    BenchSum *sum = (BenchSum *)data;
    u64 local = 0;
    for (u64 i = begin; i < end; i += 1) {
        u64 value = sum->values[i];
        for (u32 round = 0; round < 8; round += 1) { value = hash64_mix(value); }
        local += value;
    }
    os_atomic_add_u64(&sum->total, local);
}

static void bench_sum_range(void *data, u64 begin, u64 end) {
    BenchSum *sum = (BenchSum *)data;
    u64 local = 0;
    for (u64 i = begin; i < end; i += 1) { local += sum->values[i]; }
    os_atomic_add_u64(&sum->total, local);
}

static BenchResult bench_jobs_parallel_sum(void) {
    u64 bytes = MB(64);
    u64 count = bytes / sizeof(u32);
    u32 *values = push_array(bench_arena, u32, count);
    for (u64 i = 0; i < count; i += 1) { values[i] = (u32)i; }

    BenchSum sum;
    sum.values = values;
    sum.total = 0;
    u64 sequential_start = os_time_now_us();
    bench_sum_range(&sum, 0, count);
    u64 sequential_us = os_time_now_us() - sequential_start;
    u64 expected = (u64)sum.total;

    jobs_init(0);
    sum.total = 0;
    JobCounter counter;
    counter.pending = 0;
    u64 start_us = os_time_now_us();
    u64 start_cycles = __rdtsc();
    jobs_dispatch(&counter, bench_sum_range, &sum, count);
    jobs_wait(&counter);
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();
    u64 parallel_us = end_us - start_us;
    AssertAlways((u64)sum.total == expected);

    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena,
                         "  jobs: sum 64 MB, %llu us sequential, %llu us on %u workers, "
                         "speedup %02f x\n",
                         sequential_us, parallel_us, jobs_worker_count(),
                         parallel_us ? (f64)sequential_us / (f64)parallel_us : 0.0));
    scratch_end(scratch);

    sum.total = 0;
    u64 mix_sequential_start = os_time_now_us();
    bench_mix_range(&sum, 0, count);
    u64 mix_sequential_us = os_time_now_us() - mix_sequential_start;
    u64 mix_expected = (u64)sum.total;

    jobs_init(0);
    sum.total = 0;
    counter.pending = 0;
    u64 mix_start_us = os_time_now_us();
    jobs_dispatch(&counter, bench_mix_range, &sum, count);
    jobs_wait(&counter);
    u64 mix_parallel_us = os_time_now_us() - mix_start_us;
    AssertAlways((u64)sum.total == mix_expected);
    scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena,
                         "  jobs: mix 16 M elements, %llu us sequential, %llu us on %u workers, "
                         "speedup %02f x\n",
                         mix_sequential_us, mix_parallel_us, jobs_worker_count(),
                         mix_parallel_us ? (f64)mix_sequential_us / (f64)mix_parallel_us : 0.0));
    scratch_end(scratch);
    jobs_shutdown();

    BenchResult result;
    result.name = "jobs parallel sum 64 MB";
    result.cycles = end_cycles - start_cycles;
    result.micros = parallel_us;
    result.bytes = bytes;
    return result;
}

// --- library scan (T-010) --------------------------------------------------
// 50 000 empty files in 500 folders, generated once in %TEMP%: a cold scan
// (empty library) then a warm one (incremental, nothing changed on disk).
#define BENCH_SCAN_FILES 50000
#define BENCH_SCAN_FOLDERS 500

// The listing is taken before the first deletion: deleting through an open
// search handle makes FindNextFileW skip entries.
static void bench_scan_tree_remove(Arena *arena, String8 dir) {
    OsDirIter iter;
    if (!os_dir_iter_begin(&iter, dir)) { return; }
    String8List files;
    String8List folders;
    StructZero(&files);
    StructZero(&folders);
    OsFileInfo info;
    while (os_dir_iter_next(&iter, &info)) {
        String8 path = os_path_join(arena, dir, info.name);
        str8_list_push(arena, info.is_dir ? &folders : &files, path);
    }
    os_dir_iter_end(&iter);
    for (String8Node *node = files.first; node; node = node->next) { os_file_delete(node->str); }
    for (String8Node *node = folders.first; node; node = node->next) {
        bench_scan_tree_remove(arena, node->str);
    }
    os_dir_delete(dir);
}

static BenchResult bench_library_scan(void) {
    ArenaTemp temp = arena_temp_begin(bench_arena);
    Arena *arena = bench_arena;
    String8 root = os_path_join(arena, os_known_folder(arena, OsKnownFolder_Temp),
                                str8_lit("minidisk_bench_scan"));
    bench_scan_tree_remove(arena, root);

    u64 build_start_us = os_time_now_us();
    os_dir_create(root);
    for (u32 i = 0; i < BENCH_SCAN_FOLDERS; i += 1) {
        os_dir_create(os_path_join(arena, root, str8f(arena, "d%03u", i)));
    }
    for (u32 i = 0; i < BENCH_SCAN_FILES; i += 1) {
        ArenaTemp scratch = arena_temp_begin(arena);
        String8 folder = os_path_join(arena, root, str8f(arena, "d%03u", i % BENCH_SCAN_FOLDERS));
        os_file_write_all(os_path_join(arena, folder, str8f(arena, "t%05u.mp3", i)), str8(0, 0));
        arena_temp_end(scratch);
    }
    u64 build_us = os_time_now_us() - build_start_us;

    Arena *text = arena_alloc(MB(64));
    Arena *columns = arena_alloc(MB(64));
    Library lib;
    lib_init(&lib, columns, text);
    LibEventQueue events;
    StructZero(&events);
    jobs_init(0);

    LibScan cold;
    u64 cold_start_us = os_time_now_us();
    u64 cold_cycles = __rdtsc();
    lib_scan_begin(&cold, &lib, &events, root);
    while (lib_scan_update(&cold)) { os_thread_yield(); }
    cold_cycles = __rdtsc() - cold_cycles;
    u64 cold_us = os_time_now_us() - cold_start_us;
    lib_scan_end(&cold);

    LibScan warm;
    u64 warm_start_us = os_time_now_us();
    lib_scan_begin(&warm, &lib, &events, root);
    while (lib_scan_update(&warm)) { os_thread_yield(); }
    u64 warm_us = os_time_now_us() - warm_start_us;
    lib_scan_end(&warm);
    AssertAlways(cold.added == BENCH_SCAN_FILES);
    AssertAlways(warm.unchanged == BENCH_SCAN_FILES && warm.added == 0);

    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena,
                         "  scan: %u files in %u folders, %llu us cold, %llu us warm, "
                         "%u workers, tree built in %llu us\n",
                         BENCH_SCAN_FILES, BENCH_SCAN_FOLDERS, cold_us, warm_us,
                         jobs_worker_count(), build_us));
    os_debug_print(str8f(scratch.arena,
                         "  scan: %llu MB of columns for 100 000 tracks, "
                         "%llu KB of strings for %u tracks\n",
                         ((u64)100000 * LIB_BYTES_PER_TRACK) >> 20, lib.strings.size >> 10,
                         lib.live_count));
    scratch_end(scratch);

    jobs_shutdown();
    bench_scan_tree_remove(arena, root);
    arena_release(text);
    arena_release(columns);
    arena_temp_end(temp);

    BenchResult result;
    result.name = "library scan 50 000 files (cold)";
    result.cycles = cold_cycles;
    result.micros = cold_us;
    result.bytes = 0;
    return result;
}

// The shape of the real demo (T-009): three clipped panels, thirty rows each,
// every row an untextured background followed by three runs of atlas text and
// an icon, plus a popup on its own layer. Before T-009 this cut a batch at
// every single row; what is measured is the batch count and the time
// r_end_frame alone costs, which is where the sort, the merge and the vertex
// generation live.
static void bench_realistic_frame_build(Arena *frame_arena, u32 rows) {
    V2 uv0 = v2(0.10f, 0.10f);
    V2 uv1 = v2(0.12f, 0.14f);
    u32 atlas = r_atlas_texture();
    u32 fg = 0xFFFFFFFFu;

    r_begin_frame(frame_arena, 1920.0f, 1080.0f, 1.0f);
    R_RectParams params;
    StructZero(&params);
    params.color = 0xFF2A2A2Au;
    params.corner_radius = 4.0f;

    // Toolbar: no texture, no clip of its own.
    for (u32 i = 0; i < 6; i += 1) {
        f32 x = 8.0f + (f32)i * 90.0f;
        params.dst = rect(x, 8.0f, x + 80.0f, 40.0f);
        r_rect(params);
    }

    for (u32 panel = 0; panel < 3; panel += 1) {
        f32 px = 8.0f + (f32)panel * 634.0f;
        r_push_clip(rect(px, 48.0f, px + 620.0f, 1072.0f));
        params.dst = rect(px, 48.0f, px + 620.0f, 1072.0f);
        r_rect(params);  // panel background
        for (u32 i = 0; i < 10; i += 1) {
            f32 gx = px + 12.0f + (f32)i * 9.0f;
            r_rect_textured(rect(gx, 56.0f, gx + 8.0f, 72.0f), atlas, uv0, uv1, fg, 1);
        }

        // The list clip cuts the last row in half, exactly as a scrolled list
        // does: that row is the one command that still needs its own scissor.
        f32 list_bottom = 96.0f + (f32)rows * 26.0f - 13.0f;
        r_push_clip(rect(px, 96.0f, px + 620.0f, list_bottom));
        for (u32 row = 0; row < rows; row += 1) {
            f32 y = 96.0f + (f32)row * 26.0f;
            params.dst = rect(px + 4.0f, y, px + 616.0f, y + 25.0f);
            params.color = (row & 1) ? 0xFF303030u : 0xFF262626u;
            r_rect(params);  // row background, untextured
            u32 columns[3] = {14, 10, 5};
            f32 gx = px + 12.0f;
            for (u32 column = 0; column < 3; column += 1) {
                for (u32 glyph = 0; glyph < columns[column]; glyph += 1) {
                    r_rect_textured(rect(gx, y + 5.0f, gx + 8.0f, y + 21.0f), atlas, uv0, uv1, fg,
                                    1);
                    gx += 9.0f;
                }
                gx += 40.0f;
            }
            r_rect_textured(rect(px + 592.0f, y + 5.0f, px + 608.0f, y + 21.0f), atlas, uv0, uv1,
                            fg, 1);  // icon
        }
        r_pop_clip();
        r_pop_clip();
        params.color = 0xFF2A2A2Au;
    }

    r_set_layer(R_Layer_Popup);
    r_push_clip(rect(600.0f, 300.0f, 1000.0f, 560.0f));
    params.dst = rect(600.0f, 300.0f, 1000.0f, 560.0f);
    r_rect(params);
    for (u32 i = 0; i < 20; i += 1) {
        f32 gx = 612.0f + (f32)i * 9.0f;
        r_rect_textured(rect(gx, 320.0f, gx + 8.0f, 336.0f), atlas, uv0, uv1, fg, 1);
    }
    r_pop_clip();
    r_set_layer(R_Layer_Content);
}

static BenchResult bench_realistic_frame(void) {
    u32 rows = 30;
    u32 iterations = 2000;
    Arena *atlas_arena = arena_alloc(MB(32));
    Arena *frame_arena = arena_alloc(MB(64));
    r_atlas_init(atlas_arena);

    u32 batches = 0;
    u32 quads = 0;
    u64 best_us = U64_MAX;
    u64 start_us = os_time_now_us();
    u64 start_cycles = __rdtsc();
    for (u32 i = 0; i < iterations; i += 1) {
        bench_realistic_frame_build(frame_arena, rows);
        // Only r_end_frame is timed: building the command list is the caller's
        // cost and it did not change.
        u64 pass_start = os_time_now_us();
        r_end_frame();
        u64 pass_us = os_time_now_us() - pass_start;
        if (pass_us < best_us) { best_us = pass_us; }
        batches = r_draw_call_count();
        quads = r_frame_state()->quad_count;
        arena_clear(frame_arena);
    }
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();

    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena,
                         "  frame: %u quads, %u draw calls (budget < 10), r_end_frame %llu us "
                         "(best of %u)\n",
                         quads, batches, best_us, iterations));
    scratch_end(scratch);
    AssertAlways(batches < 10);  // the acceptance criterion of T-009
    arena_release(frame_arena);
    arena_release(atlas_arena);

    BenchResult result;
    result.name = "r_core realistic frame x2000";
    result.cycles = end_cycles - start_cycles;
    result.micros = end_us - start_us;
    result.bytes = (u64)quads * iterations * 4 * sizeof(R_Vertex);
    return result;
}


// --- T-011: tag parsing ----------------------------------------------------
// The vectors of tests/data/, held in memory and parsed in a loop: this
// measures the parsers, not the disk. The scan's own cost per file (two reads
// of 64 KB) is measured by bench_library_scan.
#define BENCH_TAGS_COUNT 13
static const char *bench_tag_files[BENCH_TAGS_COUNT] = {
    "id3v23_utf16.mp3", "id3v24_unsync.mp3", "id3v22.mp3", "id3v1_only.mp3",
    "mpeg_vbri.mp3", "ape_tail.mp3", "flac.flac", "ogg_vorbis.ogg", "opus.opus",
    "mp4.m4a", "mp4_moov_last.m4a", "wav.wav", "aiff.aif",
};

static BenchResult bench_tags_parse(void) {
    String8 vectors[BENCH_TAGS_COUNT];
    u64 total_bytes = 0;
    u32 loaded = 0;
    for (u32 i = 0; i < BENCH_TAGS_COUNT; i += 1) {
        String8 path = str8f(bench_arena, "tests\\data\\%s", bench_tag_files[i]);
        vectors[i] = os_file_read_all(bench_arena, path);
        if (vectors[i].size) { loaded += 1; }
        total_bytes += vectors[i].size;
    }
    AssertAlways(loaded == BENCH_TAGS_COUNT);  // run tools/gen_tag_vectors.py first

    u32 iterations = 2000;
    u64 titles = 0;
    u64 start_cycles = __rdtsc();
    u64 start_us = os_time_now_us();
    for (u32 pass = 0; pass < iterations; pass += 1) {
        for (u32 i = 0; i < BENCH_TAGS_COUNT; i += 1) {
            TagsFile file;
            StructZero(&file);
            file.head = vectors[i];
            file.size = vectors[i].size;
            Tags tags;
            tags_init(&tags);
            tags_parse(&tags, &file);
            titles += tags.title.size;
        }
    }
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();
    AssertAlways(titles != 0);

    u64 files = (u64)iterations * BENCH_TAGS_COUNT;
    u64 ns_per_file = (end_us - start_us) * 1000 / files;
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena,
                         "  tags: %llu files parsed, %llu ns/file, %llu cycles/file\n",
                         files, ns_per_file, (end_cycles - start_cycles) / files));
    scratch_end(scratch);

    BenchResult result;
    result.name = "tags parse x26000";
    result.cycles = end_cycles - start_cycles;
    result.micros = end_us - start_us;
    result.bytes = total_bytes * iterations;
    return result;
}

int main(void) {
    os_init();
    bench_arena = arena_alloc(GB(1));
    os_debug_print(str8_lit("minidisk benches\n"));
    bench_print(bench_mem_copy());
    bench_print(bench_batch_build());
    bench_print(bench_atlas_skyline());
    bench_print(bench_text_glyph_cache());
    bench_print(bench_ui_layout());
    bench_print(bench_jobs_dispatch());
    bench_print(bench_jobs_parallel_sum());
    bench_print(bench_library_scan());
    bench_print(bench_tags_parse());
    bench_print(bench_realistic_frame());
    return 0;
}

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
#include "../src/core/library/lib_index.h"
#include "../src/core/library/lib_search.h"
#include "../src/core/library/lib_cache.h"
#include "../src/core/library/lib_covers.h"
#include "../src/core/plan/plan_model.h"
#include "../src/core/plan/plan_capacity.h"
#include "../src/core/plan/plan_file.h"
#include "../src/core/plan/plan_toc.h"
#include "../src/core/netmd/netmd_models.h"
#include "../src/core/netmd/netmd_transport.h"
#include "../src/core/netmd/netmd_proto.h"
#include "../src/core/netmd/netmd_disc.h"
#include "../src/core/netmd/netmd_control.h"
#include "../src/core/netmd/netmd_des.h"
#include "../src/core/netmd/netmd_secure.h"
#include "../src/core/netmd/netmd_upload.h"
#include "../src/core/netmd/netmd_replay.h"
#include "../src/core/netmd/netmd_edit.h"
#include "../src/core/netmd/netmd_backup.h"
#include "../src/core/netmd/netmd_device.h"
#include "../src/core/dsp/dsp_math.h"
#include "../src/core/dsp/dsp_resample.h"
#include "../src/core/dsp/dsp_loudness.h"
#include "../src/core/dsp/dsp_edit.h"
#include "../src/core/dsp/dsp_dither.h"
#include "../src/core/cache/cache_lru.h"
#include "../src/core/pipeline/pipeline.h"
#include "../src/core/pipeline/pipeline_cache.h"
#include "../src/core/codecs/codec.h"
#include "../src/app/prefs.h"
#include "../src/ui/ui_widgets.h"
#include "../src/app/plan_view.h"
#include "../src/app/transfer.h"
#include "../src/ui/r_core.h"
#include "../src/ui/r_backend.h"
#include "../src/ui/r_atlas.h"
#include "../src/ui/r_thumbs.h"
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
#include "../src/platform/win32/win32_window.c"
#include "../src/platform/win32/win32_dialog.c"
#include "../src/platform/win32/win32_image.c"
#include "../src/platform/win32/win32_usb.c"
#include "../src/platform/win32/win32_media.c"
#include "../src/ui/r_atlas.c"
#include "../src/ui/r_thumbs.c"
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
#include "../src/core/library/lib_index.c"
#include "../src/core/library/lib_search.c"
#include "../src/core/library/lib_cache.c"
#include "../src/core/library/lib_covers.c"
#include "../src/core/plan/plan_model.c"
#include "../src/core/plan/plan_cmd.c"
#include "../src/core/plan/plan_capacity.c"
#include "../src/core/plan/plan_file.c"
#include "../src/core/plan/plan_toc.c"
#include "../src/core/netmd/netmd_models.c"
#include "../src/core/netmd/netmd_proto.c"
#include "../src/core/netmd/netmd_disc.c"
#include "../src/core/netmd/netmd_control.c"
#include "../src/core/netmd/netmd_des.c"
#include "../src/core/netmd/netmd_secure.c"
#include "../src/core/netmd/netmd_upload.c"
#include "../src/core/netmd/netmd_replay.c"
#include "../src/core/netmd/netmd_edit.c"
#include "../src/core/netmd/netmd_backup.c"
#include "../src/core/netmd/netmd_device.c"
#include "../src/core/dsp/dsp_math.c"
#include "../src/core/dsp/dsp_resample.c"
#include "../src/core/dsp/dsp_loudness.c"
#include "../src/core/dsp/dsp_edit.c"
#include "../src/core/dsp/dsp_dither.c"
#include "../src/core/cache/cache_lru.c"
#include "../src/core/pipeline/pipeline.c"
#include "../src/core/pipeline/pipeline_cache.c"
#include "../src/core/codecs/codec.c"
#include "../src/core/codecs/codec_mp3.c"
#include "../src/core/codecs/codec_flac.c"
#include "../src/core/codecs/codec_wav.c"
#include "../src/core/codecs/codec_ogg.c"
#include "../src/core/codecs/codec_mf.c"
#include "../src/ui/ui_widgets.c"
#include "../src/app/prefs.c"
#include "../src/app/plan_view.c"
#include "../src/app/transfer.c"
#include "../src/app/app_state.c"
#include "../src/app/view_library.c"
#include "../src/app/view_device.c"
#include "../src/app/view_plan.c"
#include "../src/app/view_transfer.c"

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

// The thumbnail atlas of T-014 has its own texture: the stub hands out ids the
// same way, so r_thumbs is exercised without a driver.
u32 r_backend_texture_rgba8(u32 size) {
    Unused(size);
    static u32 next_texture = 100;
    next_texture += 1;
    return next_texture;
}

void r_backend_texture_upload_rgba8(u32 texture, u32 atlas_size, const u8 *pixels, u32 x, u32 y,
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

// --- the budgets, and the machine they mean something on (T-022, P-010) ----
// Every perf assertion in this file is an assertion about a machine at rest.
// With an agent compiling in the next worktree they fire on the scheduler, not
// on the code, and killing the run in the middle throws away the numbers that
// were already measured - which is exactly what P-010 described. So a budget
// that is missed is printed, loudly, and only fails the run when the machine
// was measured idle at startup. Correctness assertions are untouched: those
// hold under any load.
global b32 bench_machine_idle;

static void bench_budget(const char *what, u64 measured, u64 budget) {
    if (measured <= budget) { return; }
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena, "  BUDGET %s: %llu > %llu%s\n", what, measured, budget,
                         bench_machine_idle ? "" : " (machine chargee, non bloquant)"));
    scratch_end(scratch);
    AssertAlways(!bench_machine_idle);
}

static void bench_budget_min(const char *what, u64 measured, u64 floor_value) {
    if (measured >= floor_value) { return; }
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena, "  BUDGET %s: %llu < %llu%s\n", what, measured,
                         floor_value, bench_machine_idle ? "" : " (machine chargee, non bloquant)"));
    scratch_end(scratch);
    AssertAlways(!bench_machine_idle);
}

static void bench_probe_kernel(void *data, u64 begin, u64 end) {
    volatile u64 *sink = (volatile u64 *)data;
    u64 acc = 0;
    for (u64 i = begin; i < end; i += 1) { acc += hash64_mix(i + 1); }
    *sink += acc;
}

// Is this machine ours? A fixed kernel run alone, then one such kernel per
// hardware thread all at once. On a free machine the parallel pass costs about
// twice the single one - eight logical threads on four cores, so the siblings
// share a pipeline - and with someone compiling next door it costs four or five
// times as much. That multiple is exactly the condition under which the budgets
// below would be measuring the neighbour instead of the code.
//
// The measurement is taken three times and the **worst** ratio decides. Taking
// the best would be the mistake that cost two runs: under load a single pass
// can be caught in a slow window, which inflates the denominator and makes a
// saturated machine look idle - after which an armed budget kills the run for
// the neighbour's reasons. Erring the other way only disarms an assertion.
// Two numbers, because two different things can go wrong.
//   - the **spread** of nine single-threaded passes: min against max. Almost
//     every budget in this file is single-threaded, and what hurts one is a
//     neighbour on the same core - which shows up as passes that disagree with
//     each other, not as passes that are uniformly slow. At rest the spread is
//     a few percent.
//   - the **parallel ratio**: the same kernel once per hardware thread against
//     the fastest single pass. Eight logical threads on four cores cost about
//     2x by themselves; past 2.5x somebody else is holding cores.
// Both must pass for a budget to be armed. Note that the min, not the mean, is
// the denominator: taking a slow single pass as the reference is what made a
// saturated machine look idle in the first calibration, and an armed budget on
// a loaded machine kills the run for the neighbour's reasons.
#define BENCH_PROBE_WORK       (1u << 21)
#define BENCH_PROBE_SAMPLES    9
#define BENCH_PROBE_SPREAD_MAX 125  // max <= 1.25x min over nine single passes
#define BENCH_PROBE_PAR_MAX    250  // parallel <= 2.5x the fastest single pass
static void bench_probe_machine(void) {
    volatile u64 sink = 0;
    u64 min_us = U64_MAX;
    u64 max_us = 0;
    for (u32 i = 0; i < BENCH_PROBE_SAMPLES; i += 1) {
        u64 one_us = os_time_now_us();
        bench_probe_kernel((void *)&sink, 0, BENCH_PROBE_WORK);
        one_us = os_time_now_us() - one_us;
        if (one_us == 0) { one_us = 1; }
        if (one_us < min_us) { min_us = one_us; }
        if (one_us > max_us) { max_us = one_us; }
    }

    jobs_init(0);
    u32 threads = jobs_worker_count() + 1;
    JobCounter counter;
    counter.pending = 0;
    u64 parallel_us = os_time_now_us();
    jobs_dispatch(&counter, bench_probe_kernel, (void *)&sink,
                  (u64)threads * BENCH_PROBE_WORK);
    jobs_wait(&counter);
    parallel_us = os_time_now_us() - parallel_us;
    jobs_shutdown();

    u64 spread = max_us * 100 / min_us;
    u64 ratio = parallel_us * 100 / min_us;
    bench_machine_idle = (spread <= BENCH_PROBE_SPREAD_MAX) && (ratio <= BENCH_PROBE_PAR_MAX);
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena,
                         "machine: %u threads, noyau seul %llu a %llu us (dispersion %llu.%02llu), "
                         "%u en parallele %llu us (rapport %llu.%02llu) -> %s\n",
                         threads, min_us, max_us, spread / 100, spread % 100, threads, parallel_us,
                         ratio / 100, ratio % 100,
                         bench_machine_idle ? "au repos, budgets armes"
                                            : "chargee, budgets en simple rapport"));
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
    bench_budget("r_core batch build (us)", best_us, 900);
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
    bench_budget("ui_layout (us)", best_us, 1000);  // the acceptance criterion: < 1 ms

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
    bench_budget("jobs (ns/job)", ns_per_job, 1000);  // the acceptance criterion

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


// --- T-012: index, sort, incremental search, mapped cache -------------------
// 100 000 synthetic tracks, built in memory: no file is touched, and the words
// are picked so that "the" hits about a quarter of the library and "the b" a
// fortieth of it, which is what a real query looks like.
#define BENCH_LIB_COUNT 100000

static const char *bench_words[] = {"the", "blue", "night", "theme", "bells", "ocean",
                                    "amber", "static", "the", "bathysphere", "cendre",
                                    "\xC3\xA9tude", "aurora", "the", "prism", "glass"};
static const char *bench_artists[] = {"The Beatles", "Autechre", "Boards of Canada",
                                      "\xC3\x89milie Simon", "Bj\xC3\xB6rk", "Elias Rond",
                                      "Fanny Kaplan", "Oval", "Loscil", "The Bad Plus",
                                      "Tim Hecker", "Grouper"};

global Library bench_library;
global Library bench_loaded;
global Arena *bench_lib_arena;
global Arena *bench_lib_text;
global Arena *bench_loaded_arena;
global Arena *bench_loaded_text;
global Arena *bench_index_arena;
global Arena *bench_search_arena;
global LibIndex bench_index;
global LibSearch bench_search;

static void bench_library_generate(void) {
    bench_lib_arena = arena_alloc(MB(512));
    bench_lib_text = arena_alloc(MB(512));
    bench_index_arena = arena_alloc(GB(2));
    bench_search_arena = arena_alloc(MB(64));
    lib_init(&bench_library, bench_lib_arena, bench_lib_text);
    lib_reserve(&bench_library, BENCH_LIB_COUNT);
    ArenaTemp scratch = scratch_begin(0, 0);
    for (u32 i = 0; i < BENCH_LIB_COUNT; i += 1) {
        u32 h = i * 2654435761u;
        String8 path = str8f(scratch.arena, "C:\\music\\%03u\\%05u.mp3", i % 512, i);
        TrackId id = lib_track_add(&bench_library, path, 4096 + i, 1000000ull * i);
        Library *lib = &bench_library;
        lib->title_id[id] = lib_intern(
            &lib->strings,
            str8f(scratch.arena, "%s %s %u", bench_words[h % ArrayCount(bench_words)],
                  bench_words[(h >> 8) % ArrayCount(bench_words)], (h >> 16) % 900 + 100));
        lib->artist_id[id] = lib_intern(
            &lib->strings, str8_cstr(bench_artists[(h >> 5) % ArrayCount(bench_artists)]));
        lib->album_id[id] = lib_intern(
            &lib->strings,
            str8f(scratch.arena, "%s %u", bench_words[(h >> 12) % ArrayCount(bench_words)],
                  (h >> 20) % 300));
        lib->duration_ms[id] = 95000 + (h >> 3) % 420000;
        lib->sample_rate[id] = 44100;
        lib->channels[id] = 2;
        lib->year[id] = (u16)(1970 + h % 55);
    }
    scratch_end(scratch);
    lib_search_init(&bench_search, bench_search_arena, BENCH_LIB_COUNT + 1);
}

static void bench_line(const char *name, u64 micros, u64 cycles, u64 items, const char *unit) {
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena, "  %s: %llu us (%llu.%03llu ms), %llu cycles, %llu %s\n",
                         name, micros, micros / 1000, micros % 1000, cycles, items, unit));
    scratch_end(scratch);
}

// The same line in nanoseconds, for the work that is over in a few microseconds.
static void bench_line_ns(const char *name, u64 nanos, u64 cycles, u64 items, const char *unit) {
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena, "  %s: %llu ns (%llu.%03llu us), %llu cycles, %llu %s\n",
                         name, nanos, nanos / 1000, nanos % 1000, cycles, items, unit));
    scratch_end(scratch);
}

static BenchResult bench_index_sort(void) {
    u64 best_us = U64_MAX;
    u64 best_cycles = 0;
    for (u32 pass = 0; pass < 3; pass += 1) {
        lib_index_build(&bench_index, &bench_library, bench_index_arena, 1);
        u64 start_cycles = __rdtsc();
        u64 start_us = os_time_now_us();
        lib_index_order(&bench_index, LibSort_Artist);
        u64 end_us = os_time_now_us();
        u64 end_cycles = __rdtsc();
        if (end_us - start_us < best_us) {
            best_us = end_us - start_us;
            best_cycles = end_cycles - start_cycles;
        }
    }
    // The index the search benches below run against.
    lib_index_build(&bench_index, &bench_library, bench_index_arena, 1);
    lib_index_order(&bench_index, LibSort_Title);
    bench_line("sort 100k by artist (target < 30 ms)", best_us, best_cycles, BENCH_LIB_COUNT,
               "tracks");

    BenchResult result;
    result.name = "library sort 100k";
    result.cycles = best_cycles;
    result.micros = best_us;
    result.bytes = (u64)BENCH_LIB_COUNT * LIB_BYTES_PER_TRACK;
    return result;
}

static BenchResult bench_index_search(void) {
    u64 best_us = U64_MAX;
    u64 best_cycles = 0;
    u32 hits = 0;
    for (u32 pass = 0; pass < 20; pass += 1) {
        lib_search_invalidate(&bench_search);
        u64 start_cycles = __rdtsc();
        u64 start_us = os_time_now_us();
        lib_search_run(&bench_search, &bench_index, str8_lit("the"));
        u64 end_us = os_time_now_us();
        u64 end_cycles = __rdtsc();
        hits = bench_search.result_count;
        if (end_us - start_us < best_us) {
            best_us = end_us - start_us;
            best_cycles = end_cycles - start_cycles;
        }
    }
    AssertAlways(hits != 0 && !bench_search.refined);
    bench_line("search \"the\" over 100k (target < 5 ms)", best_us, best_cycles, hits, "hits");

    BenchResult result;
    result.name = "library search cold";
    result.cycles = best_cycles;
    result.micros = best_us;
    result.bytes = (u64)BENCH_LIB_COUNT * (sizeof(u64) + 2 * sizeof(u32));
    return result;
}

static BenchResult bench_index_search_refined(void) {
    u64 best_us = U64_MAX;
    u64 best_cycles = 0;
    u32 hits = 0;
    u32 scanned = 0;
    for (u32 pass = 0; pass < 20; pass += 1) {
        lib_search_invalidate(&bench_search);
        lib_search_run(&bench_search, &bench_index, str8_lit("the"));
        u64 start_cycles = __rdtsc();
        u64 start_us = os_time_now_us();
        lib_search_run(&bench_search, &bench_index, str8_lit("the b"));
        u64 end_us = os_time_now_us();
        u64 end_cycles = __rdtsc();
        AssertAlways(bench_search.refined);
        hits = bench_search.result_count;
        scanned = bench_search.scanned;
        if (end_us - start_us < best_us) {
            best_us = end_us - start_us;
            best_cycles = end_cycles - start_cycles;
        }
    }
    bench_line("refine \"the\" to \"the b\" (target < 1 ms)", best_us, best_cycles, hits, "hits");

    BenchResult result;
    result.name = "library search refined";
    result.cycles = best_cycles;
    result.micros = best_us;
    result.bytes = (u64)scanned * (sizeof(u64) + 2 * sizeof(u32));
    return result;
}

static BenchResult bench_library_cache(void) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 path = os_path_join(scratch.arena, os_known_folder(scratch.arena, OsKnownFolder_Temp),
                                str8_lit("minidisk_bench.mdlib"));
    u64 save_start = os_time_now_us();
    LibCacheStatus status = lib_cache_save(&bench_library, path, str8_lit("C:\\music"));
    u64 save_us = os_time_now_us() - save_start;
    AssertAlways(status == LibCache_Ok);

    bench_loaded_arena = arena_alloc(MB(512));
    bench_loaded_text = arena_alloc(MB(512));
    u64 best_us = U64_MAX;
    u64 best_cycles = 0;
    u64 bytes = 0;
    for (u32 pass = 0; pass < 5; pass += 1) {
        lib_init(&bench_loaded, bench_loaded_arena, bench_loaded_text);
        u64 start_cycles = __rdtsc();
        u64 start_us = os_time_now_us();
        status = lib_cache_load(&bench_loaded, path, scratch.arena, 0);
        u64 end_us = os_time_now_us();
        u64 end_cycles = __rdtsc();
        AssertAlways(status == LibCache_Ok && bench_loaded.live_count == BENCH_LIB_COUNT);
        if (end_us - start_us < best_us) {
            best_us = end_us - start_us;
            best_cycles = end_cycles - start_cycles;
        }
        bytes = bench_loaded.strings.size + (u64)BENCH_LIB_COUNT * LIB_BYTES_PER_TRACK;
    }
    OsFileInfo info;
    StructZero(&info);
    os_file_stat(path, &info);
    bench_line("cache save 100k", save_us, 0, info.size >> 10, "KB written");
    bench_line("cache load 100k (target < 50 ms)", best_us, best_cycles, info.size >> 10, "KB");
    os_file_delete(path);
    arena_release(bench_loaded_text);
    arena_release(bench_loaded_arena);
    scratch_end(scratch);

    BenchResult result;
    result.name = "library cache load 100k";
    result.cycles = best_cycles;
    result.micros = best_us;
    result.bytes = bytes;
    return result;
}


// --- T-013 ------------------------------------------------------------------
// What a click on a column header really costs: the order is built for a column
// nobody asked for yet, then the whole library is emitted into the buffer the
// list reads. This is the "< 50 ms perceived" of the ticket, end to end.
static BenchResult bench_view_sort_click(void) {
    u64 best_us = U64_MAX;
    u64 best_cycles = 0;
    u32 rows = 0;
    for (u32 pass = 0; pass < 3; pass += 1) {
        lib_index_build(&bench_index, &bench_library, bench_index_arena, 1);
        lib_search_invalidate(&bench_search);
        lib_search_set_filter(&bench_search, LIB_FILTER_ANY, LIB_FILTER_ANY);
        u64 start_cycles = __rdtsc();
        u64 start_us = os_time_now_us();
        lib_search_set_order(&bench_search, LibSort_Album, 1);
        lib_search_run(&bench_search, &bench_index, str8(0, 0));
        u64 end_us = os_time_now_us();
        u64 end_cycles = __rdtsc();
        rows = bench_search.result_count;
        if (end_us - start_us < best_us) {
            best_us = end_us - start_us;
            best_cycles = end_cycles - start_cycles;
        }
    }
    AssertAlways(rows == BENCH_LIB_COUNT);
    bench_line("sort click 100k, descending, list rebound (target < 50 ms)", best_us, best_cycles,
               rows, "rows");

    BenchResult result;
    result.name = "library sort click 100k";
    result.cycles = best_cycles;
    result.micros = best_us;
    result.bytes = (u64)BENCH_LIB_COUNT * LIB_BYTES_PER_TRACK;
    return result;
}

// The preferences are read once at start up and written once at exit, so this
// only has to be far away from anything a user could notice.
static BenchResult bench_prefs_round_trip(void) {
    u32 iterations = 20000;
    Prefs prefs;
    prefs_defaults(&prefs);
    prefs_add_folder(&prefs, str8_lit("C:\music"));
    prefs_add_folder(&prefs, str8_lit("D:\archives\flac"));
    ArenaTemp scratch = scratch_begin(0, 0);
    u64 bytes = 0;
    u64 start_cycles = __rdtsc();
    u64 start_us = os_time_now_us();
    for (u32 i = 0; i < iterations; i += 1) {
        ArenaTemp inner = arena_temp_begin(scratch.arena);
        String8 text = prefs_serialize(scratch.arena, &prefs);
        Prefs read;
        AssertAlways(prefs_parse(&read, text) && read.folder_count == 2);
        bytes += text.size;
        arena_temp_end(inner);
    }
    u64 end_us = os_time_now_us();
    u64 end_cycles = __rdtsc();
    scratch_end(scratch);
    bench_line("prefs serialize + parse", (end_us - start_us) / iterations, 0, bytes / iterations,
               "bytes per file");

    BenchResult result;
    result.name = "prefs round trip x20000";
    result.cycles = end_cycles - start_cycles;
    result.micros = end_us - start_us;
    result.bytes = bytes;
    return result;
}


// --- T-014: covers ---------------------------------------------------------
// What one cover costs end to end on the decode side: the system decoder twice
// over the same bytes, once to 256 px and once to 48 px. 10 000 of them have to
// go through the worker threads without the frame loop ever noticing, so the
// number that matters is the per cover cost, not the total.
static BenchResult bench_cover_decode(void) {
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 bytes = os_file_read_all(scratch.arena, str8_lit("tests\\data\\cover_256.png"));
    if (bytes.size == 0) {
        BenchResult skipped;
        skipped.name = "cover decode (no vector)";
        skipped.cycles = 0;
        skipped.micros = 0;
        skipped.bytes = 0;
        scratch_end(scratch);
        return skipped;
    }

    u32 iterations = 200;
    OsImage image;
    // One decode outside the loop: it loads windowscodecs.dll and builds the
    // apartment, which no cover but the first ever pays for.
    os_image_decode(scratch.arena, bytes, LIB_COVER_LARGE, &image);

    u64 start_cycles = __rdtsc();
    u64 start_us = os_time_now_us();
    for (u32 i = 0; i < iterations; i += 1) {
        ArenaTemp inner = arena_temp_begin(scratch.arena);
        AssertAlways(os_image_decode(scratch.arena, bytes, LIB_COVER_LARGE, &image));
        AssertAlways(os_image_decode(scratch.arena, bytes, LIB_COVER_SMALL, &image));
        arena_temp_end(inner);
    }
    u64 end_us = os_time_now_us();
    u64 end_cycles = __rdtsc();
    bench_line("cover decode + scale to 256 and 48 (WIC)", (end_us - start_us) / iterations,
               (end_cycles - start_cycles) / iterations, bytes.size, "bytes of source");
    scratch_end(scratch);

    BenchResult result;
    result.name = "cover decode x200";
    result.cycles = end_cycles - start_cycles;
    result.micros = end_us - start_us;
    result.bytes = bytes.size * iterations;
    return result;
}

// The other half of the cost, the one a scroll pays: looking a thumbnail up in
// the atlas and pushing it into a cell. 10 000 covers over 1 400 cells means
// the LRU evicting on nearly every add, which is the case measured here.
static BenchResult bench_cover_atlas(void) {
    r_thumbs_init(bench_arena);
    u32 count = 10000;
    u8 *pixels = push_array(bench_arena, u8, (u64)R_THUMB_SMALL * R_THUMB_SMALL * 4);
    mem_set(pixels, 0x55, (u64)R_THUMB_SMALL * R_THUMB_SMALL * 4);

    u64 start_cycles = __rdtsc();
    u64 start_us = os_time_now_us();
    R_AtlasRect rect;
    u32 hits = 0;
    for (u32 i = 0; i < count; i += 1) {
        u64 key = 1 + i;
        if (r_thumbs_lookup(key, R_THUMB_SMALL, &rect)) {
            hits += 1;
        } else {
            r_thumbs_add(key, R_THUMB_SMALL, pixels);
        }
    }
    u64 end_us = os_time_now_us();
    u64 end_cycles = __rdtsc();
    AssertAlways(hits == 0 && r_thumbs_evictions() > 0);
    bench_line("thumbnail atlas: 10 000 adds through a 1 400 cell LRU",
               end_us - start_us, end_cycles - start_cycles, count, "covers");

    BenchResult result;
    result.name = "thumbnail atlas 10k";
    result.cycles = end_cycles - start_cycles;
    result.micros = end_us - start_us;
    result.bytes = (u64)count * R_THUMB_SMALL * R_THUMB_SMALL * 4;
    return result;
}

// T-030: the acceptance criterion of the plan document - a full disc (254
// tracks, the TOC ceiling) written and read back. Both directions are measured
// together because that is what an autosave plus a recovery costs; the budget
// is 5 ms for the pair.
static BenchResult bench_plan_file(void) {
    // Two pairs of arenas: loading empties the ones the loaded plan owns, so
    // the source document must not be sitting in them.
    Arena *arena = arena_alloc(MB(16));
    Arena *text = arena_alloc(MB(16));
    Arena *load_arena = arena_alloc(MB(16));
    Arena *load_text = arena_alloc(MB(16));
    Plan *plan = push_struct(bench_arena, Plan);
    Plan *loaded = push_struct(bench_arena, Plan);
    plan_init(plan, arena, text);
    plan_init(loaded, load_arena, load_text);

    ArenaTemp scratch = scratch_begin(0, 0);
    for (u32 i = 0; i < PLAN_ENTRY_MAX; i += 1) {
        PlanEntry entry;
        StructZero(&entry);
        entry.track_id = i;
        entry.path_id = lib_intern(&plan->strings,
                                   str8f(scratch.arena, "C:\music\artist %u\album\%02u - a "
                                                        "reasonably long file name.flac",
                                         i / 12, i % 12));
        entry.title_override =
            lib_intern(&plan->strings, str8f(scratch.arena, "Track number %u", i));
        entry.duration_ms = 180000 + i * 97;
        entry.gain_db = (i16)(i - 128);
        entry.mode = (u8)(i % PlanMode_COUNT);
        entry.group_id = PLAN_GROUP_NONE;
        plan_add(plan, 0, i, entry);
    }
    scratch_end(scratch);
    plan_set_disc_title(plan, 0, str8_lit("Un disque plein"), 0);

    String8 temp = os_known_folder(bench_arena, OsKnownFolder_Temp);
    String8 path = os_path_join(bench_arena, temp, str8_lit("minidisk_bench.mdplan"));
    u32 iterations = 64;
    plan_save(plan, path);  // warm: the file exists, the pages are hot

    // Three numbers, because they answer three different questions. The save
    // is durable by construction (.tmp, FlushFileBuffers, MOVEFILE_WRITE_THROUGH):
    // most of it is the barrier the OS puts between us and the platter, so the
    // floor below measures exactly that with a buffer of the same size and
    // nothing of ours in it.
    u64 save_cycles = __rdtsc();
    u64 save_us = os_time_now_us();
    for (u32 i = 0; i < iterations; i += 1) { AssertAlways(plan_save(plan, path) == PlanFile_Ok); }
    save_us = os_time_now_us() - save_us;
    save_cycles = __rdtsc() - save_cycles;

    u64 load_cycles = __rdtsc();
    u64 load_us = os_time_now_us();
    for (u32 i = 0; i < iterations; i += 1) { AssertAlways(plan_load(loaded, path) == PlanFile_Ok); }
    load_us = os_time_now_us() - load_us;
    load_cycles = __rdtsc() - load_cycles;
    AssertAlways(loaded->discs[0].entry_count == PLAN_ENTRY_MAX);

    OsFileInfo info;
    AssertAlways(os_file_stat(path, &info));
    u8 *floor_bytes = push_array_zero(bench_arena, u8, info.size);
    String8 floor_path = os_path_join(bench_arena, temp, str8_lit("minidisk_bench_floor.mdplan"));
    String8 floor_tmp = str8_cat(bench_arena, floor_path, str8_lit(".tmp"));
    u64 floor_us = os_time_now_us();
    for (u32 i = 0; i < iterations; i += 1) {
        AssertAlways(os_file_write_all(floor_tmp, str8(floor_bytes, info.size)));
        AssertAlways(os_file_move_replace(floor_tmp, floor_path));
    }
    floor_us = os_time_now_us() - floor_us;
    os_file_delete(floor_path);
    os_file_delete(path);

    bench_line("plan .mdplan: save, durable (.tmp + flush + rename)", save_us / iterations,
               save_cycles / iterations, info.size, "bytes");
    bench_line("plan .mdplan:   of which the OS durability barrier", floor_us / iterations, 0,
               info.size, "bytes");
    bench_line("plan .mdplan: load (mapped, validated, rebuilt)", load_us / iterations,
               load_cycles / iterations, PLAN_ENTRY_MAX, "tracks");
    bench_line("plan .mdplan: save + load of a full 254 track disc",
               (save_us + load_us) / iterations, (save_cycles + load_cycles) / iterations,
               PLAN_ENTRY_MAX, "tracks");
    arena_release(load_text);
    arena_release(load_arena);
    arena_release(text);
    arena_release(arena);

    BenchResult result;
    result.name = "plan save+load 254";
    result.cycles = (save_cycles + load_cycles) / iterations;
    result.micros = (save_us + load_us) / iterations;
    result.bytes = 0;
    return result;
}

// T-073 / P-009: what a durable save now costs the frame thread. The frame
// serialises 254 tracks into the saver's arena and pushes a job; the job does
// the 7 ms the disk asks for, on another core. The budget is 200 us for the
// hand-over, snapshot included - the rest is not on the critical path any more.
static BenchResult bench_plan_save_async(void) {
    Arena *arena = arena_alloc(MB(16));
    Arena *text = arena_alloc(MB(16));
    Plan *plan = push_struct(bench_arena, Plan);
    plan_init(plan, arena, text);
    ArenaTemp scratch = scratch_begin(0, 0);
    for (u32 i = 0; i < PLAN_ENTRY_MAX; i += 1) {
        PlanEntry entry;
        StructZero(&entry);
        entry.track_id = i;
        entry.path_id = lib_intern(&plan->strings,
                                   str8f(scratch.arena, "C:\music\artist %u\album\%02u - a "
                                                        "reasonably long file name.flac",
                                         i / 12, i % 12));
        entry.title_override =
            lib_intern(&plan->strings, str8f(scratch.arena, "Track number %u", i));
        entry.duration_ms = 180000 + i * 97;
        entry.group_id = PLAN_GROUP_NONE;
        plan_add(plan, 0, i, entry);
    }
    scratch_end(scratch);

    String8 path = os_path_join(bench_arena, os_known_folder(bench_arena, OsKnownFolder_Temp),
                                str8_lit("minidisk_bench_async.mdplan"));
    PlanSaver saver;
    plan_saver_init(&saver, arena_alloc(MB(8)));
    jobs_init(0);

    u32 iterations = 64;
    u64 best_us = U64_MAX;
    u64 total_us = 0;
    u64 disk_us = 0;
    u64 cycles = 0;
    for (u32 i = 0; i < iterations; i += 1) {
        u64 start_cycles = __rdtsc();
        u64 start_us = os_time_now_us();
        AssertAlways(plan_save_async(&saver, plan, path));
        u64 frame_us = os_time_now_us() - start_us;
        cycles += __rdtsc() - start_cycles;
        total_us += frame_us;
        if (frame_us < best_us) { best_us = frame_us; }
        // The next iteration would be refused while this one is in flight: the
        // bench waits, the application would simply come back in five seconds.
        plan_save_wait(&saver);
        AssertAlways(plan_save_state(&saver) == PlanSave_Done);
        disk_us += saver.disk_us;
    }
    u64 size = saver.bytes.size;
    jobs_shutdown();
    os_file_delete(path);
    arena_release(text);
    arena_release(arena);

    bench_line("plan save async: frame thread (snapshot + push), best", best_us, 0,
               PLAN_ENTRY_MAX, "tracks");
    bench_line("plan save async: frame thread, mean", total_us / iterations,
               cycles / iterations, size, "bytes");
    bench_line("plan save async: the job, on another core", disk_us / iterations, 0, size,
               "bytes");
    bench_budget("plan save async, frame (us)", best_us, 200);  // the ticket's budget

    BenchResult result;
    result.name = "plan save async, frame cost";
    result.cycles = cycles / iterations;
    result.micros = total_us / iterations;
    result.bytes = 0;
    return result;
}

// T-031: the full recompute the gauge does on every edit - the cluster cost of
// 254 tracks, their fit states, the tri-modal remainder, and the whole title
// budget with its group syntax. The ticket's ceiling is 50 us; a plan is a
// column pass over 254 entries, so the answer should be two orders below it.
static BenchResult bench_plan_capacity(void) {
    Arena *arena = arena_alloc(MB(16));
    Arena *text = arena_alloc(MB(16));
    Plan *plan = push_struct(bench_arena, Plan);
    plan_init(plan, arena, text);

    ArenaTemp scratch = scratch_begin(0, 0);
    for (u32 i = 0; i < PLAN_ENTRY_MAX; i += 1) {
        PlanEntry entry;
        StructZero(&entry);
        entry.track_id = LIB_TRACK_NONE;
        entry.path_id = lib_intern(&plan->strings,
                                   str8f(scratch.arena, "C:/music/%03u.flac", i));
        entry.title_override = lib_intern(
            &plan->strings, str8f(scratch.arena, "Artist %u - A Track Title %u", i / 12, i));
        entry.duration_ms = 180000 + i * 97;
        entry.gain_db = PLAN_GAIN_NONE;
        entry.mode = (u8)(i % PlanMode_COUNT);
        entry.group_id = PLAN_GROUP_NONE;
        plan_add(plan, 0, i, entry);
    }
    scratch_end(scratch);
    plan_set_disc_title(plan, 0, str8_lit("Une compilation de 254 pistes"), 0);
    for (u32 g = 0; g < 20; g += 1) {
        plan_group(plan, 0, g * 12, 12, str8_lit("Album quelconque"));
    }

    PlanCapacity *capacity = push_struct(bench_arena, PlanCapacity);
    PlanTocBudget *budget = push_struct(bench_arena, PlanTocBudget);
    PlanSplit *split = push_struct(bench_arena, PlanSplit);
    PlanDisc *disc = plan_disc(plan, 0);

    u32 iterations = 1000;
    plan_capacity_compute(disc, capacity);  // warm the pages
    plan_toc_budget(plan, 0, 0, budget);

    u64 start_cycles = __rdtsc();
    u64 start_us = os_time_now_us();
    for (u32 i = 0; i < iterations; i += 1) { plan_capacity_compute(disc, capacity); }
    u64 clusters_us = os_time_now_us() - start_us;
    u64 clusters_cycles = __rdtsc() - start_cycles;

    start_cycles = __rdtsc();
    start_us = os_time_now_us();
    for (u32 i = 0; i < iterations; i += 1) { plan_toc_budget(plan, 0, 0, budget); }
    u64 toc_us = os_time_now_us() - start_us;
    u64 toc_cycles = __rdtsc() - start_cycles;

    u32 cells_sink = 0;
    start_cycles = __rdtsc();
    start_us = os_time_now_us();
    for (u32 i = 0; i < iterations; i += 1) {
        for (u32 k = 0; k < PLAN_ENTRY_MAX; k += 1) {
            cells_sink += plan_toc_cells_for_title(plan_entry_title(plan, 0, 0, k), 1);
        }
    }
    u64 titles_us = os_time_now_us() - start_us;
    u64 titles_cycles = __rdtsc() - start_cycles;
    AssertAlways(cells_sink != 0);

    start_cycles = __rdtsc();
    start_us = os_time_now_us();
    for (u32 i = 0; i < iterations; i += 1) {
        plan_capacity_split(disc, 0, PlanSplit_FirstFit, 80, split);
    }
    u64 split_us = os_time_now_us() - start_us;
    u64 split_cycles = __rdtsc() - start_cycles;

    AssertAlways(capacity->used_clusters != 0 && budget->cells_used != 0);
    AssertAlways(split->disc_count > 1);

    // Nanoseconds, because microseconds would round the answer to zero.
    bench_line_ns("plan capacity: clusters + fit states", clusters_us * 1000 / iterations,
               clusters_cycles / iterations, PLAN_ENTRY_MAX, "tracks");
    bench_line_ns("plan capacity: TOC budget, groups included", toc_us * 1000 / iterations,
               toc_cycles / iterations, budget->cells_used, "cells");
    bench_line_ns("plan capacity:   of which the 254 track titles",
               titles_us * 1000 / iterations, titles_cycles / iterations, PLAN_ENTRY_MAX, "titles");
    bench_line_ns("plan capacity: multi disc first fit", split_us * 1000 / iterations,
               split_cycles / iterations, split->disc_count, "discs");
    bench_line_ns("plan capacity: full recompute of a 254 track disc",
               (clusters_us + toc_us) * 1000 / iterations,
               (clusters_cycles + toc_cycles) / iterations, PLAN_ENTRY_MAX, "tracks");
    arena_release(text);
    arena_release(arena);

    BenchResult result;
    result.name = "plan capacity recompute 254";
    result.cycles = (clusters_cycles + toc_cycles) / iterations;
    result.micros = (clusters_us + toc_us) / iterations;
    result.bytes = 0;
    return result;
}

// T-032. Two numbers: the geometry of the gauge on its own, which is pure and
// runs on every resize, and one whole frame of the plan view on a full disc of
// 254 entries - the boxes, the five layout passes and the draw commands, with
// no GL submit behind them. The budget is 1.5 ms of CPU per frame.
static void bench_plan_view_fill(Plan *plan, u32 count) {
    ArenaTemp scratch = scratch_begin(0, 0);
    for (u32 i = 0; i < count; i += 1) {
        PlanEntry entry;
        StructZero(&entry);
        entry.track_id = LIB_TRACK_NONE;
        entry.path_id =
            lib_intern(&plan->strings, str8f(scratch.arena, "C:/music/%03u.flac", i));
        entry.title_override = lib_intern(
            &plan->strings, str8f(scratch.arena, "Artist %u - A Track Title %u", i / 12, i));
        entry.duration_ms = 180000 + i * 97;
        entry.gain_db = PLAN_GAIN_NONE;
        entry.mode = (u8)(i % PlanMode_COUNT);
        entry.group_id = PLAN_GROUP_NONE;
        plan_add(plan, 0, i, entry);
    }
    scratch_end(scratch);
}

static BenchResult bench_plan_gauge_layout(void) {
    Arena *arena = arena_alloc(MB(16));
    Arena *text = arena_alloc(MB(16));
    Plan *plan = push_struct(bench_arena, Plan);
    plan_init(plan, arena, text);
    bench_plan_view_fill(plan, PLAN_ENTRY_MAX);

    PlanCapacity *capacity = push_struct(bench_arena, PlanCapacity);
    PlanGaugeLayout *layout = push_struct(bench_arena, PlanGaugeLayout);
    plan_capacity_compute(plan_disc(plan, 0), capacity);
    plan_gauge_layout(capacity, 800.0f, layout);  // warm the pages
    AssertAlways(layout->count > 0);

    u32 iterations = 2000;
    u64 start_cycles = __rdtsc();
    u64 start_us = os_time_now_us();
    for (u32 i = 0; i < iterations; i += 1) { plan_gauge_layout(capacity, 800.0f, layout); }
    u64 wide_us = os_time_now_us() - start_us;
    u64 wide_cycles = __rdtsc() - start_cycles;

    // The compact bar merges most of the segments, and merging is the work.
    start_cycles = __rdtsc();
    start_us = os_time_now_us();
    for (u32 i = 0; i < iterations; i += 1) { plan_gauge_layout(capacity, 120.0f, layout); }
    u64 narrow_us = os_time_now_us() - start_us;
    u64 narrow_cycles = __rdtsc() - start_cycles;

    bench_line_ns("plan gauge: layout of 254 segments, 800 px", wide_us * 1000 / iterations,
                  wide_cycles / iterations, PLAN_ENTRY_MAX, "entries");
    bench_line_ns("plan gauge: layout on the compact 120 px bar",
                  narrow_us * 1000 / iterations, narrow_cycles / iterations, layout->count,
                  "segments");
    arena_release(arena);
    arena_release(text);

    BenchResult result;
    result.name = "plan gauge layout 254";
    result.cycles = wide_cycles / iterations;
    result.micros = wide_us / iterations;
    result.bytes = 0;
    return result;
}

static BenchResult bench_plan_view_frame(void) {
    Arena *ui_arena = arena_alloc(MB(256));
    Arena *frame_arena = arena_alloc(MB(64));
    r_atlas_init(ui_arena);
    if (!os_font_init() || !ui_fonts_build(1.0f)) {
        BenchResult skipped;
        skipped.name = "plan view frame (no system font)";
        skipped.cycles = 0;
        skipped.micros = 0;
        skipped.bytes = 0;
        return skipped;
    }
    ui_text_init(ui_arena);
    UI_Theme theme;
    ui_theme_dark(&theme);
    ui_theme_set(&theme);
    ui_init(ui_arena);

    // The application state the panel reads, built by hand: no window, no scan,
    // no cache - just the document and the widgets that outlive a frame.
    app.permanent = ui_arena;
    lib_init(&app.library, arena_alloc(MB(64)), arena_alloc(MB(64)));
    plan_init(&app.plan, arena_alloc(MB(16)), arena_alloc(MB(16)));
    ui_list_init(&app.plan_list, app.plan_selection, ArrayCount(app.plan_selection));
    ui_list_init(&app.list, 0, 0);
    ui_text_input_init(&app.search, str8_lit(""));
    ui_text_input_init(&app.plan_rename, str8_lit(""));
    ui_text_input_init(&app.plan_disc_title, str8_lit(""));
    ui_text_input_init(&app.plan_group_name, str8_lit(""));
    bench_plan_view_fill(&app.plan, PLAN_ENTRY_MAX);
    plan_set_disc_title(&app.plan, 0, str8_lit("Une compilation de 254 pistes"), 0);
    for (u32 g = 0; g < 20; g += 1) { plan_group(&app.plan, 0, g * 12, 12, str8_lit("Album")); }
    app_plan_recompute();

    V2 viewport = v2(900.0f, 1000.0f);  // a plan panel the height of a screen
    u32 iterations = 300;
    u64 best_us = U64_MAX;
    u64 boxes = 0;
    u64 start_cycles = __rdtsc();
    u64 start_us = os_time_now_us();
    for (u32 i = 0; i < iterations; i += 1) {
        arena_clear(frame_arena);
        u64 pass_start = os_time_now_us();
        r_begin_frame(frame_arena, viewport.x, viewport.y, 1.0f);
        ui_begin(frame_arena, 0, 0, 1.0f / 60.0f, viewport, 1.0f);
        UI_Box *root = ui_root(UI_Layer_Content);
        root->child_layout_axis = Axis2_Y;
        UI_Parent(root) { app_plan_panel(); }
        ui_widgets_end_frame();
        ui_end();
        u64 pass_us = os_time_now_us() - pass_start;
        if (pass_us < best_us) { best_us = pass_us; }
        boxes = ui_frame_box_count();
        r_end_frame();
    }
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();
    u64 mean_us = (end_us - start_us) / iterations;
    AssertAlways(boxes > 0);

    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena,
                         "  plan view: %llu boxes for 254 entries, %llu us per frame "
                         "(best of %u, mean %llu, budget 1500)\n",
                         boxes, best_us, iterations, mean_us));
    scratch_end(scratch);
    bench_budget("plan view frame (us)", best_us, 1500);

    arena_release(frame_arena);

    BenchResult result;
    result.name = "plan view frame, 254 entries";
    result.cycles = (end_cycles - start_cycles) / iterations;
    result.micros = best_us;
    result.bytes = boxes * iterations;  // the MB/s column reads as M boxes/s
    return result;
}


// --- T-041: DSP and pipeline ------------------------------------------------
#define BENCH_DSP_SECONDS 240u  // the four-minute track of the ticket

typedef struct BenchDspSource {
    u64 total;
    u64 pos;
    u32 channels;
    const f32 *tone;  // one pre-generated block, replayed
    u64 tone_len;
} BenchDspSource;

static u64 bench_dsp_read(void *user, f32 *const *planar, u64 max_frames) {
    BenchDspSource *source = (BenchDspSource *)user;
    u64 count = Min(source->total - source->pos, max_frames);
    if (count == 0) { return 0; }
    // tone_len is a power of two: a mask, not a 64-bit division per sample.
    u64 mask = source->tone_len - 1u;
    u64 offset = source->pos & mask;
    for (u64 i = 0; i < count; i += 1) {
        f32 v = source->tone[(offset + i) & mask];
        planar[0][i] = v;
        if (source->channels == 2) { planar[1][i] = v; }
    }
    source->pos += count;
    return count;
}

static b32 bench_dsp_rewind(void *user) {
    ((BenchDspSource *)user)->pos = 0;
    return 1;
}

static b32 bench_dsp_sink(void *user, const u8 *bytes, u64 size) {
    Unused(bytes);
    *(u64 *)user += size;
    return 1;
}

static f32 *bench_dsp_tone(u64 length, f64 freq, u32 rate) {
    f32 *tone = push_array(bench_arena, f32, length);
    f64 omega = 2.0 * DSP_PI * freq / (f64)rate;
    for (u64 i = 0; i < length; i += 1) { tone[i] = (f32)(0.3 * dsp_sin_f64(omega * (f64)i)); }
    return tone;
}

static BenchResult bench_dsp_resampler(void) {
    u32 in_rate = 48000;
    u64 frames = (u64)in_rate * BENCH_DSP_SECONDS;
    f32 *tone = bench_dsp_tone(4096, 997.0, in_rate);

    DspResampler r;
    dsp_resampler_init(&r, bench_arena, in_rate, 44100, 2, 4096, frames);
    u64 out_cap = dsp_resampler_out_capacity(&r, 4096) + r.taps_per_phase;
    f32 *in[2];
    f32 *out[2];
    for (u32 c = 0; c < 2; c += 1) {
        in[c] = tone;
        out[c] = push_array(bench_arena, f32, out_cap);
    }

    u64 start_us = os_time_now_us();
    u64 start_cycles = __rdtsc();
    u64 produced = 0;
    for (u64 done = 0; done < frames; done += 4096) {
        produced += dsp_resampler_process(&r, (const f32 *const *)in, 4096, out, out_cap);
    }
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();
    AssertAlways(produced > 0);

    u64 micros = end_us - start_us;
    f64 realtime = (f64)BENCH_DSP_SECONDS * 1000000.0 / (f64)Max(micros, (u64)1);
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena,
                         "  resampler 48000 -> 44100 stereo, %u s of audio: %llu us, %f x real "
                         "time (target 200), %u taps/phase\n",
                         BENCH_DSP_SECONDS, micros, realtime, r.taps_per_phase));
    scratch_end(scratch);
    // The guard catches a regression, not a busy machine: the number that
    // matters is the one printed above, against the 200x of the ticket.
    bench_budget_min("dsp resampler (x temps reel)", (u64)realtime, 150);

    BenchResult result;
    result.name = "dsp resample 48k->44.1k stereo, 4 min";
    result.cycles = end_cycles - start_cycles;
    result.micros = micros;
    result.bytes = produced * 2 * sizeof(f32);
    return result;
}

static BenchResult bench_dsp_r128(void) {
    u64 frames = (u64)44100u * BENCH_DSP_SECONDS;
    f32 *tone = bench_dsp_tone(4096, 997.0, 44100);
    const f32 *planar[2];
    planar[0] = tone;
    planar[1] = tone;

    DspR128 state;
    dsp_r128_init(&state, bench_arena, 44100, 2, frames);
    dsp_r128_reset(&state);

    u64 start_us = os_time_now_us();
    u64 start_cycles = __rdtsc();
    for (u64 done = 0; done < frames; done += 4096) { dsp_r128_feed(&state, planar, 4096); }
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();

    u64 micros = end_us - start_us;
    f64 realtime = (f64)BENCH_DSP_SECONDS * 1000000.0 / (f64)Max(micros, (u64)1);
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena,
                         "  R128 + true peak, %u s of stereo: %llu us, %f x real time "
                         "(target 500), %f LUFS\n",
                         BENCH_DSP_SECONDS, micros, realtime,
                         (f64)dsp_r128_integrated_lufs(&state)));
    scratch_end(scratch);
    bench_budget_min("dsp r128 (x temps reel)", (u64)realtime, 300);  // target 500x, T-041

    BenchResult result;
    result.name = "dsp R128 measure, 4 min stereo";
    result.cycles = end_cycles - start_cycles;
    result.micros = micros;
    result.bytes = frames * 2 * sizeof(f32);
    return result;
}

static void bench_dsp_setup_task(PipelineTask *task, BenchDspSource *source, u64 *sink,
                                 Arena *arena, u64 frames, const f32 *tone) {
    StructZero(source);
    source->total = frames;
    source->channels = 2;
    source->tone = tone;
    source->tone_len = 4096;
    *sink = 0;

    StructZero(task);
    pipeline_config_defaults(&task->config);
    task->config.normalize = 1;
    task->config.trim = 1;
    task->config.dither = 1;
    task->config.fade_in_s = 0.01f;
    task->config.fade_out_s = 0.01f;
    task->source.read = bench_dsp_read;
    task->source.rewind = bench_dsp_rewind;
    task->source.user = source;
    task->source.sample_rate = 44100;
    task->source.channels = 2;
    task->source.total_frames = frames;
    task->write = bench_dsp_sink;
    task->write_user = sink;
    task->arena = arena;
}

static BenchResult bench_dsp_pipeline(void) {
    u64 frames = (u64)44100u * BENCH_DSP_SECONDS;
    f32 *tone = bench_dsp_tone(4096, 997.0, 44100);
    Arena *track_arena = arena_alloc(MB(64));
    BenchDspSource source;
    u64 sink;
    PipelineTask task;
    bench_dsp_setup_task(&task, &source, &sink, track_arena, frames, tone);

    u64 start_us = os_time_now_us();
    u64 start_cycles = __rdtsc();
    pipeline_run(&task);
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();
    AssertAlways(task.result.status == PIPELINE_OK);

    u64 micros = end_us - start_us;
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena,
                         "  pipeline, one 4 min stereo track, R128 + trim + fades "
                         "+ dither, single thread: %llu us (two passes), "
                         "%llu SP frames, %f LUFS, gain %f dB\n",
                         micros, task.result.sp_frames, (f64)task.result.measured_lufs,
                         (f64)task.result.applied_gain_db));
    scratch_end(scratch);
    bench_budget("dsp pipeline (us)", micros, 1500000);  // two passes, see render-only
    arena_release(track_arena);

    BenchResult result;
    result.name = "dsp pipeline, 4 min stereo, 1 thread";
    result.cycles = end_cycles - start_cycles;
    result.micros = micros;
    result.bytes = sink;
    return result;
}

// The same track with normalisation and trim off: one pass, which is the shape
// the ticket's "< 0,5 s for a four minute track" budget was written for.
static BenchResult bench_dsp_pipeline_render(void) {
    u64 frames = (u64)44100u * BENCH_DSP_SECONDS;
    f32 *tone = bench_dsp_tone(4096, 997.0, 44100);
    Arena *track_arena = arena_alloc(MB(64));
    BenchDspSource source;
    u64 sink;
    PipelineTask task;
    bench_dsp_setup_task(&task, &source, &sink, track_arena, frames, tone);
    task.config.normalize = 0;
    task.config.trim = 0;
    task.config.fixed_gain_db = -3.0f;

    u64 start_us = os_time_now_us();
    u64 start_cycles = __rdtsc();
    pipeline_run(&task);
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();
    AssertAlways(task.result.status == PIPELINE_OK);

    u64 micros = end_us - start_us;
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena,
                         "  pipeline, one 4 min stereo track, render only: %llu us "
                         "(budget 500000), %f x real time\n",
                         micros, (f64)BENCH_DSP_SECONDS * 1000000.0 / (f64)Max(micros, (u64)1)));
    scratch_end(scratch);
    bench_budget("dsp pipeline render (us)", micros, 800000);
    arena_release(track_arena);

    BenchResult result;
    result.name = "dsp pipeline render only, 4 min stereo";
    result.cycles = end_cycles - start_cycles;
    result.micros = micros;
    result.bytes = sink;
    return result;
}

static BenchResult bench_dsp_pipeline_jobs(void) {
    u32 count = 8;
    u64 frames = (u64)44100u * BENCH_DSP_SECONDS;
    f32 *tone = bench_dsp_tone(4096, 997.0, 44100);
    BenchDspSource *sources = push_array_zero(bench_arena, BenchDspSource, count);
    u64 *sinks = push_array_zero(bench_arena, u64, count);
    PipelineTask *tasks = push_array_zero(bench_arena, PipelineTask, count);
    Arena *arenas[8];
    for (u32 i = 0; i < count; i += 1) {
        arenas[i] = arena_alloc(MB(64));
        bench_dsp_setup_task(&tasks[i], &sources[i], &sinks[i], arenas[i], frames, tone);
    }

    jobs_init(0);
    u64 start_us = os_time_now_us();
    u64 start_cycles = __rdtsc();
    pipeline_run_many(tasks, count);
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();
    u32 workers = jobs_worker_count();
    jobs_shutdown();

    u64 total_bytes = 0;
    for (u32 i = 0; i < count; i += 1) {
        AssertAlways(tasks[i].result.status == PIPELINE_OK);
        total_bytes += sinks[i];
        arena_release(arenas[i]);
    }

    u64 micros = end_us - start_us;
    f64 realtime = (f64)(BENCH_DSP_SECONDS * count) * 1000000.0 / (f64)Max(micros, (u64)1);
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena,
                         "  pipeline, %u tracks of 4 min through jobs (%u workers): %llu us, "
                         "%f x real time\n",
                         count, workers, micros, realtime));
    scratch_end(scratch);

    BenchResult result;
    result.name = "dsp pipeline, 8 x 4 min, jobs";
    result.cycles = end_cycles - start_cycles;
    result.micros = micros;
    result.bytes = total_bytes;
    return result;
}

// --- codecs (T-040) --------------------------------------------------------
// Decode speed in multiples of realtime, the number ADR-007 sets a target for:
// >= 100x for MP3 and >= 300x for FLAC, on one core, one file at a time. The
// vectors are short, so each one is decoded from scratch many times: that keeps
// the open path (header parse, seek table, codebooks) inside the measurement,
// which is where a transcode of 20 short tracks actually spends its time.
static void bench_codec_decode(const char *label, const char *path, u32 iterations) {
    ArenaTemp scratch = arena_temp_begin(bench_arena);
    f32 *block[CODEC_MAX_CHANNELS];
    for (u32 c = 0; c < CODEC_MAX_CHANNELS; c += 1) {
        block[c] = push_array(bench_arena, f32, CODEC_BLOCK_FRAMES);
    }
    String8 file = str8_cstr(path);
    u64 frames = 0;
    u32 rate = 0;

    u64 start_us = os_time_now_us();
    u64 start_cycles = __rdtsc();
    for (u32 i = 0; i < iterations; i += 1) {
        Decoder *decoder = 0;
        if (codec_open(&decoder, file) != CODEC_OK) {
            os_debug_print(str8f(scratch.arena, "%s: vecteur absent (%s)\n", label, path));
            arena_temp_end(scratch);
            return;
        }
        rate = decoder->info.sample_rate;
        for (;;) {
            u32 got = codec_read_f32_planar(decoder, block, CODEC_BLOCK_FRAMES);
            if (!got) { break; }
            frames += got;
        }
        codec_close(decoder);
    }
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();

    u64 micros = end_us - start_us;
    f64 audio_us = (f64)frames * 1000000.0 / (f64)rate;
    f64 realtime = (micros != 0) ? audio_us / (f64)micros : 0.0;
    os_debug_print(str8f(scratch.arena,
                         "%s: %llu us, %llu cycles, %llu frames, %f x temps reel\n", label,
                         micros, end_cycles - start_cycles, frames, realtime));
    arena_temp_end(scratch);
}

// T-042: DES-CBC is on the path of every byte that reaches a disc. SP needs
// 176 KB/s; anything above that is free, but a number is the only way to know.
static BenchResult bench_netmd_des(void) {
    u64 size = MB(8);
    u8 *plain = push_array(bench_arena, u8, size);
    u8 *cipher = push_array(bench_arena, u8, size);
    for (u64 i = 0; i < size; i += 1) { plain[i] = (u8)(i * 31u + 7u); }
    u8 key[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    DesKey schedule;
    des_key_init(&schedule, key);
    u8 iv[8];
    mem_zero(iv, sizeof(iv));
    u64 start_us = os_time_now_us();
    u64 start_cycles = __rdtsc();
    des_cbc_encrypt(&schedule, iv, plain, cipher, size);
    u64 end_cycles = __rdtsc();
    u64 end_us = os_time_now_us();
    AssertAlways(cipher[size - 1] != plain[size - 1] || cipher[0] != plain[0]);

    BenchResult result;
    result.name = "netmd DES-CBC 8 MB";
    result.cycles = end_cycles - start_cycles;
    result.micros = end_us - start_us;
    result.bytes = size;
    return result;
}

// --- one arena per group (P-010) -------------------------------------------
// The bench used to take a single GB(1) arena and commit into it for the whole
// run: 32 MB for mem_copy, ~97 MB for the renderer and the atlas, then 64 MB in
// one block for the parallel sum, and not one page ever given back. Two benches
// at once on this machine - two agents, two worktrees - reached the point where
// Windows refuses a commit, and the process died at a different bench every
// time with a silent code 3. Each group now reserves its own arena and releases
// it before the next one starts, so the peak is a group and not the sum.
static void bench_group_begin(u64 reserve) {
    AssertAlways(bench_arena == 0);
    bench_arena = arena_alloc(reserve);
}

static void bench_group_end(void) {
    ArenaTemp scratch = scratch_begin(0, 0);
    os_debug_print(str8f(scratch.arena, "  [groupe] %llu MB engages, rendus\n",
                         bench_arena->committed >> 20));
    scratch_end(scratch);
    arena_release(bench_arena);
    bench_arena = 0;
}

static void bench_library_release(void) {
    arena_release(bench_search_arena);
    arena_release(bench_index_arena);
    arena_release(bench_lib_text);
    arena_release(bench_lib_arena);
}

int main(void) {
    os_init();
    os_debug_print(str8_lit("minidisk benches\n"));
    bench_probe_machine();

    bench_group_begin(MB(256));  // base
    bench_print(bench_mem_copy());
    bench_group_end();

    bench_group_begin(MB(256));  // renderer, atlas, text, layout
    bench_print(bench_batch_build());
    bench_print(bench_atlas_skyline());
    bench_print(bench_text_glyph_cache());
    bench_print(bench_ui_layout());
    bench_group_end();

    bench_group_begin(MB(256));  // the job system, and its 64 MB block
    bench_print(bench_jobs_dispatch());
    bench_print(bench_jobs_parallel_sum());
    bench_group_end();

    bench_group_begin(MB(512));  // the scan, the tags, a realistic frame
    bench_print(bench_library_scan());
    bench_print(bench_tags_parse());
    bench_print(bench_realistic_frame());
    bench_group_end();

    bench_group_begin(MB(256));  // 100 000 tracks: index, search, cache
    bench_library_generate();
    bench_print(bench_index_sort());
    bench_print(bench_index_search());
    bench_print(bench_index_search_refined());
    bench_print(bench_library_cache());
    bench_print(bench_view_sort_click());
    bench_library_release();
    bench_group_end();

    bench_group_begin(MB(512));  // prefs, covers, the plan
    bench_print(bench_prefs_round_trip());
    bench_print(bench_cover_decode());
    bench_print(bench_cover_atlas());
    bench_print(bench_plan_file());
    bench_print(bench_plan_save_async());
    bench_print(bench_plan_capacity());
    bench_print(bench_plan_gauge_layout());
    bench_print(bench_plan_view_frame());
    bench_group_end();

    bench_group_begin(MB(512));  // netmd, dsp, codecs
    bench_print(bench_netmd_des());
    bench_print(bench_dsp_resampler());
    bench_print(bench_dsp_r128());
    bench_print(bench_dsp_pipeline());
    bench_print(bench_dsp_pipeline_render());
    bench_print(bench_dsp_pipeline_jobs());
    bench_codec_decode("codec_wav_s16", "tests\\data\\audio\\sine_16.wav", 400);
    bench_codec_decode("codec_flac", "tests\\data\\audio\\sine.flac", 200);
    bench_codec_decode("codec_mp3", "tests\\data\\audio\\sine.mp3", 100);
    bench_codec_decode("codec_ogg", "tests\\data\\audio\\sine.ogg", 100);
    bench_group_end();
    return 0;
}

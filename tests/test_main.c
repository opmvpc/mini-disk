// test_main.c - test runner: TEST()/EXPECT(), counters, non zero exit code on failure.
// Unity build like src/main.c, but with the CRT and ASan (ADR-012).
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
#include "../src/core/plan/plan_file.h"
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
#include "../src/ui/ui_widgets.h"
#include "../src/ui/ui_debug_overlay.h"
#include "../src/app/prefs.h"

#include "../src/base/base_arena.c"
#include "../src/base/base_string.c"
#include "../src/base/base_math.c"
#include "../src/base/base_hash.c"
#include "../src/base/base_jobs.c"
#include "../src/platform/win32/win32_platform.c"
#include "../src/platform/win32/win32_file.c"
#include "../src/platform/win32/win32_thread.c"
#include "../src/platform/win32/win32_window.c"
#include "../src/platform/win32/win32_image.c"
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
#include "../src/ui/ui_widgets.c"
#include "../src/ui/ui_debug_overlay.c"
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
#include "../src/core/plan/plan_file.c"
#include "../src/app/prefs.c"

typedef struct TestState {
    Arena *arena;         // per test case arena, reset between cases
    const char *current;  // name of the running case
    u64 checks;
    u64 failures;
    u64 cases;
} TestState;

global TestState test_state;

static void test_report(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ArenaTemp scratch = scratch_begin(0, 0);
    String8 line = str8fv(scratch.arena, fmt, args);
    os_debug_print(line);
    scratch_end(scratch);
    va_end(args);
}

static void test_check(b32 condition, const char *expression, i32 line) {
    test_state.checks += 1;
    if (!condition) {
        test_state.failures += 1;
        test_report("  FAIL %s:%d  %s\n", test_state.current, line, expression);
    }
}

#define TEST(name) static void test_##name(Arena *arena)
#define EXPECT(cond) test_check((cond) ? 1 : 0, #cond, __LINE__)
#define RUN(name)                                    \
    do {                                             \
        test_state.current = #name;                  \
        test_state.cases += 1;                       \
        u64 before = test_state.failures;            \
        arena_clear(test_state.arena);               \
        test_##name(test_state.arena);               \
        if (test_state.failures == before) {         \
            test_report("  ok   %s\n", #name);       \
        }                                            \
    } while (0)

#include "test_base.c"
#include "test_jobs.c"
#include "test_events.c"
#include "test_render.c"
#include "test_text.c"
#include "test_ui.c"
#include "test_widgets.c"
#include "test_library.c"
#include "test_tags.c"
#include "test_index.c"
#include "test_prefs.c"
#include "test_covers.c"
#include "test_plan.c"

int main(void) {
    os_init();
    test_state.arena = arena_alloc(MB(64));
    test_report("minidisk tests\n");

    test_base_run_all();
    test_jobs_run_all();
    test_events_run_all();
    test_render_run_all();
    test_text_run_all();
    test_ui_run_all();
    test_widgets_run_all();
    test_library_run_all();
    test_tags_run_all();
    test_index_run_all();
    test_prefs_run_all();
    test_covers_run_all();
    test_plan_run_all();

    test_report("%llu case(s), %llu check(s), %llu failure(s)\n", test_state.cases,
                test_state.checks, test_state.failures);
    return (test_state.failures == 0) ? 0 : 1;
}

// ui_debug_overlay.c - F11. Everything the frame cost, in boxes: no immediate
// drawing, no font of its own, no state the application has to carry.
//
// The two message counters at the bottom are there to settle P-005 (residual
// cpu at rest since the GL context arrived): `wakeups` says whether our loop is
// the one burning it, `messages` minus `dispatched` says whether another thread
// is sending messages into our window proc while we sleep.

#include "ui_debug_overlay.h"

#include "../base/base_jobs.h"
#include "r_atlas.h"
#include "r_core.h"
#include "ui_font.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

// The panel geometry, in logical units (dp). ui_dp() turns them into pixels
// exactly once, at the top of ui_debug_overlay_build: everything the placement
// arithmetic touches after that is in the same unit as ui_viewport(), which is
// physical pixels. Mixing the two is what put the right hand column off screen
// at 125 % (T-015).
#define UI_DEBUG_WIDTH_DP     304.0f
#define UI_DEBUG_ROW_DP       16.0f
#define UI_DEBUG_GRAPH_DP     38.0f
#define UI_DEBUG_GRAPH_ROW_DP 46.0f

// The rows, formatted before the tree is built: the panel has to be as wide as
// its widest line (otherwise "draw calls" is cut off), and that width is only
// known once the strings exist.
enum {
    UI_DebugRow_Fps = 0,
    UI_DebugRow_MinMax,
    UI_DebugRow_Boxes,
    UI_DebugRow_List,
    UI_DebugRow_Vertices,
    UI_DebugRow_Atlas,
    UI_DebugRow_Arenas,
    UI_DebugRow_Jobs,
    UI_DebugRow_Dpi,
    UI_DebugRow_Pump,
    UI_DebugRow_WndProc,
    UI_DebugRow_FIXED,
};
#define UI_DEBUG_ROW_MAX (UI_DebugRow_FIXED + OS_MESSAGE_TOP_COUNT)

typedef struct UI_DebugOverlay {
    b32 visible;
    Arena *permanent;
    Arena *frame;
    UI_Box *panel;  // last built panel, for the placement test

    f32 samples[UI_DEBUG_FRAME_SAMPLES];  // milliseconds
    u32 sample_count;
    u32 sample_next;

    u32 draw_calls;  // snapshot of the last completed frame
    u32 quads;
    // What the virtualized list spent this frame (T-075, C5): it used to be in
    // the status bar, where it competed with the numbers the user came for.
    u64 list_boxes, list_visible;
} UI_DebugOverlay;

global UI_DebugOverlay ui_debug;

void ui_debug_overlay_set_arenas(Arena *permanent, Arena *frame) {
    ui_debug.permanent = permanent;
    ui_debug.frame = frame;
}

void ui_debug_overlay_set_list_stats(u64 boxes, u64 visible) {
    ui_debug.list_boxes = boxes;
    ui_debug.list_visible = visible;
}

void ui_debug_overlay_toggle(void) { ui_debug.visible = !ui_debug.visible; }
b32 ui_debug_overlay_visible(void) { return ui_debug.visible; }

Rect ui_debug_overlay_panel_rect(void) {
    if (!ui_debug.panel) { return rect(0.0f, 0.0f, 0.0f, 0.0f); }
    return ui_debug.panel->rect;
}

void ui_debug_overlay_end_frame(void) {
    const R_Frame *frame = r_frame_state();
    ui_debug.draw_calls = frame->batch_count;
    ui_debug.quads = frame->quad_count;
}

// --- rows ------------------------------------------------------------------
// One line of text is one box, with no key: nothing here is interactive, so
// nothing here needs an identity that survives the frame.
static void ui_debug_row(String8 text) {
    UI_PrefWidth(ui_pct(1.0f, 0.0f))
    UI_PrefHeight(ui_px(ui_dp(16.0f), 1.0f)) {
        UI_Box *box = ui_build_box_from_key(UI_DrawText, 0);
        box->display_string = text;
    }
}

// The frame time graph: one floating rect per sample, growing from the bottom,
// red above the 16.7 ms budget. Floating and not a column of spacers, because
// 120 bars are then 120 boxes instead of 360.
static void ui_debug_graph(f32 width, f32 max_ms) {
    const UI_Theme *theme = ui_theme();
    f32 height = ui_dp(UI_DEBUG_GRAPH_DP);
    f32 bar_width = width / (f32)UI_DEBUG_FRAME_SAMPLES;
    f32 scale = (max_ms > 0.0f) ? (height / max_ms) : 0.0f;

    UI_PrefWidth(ui_px(width, 1.0f))
    UI_PrefHeight(ui_px(height, 1.0f))
    UI_BgColor(theme->control)
    UI_CornerRadius(ui_dp(theme->space[UI_Space_2])) {
        UI_Box *graph = ui_build_box_from_key(UI_DrawBackground, 0);
        UI_Parent(graph) {
            for (u32 i = 0; i < ui_debug.sample_count; i += 1) {
                // Oldest first: the graph reads left to right like a timeline.
                u32 slot = (ui_debug.sample_next + UI_DEBUG_FRAME_SAMPLES - ui_debug.sample_count +
                            i) % UI_DEBUG_FRAME_SAMPLES;
                f32 ms = ui_debug.samples[slot];
                f32 bar = clamp_f32(ms * scale, 1.0f, height);
                u32 color = (ms > 16.7f) ? theme->warning : theme->accent;
                UI_FixedX((f32)i * bar_width)
                UI_FixedY(height - bar)
                UI_PrefWidth(ui_px(max_f32(bar_width - 1.0f, 1.0f), 1.0f))
                UI_PrefHeight(ui_px(bar, 1.0f))
                UI_BgColor(color) {
                    ui_build_box_from_key(UI_FloatingX | UI_FloatingY | UI_DrawBackground, 0);
                }
            }
        }
    }
}

// --- build -----------------------------------------------------------------

void ui_debug_overlay_build(void) {
    const UI_Theme *theme = ui_theme();

    f32 ms = ui_dt() * 1000.0f;
    ui_debug.samples[ui_debug.sample_next] = ms;
    ui_debug.sample_next = (ui_debug.sample_next + 1) % UI_DEBUG_FRAME_SAMPLES;
    if (ui_debug.sample_count < UI_DEBUG_FRAME_SAMPLES) { ui_debug.sample_count += 1; }
    if (!ui_debug.visible) { return; }

    // An open overlay is a live instrument: keep the loop running at 16 ms so
    // the numbers move, instead of freezing on the last event that woke us.
    ui_request_animation();

    f32 min_ms = 1e9f;
    f32 max_ms = 0.0f;
    f32 total_ms = 0.0f;
    for (u32 i = 0; i < ui_debug.sample_count; i += 1) {
        f32 sample = ui_debug.samples[i];
        min_ms = min_f32(min_ms, sample);
        max_ms = max_f32(max_ms, sample);
        total_ms += sample;
    }
    f32 avg_ms = total_ms / (f32)ui_debug.sample_count;
    f32 fps = (avg_ms > 0.0f) ? (1000.0f / avg_ms) : 0.0f;

    u32 atlas_size = r_atlas_size();
    f32 atlas_fill = 0.0f;
    if (atlas_size > 0) {
        atlas_fill = 100.0f * (f32)r_atlas_used_area() / ((f32)atlas_size * (f32)atlas_size);
    }
    OsEventCounters counters;
    os_event_counters(&counters);
    V2 viewport_size = ui_viewport();

    // --- the lines, before the boxes ---------------------------------------
    String8 rows[UI_DEBUG_ROW_MAX];
    u32 row_count = UI_DebugRow_FIXED;
    Arena *frame_arena = ui_frame_arena();
    rows[UI_DebugRow_Fps] =
            str8f(frame_arena, "%02f fps - frame %02f ms", (f64)fps, (f64)avg_ms);
    rows[UI_DebugRow_MinMax] =
            str8f(frame_arena, "min %02f  avg %02f  max %02f ms (%u frames)", (f64)min_ms,
                  (f64)avg_ms, (f64)max_ms, ui_debug.sample_count);
    rows[UI_DebugRow_Boxes] =
            str8f(frame_arena, "boxes %llu (%llu live)  draw calls %u", ui_frame_box_count(),
                  ui_box_count(), ui_debug.draw_calls);
    rows[UI_DebugRow_List] = str8f(frame_arena, "list %llu boxes for %llu visible rows",
                                   ui_debug.list_boxes, ui_debug.list_visible);
    rows[UI_DebugRow_Vertices] =
            str8f(frame_arena, "vertices %u  quads %u", ui_debug.quads * 4, ui_debug.quads);
    rows[UI_DebugRow_Atlas] = str8f(frame_arena, "atlas %u x %u  fill %02f %%", atlas_size,
                                    atlas_size, (f64)atlas_fill);
    rows[UI_DebugRow_Arenas] =
            str8f(frame_arena, "arenas KB: perm %llu  frame %llu  scratch %llu",
                  ui_debug.permanent ? ui_debug.permanent->committed >> 10 : 0,
                  ui_debug.frame ? ui_debug.frame->committed >> 10 : 0,
                  scratch_thread_committed() >> 10);
    rows[UI_DebugRow_Jobs] = str8f(frame_arena, "jobs %u pending  %u busy / %u workers",
                                   jobs_pending(), jobs_busy(), jobs_worker_count());
    rows[UI_DebugRow_Dpi] = str8f(frame_arena, "dpi %02f  window %u x %u", (f64)ui_dpi_scale(),
                                  (u32)viewport_size.x, (u32)viewport_size.y);
    rows[UI_DebugRow_Pump] = str8f(frame_arena, "pump %llu calls  %llu wake-ups",
                                   counters.pump_calls, counters.wakeups);
    rows[UI_DebugRow_WndProc] =
            str8f(frame_arena, "wndproc %llu msg  %llu dispatched  %llu sent", counters.messages,
                  counters.dispatched, counters.messages - counters.dispatched);
    for (u32 i = 0; i < OS_MESSAGE_TOP_COUNT; i += 1) {
        if (counters.top_count[i] == 0) { continue; }
        rows[row_count] = str8f(frame_arena, "  0x%04x  %llu", counters.top_message[i],
                                counters.top_count[i]);
        row_count += 1;
    }

    // --- placement, one unit throughout ------------------------------------
    // Wide enough for the longest line, never wider than the viewport, and
    // moved back inside it: at 125 % the caption is rounded up to a whole pixel,
    // so 304 dp of text no longer fits in 304 dp of panel.
    V2 viewport = viewport_size;
    OsFont font = ui_font(UI_FontStyle_Caption);
    f32 padding = ui_dp(theme->space[UI_Space_8]);
    f32 margin = ui_dp(theme->space[UI_Space_8]);
    f32 text_width = 0.0f;
    for (u32 i = 0; i < row_count; i += 1) {
        text_width = max_f32(text_width, ui_text_width(font, rows[i], 0));
    }
    f32 width = max_f32(ui_dp(UI_DEBUG_WIDTH_DP), text_width + 2.0f * padding);
    width = min_f32(width, max_f32(viewport.x - 2.0f * margin, 0.0f));
    // The exact sum of what the block below emits, in the same order: two
    // paddings, the graph row, the separator, and one line per row.
    f32 height = 2.0f * padding + ui_dp(UI_DEBUG_GRAPH_ROW_DP) +
                 ui_dp(theme->space[UI_Space_4]) + (f32)row_count * ui_dp(UI_DEBUG_ROW_DP);
    height = min_f32(height, max_f32(viewport.y - 2.0f * margin, 0.0f));
    f32 x = clamp_f32(viewport.x - width - margin, 0.0f, max_f32(viewport.x - width, 0.0f));
    f32 y = clamp_f32(margin, 0.0f, max_f32(viewport.y - height, 0.0f));

    UI_LayerScope(UI_Layer_Tooltip)
    UI_FixedX(x)
    UI_FixedY(y)
    UI_PrefWidth(ui_px(width, 1.0f))
    UI_PrefHeight(ui_px(height, 1.0f))
    UI_ChildLayoutAxis(Axis2_Y)
    UI_BgColor(theme->panel)
    UI_BorderColor(theme->border_control)
    UI_CornerRadius(ui_dp(theme->radius_popup))
    UI_BorderThickness(ui_dp(theme->border))
    UI_TextColor(theme->fg_secondary)
    UI_TextPadding(padding)
    UI_Font(ui_font(UI_FontStyle_Caption)) {
        // UI_Clip: a window too short for every counter cuts the panel instead
        // of painting the rest of it outside the viewport.
        // Keyed, alone in this file: the box then survives the frame, and its
        // laid out rect can still be read after ui_end (placement test).
        UI_Box *panel = ui_build_box(UI_FloatingX | UI_FloatingY | UI_DrawBackground |
                                             UI_DrawBorder | UI_DrawDropShadow | UI_Clip,
                                     str8_lit("###debug_overlay"));
        ui_debug.panel = panel;
        UI_Parent(panel) {
            ui_spacer(ui_px(padding, 1.0f));
            UI_TextColor(theme->fg_primary) { ui_debug_row(rows[UI_DebugRow_Fps]); }
            ui_debug_row(rows[UI_DebugRow_MinMax]);

            UI_PrefWidth(ui_pct(1.0f, 0.0f))
            UI_PrefHeight(ui_px(ui_dp(UI_DEBUG_GRAPH_ROW_DP), 1.0f))
            UI_ChildLayoutAxis(Axis2_X) {
                UI_Box *row = ui_build_box_from_key(0, 0);
                UI_Parent(row) {
                    ui_spacer(ui_px(padding, 1.0f));
                    ui_debug_graph(max_f32(width - 2.0f * padding, 1.0f), max_f32(max_ms, 16.7f));
                    ui_spacer(ui_px(padding, 1.0f));
                }
            }

            for (u32 i = UI_DebugRow_Boxes; i <= UI_DebugRow_Dpi; i += 1) {
                ui_debug_row(rows[i]);
            }
            ui_spacer(ui_px(ui_dp(theme->space[UI_Space_4]), 1.0f));
            UI_TextColor(theme->fg_primary) {
                ui_debug_row(rows[UI_DebugRow_Pump]);
                ui_debug_row(rows[UI_DebugRow_WndProc]);
            }
            for (u32 i = UI_DebugRow_FIXED; i < row_count; i += 1) { ui_debug_row(rows[i]); }
            ui_spacer(ui_px(padding, 1.0f));
        }
    }
}

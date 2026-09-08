// ui_debug_overlay.h - the F11 overlay: what the frame cost and what the
// process is doing, on the tooltip layer, built out of the same boxes as
// everything else (T-008).
#ifndef UI_DEBUG_OVERLAY_H
#define UI_DEBUG_OVERLAY_H

#include "../base/base.h"
#include "../base/base_arena.h"
#include "ui_core.h"

#define UI_DEBUG_FRAME_SAMPLES 120  // two seconds at 60 Hz

// The arenas whose committed size the overlay reports. Called once at startup;
// the scratch arenas find themselves.
void ui_debug_overlay_set_arenas(Arena *permanent, Arena *frame);

// What the virtualized list of the moment spent: the counters C5 moved out of
// the status bar. Called once a frame by the application.
void ui_debug_overlay_set_list_stats(u64 boxes, u64 visible);

void ui_debug_overlay_toggle(void);
b32  ui_debug_overlay_visible(void);

// The panel as the layout placed it, absolute physical pixels; empty while the
// overlay has never been built. Read after ui_end: the placement test checks it
// stays wholly inside the viewport at every DPI (T-015).
Rect ui_debug_overlay_panel_rect(void);

// Called between the application's UI and ui_end. Samples the frame time on
// every call, whether or not the overlay is visible, so opening it shows the
// history that was already there.
void ui_debug_overlay_build(void);

// Called right after r_end_frame: the draw call and vertex counts only exist
// once the frame has been batched, so the overlay reports the previous one.
void ui_debug_overlay_end_frame(void);

#endif // UI_DEBUG_OVERLAY_H

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

void ui_debug_overlay_toggle(void);
b32  ui_debug_overlay_visible(void);

// Called between the application's UI and ui_end. Samples the frame time on
// every call, whether or not the overlay is visible, so opening it shows the
// history that was already there.
void ui_debug_overlay_build(void);

// Called right after r_end_frame: the draw call and vertex counts only exist
// once the frame has been batched, so the overlay reports the previous one.
void ui_debug_overlay_end_frame(void);

#endif // UI_DEBUG_OVERLAY_H

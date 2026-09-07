// dsp_edit.h - the editing steps of the chain: downmix, silence trim, fades,
// inter-track gap and a fixed per-track gain (ADR-007 steps 3 and 5).
//
// The trim is a two-part affair on purpose: the scan is a streaming state
// machine that runs during the measurement pass and hands back two frame
// indices; the render pass then simply skips outside that window. Nothing here
// allocates and nothing here needs to see the whole track at once.
#ifndef DSP_EDIT_H
#define DSP_EDIT_H

#include "../../base/base.h"

#define DSP_TRIM_THRESHOLD_DB (-60.0f)
#define DSP_TRIM_HYSTERESIS_MS 50u
#define DSP_TRIM_WINDOW_MS 1u

// The decision is taken on the envelope, not on the samples: a sine crosses
// zero fifty times a second and every crossing puts a sample below any
// threshold, so a raw per-sample run length would never reach 50 ms of "loud".
// We take the peak over a 1 ms window, then ask for 50 consecutive loud (or
// quiet) windows before flipping state - that is the 50 ms hysteresis.
typedef struct DspTrimScan {
    f32 threshold;      // linear amplitude
    u32 window_frames;
    u32 hysteresis_windows;

    f32 window_peak;
    u32 frames_in_window;
    u64 window_start;   // frame index where the current window began

    b32 in_sound;
    u32 loud_run;       // consecutive loud windows
    u32 quiet_run;
    u64 loud_run_start;
    u64 first_sound;
    u64 last_sound_end;
    b32 found;

    u64 position;
} DspTrimScan;

void dsp_trim_scan_init(DspTrimScan *scan, u32 sample_rate, f32 threshold_db, u32 hysteresis_ms);
void dsp_trim_scan_feed(DspTrimScan *scan, const f32 *const *planar, u32 channels, u64 frames);
// Half-open [begin, end) of frames worth keeping. Empty when the track is
// silent. Closes the window still in flight, so call it once, at the end.
void dsp_trim_scan_result(DspTrimScan *scan, u64 total_frames, u64 *begin, u64 *end);

// (L + R) / 2 into `dst`, which may alias `left`.
void dsp_downmix_mono(f32 *dst, const f32 *left, const f32 *right, u64 frames);

void dsp_apply_gain(f32 *const *planar, u32 channels, u64 frames, f32 linear_gain);

// Cosine fade. `position` is the frame index inside the fade, `length` its total
// length in frames; the shape is 0.5 - 0.5*cos(pi*t) so it starts and ends flat.
typedef struct DspFade {
    u64 in_length;
    u64 out_length;
    u64 out_start;  // frame index where the fade-out begins
} DspFade;

void dsp_fade_apply(const DspFade *fade, f32 *const *planar, u32 channels, u64 frames,
                    u64 stream_position);

#endif  // DSP_EDIT_H

#include "dsp_edit.h"

#include <emmintrin.h>

#include "dsp_math.h"

// --- silence trim ----------------------------------------------------------
void dsp_trim_scan_init(DspTrimScan *scan, u32 sample_rate, f32 threshold_db, u32 hysteresis_ms) {
    StructZero(scan);
    scan->threshold = dsp_amp_from_db(threshold_db);
    scan->window_frames = sample_rate * DSP_TRIM_WINDOW_MS / 1000u;
    if (scan->window_frames == 0) { scan->window_frames = 1; }
    scan->hysteresis_windows = hysteresis_ms / DSP_TRIM_WINDOW_MS;
    if (scan->hysteresis_windows == 0) { scan->hysteresis_windows = 1; }
}

static void dsp_trim_close_window(DspTrimScan *scan, u64 window_end) {
    b32 loud = (scan->window_peak > scan->threshold);
    if (loud) {
        if (scan->loud_run == 0) { scan->loud_run_start = scan->window_start; }
        scan->loud_run += 1;
        scan->quiet_run = 0;
        if (!scan->in_sound && scan->loud_run >= scan->hysteresis_windows) {
            scan->in_sound = 1;
            if (!scan->found) {
                scan->found = 1;
                scan->first_sound = scan->loud_run_start;
            }
        }
        if (scan->in_sound) { scan->last_sound_end = window_end; }
    } else {
        scan->loud_run = 0;
        scan->quiet_run += 1;
        // A rest between two notes is not the end of the track: only a quiet
        // stretch as long as the hysteresis closes the sound.
        if (scan->in_sound && scan->quiet_run >= scan->hysteresis_windows) { scan->in_sound = 0; }
    }
    scan->window_peak = 0.0f;
    scan->frames_in_window = 0;
    scan->window_start = window_end;
}

void dsp_trim_scan_feed(DspTrimScan *scan, const f32 *const *planar, u32 channels, u64 frames) {
    for (u64 i = 0; i < frames; i += 1) {
        f32 peak = scan->window_peak;
        for (u32 c = 0; c < channels; c += 1) {
            f32 v = planar[c][i];
            f32 magnitude = (v < 0.0f) ? -v : v;
            if (magnitude > peak) { peak = magnitude; }
        }
        scan->window_peak = peak;
        scan->frames_in_window += 1;
        if (scan->frames_in_window == scan->window_frames) {
            dsp_trim_close_window(scan, scan->position + i + 1);
        }
    }
    scan->position += frames;
}

void dsp_trim_scan_result(DspTrimScan *scan, u64 total_frames, u64 *begin, u64 *end) {
    // The tail of the track rarely lands on a window boundary; close the
    // partial one so a fade-out ending mid-window is not cut short.
    if (scan->frames_in_window != 0) { dsp_trim_close_window(scan, scan->position); }
    if (!scan->found) {
        *begin = 0;
        *end = 0;
        return;
    }
    u64 last = Min(scan->last_sound_end, total_frames);
    *begin = scan->first_sound;
    *end = (last > scan->first_sound) ? last : scan->first_sound;
}

// --- downmix ---------------------------------------------------------------
void dsp_downmix_mono(f32 *dst, const f32 *left, const f32 *right, u64 frames) {
    __m128 half = _mm_set1_ps(0.5f);
    u64 i = 0;
    for (; i + 4 <= frames; i += 4) {
        __m128 l = _mm_loadu_ps(left + i);
        __m128 r = _mm_loadu_ps(right + i);
        _mm_storeu_ps(dst + i, _mm_mul_ps(_mm_add_ps(l, r), half));
    }
    for (; i < frames; i += 1) { dst[i] = (left[i] + right[i]) * 0.5f; }
}

// --- gain ------------------------------------------------------------------
void dsp_apply_gain(f32 *const *planar, u32 channels, u64 frames, f32 linear_gain) {
    if (linear_gain == 1.0f) { return; }
    __m128 g = _mm_set1_ps(linear_gain);
    for (u32 c = 0; c < channels; c += 1) {
        f32 *p = planar[c];
        u64 i = 0;
        for (; i + 4 <= frames; i += 4) {
            _mm_storeu_ps(p + i, _mm_mul_ps(_mm_loadu_ps(p + i), g));
        }
        for (; i < frames; i += 1) { p[i] *= linear_gain; }
    }
}

// --- fades -----------------------------------------------------------------
static f32 dsp_fade_curve(u64 position, u64 length) {
    // 0.5 - 0.5*cos(pi*t): equal-power enough for a fade and, unlike a linear
    // ramp, its derivative is zero at both ends so there is no audible corner.
    f64 t = (f64)position / (f64)length;
    return (f32)(0.5 - 0.5 * dsp_cos_f64(DSP_PI * t));
}

void dsp_fade_apply(const DspFade *fade, f32 *const *planar, u32 channels, u64 frames,
                    u64 stream_position) {
    if (fade->in_length == 0 && fade->out_length == 0) { return; }
    // A ten millisecond fade on a four minute track leaves 99.99 % of the
    // blocks untouched: recognise them before touching a sample.
    b32 past_fade_in = (stream_position >= fade->in_length);
    b32 before_fade_out =
        (fade->out_length == 0) || (stream_position + frames <= fade->out_start);
    if (past_fade_in && before_fade_out) { return; }
    for (u64 i = 0; i < frames; i += 1) {
        u64 index = stream_position + i;
        f32 gain = 1.0f;
        if (index < fade->in_length) { gain = dsp_fade_curve(index, fade->in_length); }
        if (fade->out_length != 0 && index >= fade->out_start) {
            u64 into = index - fade->out_start;
            f32 out_gain = (into >= fade->out_length)
                               ? 0.0f
                               : dsp_fade_curve(fade->out_length - into, fade->out_length);
            if (out_gain < gain) { gain = out_gain; }
        }
        if (gain != 1.0f) {
            for (u32 c = 0; c < channels; c += 1) { planar[c][i] *= gain; }
        }
    }
}

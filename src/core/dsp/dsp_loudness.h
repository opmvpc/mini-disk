// dsp_loudness.h - ITU-R BS.1770-4 / EBU R128 integrated loudness and true peak.
//
// Everything is derived for the rate handed to init, never copied from the
// 48 kHz table printed in the standard: the K-weighting biquads come out of the
// analogue prototype through the bilinear transform, exactly the generalisation
// libebur128 and ffmpeg use, so our numbers line up with `ffmpeg -af ebur128`.
//
// One pass gives: integrated LUFS (400 ms blocks, 75 % overlap, -70 LUFS
// absolute gate then -10 LU relative gate) and the true peak in dBTP (4x
// polyphase oversampling, BS.1770-4 annex 2). The gain toward a target is
// capped so the true peak stays under the ceiling - we lower the gain, we never
// limit (ADR-007 step 4).
#ifndef DSP_LOUDNESS_H
#define DSP_LOUDNESS_H

#include "../../base/base.h"
#include "../../base/base_arena.h"

#define DSP_R128_BLOCK_MS       400u
#define DSP_R128_STEP_MS        100u   // 75 % overlap
#define DSP_R128_SUBBLOCKS      4u
#define DSP_R128_ABS_GATE_LUFS  (-70.0)
#define DSP_R128_REL_GATE_LU    (-10.0)
#define DSP_R128_OFFSET_DB      (-0.691)  // the BS.1770 constant
#define DSP_R128_TARGET_LUFS    (-14.0f)
#define DSP_R128_CEILING_DBTP   (-1.0f)

#define DSP_TP_PHASES 4u
#define DSP_TP_TAPS   12u  // per phase: 48 coefficients in all, as in annex 2

typedef struct DspBiquad {
    f64 b0, b1, b2, a1, a2;
} DspBiquad;

typedef struct DspR128 {
    u32 sample_rate;
    u32 channels;

    DspBiquad shelf;  // stage 1: high shelf, the "head" filter
    DspBiquad rlb;    // stage 2: RLB high-pass
    f64 state[2][2][2];  // [channel][stage][z-1, z-2], direct form II transposed

    // Sub-block accumulation: one 100 ms slice at a time, four of them make a
    // 400 ms block, so the ring of four sums is the 75 % overlap.
    u32 step_frames;
    u32 frames_in_sub;
    f64 sub_sum;            // sum of squares over channels for the current slice
    f64 ring[DSP_R128_SUBBLOCKS];
    u32 ring_count;         // slices completed since the start, capped at 4
    u32 ring_head;

    f64 *block_energy;      // mean square of every completed 400 ms block
    u64  block_count;
    u64  block_cap;

    // True peak: 4x polyphase interpolation, phase 0 is the identity so the raw
    // sample peak is included for free.
    // Transposed on purpose: [tap][phase] lets one SSE2 register hold the four
    // phase coefficients of a tap, so the four interpolated samples come out in
    // the four lanes and no horizontal sum is needed per input sample.
    f32 tp_taps[DSP_TP_TAPS][DSP_TP_PHASES];
    f32 tp_hist[2][DSP_TP_TAPS * 2];  // double buffer: the window is contiguous
    u32 tp_pos;                       // next write slot, in [TAPS-1, 2*TAPS)
    f32 tp_peak4[4];                  // running per-lane maximum
    f32 true_peak;

    u64 frames_fed;
} DspR128;

// `max_frames` sizes the block-energy array once, at init: 8 bytes per 100 ms.
void dsp_r128_init(DspR128 *s, Arena *arena, u32 sample_rate, u32 channels, u64 max_frames);
void dsp_r128_reset(DspR128 *s);
void dsp_r128_feed(DspR128 *s, const f32 *const *planar, u64 frames);

f32 dsp_r128_integrated_lufs(const DspR128 *s);  // DSP_DB_SILENCE when fully gated
f32 dsp_r128_true_peak_dbtp(const DspR128 *s);

// target - measured, then pulled down until measured_peak + gain <= ceiling.
f32 dsp_r128_gain_db(const DspR128 *s, f32 target_lufs, f32 ceiling_dbtp);

#endif  // DSP_LOUDNESS_H

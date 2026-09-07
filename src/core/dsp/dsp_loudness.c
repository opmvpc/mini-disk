#include "dsp_loudness.h"

#include <emmintrin.h>

#include "dsp_math.h"

// --- K-weighting -----------------------------------------------------------
// BS.1770-4 prints the coefficients for 48 kHz only. Hard-coding those at
// 44.1 kHz is the classic bug (research/03 s6.8), so we run the bilinear
// transform on the analogue prototype instead. The prototype constants below
// are the ones that reproduce the printed 48 kHz table bit for bit.
#define DSP_K_SHELF_F0 1681.974450955533
#define DSP_K_SHELF_G  3.999843853973347
#define DSP_K_SHELF_Q  0.7071752369554196
#define DSP_K_SHELF_VB_EXP 0.4996667741545416
#define DSP_K_RLB_F0   38.13547087602444
#define DSP_K_RLB_Q    0.5003270373238773

static DspBiquad dsp_k_shelf(u32 rate) {
    f64 k = dsp_tan_f64(DSP_PI * DSP_K_SHELF_F0 / (f64)rate);
    f64 vh = dsp_pow_f64(10.0, DSP_K_SHELF_G / 20.0);
    f64 vb = dsp_pow_f64(vh, DSP_K_SHELF_VB_EXP);
    f64 kk = k * k;
    f64 a0 = 1.0 + k / DSP_K_SHELF_Q + kk;
    DspBiquad b;
    b.b0 = (vh + vb * k / DSP_K_SHELF_Q + kk) / a0;
    b.b1 = 2.0 * (kk - vh) / a0;
    b.b2 = (vh - vb * k / DSP_K_SHELF_Q + kk) / a0;
    b.a1 = 2.0 * (kk - 1.0) / a0;
    b.a2 = (1.0 - k / DSP_K_SHELF_Q + kk) / a0;
    return b;
}

static DspBiquad dsp_k_rlb(u32 rate) {
    f64 k = dsp_tan_f64(DSP_PI * DSP_K_RLB_F0 / (f64)rate);
    f64 kk = k * k;
    f64 a0 = 1.0 + k / DSP_K_RLB_Q + kk;
    DspBiquad b;
    b.b0 = 1.0;
    b.b1 = -2.0;
    b.b2 = 1.0;
    b.a1 = 2.0 * (kk - 1.0) / a0;
    b.a2 = (1.0 - k / DSP_K_RLB_Q + kk) / a0;
    return b;
}

// --- true peak interpolator ------------------------------------------------
// Phase 0 is the identity, so the raw sample peak is measured too and a signal
// whose maximum happens to sit on a sample is never underestimated.
#define DSP_TP_KAISER_B 4.5
#define DSP_TP_CUTOFF   0.45  // of the sample rate: ~19.8 kHz at 44.1 kHz

static void dsp_tp_build(DspR128 *s) {
    f64 i0_beta = dsp_bessel_i0(DSP_TP_KAISER_B);
    f64 half_span = 0.5 * (f64)DSP_TP_TAPS;
    i32 center = (i32)(DSP_TP_TAPS / 2) - 1;
    for (u32 p = 0; p < DSP_TP_PHASES; p += 1) {
        f64 frac = (f64)p / (f64)DSP_TP_PHASES;
        f64 sum = 0.0;
        f64 row[DSP_TP_TAPS];
        for (u32 k = 0; k < DSP_TP_TAPS; k += 1) {
            f64 d = (f64)((i32)k - center) - frac;
            f64 t = d / half_span;
            f64 window = 0.0;
            if (t > -1.0 && t < 1.0) {
                window = dsp_bessel_i0(DSP_TP_KAISER_B * dsp_sqrt_f64(1.0 - t * t)) / i0_beta;
            }
            row[k] = 2.0 * DSP_TP_CUTOFF * dsp_sinc(2.0 * DSP_TP_CUTOFF * d) * window;
            sum += row[k];
        }
        for (u32 k = 0; k < DSP_TP_TAPS; k += 1) { s->tp_taps[k][p] = (f32)(row[k] / sum); }
    }
}

void dsp_r128_init(DspR128 *s, Arena *arena, u32 sample_rate, u32 channels, u64 max_frames) {
    StructZero(s);
    s->sample_rate = sample_rate;
    s->channels = channels;
    s->shelf = dsp_k_shelf(sample_rate);
    s->rlb = dsp_k_rlb(sample_rate);
    s->step_frames = sample_rate * DSP_R128_STEP_MS / 1000u;
    s->block_cap = max_frames / (u64)s->step_frames + 8;
    s->block_energy = push_array(arena, f64, s->block_cap);
    s->tp_pos = DSP_TP_TAPS - 1u;
    dsp_tp_build(s);
}

void dsp_r128_reset(DspR128 *s) {
    mem_zero(s->state, sizeof(s->state));
    mem_zero(s->ring, sizeof(s->ring));
    mem_zero(s->tp_hist, sizeof(s->tp_hist));
    mem_zero(s->tp_peak4, sizeof(s->tp_peak4));
    s->tp_pos = DSP_TP_TAPS - 1u;
    s->frames_in_sub = 0;
    s->sub_sum = 0.0;
    s->ring_count = 0;
    s->ring_head = 0;
    s->block_count = 0;
    s->true_peak = 0.0f;
    s->frames_fed = 0;
}

static md_inline f64 dsp_biquad_step(const DspBiquad *b, f64 *z, f64 x) {
    f64 y = b->b0 * x + z[0];
    z[0] = b->b1 * x - b->a1 * y + z[1];
    z[1] = b->b2 * x - b->a2 * y;
    return y;
}

static void dsp_r128_close_block(DspR128 *s) {
    s->ring[s->ring_head] = s->sub_sum;
    s->ring_head = (s->ring_head + 1) & (DSP_R128_SUBBLOCKS - 1);
    if (s->ring_count < DSP_R128_SUBBLOCKS) { s->ring_count += 1; }
    s->sub_sum = 0.0;
    s->frames_in_sub = 0;

    if (s->ring_count == DSP_R128_SUBBLOCKS && s->block_count < s->block_cap) {
        f64 total = s->ring[0] + s->ring[1] + s->ring[2] + s->ring[3];
        f64 samples = (f64)s->step_frames * (f64)DSP_R128_SUBBLOCKS;
        s->block_energy[s->block_count] = total / samples;
        s->block_count += 1;
    }
}

// Stereo K-weighting with one biquad chain in each half of an __m128d: the two
// channels are independent, so the packed form costs the same as one channel
// and hides the latency of the multiply chain, which is what a direct form II
// transposed biquad is really limited by.
static void dsp_r128_kweight_stereo(DspR128 *s, const f32 *const *planar, u64 frames) {
    __m128d sb0 = _mm_set1_pd(s->shelf.b0);
    __m128d sb1 = _mm_set1_pd(s->shelf.b1);
    __m128d sb2 = _mm_set1_pd(s->shelf.b2);
    __m128d sa1 = _mm_set1_pd(s->shelf.a1);
    __m128d sa2 = _mm_set1_pd(s->shelf.a2);
    __m128d ra1 = _mm_set1_pd(s->rlb.a1);
    __m128d ra2 = _mm_set1_pd(s->rlb.a2);
    __m128d two = _mm_set1_pd(2.0);

    __m128d sz0 = _mm_set_pd(s->state[1][0][0], s->state[0][0][0]);
    __m128d sz1 = _mm_set_pd(s->state[1][0][1], s->state[0][0][1]);
    __m128d rz0 = _mm_set_pd(s->state[1][1][0], s->state[0][1][0]);
    __m128d rz1 = _mm_set_pd(s->state[1][1][1], s->state[0][1][1]);

    for (u64 i = 0; i < frames; i += 1) {
        __m128d x = _mm_set_pd((f64)planar[1][i], (f64)planar[0][i]);
        __m128d y = _mm_add_pd(_mm_mul_pd(sb0, x), sz0);
        sz0 = _mm_add_pd(_mm_sub_pd(_mm_mul_pd(sb1, x), _mm_mul_pd(sa1, y)), sz1);
        sz1 = _mm_sub_pd(_mm_mul_pd(sb2, x), _mm_mul_pd(sa2, y));
        // RLB stage: b = {1, -2, 1}, so no multiply for b0 and b2.
        __m128d w = _mm_add_pd(y, rz0);
        rz0 = _mm_add_pd(_mm_sub_pd(_mm_mul_pd(two, _mm_sub_pd(_mm_setzero_pd(), y)),
                                    _mm_mul_pd(ra1, w)),
                         rz1);
        rz1 = _mm_sub_pd(y, _mm_mul_pd(ra2, w));

        // BS.1770 weights: 1.0 for L and R, which is all a MiniDisc carries.
        __m128d square = _mm_mul_pd(w, w);
        f64 frame_sum =
            _mm_cvtsd_f64(_mm_add_sd(square, _mm_unpackhi_pd(square, square)));
        s->sub_sum += frame_sum;
        s->frames_in_sub += 1;
        if (s->frames_in_sub == s->step_frames) { dsp_r128_close_block(s); }
    }

    f64 out[2];
    _mm_storeu_pd(out, sz0);
    s->state[0][0][0] = out[0];
    s->state[1][0][0] = out[1];
    _mm_storeu_pd(out, sz1);
    s->state[0][0][1] = out[0];
    s->state[1][0][1] = out[1];
    _mm_storeu_pd(out, rz0);
    s->state[0][1][0] = out[0];
    s->state[1][1][0] = out[1];
    _mm_storeu_pd(out, rz1);
    s->state[0][1][1] = out[0];
    s->state[1][1][1] = out[1];
}

void dsp_r128_feed(DspR128 *s, const f32 *const *planar, u64 frames) {
    u32 channels = s->channels;
    if (channels == 2) {
        dsp_r128_kweight_stereo(s, planar, frames);
    } else {
        for (u64 i = 0; i < frames; i += 1) {
            f64 x = (f64)planar[0][i];
            f64 y = dsp_biquad_step(&s->shelf, s->state[0][0], x);
            y = dsp_biquad_step(&s->rlb, s->state[0][1], y);
            s->sub_sum += y * y;
            s->frames_in_sub += 1;
            if (s->frames_in_sub == s->step_frames) { dsp_r128_close_block(s); }
        }
    }

    // True peak, on the unweighted signal, 4x oversampled. Phase 0 of the
    // interpolator is the identity, so the raw sample peak is in there too.
    __m128 peak = _mm_loadu_ps(s->tp_peak4);
    __m128 abs_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
    u32 pos = s->tp_pos;
    for (u64 i = 0; i < frames; i += 1) {
        if (pos == DSP_TP_TAPS * 2u) {
            // Slide the window back to the front of the double buffer: one copy
            // every twelve samples instead of a shift per sample.
            for (u32 c = 0; c < channels; c += 1) {
                mem_copy(s->tp_hist[c], s->tp_hist[c] + DSP_TP_TAPS, DSP_TP_TAPS * sizeof(f32));
            }
            pos = DSP_TP_TAPS;
        }
        // Two accumulators per channel and both channels in the same k loop:
        // twelve chained adds at four cycles of latency each would otherwise be
        // the whole cost of this filter, and four independent chains hide it.
        if (channels == 2) {
            s->tp_hist[0][pos] = planar[0][i];
            s->tp_hist[1][pos] = planar[1][i];
            const f32 *w0 = s->tp_hist[0] + pos - (DSP_TP_TAPS - 1u);
            const f32 *w1 = s->tp_hist[1] + pos - (DSP_TP_TAPS - 1u);
            __m128 a0 = _mm_setzero_ps();
            __m128 a1 = _mm_setzero_ps();
            __m128 b0 = _mm_setzero_ps();
            __m128 b1 = _mm_setzero_ps();
            for (u32 k = 0; k + 2 <= DSP_TP_TAPS; k += 2) {
                __m128 h0 = _mm_loadu_ps(s->tp_taps[k]);
                __m128 h1 = _mm_loadu_ps(s->tp_taps[k + 1]);
                a0 = _mm_add_ps(a0, _mm_mul_ps(_mm_set1_ps(w0[k]), h0));
                a1 = _mm_add_ps(a1, _mm_mul_ps(_mm_set1_ps(w0[k + 1]), h1));
                b0 = _mm_add_ps(b0, _mm_mul_ps(_mm_set1_ps(w1[k]), h0));
                b1 = _mm_add_ps(b1, _mm_mul_ps(_mm_set1_ps(w1[k + 1]), h1));
            }
            __m128 left = _mm_and_ps(_mm_add_ps(a0, a1), abs_mask);
            __m128 right = _mm_and_ps(_mm_add_ps(b0, b1), abs_mask);
            peak = _mm_max_ps(peak, _mm_max_ps(left, right));
        } else {
            s->tp_hist[0][pos] = planar[0][i];
            const f32 *window = s->tp_hist[0] + pos - (DSP_TP_TAPS - 1u);
            __m128 a0 = _mm_setzero_ps();
            __m128 a1 = _mm_setzero_ps();
            for (u32 k = 0; k + 2 <= DSP_TP_TAPS; k += 2) {
                a0 = _mm_add_ps(a0, _mm_mul_ps(_mm_set1_ps(window[k]),
                                               _mm_loadu_ps(s->tp_taps[k])));
                a1 = _mm_add_ps(a1, _mm_mul_ps(_mm_set1_ps(window[k + 1]),
                                               _mm_loadu_ps(s->tp_taps[k + 1])));
            }
            peak = _mm_max_ps(peak, _mm_and_ps(_mm_add_ps(a0, a1), abs_mask));
        }
        pos += 1;
    }
    s->tp_pos = pos;
    _mm_storeu_ps(s->tp_peak4, peak);
    f32 best = s->tp_peak4[0];
    for (u32 lane = 1; lane < 4; lane += 1) {
        if (s->tp_peak4[lane] > best) { best = s->tp_peak4[lane]; }
    }
    s->true_peak = best;
    s->frames_fed += frames;
}

f32 dsp_r128_integrated_lufs(const DspR128 *s) {
    if (s->block_count == 0) { return DSP_DB_SILENCE; }
    // Energy form of the two gates: comparing energies avoids a log per block.
    f64 abs_gate = dsp_pow_f64(10.0, (DSP_R128_ABS_GATE_LUFS - DSP_R128_OFFSET_DB) / 10.0);
    f64 sum = 0.0;
    u64 count = 0;
    for (u64 i = 0; i < s->block_count; i += 1) {
        if (s->block_energy[i] > abs_gate) {
            sum += s->block_energy[i];
            count += 1;
        }
    }
    if (count == 0) { return DSP_DB_SILENCE; }

    f64 rel_gate = (sum / (f64)count) * dsp_pow_f64(10.0, DSP_R128_REL_GATE_LU / 10.0);
    f64 gate = (rel_gate > abs_gate) ? rel_gate : abs_gate;
    sum = 0.0;
    count = 0;
    for (u64 i = 0; i < s->block_count; i += 1) {
        if (s->block_energy[i] > gate) {
            sum += s->block_energy[i];
            count += 1;
        }
    }
    if (count == 0) { return DSP_DB_SILENCE; }
    return (f32)(DSP_R128_OFFSET_DB + 10.0 * dsp_log10_f64(sum / (f64)count));
}

f32 dsp_r128_true_peak_dbtp(const DspR128 *s) { return dsp_db_from_amp(s->true_peak); }

f32 dsp_r128_gain_db(const DspR128 *s, f32 target_lufs, f32 ceiling_dbtp) {
    f32 measured = dsp_r128_integrated_lufs(s);
    if (measured <= DSP_DB_SILENCE) { return 0.0f; }  // nothing to normalise
    f32 gain = target_lufs - measured;
    f32 peak = dsp_r128_true_peak_dbtp(s);
    f32 headroom = ceiling_dbtp - (peak + gain);
    // We only ever come down: raising the gain to reach the ceiling would be a
    // second, unasked-for normalisation of a quiet-but-peaky track.
    if (headroom < 0.0f) { gain += headroom; }
    return gain;
}

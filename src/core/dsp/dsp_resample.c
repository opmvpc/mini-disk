#include "dsp_resample.h"

#include <emmintrin.h>

#include "dsp_math.h"

// The table holds `phases + 1` rows so the linear interpolation between phase p
// and phase p+1 never has to wrap: row `phases` is the same prototype sampled
// one whole input sample further along, which is exactly what a fraction of 1.0
// asks for.
static void dsp_resampler_build_table(DspResampler *r, Arena *arena) {
    u32 taps = r->taps_per_phase;
    u32 rows = r->phases + 1;
    r->taps = (f32 *)arena_push(arena, (u64)rows * taps * sizeof(f32), 16);

    // Cutoff: the lower of the two Nyquist frequencies, pulled down by half the
    // Kaiser transition width so the stopband already starts at that Nyquist -
    // that is what keeps the images out of the audible band after decimation.
    f64 in_rate = (f64)r->in_rate;
    f64 nyquist = 0.5 * (f64)Min(r->in_rate, r->out_rate);
    f64 half_transition = 0.5 * DSP_RESAMPLE_TRANSITION_D * in_rate / (f64)taps;
    f64 cutoff_hz = nyquist - half_transition;
    if (cutoff_hz < 0.25 * nyquist) { cutoff_hz = 0.25 * nyquist; }
    f64 fc = cutoff_hz / in_rate;  // cycles per input sample, < 0.5

    f64 half_span = 0.5 * (f64)taps;
    f64 i0_beta = dsp_bessel_i0(DSP_RESAMPLE_KAISER_B);
    i32 center = (i32)(taps / 2) - 1;

    for (u32 p = 0; p < rows; p += 1) {
        f64 frac = (f64)p / (f64)r->phases;
        f32 *row = r->taps + (u64)p * taps;
        for (u32 k = 0; k < taps; k += 1) {
            // Distance, in input samples, between tap k and the output instant.
            f64 d = (f64)((i32)k - center) - frac;
            f64 t = d / half_span;
            f64 window = 0.0;
            if (t > -1.0 && t < 1.0) {
                window = dsp_bessel_i0(DSP_RESAMPLE_KAISER_B * dsp_sqrt_f64(1.0 - t * t)) / i0_beta;
            }
            row[k] = (f32)(2.0 * fc * dsp_sinc(2.0 * fc * d) * window);
        }
    }

    // Normalise each phase to unit DC gain: without it the passband droops by a
    // few thousandths of a dB and, worse, it droops *differently* per phase,
    // which is a periodic modulation an ear can find on a held tone.
    for (u32 p = 0; p < rows; p += 1) {
        f32 *row = r->taps + (u64)p * taps;
        f64 sum = 0.0;
        for (u32 k = 0; k < taps; k += 1) { sum += (f64)row[k]; }
        if (sum > 1e-9) {
            f32 scale = (f32)(1.0 / sum);
            for (u32 k = 0; k < taps; k += 1) { row[k] *= scale; }
        }
    }
}

void dsp_resampler_init(DspResampler *r, Arena *arena, u32 in_rate, u32 out_rate, u32 channels,
                        u32 max_in_frames, u64 total_in_frames) {
    StructZero(r);
    r->in_rate = in_rate;
    r->out_rate = out_rate;
    r->channels = channels;
    r->phases = DSP_RESAMPLE_PHASES;
    r->passthrough = (in_rate == out_rate);
    r->out_limit = (total_in_frames != 0)
                       ? dsp_resampler_expected_out(in_rate, out_rate, total_in_frames)
                       : 0;
    if (r->passthrough) { return; }

    // A fixed 64-tap window spans 64 input samples, so on a 96 kHz source the
    // transition band would be twice as wide as at 48 kHz and eat 4 kHz of
    // bandwidth. Stretching the tap count with the decimation ratio, rounded to
    // nearest, keeps the transition a roughly constant fraction of the *output*
    // rate: 64 taps at 44.1 and 48 kHz, 128 at 88.2 and 96 kHz. Deviation from
    // the flat "64 taps" of the ticket, documented in T-041.
    u32 decimation = (in_rate + out_rate / 2u) / out_rate;
    if (decimation < 1) { decimation = 1; }
    r->taps_per_phase = DSP_RESAMPLE_TAPS * decimation;
    r->history = r->taps_per_phase - 1;

    r->step = ((u64)in_rate << 32) / (u64)out_rate;

    u32 block = Max(max_in_frames, r->taps_per_phase);
    r->work_cap = r->history + block;
    for (u32 c = 0; c < channels; c += 1) {
        r->work[c] = (f32 *)arena_push_zero(arena, (u64)r->work_cap * sizeof(f32), 16);
    }
    dsp_resampler_build_table(r, arena);
}

void dsp_resampler_reset(DspResampler *r) {
    r->pos = 0;
    r->consumed = 0;
    r->produced = 0;
    if (!r->passthrough) {
        for (u32 c = 0; c < r->channels; c += 1) {
            mem_zero(r->work[c], (u64)r->work_cap * sizeof(f32));
        }
    }
}

u64 dsp_resampler_out_capacity(const DspResampler *r, u64 in_frames) {
    if (r->passthrough) { return in_frames; }
    return (in_frames * (u64)r->out_rate) / (u64)r->in_rate + 2;
}

u64 dsp_resampler_expected_out(u32 in_rate, u32 out_rate, u64 in_frames) {
    if (in_frames == 0) { return 0; }
    if (in_rate == out_rate) { return in_frames; }
    u64 step = ((u64)in_rate << 32) / (u64)out_rate;
    // Outputs sit at m*step for every m whose input instant is still inside the
    // source, the last usable instant being in_frames - 1.
    return ((in_frames - 1) << 32) / step + 1;
}

// 64-tap-and-up dot product against two neighbouring phases at once. Two
// independent accumulator pairs so the multiplies of one phase fill the latency
// of the other; the taps are aligned, the signal window never is.
static void dsp_dot_two_phases(const f32 *x, const f32 *h0, const f32 *h1, u32 taps, f32 *out0,
                               f32 *out1) {
    __m128 a0 = _mm_setzero_ps();
    __m128 a1 = _mm_setzero_ps();
    __m128 b0 = _mm_setzero_ps();
    __m128 b1 = _mm_setzero_ps();
    for (u32 k = 0; k < taps; k += 8) {
        __m128 xa = _mm_loadu_ps(x + k);
        __m128 xb = _mm_loadu_ps(x + k + 4);
        a0 = _mm_add_ps(a0, _mm_mul_ps(xa, _mm_load_ps(h0 + k)));
        b0 = _mm_add_ps(b0, _mm_mul_ps(xb, _mm_load_ps(h0 + k + 4)));
        a1 = _mm_add_ps(a1, _mm_mul_ps(xa, _mm_load_ps(h1 + k)));
        b1 = _mm_add_ps(b1, _mm_mul_ps(xb, _mm_load_ps(h1 + k + 4)));
    }
    a0 = _mm_add_ps(a0, b0);
    a1 = _mm_add_ps(a1, b1);
    // Horizontal sum of both accumulators, SSE2 only (no _mm_hadd_ps).
    __m128 s0 = _mm_add_ps(a0, _mm_shuffle_ps(a0, a0, _MM_SHUFFLE(1, 0, 3, 2)));
    s0 = _mm_add_ss(s0, _mm_shuffle_ps(s0, s0, _MM_SHUFFLE(1, 1, 1, 1)));
    __m128 s1 = _mm_add_ps(a1, _mm_shuffle_ps(a1, a1, _MM_SHUFFLE(1, 0, 3, 2)));
    s1 = _mm_add_ss(s1, _mm_shuffle_ps(s1, s1, _MM_SHUFFLE(1, 1, 1, 1)));
    *out0 = _mm_cvtss_f32(s0);
    *out1 = _mm_cvtss_f32(s1);
}

// `in` == 0 feeds silence, which is how the tail is drained at EOF.
static u64 dsp_resampler_feed(DspResampler *r, const f32 *const *in, u64 in_frames,
                              f32 *const *out, u64 out_cap) {
    u32 taps = r->taps_per_phase;
    u32 history = r->history;
    i32 center = (i32)(taps / 2) - 1;

    for (u32 c = 0; c < r->channels; c += 1) {
        if (in) {
            mem_copy(r->work[c] + history, in[c], in_frames * sizeof(f32));
        } else {
            mem_zero(r->work[c] + history, in_frames * sizeof(f32));
        }
    }

    // We may emit while the window [pos - center, pos - center + taps) still
    // fits inside history + in_frames.
    i64 limit = (i64)in_frames + center - (i64)taps;
    u64 written = 0;
    u64 pos = r->pos;
    while (written < out_cap) {
        i64 integer = (i64)(pos >> 32) - (i64)r->consumed;
        if (integer > limit) { break; }
        if (r->out_limit != 0 && r->produced + written >= r->out_limit) { break; }

        u32 frac = (u32)pos;
        u32 phase = frac >> 23;                 // 512 phases: the top 9 bits
        f32 weight = (f32)(frac & 0x7FFFFFu) * (1.0f / 8388608.0f);
        const f32 *h0 = r->taps + (u64)phase * taps;
        const f32 *h1 = h0 + taps;
        u64 j0 = (u64)(integer - center + (i64)history);

        for (u32 c = 0; c < r->channels; c += 1) {
            f32 y0, y1;
            dsp_dot_two_phases(r->work[c] + j0, h0, h1, taps, &y0, &y1);
            out[c][written] = y0 + weight * (y1 - y0);
        }
        written += 1;
        pos += r->step;
    }
    r->pos = pos;
    r->produced += written;

    // Keep the last `history` input samples for the next block.
    for (u32 c = 0; c < r->channels; c += 1) {
        mem_move(r->work[c], r->work[c] + in_frames, (u64)history * sizeof(f32));
    }
    r->consumed += in_frames;
    return written;
}

u64 dsp_resampler_process(DspResampler *r, const f32 *const *in, u64 in_frames, f32 *const *out,
                          u64 out_cap) {
    if (r->passthrough) {
        u64 n = Min(in_frames, out_cap);
        for (u32 c = 0; c < r->channels; c += 1) { mem_copy(out[c], in[c], n * sizeof(f32)); }
        r->produced += n;
        return n;
    }
    Assert(in_frames + r->history <= r->work_cap);
    return dsp_resampler_feed(r, in, in_frames, out, out_cap);
}

u64 dsp_resampler_flush(DspResampler *r, f32 *const *out, u64 out_cap) {
    if (r->passthrough) { return 0; }
    return dsp_resampler_feed(r, 0, r->taps_per_phase, out, out_cap);
}

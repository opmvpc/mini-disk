#include "dsp_dither.h"

#include <emmintrin.h>

#include "dsp_math.h"

#define DSP_S16_SCALE 32768.0f

void dsp_dither_init(DspDither *d, b32 enabled, b32 noise_shaping, u32 seed) {
    StructZero(d);
    d->enabled = enabled;
    d->noise_shaping = noise_shaping;
    // Two streams from one seed; the odd multiplier keeps them apart.
    d->rng[0] = seed * 2654435761u + 1u;
    d->rng[1] = seed * 40503u + 0x9E3779B9u;
}

// Numerical Recipes LCG: one multiply-add, full 32-bit period, and only the top
// bits are used - which are the ones an LCG gets right.
static md_inline u32 dsp_lcg_next(u32 *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static md_inline i16 dsp_clamp_s16(f32 v) {
    // cvtss2si rounds to nearest-even in one instruction; the bypass path is
    // unaffected because the values it quantises are already whole numbers.
    i32 q = _mm_cvt_ss2si(_mm_set_ss(v));
    if (q > 32767) { q = 32767; }
    if (q < -32768) { q = -32768; }
    return (i16)q;
}

void dsp_quantize_s16(DspDither *d, const f32 *const *planar, u32 channels, u64 frames, i16 *out) {
    if (!d->enabled) {
        for (u64 i = 0; i < frames; i += 1) {
            for (u32 c = 0; c < channels; c += 1) {
                out[i * channels + c] = dsp_clamp_s16(planar[c][i] * DSP_S16_SCALE);
            }
        }
        return;
    }

    for (u32 c = 0; c < channels; c += 1) {
        u32 rng = d->rng[c];
        f32 error = d->error[c];
        const f32 *src = planar[c];
        for (u64 i = 0; i < frames; i += 1) {
            f32 x = src[i] * DSP_S16_SCALE;
            // First-order shaping: subtracting the previous error gives the
            // quantisation noise a (1 - z^-1) tilt, which pushes it above
            // 10 kHz where the ear cares far less.
            f32 shaped = d->noise_shaping ? (x - error) : x;
            // TPDF = two independent uniforms: mean 0, variance 1/6 LSB^2, and
            // - the reason it is the standard - a noise floor whose level no
            // longer depends on the signal.
            f32 r1 = (f32)(dsp_lcg_next(&rng) >> 8) * (1.0f / 16777216.0f) - 0.5f;
            f32 r2 = (f32)(dsp_lcg_next(&rng) >> 8) * (1.0f / 16777216.0f) - 0.5f;
            f32 v = shaped + r1 + r2;
            i16 q = dsp_clamp_s16(v);
            error = (f32)q - v;
            out[i * channels + c] = q;
        }
        d->rng[c] = rng;
        d->error[c] = d->noise_shaping ? error : 0.0f;
    }
}

void dsp_s16_to_be(u8 *dst, const i16 *src, u64 samples) {
    u64 i = 0;
    for (; i + 8 <= samples; i += 8) {
        __m128i v = _mm_loadu_si128((const __m128i *)(src + i));
        // Swap the two bytes of each 16-bit lane: SSE2 has no byte shuffle.
        __m128i swapped = _mm_or_si128(_mm_slli_epi16(v, 8), _mm_srli_epi16(v, 8));
        _mm_storeu_si128((__m128i *)(dst + i * 2), swapped);
    }
    for (; i < samples; i += 1) {
        u16 v = (u16)src[i];
        dst[i * 2 + 0] = (u8)(v >> 8);
        dst[i * 2 + 1] = (u8)(v & 0xFFu);
    }
}

// --- SP framer -------------------------------------------------------------
void dsp_sp_writer_init(DspSpWriter *w, DspWriteFunc *write, void *user) {
    StructZero(w);
    w->write = write;
    w->user = user;
}

b32 dsp_sp_writer_push(DspSpWriter *w, const i16 *interleaved, u64 samples) {
    if (w->failed) { return 0; }
    u64 done = 0;
    while (done < samples) {
        u64 room = (DSP_SP_FRAME_BYTES - w->fill) / 2;
        u64 take = Min(room, samples - done);
        dsp_s16_to_be(w->frame + w->fill, interleaved + done, take);
        w->fill += (u32)(take * 2);
        w->bytes_audio += take * 2;
        done += take;
        if (w->fill == DSP_SP_FRAME_BYTES) {
            if (!w->write(w->user, w->frame, DSP_SP_FRAME_BYTES)) {
                w->failed = 1;
                return 0;
            }
            w->frames_written += 1;
            w->fill = 0;
        }
    }
    return 1;
}

b32 dsp_sp_writer_finish(DspSpWriter *w) {
    if (w->failed) { return 0; }
    if (w->fill == 0) { return 1; }
    mem_zero(w->frame + w->fill, DSP_SP_FRAME_BYTES - w->fill);
    if (!w->write(w->user, w->frame, DSP_SP_FRAME_BYTES)) {
        w->failed = 1;
        return 0;
    }
    w->frames_written += 1;
    w->fill = 0;
    return 1;
}

// --- WAV preview -----------------------------------------------------------
static void dsp_put_u32_le(u8 *p, u32 v) {
    p[0] = (u8)(v & 0xFFu);
    p[1] = (u8)((v >> 8) & 0xFFu);
    p[2] = (u8)((v >> 16) & 0xFFu);
    p[3] = (u8)((v >> 24) & 0xFFu);
}

static void dsp_put_u16_le(u8 *p, u16 v) {
    p[0] = (u8)(v & 0xFFu);
    p[1] = (u8)((v >> 8) & 0xFFu);
}

static void dsp_put_tag(u8 *p, const char *tag) {
    p[0] = (u8)tag[0];
    p[1] = (u8)tag[1];
    p[2] = (u8)tag[2];
    p[3] = (u8)tag[3];
}

void dsp_wav_header(u8 out[DSP_WAV_HEADER_BYTES], u32 sample_rate, u32 channels, u64 data_bytes) {
    u32 block_align = channels * 2u;
    dsp_put_tag(out + 0, "RIFF");
    dsp_put_u32_le(out + 4, (u32)(36 + data_bytes));
    dsp_put_tag(out + 8, "WAVE");
    dsp_put_tag(out + 12, "fmt ");
    dsp_put_u32_le(out + 16, 16);
    dsp_put_u16_le(out + 20, 1);  // WAVE_FORMAT_PCM
    dsp_put_u16_le(out + 22, (u16)channels);
    dsp_put_u32_le(out + 24, sample_rate);
    dsp_put_u32_le(out + 28, sample_rate * block_align);
    dsp_put_u16_le(out + 32, (u16)block_align);
    dsp_put_u16_le(out + 34, 16);
    dsp_put_tag(out + 36, "data");
    dsp_put_u32_le(out + 40, (u32)data_bytes);
}

// dsp_dither.h - the last two steps of the chain: TPDF dither to s16 and the
// two wire formats we write (ADR-007 steps 6 and 7).
//
// Scale: 32768, not 32767, with a clamp. That is what makes the bypass path
// bit-exact - a sample that came in as s/32768 goes back out as exactly s for
// every s in [-32768, 32767] - and the one value it costs us (+32768 folded to
// +32767) is a sample that was already at digital full scale.
#ifndef DSP_DITHER_H
#define DSP_DITHER_H

#include "../../base/base.h"

#define DSP_SP_FRAME_BYTES 2048u  // NetMD SP wire frame (research/01 s4.7)
#define DSP_WAV_HEADER_BYTES 44u

typedef struct DspDither {
    b32 enabled;
    b32 noise_shaping;
    u32 rng[2];    // one LCG per channel, never shared between threads
    f32 error[2];  // first-order noise shaping feedback
} DspDither;

void dsp_dither_init(DspDither *d, b32 enabled, b32 noise_shaping, u32 seed);

// Planar f32 in, interleaved s16 out (host order). `channels` is 1 or 2.
void dsp_quantize_s16(DspDither *d, const f32 *const *planar, u32 channels, u64 frames, i16 *out);

// s16 host order -> s16 big-endian, in place or into another buffer.
void dsp_s16_to_be(u8 *dst, const i16 *src, u64 samples);

// --- SP framer -------------------------------------------------------------
// The wire wants whole 2048-byte frames of big-endian interleaved stereo; the
// last one is zero padded. The writer owns one frame of staging and calls back
// whenever it is full, so nothing downstream ever sees a partial frame.
typedef b32 DspWriteFunc(void *user, const u8 *bytes, u64 size);

typedef struct DspSpWriter {
    u8 frame[DSP_SP_FRAME_BYTES];
    u32 fill;
    u64 frames_written;  // 2048-byte frames
    u64 bytes_audio;     // real audio bytes, padding excluded
    DspWriteFunc *write;
    void *user;
    b32 failed;
} DspSpWriter;

void dsp_sp_writer_init(DspSpWriter *w, DspWriteFunc *write, void *user);
b32  dsp_sp_writer_push(DspSpWriter *w, const i16 *interleaved, u64 samples);
b32  dsp_sp_writer_finish(DspSpWriter *w);  // zero-pads and flushes the last frame

// --- WAV preview -----------------------------------------------------------
// 44 bytes written by hand: canonical RIFF/WAVE, PCM s16 little-endian.
void dsp_wav_header(u8 out[DSP_WAV_HEADER_BYTES], u32 sample_rate, u32 channels, u64 data_bytes);

#endif  // DSP_DITHER_H

// dsp_resample.h - windowed-sinc polyphase resampler, any input rate -> 44100 Hz.
//
// Shape (research/03 s6.6, ADR-007 step 2): a Kaiser windowed sinc prototype
// sampled on DSP_RESAMPLE_PHASES phases plus a guard row, linear interpolation
// between two neighbouring phases, an SSE2 dot product per output sample, the
// whole table built once into an arena at init. Nothing allocates per block.
//
// The filter is *centred*: output sample n reads the input around time
// n * in_rate / out_rate, so an impulse at input index i comes back out at
// output index i * out_rate / in_rate. There is no latency to compensate for
// downstream - the compensation is inside, in the history the resampler keeps.
#ifndef DSP_RESAMPLE_H
#define DSP_RESAMPLE_H

#include "../../base/base.h"
#include "../../base/base_arena.h"

#define DSP_OUT_RATE          44100u
#define DSP_RESAMPLE_PHASES   512u
#define DSP_RESAMPLE_TAPS     64u    // taps per phase at a ratio of 1:1 or upward
#define DSP_RESAMPLE_KAISER_B 9.0    // beta: ~90 dB of stopband rejection
// Kaiser transition width, normalised to the tap spacing: D/N with
// D = (A - 7.95) / 14.36 for A = 90.4 dB. Half of it is the margin we leave
// between the cutoff and the lower of the two Nyquist frequencies.
#define DSP_RESAMPLE_TRANSITION_D 5.74

#define DSP_MAX_CHANNELS 2u

typedef struct DspResampler {
    f32 *taps;            // (phases + 1) rows of taps_per_phase, 16-byte aligned
    u32 phases;
    u32 taps_per_phase;   // DSP_RESAMPLE_TAPS * ceil(in_rate / out_rate)
    u32 in_rate;
    u32 out_rate;
    u32 channels;
    b32 passthrough;      // in_rate == out_rate: the fast path of research/03

    u64 step;             // 32.32 input samples per output sample
    u64 pos;              // 32.32 input time of the next output sample
    u64 consumed;         // input index of work[history]
    u64 produced;
    u64 out_limit;        // 0: unbounded

    f32 *work[DSP_MAX_CHANNELS];  // history + one block of new input, per channel
    u32  history;                 // taps_per_phase - 1
    u32  work_cap;                // history + max_in_frames
} DspResampler;

// `max_in_frames` is the largest block the caller will ever hand over;
// `total_in_frames` caps the number of output frames so the tail flush cannot
// invent samples past the end of the source (0 when the length is unknown).
void dsp_resampler_init(DspResampler *r, Arena *arena, u32 in_rate, u32 out_rate, u32 channels,
                        u32 max_in_frames, u64 total_in_frames);
void dsp_resampler_reset(DspResampler *r);

// Output frames a call with `in_frames` of input can produce, worst case.
u64 dsp_resampler_out_capacity(const DspResampler *r, u64 in_frames);
// Output frames a whole source of `in_frames` input frames yields, exactly.
u64 dsp_resampler_expected_out(u32 in_rate, u32 out_rate, u64 in_frames);

// Both `in` and `out` are planar, one pointer per channel. Returns frames written.
u64 dsp_resampler_process(DspResampler *r, const f32 *const *in, u64 in_frames, f32 *const *out,
                          u64 out_cap);
// Pushes silence through the tail of the filter to drain it. Call once at EOF.
u64 dsp_resampler_flush(DspResampler *r, f32 *const *out, u64 out_cap);

#endif  // DSP_RESAMPLE_H

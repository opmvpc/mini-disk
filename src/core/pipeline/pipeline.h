// pipeline.h - the per-track chain of ADR-007, one job per track.
//
//   decode -> resample -> downmix -> trim -> gain -> fade -> dither -> output
//
// The source is a function-pointer struct so this file knows nothing about
// codecs: T-040's decoder implements it, the tests implement it with a
// generator, and T-043 binds the writer to the cache file.
//
// Why two passes rather than buffering the track:
//   * R128 needs the whole track before it can hand back one gain, and the trim
//     needs the last loud frame before it can hand back the tail.
//   * A four-minute stereo track in f32 at 44.1 kHz is 42 MB. Seven workers
//     with two tracks each would hold 590 MB resident, on a machine we have
//     been sizing for 100 000-track libraries.
//   * The measurement pass costs a second decode, which runs above 100x real
//     time - about 40 ms on a four-minute track, against 42 MB of footprint.
// So: measure, then render. A source that cannot rewind is decoded once with
// normalisation and trim disabled, which is the honest fallback.
#ifndef PIPELINE_H
#define PIPELINE_H

#include "../../base/base.h"
#include "../../base/base_arena.h"
#include "../dsp/dsp_dither.h"
#include "../dsp/dsp_edit.h"
#include "../dsp/dsp_loudness.h"
#include "../dsp/dsp_resample.h"
#include "../codecs/codec.h"

#define PIPELINE_BLOCK_FRAMES 4096u  // ~93 ms, 32 KB of stereo f32: L2 resident

typedef struct PipelineSource {
    // Fills `planar` with up to `max_frames` frames; returns how many, 0 at EOF.
    u64 (*read)(void *user, f32 *const *planar, u64 max_frames);
    b32 (*rewind)(void *user);  // 0 when the source cannot be replayed
    void *user;
    u32 sample_rate;
    u32 channels;      // 1 or 2
    u64 total_frames;  // 0 when unknown
} PipelineSource;

typedef enum PipelineFormat {
    PIPELINE_FORMAT_SP_BE = 0,  // 2048-byte frames, s16 big-endian interleaved
    PIPELINE_FORMAT_WAV,        // 44.1 kHz s16 little-endian, 44-byte header
} PipelineFormat;

typedef struct PipelineConfig {
    b32 mono;
    b32 normalize;       // EBU R128 toward `target_lufs`
    f32 target_lufs;     // DSP_R128_TARGET_LUFS by default
    f32 ceiling_dbtp;    // DSP_R128_CEILING_DBTP by default
    f32 fixed_gain_db;   // applied when `normalize` is off
    b32 trim;
    f32 fade_in_s;
    f32 fade_out_s;
    f32 gap_s;           // silence appended after the track; negative cuts audio
    b32 dither;
    b32 noise_shaping;
    PipelineFormat format;
} PipelineConfig;

void pipeline_config_defaults(PipelineConfig *config);

typedef enum PipelineStatus {
    PIPELINE_OK = 0,
    PIPELINE_CANCELLED,
    PIPELINE_WRITE_FAILED,
} PipelineStatus;

typedef struct PipelineResult {
    PipelineStatus status;
    f32 measured_lufs;
    f32 measured_dbtp;
    f32 applied_gain_db;
    u64 out_frames;    // frames actually written, gap included
    u64 out_bytes;     // payload bytes handed to the writer, padding included
    u64 sp_frames;     // 2048-byte frames, SP output only
    b32 bit_perfect;   // 44.1 kHz, no resampling, no gain, no dither
} PipelineResult;

typedef struct PipelineTask {
    PipelineSource source;
    PipelineConfig config;
    DspWriteFunc *write;
    void *write_user;
    Arena *arena;                  // one per task: a job never shares an arena
    volatile u32 *cancel;          // checked once per block
    volatile long long *progress;  // output frames done so far, atomic
    PipelineResult result;
} PipelineTask;

// T-040 and T-041 were written in parallel, so the decoder never learned what a
// PipelineSource is. This is the adapter, and it is three function pointers:
// codec_read_f32_planar already hands back planar f32 in blocks of at most
// CODEC_BLOCK_FRAMES, which is PIPELINE_BLOCK_FRAMES. 0 when the file has more
// than two channels - the pipeline's downmix is stereo to mono and nothing
// wider, and silently dropping channels would be the wrong kind of quiet.
b32 pipeline_source_from_decoder(PipelineSource *out, Decoder *decoder);

void pipeline_run(PipelineTask *task);
// One job per track through base_jobs; returns when every track is done.
void pipeline_run_many(PipelineTask *tasks, u32 count);

#endif  // PIPELINE_H

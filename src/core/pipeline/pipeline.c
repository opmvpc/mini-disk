#include "pipeline.h"

#include "../../base/base_jobs.h"
#include "../dsp/dsp_math.h"

void pipeline_config_defaults(PipelineConfig *config) {
    StructZero(config);
    config->target_lufs = DSP_R128_TARGET_LUFS;
    config->ceiling_dbtp = DSP_R128_CEILING_DBTP;
    config->format = PIPELINE_FORMAT_SP_BE;
}

typedef struct PipelineRun {
    PipelineTask *task;
    u32 out_channels;
    u64 resample_cap;

    f32 *in_buf[DSP_MAX_CHANNELS];
    f32 *rs_buf[DSP_MAX_CHANNELS];
    f32 *stage[DSP_MAX_CHANNELS];  // the planar view handed to each stage
    i16 *quant;

    DspResampler resampler;
    DspR128 r128;
    DspTrimScan trim;
    DspDither dither;
    DspSpWriter sp;
    DspFade fade;

    u64 total_out_frames;  // after resampling, before trim and gap
} PipelineRun;

// --- the shared front half: decode, resample, downmix -----------------------
typedef void PipelineBlockFn(PipelineRun *run, f32 *const *planar, u64 frames, u64 stream_pos);

static b32 pipeline_cancelled(PipelineRun *run) {
    return run->task->cancel && os_atomic_load_u32(run->task->cancel) != 0;
}

static b32 pipeline_stream(PipelineRun *run, PipelineBlockFn *fn) {
    PipelineTask *task = run->task;
    PipelineSource *source = &task->source;
    dsp_resampler_reset(&run->resampler);

    u64 stream_pos = 0;
    b32 eof = 0;
    while (!eof) {
        if (pipeline_cancelled(run)) { return 0; }

        u64 in_frames = source->read(source->user, run->in_buf, PIPELINE_BLOCK_FRAMES);
        u64 out_frames;
        if (in_frames == 0) {
            eof = 1;
            out_frames = dsp_resampler_flush(&run->resampler, run->rs_buf, run->resample_cap);
        } else {
            out_frames = dsp_resampler_process(&run->resampler, (const f32 *const *)run->in_buf,
                                               in_frames, run->rs_buf, run->resample_cap);
        }
        if (out_frames == 0) { continue; }

        u32 channels = source->channels;
        if (task->config.mono && channels == 2) {
            // In place: the mono result lands in channel 0, which is exactly
            // the planar view the rest of the chain then sees.
            dsp_downmix_mono(run->rs_buf[0], run->rs_buf[0], run->rs_buf[1], out_frames);
        }
        fn(run, run->rs_buf, out_frames, stream_pos);
        stream_pos += out_frames;
    }
    run->total_out_frames = stream_pos;
    return 1;
}

// --- pass one: measure ------------------------------------------------------
static void pipeline_measure_block(PipelineRun *run, f32 *const *planar, u64 frames,
                                   u64 stream_pos) {
    Unused(stream_pos);
    if (run->task->config.normalize) {
        dsp_r128_feed(&run->r128, (const f32 *const *)planar, frames);
    }
    if (run->task->config.trim) {
        dsp_trim_scan_feed(&run->trim, (const f32 *const *)planar, run->out_channels, frames);
    }
}

// --- pass two: render -------------------------------------------------------
typedef struct PipelineRender {
    u64 keep_begin;
    u64 keep_end;
    f32 gain;
    u64 emitted;
} PipelineRender;

static b32 pipeline_emit(PipelineRun *run, f32 *const *planar, u64 frames) {
    PipelineTask *task = run->task;
    u32 channels = run->out_channels;
    dsp_quantize_s16(&run->dither, (const f32 *const *)planar, channels, frames, run->quant);
    u64 samples = frames * channels;
    if (task->config.format == PIPELINE_FORMAT_SP_BE) {
        if (!dsp_sp_writer_push(&run->sp, run->quant, samples)) { return 0; }
    } else {
        // WAV is little-endian, which is the host order: no swap, no copy.
        if (!task->write(task->write_user, (const u8 *)run->quant, samples * 2)) { return 0; }
        run->task->result.out_bytes += samples * 2;
    }
    run->task->result.out_frames += frames;
    if (task->progress) { os_atomic_add_u64(task->progress, (u64)frames); }
    return 1;
}

// The render pass is written straight rather than through the block callback:
// it has to slice the block against the trim window, and a callback that can
// fail needs a way out of the loop.
static b32 pipeline_render(PipelineRun *run, PipelineRender *render) {
    PipelineTask *task = run->task;
    PipelineSource *source = &task->source;
    dsp_resampler_reset(&run->resampler);

    u64 stream_pos = 0;
    b32 eof = 0;
    while (!eof) {
        if (pipeline_cancelled(run)) { return 0; }

        u64 in_frames = source->read(source->user, run->in_buf, PIPELINE_BLOCK_FRAMES);
        u64 out_frames;
        if (in_frames == 0) {
            eof = 1;
            out_frames = dsp_resampler_flush(&run->resampler, run->rs_buf, run->resample_cap);
        } else {
            out_frames = dsp_resampler_process(&run->resampler, (const f32 *const *)run->in_buf,
                                               in_frames, run->rs_buf, run->resample_cap);
        }
        if (out_frames == 0) { continue; }

        if (task->config.mono && source->channels == 2) {
            dsp_downmix_mono(run->rs_buf[0], run->rs_buf[0], run->rs_buf[1], out_frames);
        }

        // Clip this block to the kept window.
        u64 block_begin = stream_pos;
        u64 block_end = stream_pos + out_frames;
        stream_pos = block_end;
        u64 from = Max(block_begin, render->keep_begin);
        u64 to = Min(block_end, render->keep_end);
        if (from >= to) { continue; }

        u64 offset = from - block_begin;
        u64 count = to - from;
        for (u32 c = 0; c < run->out_channels; c += 1) { run->stage[c] = run->rs_buf[c] + offset; }

        dsp_apply_gain(run->stage, run->out_channels, count, render->gain);
        dsp_fade_apply(&run->fade, run->stage, run->out_channels, count, render->emitted);
        if (!pipeline_emit(run, run->stage, count)) { return 0; }
        render->emitted += count;
    }
    return 1;
}

static b32 pipeline_emit_gap(PipelineRun *run, u64 gap_frames) {
    if (gap_frames == 0) { return 1; }
    u32 channels = run->out_channels;
    for (u32 c = 0; c < channels; c += 1) {
        mem_zero(run->rs_buf[c], PIPELINE_BLOCK_FRAMES * sizeof(f32));
        run->stage[c] = run->rs_buf[c];
    }
    // Dither over digital silence would print noise into the gap, which is the
    // one place a MiniDisc listener would hear it: quantise the gap clean.
    b32 saved = run->dither.enabled;
    run->dither.enabled = 0;
    b32 ok = 1;
    u64 left = gap_frames;
    while (left != 0 && ok) {
        u64 chunk = Min(left, (u64)PIPELINE_BLOCK_FRAMES);
        if (pipeline_cancelled(run)) {
            ok = 0;
            break;
        }
        ok = pipeline_emit(run, run->stage, chunk);
        left -= chunk;
    }
    run->dither.enabled = saved;
    return ok;
}

void pipeline_run(PipelineTask *task) {
    PipelineConfig *config = &task->config;
    PipelineSource *source = &task->source;
    StructZero(&task->result);

    ArenaTemp temp = arena_temp_begin(task->arena);
    PipelineRun run;
    StructZero(&run);
    run.task = task;
    run.out_channels = (config->mono && source->channels == 2) ? 1u : source->channels;

    // A source that cannot be replayed gets neither normalisation nor trim:
    // both need a first pass, and inventing a gain from a partial track would
    // be worse than not normalising at all.
    b32 replayable = (source->rewind != 0);
    if (!replayable) {
        config->normalize = 0;
        config->trim = 0;
    }

    dsp_resampler_init(&run.resampler, task->arena, source->sample_rate, DSP_OUT_RATE,
                       source->channels, PIPELINE_BLOCK_FRAMES, source->total_frames);
    run.resample_cap = dsp_resampler_out_capacity(&run.resampler, PIPELINE_BLOCK_FRAMES) +
                       run.resampler.taps_per_phase;

    for (u32 c = 0; c < DSP_MAX_CHANNELS; c += 1) {
        run.in_buf[c] = push_array_zero(task->arena, f32, PIPELINE_BLOCK_FRAMES);
        run.rs_buf[c] = push_array_zero(task->arena, f32, run.resample_cap);
        run.stage[c] = run.rs_buf[c];
    }
    run.quant = push_array(task->arena, i16, run.resample_cap * DSP_MAX_CHANNELS);

    u64 expected_out =
        dsp_resampler_expected_out(source->sample_rate, DSP_OUT_RATE, source->total_frames);

    // --- pass one -----------------------------------------------------------
    f32 gain_db = config->fixed_gain_db;
    u64 keep_begin = 0;
    u64 keep_end = U64_MAX;
    if (config->normalize || config->trim) {
        if (config->normalize) {
            dsp_r128_init(&run.r128, task->arena, DSP_OUT_RATE, run.out_channels,
                          (expected_out != 0) ? expected_out : (u64)DSP_OUT_RATE * 3600u);
            dsp_r128_reset(&run.r128);
        }
        if (config->trim) {
            dsp_trim_scan_init(&run.trim, DSP_OUT_RATE, DSP_TRIM_THRESHOLD_DB,
                               DSP_TRIM_HYSTERESIS_MS);
        }
        if (!pipeline_stream(&run, pipeline_measure_block)) {
            task->result.status = PIPELINE_CANCELLED;
            arena_temp_end(temp);
            return;
        }
        if (!source->rewind(source->user)) {
            task->result.status = PIPELINE_WRITE_FAILED;
            arena_temp_end(temp);
            return;
        }
        if (config->normalize) {
            task->result.measured_lufs = dsp_r128_integrated_lufs(&run.r128);
            task->result.measured_dbtp = dsp_r128_true_peak_dbtp(&run.r128);
            gain_db = dsp_r128_gain_db(&run.r128, config->target_lufs, config->ceiling_dbtp);
        }
        if (config->trim) {
            dsp_trim_scan_result(&run.trim, run.total_out_frames, &keep_begin, &keep_end);
        }
    } else {
        task->result.measured_lufs = DSP_DB_SILENCE;
        task->result.measured_dbtp = DSP_DB_SILENCE;
    }
    if (keep_end == U64_MAX) {
        keep_end = (expected_out != 0) ? expected_out : U64_MAX;
    }

    // A negative gap eats audio off the tail; a positive one appends silence.
    i64 gap_frames = (i64)dsp_round_f64((f64)config->gap_s * (f64)DSP_OUT_RATE);
    u64 gap_silence = 0;
    if (gap_frames > 0) {
        gap_silence = (u64)gap_frames;
    } else if (gap_frames < 0 && keep_end != U64_MAX) {
        u64 cut = (u64)(-gap_frames);
        keep_end = (keep_end > keep_begin + cut) ? (keep_end - cut) : keep_begin;
    }

    task->result.applied_gain_db = gain_db;
    task->result.bit_perfect = (source->sample_rate == DSP_OUT_RATE) && (gain_db == 0.0f) &&
                               !config->dither && !config->trim && config->fade_in_s == 0.0f &&
                               config->fade_out_s == 0.0f;

    // --- pass two -----------------------------------------------------------
    PipelineRender render;
    StructZero(&render);
    render.keep_begin = keep_begin;
    render.keep_end = keep_end;
    render.gain = dsp_amp_from_db(gain_db);

    // A fade-out needs the length; an unknown-length source only gets the fade-in.
    b32 length_known = (keep_end != U64_MAX);
    u64 body = (length_known && keep_end > keep_begin) ? (keep_end - keep_begin) : 0;
    run.fade.in_length = (u64)dsp_round_f64((f64)config->fade_in_s * (f64)DSP_OUT_RATE);
    run.fade.out_length =
        length_known ? (u64)dsp_round_f64((f64)config->fade_out_s * (f64)DSP_OUT_RATE) : 0;
    if (length_known) {
        if (run.fade.out_length > body) { run.fade.out_length = body; }
        if (run.fade.in_length > body) { run.fade.in_length = body; }
    }
    run.fade.out_start = (body > run.fade.out_length) ? (body - run.fade.out_length) : 0;

    dsp_dither_init(&run.dither, config->dither, config->noise_shaping,
                    (u32)(source->total_frames + 1u));
    dsp_sp_writer_init(&run.sp, task->write, task->write_user);

    if (config->format == PIPELINE_FORMAT_WAV) {
        u64 data_bytes = (body + gap_silence) * run.out_channels * 2u;
        u8 header[DSP_WAV_HEADER_BYTES];
        dsp_wav_header(header, DSP_OUT_RATE, run.out_channels, data_bytes);
        if (!task->write(task->write_user, header, DSP_WAV_HEADER_BYTES)) {
            task->result.status = PIPELINE_WRITE_FAILED;
            arena_temp_end(temp);
            return;
        }
        task->result.out_bytes += DSP_WAV_HEADER_BYTES;
    }

    b32 ok = pipeline_render(&run, &render);
    if (ok) { ok = pipeline_emit_gap(&run, gap_silence); }
    if (ok && config->format == PIPELINE_FORMAT_SP_BE) {
        ok = dsp_sp_writer_finish(&run.sp);
        task->result.sp_frames = run.sp.frames_written;
        task->result.out_bytes = run.sp.frames_written * DSP_SP_FRAME_BYTES;
    }

    if (!ok) {
        task->result.status = pipeline_cancelled(&run) ? PIPELINE_CANCELLED : PIPELINE_WRITE_FAILED;
    }
    arena_temp_end(temp);
}

// --- one job per track ------------------------------------------------------
static void pipeline_job(void *data, u64 begin, u64 end) {
    PipelineTask *tasks = (PipelineTask *)data;
    for (u64 i = begin; i < end; i += 1) { pipeline_run(&tasks[i]); }
}

void pipeline_run_many(PipelineTask *tasks, u32 count) {
    JobCounter counter;
    counter.pending = 0;
    // One chunk per track: a track is already a big, uneven unit of work, and
    // chunking below that would make two tracks share an arena.
    for (u32 i = 0; i < count; i += 1) { jobs_push(&counter, pipeline_job, &tasks[i]); }
    jobs_wait(&counter);
}

// --- the decoder adapter (T-042) --------------------------------------------

static u64 pipeline_decoder_read(void *user, f32 *const *planar, u64 max_frames) {
    Decoder *decoder = (Decoder *)user;
    u32 want = (u32)Min(max_frames, (u64)CODEC_BLOCK_FRAMES);
    return codec_read_f32_planar(decoder, planar, want);
}

static b32 pipeline_decoder_rewind(void *user) { return codec_seek((Decoder *)user, 0); }

b32 pipeline_source_from_decoder(PipelineSource *out, Decoder *decoder) {
    if (decoder->info.channels == 0 || decoder->info.channels > 2) { return 0; }
    StructZero(out);
    out->read = pipeline_decoder_read;
    out->rewind = pipeline_decoder_rewind;
    out->user = decoder;
    out->sample_rate = decoder->info.sample_rate;
    out->channels = decoder->info.channels;
    out->total_frames = decoder->info.total_frames;
    return 1;
}

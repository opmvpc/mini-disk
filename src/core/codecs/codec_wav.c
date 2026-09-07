// codec_wav.c - RIFF/WAVE through dr_wav, AIFF and AIFF-C by hand.
//
// dr_wav carries every WAVE oddity worth having (8/16/24/32 bit PCM, IEEE
// float, extensible, W64, RF64) and it is already vendored. AIFF is a different
// container with two chunks that matter, so it is fifty lines here instead of
// another dependency - and it is where the big endian samples of ADR-007 step 7
// get their first exercise.
#include "codec.h"

#include "../../third_party.h"

// --- WAVE ------------------------------------------------------------------

typedef struct CodecWav {
    drwav wav;
    f32 *interleaved;
} CodecWav;

static size_t codec_wav_on_read(void *user, void *out, size_t bytes) {
    return (size_t)codec_source_read((CodecSource *)user, out, (u64)bytes);
}

static drwav_bool32 codec_wav_on_seek(void *user, int offset, drwav_seek_origin origin) {
    CodecSource *source = (CodecSource *)user;
    i64 target = offset;
    if (origin == DRWAV_SEEK_CUR) { target += (i64)source->pos; }
    if (origin == DRWAV_SEEK_END) { target += (i64)source->size; }
    if (target < 0) { return 0; }
    return codec_source_seek(source, (u64)target);
}

static drwav_bool32 codec_wav_on_tell(void *user, drwav_int64 *cursor) {
    *cursor = (drwav_int64)((CodecSource *)user)->pos;
    return 1;
}

CodecStatus codec_wav_open(Decoder *decoder) {
    CodecWav *state = push_struct_zero(decoder->arena, CodecWav);
    tp_arena_bind(decoder->arena);
    drwav_bool32 ok = drwav_init(&state->wav, codec_wav_on_read, codec_wav_on_seek,
                                 codec_wav_on_tell, &decoder->source, 0);
    tp_arena_unbind();
    if (!ok) { return CODEC_ERR_CORRUPT; }
    if (state->wav.sampleRate == 0 || state->wav.channels == 0 ||
        state->wav.channels > CODEC_MAX_CHANNELS) {
        return CODEC_ERR_CORRUPT;
    }
    state->interleaved = push_array(decoder->arena, f32, CODEC_BLOCK_FRAMES * state->wav.channels);
    decoder->impl = state;
    decoder->info.sample_rate = state->wav.sampleRate;
    decoder->info.channels = state->wav.channels;
    decoder->info.total_frames = state->wav.totalPCMFrameCount;
    return CODEC_OK;
}

u32 codec_wav_read(Decoder *decoder, f32 *const *channels, u32 frames) {
    CodecWav *state = (CodecWav *)decoder->impl;
    tp_arena_bind(decoder->arena);
    u64 produced = drwav_read_pcm_frames_f32(&state->wav, frames, state->interleaved);
    tp_arena_unbind();
    codec_deinterleave_f32(state->interleaved, channels, decoder->info.channels, (u32)produced, 0);
    return (u32)produced;
}

b32 codec_wav_seek(Decoder *decoder, u64 frame) {
    CodecWav *state = (CodecWav *)decoder->impl;
    tp_arena_bind(decoder->arena);
    b32 ok = drwav_seek_to_pcm_frame(&state->wav, frame) != 0;
    tp_arena_unbind();
    return ok;
}

// --- AIFF / AIFF-C ---------------------------------------------------------
// Boundary code (ADR-012): every length below comes off the file, so the chunk
// walk only ever trusts what it has already checked against the file size.

typedef enum CodecAiffSample {
    CODEC_AIFF_PCM_BE,  // 'NONE', 'twos', and plain AIFF
    CODEC_AIFF_PCM_LE,  // 'sowt'
    CODEC_AIFF_F32_BE,  // 'fl32' / 'FL32'
    CODEC_AIFF_F64_BE,  // 'fl64' / 'FL64'
} CodecAiffSample;

typedef struct CodecAiff {
    u64 data_offset;   // first byte of the first frame
    u64 frame_bytes;   // channels * bytes per sample
    u32 bytes_per_sample;
    u32 format;        // CodecAiffSample
    u8 *raw;           // one block of interleaved bytes, straight off the disk
} CodecAiff;

// The 80 bit IEEE 754 extended the COMM chunk stores the sample rate in.
static u32 codec_aiff_extended_rate(const u8 *bytes) {
    i32 exponent = (i32)(((u32)(bytes[0] & 0x7F) << 8) | bytes[1]) - 16383;
    u64 mantissa = 0;
    for (u32 i = 0; i < 8; i += 1) { mantissa = (mantissa << 8) | bytes[2 + i]; }
    if (exponent < 0 || exponent > 62 || (bytes[0] & 0x80)) { return 0; }
    u64 value = mantissa >> (63 - exponent);
    if ((mantissa >> (62 - exponent)) & 1) { value += 1; }  // round to nearest
    return (value <= U32_MAX) ? (u32)value : 0;
}

static b32 codec_aiff_read_exact(CodecSource *source, u64 offset, u8 *dst, u64 size) {
    return codec_source_seek(source, offset) && codec_source_read(source, dst, size) == size;
}

CodecStatus codec_aiff_open(Decoder *decoder) {
    CodecSource *source = &decoder->source;
    u8 header[12];
    if (!codec_aiff_read_exact(source, 0, header, sizeof(header))) { return CODEC_ERR_CORRUPT; }
    b32 is_aifc = header[8] == 'A' && header[9] == 'I' && header[10] == 'F' && header[11] == 'C';

    CodecAiff *state = push_struct_zero(decoder->arena, CodecAiff);
    u32 sample_bits = 0;
    u64 total_frames = 0;
    b32 saw_comm = 0;
    b32 saw_ssnd = 0;

    u64 at = 12;
    while (at + 8 <= source->size && !(saw_comm && saw_ssnd)) {
        u8 chunk[8];
        if (!codec_aiff_read_exact(source, at, chunk, sizeof(chunk))) { break; }
        u64 chunk_size = ((u64)chunk[4] << 24) | ((u64)chunk[5] << 16) | ((u64)chunk[6] << 8) |
                         (u64)chunk[7];
        u64 body = at + 8;
        if (chunk_size > source->size - body) { break; }

        if (chunk[0] == 'C' && chunk[1] == 'O' && chunk[2] == 'M' && chunk[3] == 'M' &&
            chunk_size >= 18) {
            u8 comm[22];
            u64 want = (chunk_size >= 22) ? 22 : 18;
            if (!codec_aiff_read_exact(source, body, comm, want)) { break; }
            decoder->info.channels = ((u32)comm[0] << 8) | comm[1];
            total_frames = ((u64)comm[2] << 24) | ((u64)comm[3] << 16) | ((u64)comm[4] << 8) |
                           (u64)comm[5];
            sample_bits = ((u32)comm[6] << 8) | comm[7];
            decoder->info.sample_rate = codec_aiff_extended_rate(comm + 8);
            state->format = CODEC_AIFF_PCM_BE;
            if (is_aifc && want == 22) {
                if (comm[18] == 's' && comm[19] == 'o' && comm[20] == 'w' && comm[21] == 't') {
                    state->format = CODEC_AIFF_PCM_LE;
                } else if ((comm[18] == 'f' || comm[18] == 'F') && comm[19] == 'l' &&
                           comm[20] == '3' && comm[21] == '2') {
                    state->format = CODEC_AIFF_F32_BE;
                    sample_bits = 32;
                } else if ((comm[18] == 'f' || comm[18] == 'F') && comm[19] == 'l' &&
                           comm[20] == '6' && comm[21] == '4') {
                    state->format = CODEC_AIFF_F64_BE;
                    sample_bits = 64;
                } else if (!((comm[18] == 'N' && comm[19] == 'O') ||
                             (comm[18] == 't' && comm[19] == 'w'))) {
                    return CODEC_ERR_UNSUPPORTED;  // a real AIFF-C compressor
                }
            }
            saw_comm = 1;
        } else if (chunk[0] == 'S' && chunk[1] == 'S' && chunk[2] == 'N' && chunk[3] == 'D' &&
                   chunk_size >= 8) {
            u8 ssnd[8];
            if (!codec_aiff_read_exact(source, body, ssnd, sizeof(ssnd))) { break; }
            u64 offset = ((u64)ssnd[0] << 24) | ((u64)ssnd[1] << 16) | ((u64)ssnd[2] << 8) |
                         (u64)ssnd[3];
            if (offset > chunk_size - 8) { break; }
            state->data_offset = body + 8 + offset;
            saw_ssnd = 1;
        }
        at = body + chunk_size + (chunk_size & 1);  // chunks are word aligned
    }

    if (!saw_comm || !saw_ssnd) { return CODEC_ERR_CORRUPT; }
    if (decoder->info.sample_rate == 0 || decoder->info.channels == 0 ||
        decoder->info.channels > CODEC_MAX_CHANNELS) {
        return CODEC_ERR_CORRUPT;
    }
    if (sample_bits != 8 && sample_bits != 16 && sample_bits != 24 && sample_bits != 32 &&
        sample_bits != 64) {
        return CODEC_ERR_UNSUPPORTED;
    }
    state->bytes_per_sample = sample_bits / 8;
    state->frame_bytes = (u64)state->bytes_per_sample * decoder->info.channels;

    // The frame count in COMM is a claim; what the file actually holds wins.
    u64 available = (state->data_offset < source->size) ? source->size - state->data_offset : 0;
    u64 on_disk = available / state->frame_bytes;
    decoder->info.total_frames = (total_frames < on_disk) ? total_frames : on_disk;

    state->raw = push_array(decoder->arena, u8, CODEC_BLOCK_FRAMES * state->frame_bytes);
    decoder->impl = state;
    codec_source_seek(source, state->data_offset);
    return CODEC_OK;
}

static f32 codec_aiff_sample(const CodecAiff *state, const u8 *at) {
    switch (state->format) {
        case CODEC_AIFF_PCM_LE: {
            switch (state->bytes_per_sample) {
                case 1: return (f32)(i8)at[0] * (1.0f / 128.0f);
                case 2: return (f32)(i16)((u16)at[0] | ((u16)at[1] << 8)) * (1.0f / 32768.0f);
                case 3: {
                    i32 value = (i32)(((u32)at[0] << 8) | ((u32)at[1] << 16) | ((u32)at[2] << 24));
                    return (f32)(value >> 8) * (1.0f / 8388608.0f);
                }
                default: {
                    i32 value = (i32)((u32)at[0] | ((u32)at[1] << 8) | ((u32)at[2] << 16) |
                                      ((u32)at[3] << 24));
                    return (f32)value * (1.0f / 2147483648.0f);
                }
            }
        }
        case CODEC_AIFF_F32_BE: {
            u32 bits = ((u32)at[0] << 24) | ((u32)at[1] << 16) | ((u32)at[2] << 8) | (u32)at[3];
            f32 value;
            mem_copy(&value, &bits, sizeof(value));
            return value;
        }
        case CODEC_AIFF_F64_BE: {
            u64 bits = 0;
            for (u32 i = 0; i < 8; i += 1) { bits = (bits << 8) | at[i]; }
            f64 value;
            mem_copy(&value, &bits, sizeof(value));
            return (f32)value;
        }
        default: {
            switch (state->bytes_per_sample) {
                case 1: return (f32)(i8)at[0] * (1.0f / 128.0f);
                case 2: return (f32)(i16)(((u16)at[0] << 8) | at[1]) * (1.0f / 32768.0f);
                case 3: {
                    i32 value = (i32)(((u32)at[0] << 24) | ((u32)at[1] << 16) | ((u32)at[2] << 8));
                    return (f32)(value >> 8) * (1.0f / 8388608.0f);
                }
                default: {
                    i32 value = (i32)(((u32)at[0] << 24) | ((u32)at[1] << 16) | ((u32)at[2] << 8) |
                                      (u32)at[3]);
                    return (f32)value * (1.0f / 2147483648.0f);
                }
            }
        }
    }
}

u32 codec_aiff_read(Decoder *decoder, f32 *const *channels, u32 frames) {
    CodecAiff *state = (CodecAiff *)decoder->impl;
    u64 left = decoder->info.total_frames - decoder->frame_pos;
    if (left < frames) { frames = (u32)left; }
    if (frames == 0) { return 0; }

    u64 wanted = (u64)frames * state->frame_bytes;
    u64 got = codec_source_read(&decoder->source, state->raw, wanted);
    u32 produced = (u32)(got / state->frame_bytes);
    u32 channel_count = decoder->info.channels;
    for (u32 channel = 0; channel < channel_count; channel += 1) {
        f32 *dst = channels[channel];
        const u8 *at = state->raw + (u64)channel * state->bytes_per_sample;
        for (u32 frame = 0; frame < produced; frame += 1) {
            dst[frame] = codec_aiff_sample(state, at + (u64)frame * state->frame_bytes);
        }
    }
    return produced;
}

b32 codec_aiff_seek(Decoder *decoder, u64 frame) {
    CodecAiff *state = (CodecAiff *)decoder->impl;
    if (frame > decoder->info.total_frames) { return 0; }
    return codec_source_seek(&decoder->source, state->data_offset + frame * state->frame_bytes);
}

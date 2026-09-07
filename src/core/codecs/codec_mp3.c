// codec_mp3.c - MPEG Layer III through minimp3, frame by frame.
//
// minimp3_ex is not used (see third_party/LICENSES.md): the frame loop, the
// ID3v2 skip, the Xing/VBRI lookup and the seek are here, over the 256 KB
// window, so nothing ever holds more than 64 KB of compressed input.
#include "codec.h"

#include "../../third_party.h"

#define CODEC_MP3_INPUT     KB(64)  // forty times the largest legal frame
#define CODEC_MP3_FRAME_MAX 2880    // what minimp3 wants in hand to decode one

typedef struct CodecMp3 {
    mp3dec_t decoder;
    u8 *input;
    u32 input_size;   // bytes held
    u32 input_used;   // bytes already handed to minimp3
    b32 source_done;  // the file has no more bytes to give
    b32 skip_header_frame;
    u64 stream_start;  // first byte after the ID3v2 tag
    u64 position;      // frames already produced, the seek reference
    f32 *pending;      // one decoded frame, interleaved as minimp3 writes it
    u32 pending_frames;
    u32 pending_pos;
} CodecMp3;

// ID3v2: ten byte header, a syncsafe size, and an optional ten byte footer.
static u64 codec_mp3_id3v2_size(const u8 *header) {
    u64 size = ((u64)(header[6] & 0x7F) << 21) | ((u64)(header[7] & 0x7F) << 14) |
               ((u64)(header[8] & 0x7F) << 7) | (u64)(header[9] & 0x7F);
    u64 total = 10 + size;
    if (header[5] & 0x10) { total += 10; }  // footer present
    return total;
}

// The frame count a VBR header advertises, 0 when the frame carries none. The
// side info between the header and the tag has a fixed size per mode, but
// scanning the first 64 bytes for the marker is what every decoder does and it
// costs nothing here.
static u64 codec_mp3_vbr_frames(const u8 *frame, u32 size) {
    u32 limit = (size < 200) ? size : 200;
    for (u32 at = 4; at + 12 <= limit; at += 1) {
        b32 xing = frame[at] == 'X' && frame[at + 1] == 'i' && frame[at + 2] == 'n' &&
                   frame[at + 3] == 'g';
        b32 info = frame[at] == 'I' && frame[at + 1] == 'n' && frame[at + 2] == 'f' &&
                   frame[at + 3] == 'o';
        if (xing || info) {
            u32 flags = ((u32)frame[at + 4] << 24) | ((u32)frame[at + 5] << 16) |
                        ((u32)frame[at + 6] << 8) | (u32)frame[at + 7];
            if (!(flags & 1)) { return 0; }
            return ((u64)frame[at + 8] << 24) | ((u64)frame[at + 9] << 16) |
                   ((u64)frame[at + 10] << 8) | (u64)frame[at + 11];
        }
        b32 vbri = frame[at] == 'V' && frame[at + 1] == 'B' && frame[at + 2] == 'R' &&
                   frame[at + 3] == 'I';
        if (vbri && at + 26 <= limit) {
            return ((u64)frame[at + 14] << 24) | ((u64)frame[at + 15] << 16) |
                   ((u64)frame[at + 16] << 8) | (u64)frame[at + 17];
        }
    }
    return 0;
}

static void codec_mp3_refill(Decoder *decoder) {
    CodecMp3 *state = (CodecMp3 *)decoder->impl;
    if (state->input_used) {
        u32 left = state->input_size - state->input_used;
        mem_move(state->input, state->input + state->input_used, left);
        state->input_size = left;
        state->input_used = 0;
    }
    u64 got = codec_source_read(&decoder->source, state->input + state->input_size,
                                CODEC_MP3_INPUT - state->input_size);
    if (got == 0) { state->source_done = 1; }
    state->input_size += (u32)got;
}

// Decodes forward until one frame yields samples. 0 means end of stream, which
// covers a truncated tail: minimp3 refuses to decode half a frame and we stop.
static b32 codec_mp3_decode_next(Decoder *decoder) {
    CodecMp3 *state = (CodecMp3 *)decoder->impl;
    for (;;) {
        u32 available = state->input_size - state->input_used;
        if (available < CODEC_MP3_FRAME_MAX && !state->source_done) {
            codec_mp3_refill(decoder);
            available = state->input_size - state->input_used;
        }
        if (available == 0) { return 0; }

        const u8 *frame = state->input + state->input_used;
        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&state->decoder, frame, (int)available, state->pending,
                                          &info);
        if (info.frame_bytes == 0) { return 0; }  // no frame anywhere in the window
        state->input_used += (u32)info.frame_bytes;
        if (samples <= 0) { continue; }

        if (state->skip_header_frame) {
            state->skip_header_frame = 0;
            if (codec_mp3_vbr_frames(frame + info.frame_offset,
                                     (u32)(info.frame_bytes - info.frame_offset)) != 0) {
                continue;  // a Xing/VBRI frame carries no audio, only the count
            }
        }
        state->pending_frames = (u32)samples;
        state->pending_pos = 0;
        return 1;
    }
}

CodecStatus codec_mp3_open(Decoder *decoder) {
    CodecMp3 *state = push_struct_zero(decoder->arena, CodecMp3);
    state->input = push_array(decoder->arena, u8, CODEC_MP3_INPUT);
    state->pending = push_array(decoder->arena, f32, MINIMP3_MAX_SAMPLES_PER_FRAME);
    decoder->impl = state;
    mp3dec_init(&state->decoder);

    u8 header[10];
    if (codec_source_read(&decoder->source, header, sizeof(header)) == sizeof(header) &&
        header[0] == 'I' && header[1] == 'D' && header[2] == '3') {
        state->stream_start = codec_mp3_id3v2_size(header);
    }
    if (state->stream_start >= decoder->source.size) { return CODEC_ERR_CORRUPT; }
    codec_source_seek(&decoder->source, state->stream_start);

    // The first frame tells us the format; if it is a Xing/Info frame it also
    // tells us the length, and it is dropped rather than played.
    codec_mp3_refill(decoder);
    u32 available = state->input_size;
    if (available == 0) { return CODEC_ERR_CORRUPT; }
    mp3dec_frame_info_t info;
    int samples = mp3dec_decode_frame(&state->decoder, state->input, (int)available,
                                      state->pending, &info);
    if (info.frame_bytes == 0 || samples <= 0 || info.hz <= 0 || info.channels <= 0) {
        return CODEC_ERR_CORRUPT;
    }
    u64 vbr_frames = codec_mp3_vbr_frames(state->input + info.frame_offset,
                                          (u32)(info.frame_bytes - info.frame_offset));

    decoder->info.sample_rate = (u32)info.hz;
    decoder->info.channels = (u32)info.channels;
    if (vbr_frames) {
        decoder->info.total_frames = vbr_frames * (u64)samples;
    } else if (info.bitrate_kbps > 0) {
        // CBR estimate: the only thing a plain MPEG stream lets us say.
        u64 bytes = decoder->source.size - state->stream_start;
        decoder->info.total_frames = (bytes * 8 * (u64)info.hz) / ((u64)info.bitrate_kbps * 1000);
    }

    // Start again from the first frame with a clean decoder: the bit reservoir
    // of the frame we just probed must not leak into the first frame we output.
    mp3dec_init(&state->decoder);
    state->input_size = 0;
    state->input_used = 0;
    state->source_done = 0;
    state->pending_frames = 0;
    state->pending_pos = 0;
    state->skip_header_frame = 1;
    codec_source_seek(&decoder->source, state->stream_start);
    return CODEC_OK;
}

u32 codec_mp3_read(Decoder *decoder, f32 *const *channels, u32 frames) {
    CodecMp3 *state = (CodecMp3 *)decoder->impl;
    u32 channel_count = decoder->info.channels;
    u32 produced = 0;
    while (produced < frames) {
        if (state->pending_pos == state->pending_frames) {
            if (!codec_mp3_decode_next(decoder)) { break; }
        }
        u32 take = state->pending_frames - state->pending_pos;
        if (take > frames - produced) { take = frames - produced; }
        codec_deinterleave_f32(state->pending + (u64)state->pending_pos * channel_count, channels,
                               channel_count, take, produced);
        state->pending_pos += take;
        produced += take;
    }
    state->position += produced;
    return produced;
}

// No seek table exists in a plain MPEG stream and the bit reservoir makes a
// byte offset ambiguous, so seeking is decoding: forwards from where we are,
// from the first frame when the target is behind us. It stays exact, and at the
// hundreds of times realtime the bench measures a whole album costs a second.
b32 codec_mp3_seek(Decoder *decoder, u64 frame) {
    CodecMp3 *state = (CodecMp3 *)decoder->impl;
    if (frame < state->position) {
        mp3dec_init(&state->decoder);
        state->input_size = 0;
        state->input_used = 0;
        state->source_done = 0;
        state->pending_frames = 0;
        state->pending_pos = 0;
        state->skip_header_frame = 1;
        state->position = 0;
        codec_source_seek(&decoder->source, state->stream_start);
    }
    while (state->position < frame) {
        if (state->pending_pos == state->pending_frames) {
            if (!codec_mp3_decode_next(decoder)) { return 0; }
        }
        u64 take = state->pending_frames - state->pending_pos;
        if (take > frame - state->position) { take = frame - state->position; }
        state->pending_pos += (u32)take;
        state->position += take;
    }
    return 1;
}

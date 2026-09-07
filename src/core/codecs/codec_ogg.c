// codec_ogg.c - Ogg Vorbis through stb_vorbis, in pushdata mode.
//
// Pushdata rather than pulldata for two reasons: it is the only stb_vorbis API
// that never wants the whole file, and it takes a stb_vorbis_alloc buffer, so
// the decoder allocates strictly inside our arena. The price is that seeking is
// ours to do: stb_vorbis_flush_pushdata resets the decoder without re-reading
// the setup headers, and we decode forward from the first audio page.
#include "codec.h"

#include "../library/tags.h"
#include "../../third_party.h"

#define CODEC_OGG_INPUT   KB(256)  // an Ogg page cannot exceed ~64 KB
#define CODEC_OGG_SETUP   MB(1)    // codebooks + windows, generously
#define CODEC_OGG_REFILL  KB(64)   // top the window up below this

typedef struct CodecOgg {
    stb_vorbis *vorbis;
    stb_vorbis_alloc alloc;
    u8 *input;
    u32 input_size;
    u32 input_used;
    b32 source_done;
    u64 headers_end;  // file offset of the first audio page
    u64 position;     // frames already produced
    f32 *pending;     // planar, channels blocks of CODEC_OGG_BLOCK_MAX
    u32 pending_frames;
    u32 pending_pos;
} CodecOgg;

#define CODEC_OGG_BLOCK_MAX 8192  // MAX_BLOCKSIZE of the Vorbis specification

// The granule position of the last page is the stream length in frames. Reading
// the last 64 KB is enough for any page a real encoder writes.
static u64 codec_ogg_last_granule(CodecSource *source, u8 *scratch, u64 scratch_size) {
    u64 window = (source->size < scratch_size) ? source->size : scratch_size;
    u64 start = source->size - window;
    codec_source_seek(source, start);
    u64 got = codec_source_read(source, scratch, window);
    u64 granule = 0;
    for (u64 at = 0; at + 14 <= got; at += 1) {
        if (scratch[at] != 'O' || scratch[at + 1] != 'g' || scratch[at + 2] != 'g' ||
            scratch[at + 3] != 'S') {
            continue;
        }
        u64 value = 0;
        for (u32 i = 0; i < 8; i += 1) { value |= (u64)scratch[at + 6 + i] << (8 * i); }
        if (value != U64_MAX) { granule = value; }  // -1 marks a continued packet
    }
    return granule;
}

// --- the boundary in front of stb_vorbis ------------------------------------
// stb_vorbis 1.22 reads the Vorbis comment header with the lengths the file
// gives it: a comment length of 0x7FFFFFFF makes `len + 1` overflow, the
// allocation check pass on a negative size, and the copy loop walk off the end.
// It is a decoder fed files somebody else wrote, which ADR-012 says is exactly
// where a validation boundary belongs - ours, not a patch on vendored code.
// So the two header packets are reassembled here and every length in them is
// checked against the packet that carries it before stb_vorbis sees the stream.
// Found by the fuzz pass of tests/test_codecs.c.

#define CODEC_OGG_PACKET_MAX KB(256)  // a Vorbis setup header, generously

typedef struct CodecOggPackets {
    const u8 *data;
    u64 size;
    u64 at;       // offset of the page being walked
    u32 segment;  // next lacing value inside that page
} CodecOggPackets;

// Copies the next packet out of the pages we hold. 0 means "no complete packet
// here", which covers a truncated stream, a bad page and a packet too large to
// be one of the two headers.
static u64 codec_ogg_next_packet(CodecOggPackets *walk, u8 *out, u64 capacity) {
    u64 size = 0;
    for (;;) {
        if (walk->at + 27 > walk->size) { return 0; }
        const u8 *page = walk->data + walk->at;
        if (page[0] != 'O' || page[1] != 'g' || page[2] != 'g' || page[3] != 'S') { return 0; }
        u32 segments = page[26];
        if (walk->at + 27 + segments > walk->size) { return 0; }
        const u8 *table = page + 27;
        u64 body = walk->at + 27 + segments;
        u64 body_size = 0;
        for (u32 i = 0; i < segments; i += 1) { body_size += table[i]; }
        if (body_size > walk->size - body) { return 0; }

        u64 offset = body;
        for (u32 i = 0; i < walk->segment; i += 1) { offset += table[i]; }
        b32 complete = 0;
        while (walk->segment < segments) {
            u32 lacing = table[walk->segment];
            if (size + lacing > capacity) { return 0; }
            mem_copy(out + size, walk->data + offset, lacing);
            size += lacing;
            offset += lacing;
            walk->segment += 1;
            if (lacing < 255) {
                complete = 1;
                break;
            }
        }
        if (walk->segment >= segments) {
            walk->at = body + body_size;
            walk->segment = 0;
        }
        if (complete) { return size; }
    }
}

static b32 codec_ogg_headers_are_sane(const u8 *data, u64 size, u8 *scratch, u64 capacity) {
    CodecOggPackets walk;
    walk.data = data;
    walk.size = size;
    walk.at = 0;
    walk.segment = 0;
    u64 identification = codec_ogg_next_packet(&walk, scratch, capacity);
    if (identification < 30 || scratch[0] != 1) { return 0; }
    u64 comment = codec_ogg_next_packet(&walk, scratch, capacity);
    if (comment < 15 || scratch[0] != 3) { return 0; }

    TagsReader reader = tags_reader(str8(scratch, comment));
    tags_skip(&reader, 7);                     // packet type + "vorbis"
    tags_skip(&reader, tags_u32le(&reader));   // vendor string
    u64 count = tags_u32le(&reader);
    // Four bytes of length per comment is the floor, so anything above that is
    // a length the file made up.
    if (reader.fail || count > tags_remaining(&reader) / 4) { return 0; }
    for (u64 i = 0; i < count; i += 1) { tags_skip(&reader, tags_u32le(&reader)); }
    return !reader.fail;
}

static void codec_ogg_refill(Decoder *decoder) {
    CodecOgg *state = (CodecOgg *)decoder->impl;
    if (state->input_used) {
        u32 left = state->input_size - state->input_used;
        mem_move(state->input, state->input + state->input_used, left);
        state->input_size = left;
        state->input_used = 0;
    }
    u64 got = codec_source_read(&decoder->source, state->input + state->input_size,
                               CODEC_OGG_INPUT - state->input_size);
    if (got == 0) { state->source_done = 1; }
    state->input_size += (u32)got;
}

static b32 codec_ogg_decode_next(Decoder *decoder) {
    CodecOgg *state = (CodecOgg *)decoder->impl;
    u32 channel_count = decoder->info.channels;
    for (;;) {
        u32 available = state->input_size - state->input_used;
        if (available < CODEC_OGG_REFILL && !state->source_done) {
            codec_ogg_refill(decoder);
            available = state->input_size - state->input_used;
        }
        if (available == 0) { return 0; }

        int channels_out = 0;
        int samples = 0;
        float **outputs = 0;
        tp_arena_bind(decoder->arena);
        int used = stb_vorbis_decode_frame_pushdata(state->vorbis, state->input + state->input_used,
                                                    (int)available, &channels_out, &outputs,
                                                    &samples);
        tp_arena_unbind();
        if (used == 0) {
            // The decoder wants a bigger contiguous block than we are holding.
            if (state->source_done) { return 0; }
            codec_ogg_refill(decoder);
            continue;
        }
        state->input_used += (u32)used;
        if (samples <= 0) { continue; }  // resynchronising, keep feeding

        u32 produced = (u32)samples;
        if (produced > CODEC_OGG_BLOCK_MAX) { produced = CODEC_OGG_BLOCK_MAX; }
        for (u32 channel = 0; channel < channel_count; channel += 1) {
            mem_copy(state->pending + (u64)channel * CODEC_OGG_BLOCK_MAX, outputs[channel],
                     (u64)produced * sizeof(f32));
        }
        state->pending_frames = produced;
        state->pending_pos = 0;
        return 1;
    }
}

CodecStatus codec_ogg_open(Decoder *decoder) {
    CodecOgg *state = push_struct_zero(decoder->arena, CodecOgg);
    state->input = push_array(decoder->arena, u8, CODEC_OGG_INPUT);
    decoder->impl = state;

    u64 granule = codec_ogg_last_granule(&decoder->source, state->input, CODEC_OGG_INPUT);

    state->alloc.alloc_buffer = (char *)push_array(decoder->arena, u8, CODEC_OGG_SETUP);
    state->alloc.alloc_buffer_length_in_bytes = (int)CODEC_OGG_SETUP;

    codec_source_seek(&decoder->source, 0);
    state->input_size = 0;
    state->input_used = 0;
    codec_ogg_refill(decoder);

    ArenaTemp check = arena_temp_begin(decoder->arena);
    u8 *packet = push_array(decoder->arena, u8, CODEC_OGG_PACKET_MAX);
    b32 sane = codec_ogg_headers_are_sane(state->input, state->input_size, packet,
                                          CODEC_OGG_PACKET_MAX);
    arena_temp_end(check);
    if (!sane) { return CODEC_ERR_CORRUPT; }

    int consumed = 0;
    int error = 0;
    tp_arena_bind(decoder->arena);
    state->vorbis = stb_vorbis_open_pushdata(state->input, (int)state->input_size, &consumed,
                                             &error, &state->alloc);
    tp_arena_unbind();
    if (!state->vorbis) { return CODEC_ERR_CORRUPT; }

    stb_vorbis_info info = stb_vorbis_get_info(state->vorbis);
    if (info.sample_rate == 0 || info.channels <= 0 || info.channels > CODEC_MAX_CHANNELS) {
        return CODEC_ERR_CORRUPT;
    }
    decoder->info.sample_rate = info.sample_rate;
    decoder->info.channels = (u32)info.channels;
    decoder->info.total_frames = granule;

    state->headers_end = (u64)consumed;
    state->input_used = (u32)consumed;
    state->pending = push_array(decoder->arena, f32, CODEC_OGG_BLOCK_MAX * (u32)info.channels);
    return CODEC_OK;
}

u32 codec_ogg_read(Decoder *decoder, f32 *const *channels, u32 frames) {
    CodecOgg *state = (CodecOgg *)decoder->impl;
    u32 channel_count = decoder->info.channels;
    u32 produced = 0;
    while (produced < frames) {
        if (state->pending_pos == state->pending_frames) {
            if (!codec_ogg_decode_next(decoder)) { break; }
        }
        u32 take = state->pending_frames - state->pending_pos;
        if (take > frames - produced) { take = frames - produced; }
        for (u32 channel = 0; channel < channel_count; channel += 1) {
            mem_copy(channels[channel] + produced,
                     state->pending + (u64)channel * CODEC_OGG_BLOCK_MAX + state->pending_pos,
                     (u64)take * sizeof(f32));
        }
        state->pending_pos += take;
        produced += take;
    }
    // The granule position of the last page is the real length; a Vorbis stream
    // always decodes a few frames past it and they are not audio.
    if (decoder->info.total_frames && state->position + produced > decoder->info.total_frames) {
        u64 keep = decoder->info.total_frames - state->position;
        produced = (keep > produced) ? produced : (u32)keep;
    }
    state->position += produced;
    return produced;
}

b32 codec_ogg_seek(Decoder *decoder, u64 frame) {
    CodecOgg *state = (CodecOgg *)decoder->impl;
    if (frame < state->position) {
        // flush_pushdata keeps the codebooks: only the decode state is dropped.
        stb_vorbis_flush_pushdata(state->vorbis);
        state->input_size = 0;
        state->input_used = 0;
        state->source_done = 0;
        state->pending_frames = 0;
        state->pending_pos = 0;
        state->position = 0;
        codec_source_seek(&decoder->source, state->headers_end);
    }
    while (state->position < frame) {
        if (state->pending_pos == state->pending_frames) {
            if (!codec_ogg_decode_next(decoder)) { return 0; }
        }
        u64 take = state->pending_frames - state->pending_pos;
        if (take > frame - state->position) { take = frame - state->position; }
        state->pending_pos += (u32)take;
        state->position += take;
    }
    return 1;
}

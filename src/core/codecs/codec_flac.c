// codec_flac.c - FLAC through dr_flac, pulling on the 256 KB window.
#include "codec.h"

#include "../../third_party.h"

typedef struct CodecFlac {
    drflac *flac;
    f32 *interleaved;  // one block, dr_flac only speaks interleaved
} CodecFlac;

static size_t codec_flac_on_read(void *user, void *out, size_t bytes) {
    return (size_t)codec_source_read((CodecSource *)user, out, (u64)bytes);
}

static drflac_bool32 codec_flac_on_seek(void *user, int offset, drflac_seek_origin origin) {
    CodecSource *source = (CodecSource *)user;
    i64 target = offset;
    if (origin == DRFLAC_SEEK_CUR) { target += (i64)source->pos; }
    if (origin == DRFLAC_SEEK_END) { target += (i64)source->size; }
    if (target < 0) { return 0; }
    return codec_source_seek(source, (u64)target);
}

static drflac_bool32 codec_flac_on_tell(void *user, drflac_int64 *cursor) {
    *cursor = (drflac_int64)((CodecSource *)user)->pos;
    return 1;
}

CodecStatus codec_flac_open(Decoder *decoder) {
    // dr_flac allocates its seek table and block buffers here; the binding sends
    // them to the decoder arena, which codec_close releases whole.
    tp_arena_bind(decoder->arena);
    drflac *flac = drflac_open(codec_flac_on_read, codec_flac_on_seek, codec_flac_on_tell,
                               &decoder->source, 0);
    tp_arena_unbind();
    if (!flac) { return CODEC_ERR_CORRUPT; }
    if (flac->sampleRate == 0 || flac->channels == 0 || flac->channels > CODEC_MAX_CHANNELS) {
        return CODEC_ERR_CORRUPT;
    }

    CodecFlac *state = push_struct_zero(decoder->arena, CodecFlac);
    state->flac = flac;
    state->interleaved = push_array(decoder->arena, f32, CODEC_BLOCK_FRAMES * flac->channels);
    decoder->impl = state;
    decoder->info.sample_rate = flac->sampleRate;
    decoder->info.channels = flac->channels;
    decoder->info.total_frames = flac->totalPCMFrameCount;
    return CODEC_OK;
}

u32 codec_flac_read(Decoder *decoder, f32 *const *channels, u32 frames) {
    CodecFlac *state = (CodecFlac *)decoder->impl;
    tp_arena_bind(decoder->arena);
    u64 produced = drflac_read_pcm_frames_f32(state->flac, frames, state->interleaved);
    tp_arena_unbind();
    codec_deinterleave_f32(state->interleaved, channels, decoder->info.channels, (u32)produced, 0);
    return (u32)produced;
}

b32 codec_flac_seek(Decoder *decoder, u64 frame) {
    CodecFlac *state = (CodecFlac *)decoder->impl;
    tp_arena_bind(decoder->arena);
    b32 ok = drflac_seek_to_pcm_frame(state->flac, frame) != 0;
    tp_arena_unbind();
    return ok;
}

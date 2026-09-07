// codec_mf.c - AAC/M4A, ALAC and WMA, adapted from os_media_decoder_* to Decoder.
//
// Nothing here knows about Media Foundation: core/ only ever talks to
// platform.h (ADR-001). All this file does is turn the interleaved f32 the
// system hands back into the planar blocks the pipeline wants.
#include "codec.h"

typedef struct CodecMf {
    OsMediaDecoder media;
    f32 *interleaved;
} CodecMf;

CodecStatus codec_mf_open(Decoder *decoder, String8 path) {
    CodecMf *state = push_struct_zero(decoder->arena, CodecMf);
    OsMediaInfo info;
    mem_zero(&info, sizeof(info));
    if (!os_media_decoder_open(&state->media, path, &info)) { return CODEC_ERR_NO_DECODER; }
    if (info.sample_rate == 0 || info.channels == 0 || info.channels > CODEC_MAX_CHANNELS) {
        os_media_decoder_close(state->media);
        return CODEC_ERR_CORRUPT;
    }
    state->interleaved = push_array(decoder->arena, f32, CODEC_BLOCK_FRAMES * info.channels);
    decoder->impl = state;
    decoder->info.sample_rate = info.sample_rate;
    decoder->info.channels = info.channels;
    decoder->info.total_frames = info.total_frames;
    return CODEC_OK;
}

u32 codec_mf_read(Decoder *decoder, f32 *const *channels, u32 frames) {
    CodecMf *state = (CodecMf *)decoder->impl;
    u64 produced = os_media_decoder_read(state->media, state->interleaved, frames);
    codec_deinterleave_f32(state->interleaved, channels, decoder->info.channels, (u32)produced, 0);
    return (u32)produced;
}

b32 codec_mf_seek(Decoder *decoder, u64 frame) {
    CodecMf *state = (CodecMf *)decoder->impl;
    return os_media_decoder_seek(state->media, frame);
}

void codec_mf_close(Decoder *decoder) {
    CodecMf *state = (CodecMf *)decoder->impl;
    // codec_open failing before the media decoder existed leaves impl empty.
    if (state && state->media.v) { os_media_decoder_close(state->media); }
}

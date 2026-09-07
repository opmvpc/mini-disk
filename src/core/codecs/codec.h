// codec.h - one decoder interface for every input format (T-040, ADR-007).
//
// A Decoder is a file plus a position. It is opened from a path, it hands out
// planar f32 in blocks of at most CODEC_BLOCK_FRAMES frames written into buffers
// the caller owns, it seeks by frame, and it is closed. Nothing else.
//
// Memory: one arena per open decoder, taken at codec_open and released whole at
// codec_close. The decoder struct, the 256 KB read window and whatever the
// vendored libraries allocate all live in it, so closing cannot leak and the
// job system can open a decoder per worker without an allocator in sight.
//
// Reading: never the whole file. CodecSource keeps a CODEC_CHUNK_SIZE window
// filled by os_file_read_at, and every library pulls through it.
//
// Boundary (ADR-012): the file comes from somebody else's disk. codec_open and
// codec_read return typed errors on anything malformed; below this line the
// program trusts the CodecInfo it was given.
#ifndef CODEC_H
#define CODEC_H

#include "../../base/base.h"
#include "../../base/base_arena.h"
#include "../../base/base_string.h"
#include "../../platform/platform.h"

#define CODEC_BLOCK_FRAMES 4096      // the read granularity the pipeline works in
#define CODEC_CHUNK_SIZE   KB(256)   // the file window, never more
#define CODEC_MAX_CHANNELS 8         // FLAC's ceiling, and above anything we transcode

typedef enum CodecId {
    CODEC_NONE = 0,
    CODEC_MP3,
    CODEC_FLAC,
    CODEC_WAV,
    CODEC_AIFF,
    CODEC_OGG_VORBIS,
    CODEC_MP4,   // AAC or ALAC, told apart by Media Foundation
    CODEC_WMA,
    CODEC_OPUS,  // recognised, not decoded in v1 (ADR-007)
    CODEC_ID_COUNT,
} CodecId;

typedef enum CodecStatus {
    CODEC_OK = 0,
    CODEC_ERR_FILE,          // unreadable, or empty
    CODEC_ERR_FORMAT,        // no signature we know
    CODEC_ERR_UNSUPPORTED,   // known container, decoding out of scope (Opus)
    CODEC_ERR_CORRUPT,       // known format, header or stream unusable
    CODEC_ERR_NO_DECODER,    // Media Foundation missing or refusing the stream
    CODEC_STATUS_COUNT,
} CodecStatus;

String8 codec_status_text(CodecStatus status);
String8 codec_id_text(CodecId id);

typedef struct CodecInfo {
    u32 sample_rate;
    u32 channels;
    u64 total_frames;  // 0 when the container does not say
    u32 codec;         // CodecId
} CodecInfo;

// --- the file window -------------------------------------------------------
// A sequential reader over os_file_read_at with one CODEC_CHUNK_SIZE window.
// Seeks inside the window cost nothing; the libraries seek backwards a few
// bytes constantly while parsing headers.
typedef struct CodecSource {
    OsFile file;
    u64 size;
    u64 pos;           // logical cursor, what the libraries call "the position"
    u64 chunk_offset;  // file offset of chunk[0]
    u64 chunk_size;    // bytes valid in chunk
    u8 *chunk;
} CodecSource;

u64 codec_source_read(CodecSource *source, void *dst, u64 size);  // bytes read
b32 codec_source_seek(CodecSource *source, u64 offset);           // 0 past the end

typedef struct Decoder {
    Arena *arena;
    CodecSource source;
    CodecInfo info;
    void *impl;      // the format's own state, pushed on `arena`
    u64 frame_pos;   // index of the next frame read_f32_planar will return
    b32 eos;
} Decoder;

// Opens by signature, never by extension. On failure nothing is allocated and
// *out is left alone.
CodecStatus codec_open(Decoder **out, String8 path);
void        codec_close(Decoder *decoder);

// Writes up to `frames` frames (<= CODEC_BLOCK_FRAMES) into `channels[c]`, one
// buffer per channel of `decoder->info.channels`, and returns how many it wrote.
// 0 means end of stream, which is not an error.
u32 codec_read_f32_planar(Decoder *decoder, f32 *const *channels, u32 frames);

// Exact, for every format. Past the end it lands on the end and reads 0.
b32 codec_seek(Decoder *decoder, u64 frame);

// The signature dispatch on its own, for callers that only need to know what a
// file is. CODEC_NONE when nothing matches.
CodecId codec_probe_bytes(String8 head);

// --- the format back ends --------------------------------------------------
// One per container. codec.c picks by CodecId; each fills decoder->info and
// decoder->impl or returns why it cannot.
CodecStatus codec_mp3_open(Decoder *decoder);
u32         codec_mp3_read(Decoder *decoder, f32 *const *channels, u32 frames);
b32         codec_mp3_seek(Decoder *decoder, u64 frame);

CodecStatus codec_flac_open(Decoder *decoder);
u32         codec_flac_read(Decoder *decoder, f32 *const *channels, u32 frames);
b32         codec_flac_seek(Decoder *decoder, u64 frame);

CodecStatus codec_wav_open(Decoder *decoder);
u32         codec_wav_read(Decoder *decoder, f32 *const *channels, u32 frames);
b32         codec_wav_seek(Decoder *decoder, u64 frame);

CodecStatus codec_aiff_open(Decoder *decoder);
u32         codec_aiff_read(Decoder *decoder, f32 *const *channels, u32 frames);
b32         codec_aiff_seek(Decoder *decoder, u64 frame);

CodecStatus codec_ogg_open(Decoder *decoder);
u32         codec_ogg_read(Decoder *decoder, f32 *const *channels, u32 frames);
b32         codec_ogg_seek(Decoder *decoder, u64 frame);

// AAC/M4A, ALAC and WMA: Media Foundation behind os_media_decoder_*.
CodecStatus codec_mf_open(Decoder *decoder, String8 path);
u32         codec_mf_read(Decoder *decoder, f32 *const *channels, u32 frames);
b32         codec_mf_seek(Decoder *decoder, u64 frame);
void        codec_mf_close(Decoder *decoder);

// --- shared helper ---------------------------------------------------------
// Interleaved f32 in, planar out. The libraries that hand back interleaved
// frames all go through this; the loop is over floats, never bytes, so /GL has
// nothing to turn into a memcpy call.
md_inline void codec_deinterleave_f32(const f32 *src, f32 *const *channels, u32 channel_count,
                                      u32 frames, u32 dst_offset) {
    for (u32 channel = 0; channel < channel_count; channel += 1) {
        f32 *dst = channels[channel] + dst_offset;
        const f32 *in = src + channel;
        for (u32 frame = 0; frame < frames; frame += 1) {
            dst[frame] = in[(u64)frame * channel_count];
        }
    }
}

#endif  // CODEC_H

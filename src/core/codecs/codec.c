// codec.c - the file window, the signature dispatch and the Decoder front end.
#include "codec.h"

#include "../library/tags.h"

// Reserve per decoder. Virtual reserve costs nothing on x64 (base_arena.h); what
// is committed is the 256 KB window plus whatever the format needs, which peaks
// around 1.5 MB for a Vorbis stream with big blocks.
#define CODEC_ARENA_RESERVE MB(64)
#define CODEC_PROBE_BYTES   512

// --- the file window -------------------------------------------------------

// Refills the window so it contains `offset`, aligned down to the chunk size so
// a decoder walking forwards never re-reads the bytes it just used.
static b32 codec_source_fill(CodecSource *source, u64 offset) {
    if (offset >= source->size) { return 0; }
    u64 aligned = offset & ~(CODEC_CHUNK_SIZE - 1);
    u64 wanted = source->size - aligned;
    if (wanted > CODEC_CHUNK_SIZE) { wanted = CODEC_CHUNK_SIZE; }
    u64 read = os_file_read_at(source->file, aligned, source->chunk, wanted);
    source->chunk_offset = aligned;
    source->chunk_size = read;
    return read > offset - aligned;
}

u64 codec_source_read(CodecSource *source, void *dst, u64 size) {
    u8 *out = (u8 *)dst;
    u64 done = 0;
    while (done < size) {
        if (source->pos < source->chunk_offset ||
            source->pos >= source->chunk_offset + source->chunk_size) {
            if (!codec_source_fill(source, source->pos)) { break; }
        }
        u64 inside = source->pos - source->chunk_offset;
        u64 available = source->chunk_size - inside;
        u64 take = size - done;
        if (take > available) { take = available; }
        mem_copy(out + done, source->chunk + inside, take);
        source->pos += take;
        done += take;
    }
    return done;
}

b32 codec_source_seek(CodecSource *source, u64 offset) {
    if (offset > source->size) { return 0; }
    source->pos = offset;
    return 1;
}

// --- signature dispatch ----------------------------------------------------
// Same rule as tags.c: the bytes decide, never the extension. The helpers come
// from tags.h so the two dispatchers cannot drift apart.

static b32 codec_mp3_sync_at(String8 head, u64 at) {
    return at + 1 < head.size && head.str[at] == 0xFF && (head.str[at + 1] & 0xE0) == 0xE0;
}

CodecId codec_probe_bytes(String8 head) {
    TagsReader reader = tags_reader(head);
    String8 magic = tags_bytes(&reader, 4);
    if (reader.fail) { return CODEC_NONE; }

    if (tags_match(magic, "fLaC")) { return CODEC_FLAC; }
    if (tags_match(magic, "OggS")) {
        // The codec name sits in the first packet of the first page, a little
        // past the 27 byte page header plus its segment table.
        for (u64 at = 27; at + 8 <= head.size; at += 1) {
            String8 window = str8(head.str + at, 8);
            if (tags_match(window, "vorb") && window.str[4] == 'i' && window.str[5] == 's') {
                return CODEC_OGG_VORBIS;
            }
            if (tags_match(window, "Opus") && window.str[4] == 'H' && window.str[5] == 'e' &&
                window.str[6] == 'a' && window.str[7] == 'd') {
                return CODEC_OPUS;
            }
        }
        return CODEC_NONE;
    }
    if (tags_match(magic, "RIFF")) {
        String8 form = str8_substr(head, 8, 4);
        if (tags_match(form, "WAVE")) { return CODEC_WAV; }
        return CODEC_NONE;
    }
    if (tags_match(magic, "FORM")) {
        String8 form = str8_substr(head, 8, 4);
        if (tags_match(form, "AIFF") || tags_match(form, "AIFC")) { return CODEC_AIFF; }
        return CODEC_NONE;
    }
    // ASF header object GUID, the container behind every .wma.
    if (magic.str[0] == 0x30 && magic.str[1] == 0x26 && magic.str[2] == 0xB2 &&
        magic.str[3] == 0x75) {
        return CODEC_WMA;
    }
    if (tags_match(str8_substr(head, 4, 4), "ftyp")) { return CODEC_MP4; }
    if (magic.str[0] == 'I' && magic.str[1] == 'D' && magic.str[2] == '3') {
        // ID3v2 says nothing about what follows it, but in practice it only
        // ever fronts an MP3 in the files a library holds.
        return CODEC_MP3;
    }
    if (codec_mp3_sync_at(head, 0)) { return CODEC_MP3; }
    return CODEC_NONE;
}

String8 codec_status_text(CodecStatus status) {
    switch (status) {
        case CODEC_OK: return str8_lit("ok");
        case CODEC_ERR_FILE: return str8_lit("file cannot be read");
        case CODEC_ERR_FORMAT: return str8_lit("unrecognised audio format");
        case CODEC_ERR_UNSUPPORTED: return str8_lit("unsupported format");
        case CODEC_ERR_CORRUPT: return str8_lit("corrupt or truncated stream");
        case CODEC_ERR_NO_DECODER: return str8_lit("no system decoder for this format");
        default: return str8_lit("unknown error");
    }
}

String8 codec_id_text(CodecId id) {
    switch (id) {
        case CODEC_MP3: return str8_lit("MP3");
        case CODEC_FLAC: return str8_lit("FLAC");
        case CODEC_WAV: return str8_lit("WAV");
        case CODEC_AIFF: return str8_lit("AIFF");
        case CODEC_OGG_VORBIS: return str8_lit("Ogg Vorbis");
        case CODEC_MP4: return str8_lit("MP4");
        case CODEC_WMA: return str8_lit("WMA");
        case CODEC_OPUS: return str8_lit("Opus");
        default: return str8_lit("unknown");
    }
}

// --- front end -------------------------------------------------------------

CodecStatus codec_open(Decoder **out, String8 path) {
    OsFileInfo stat;
    if (!os_file_stat(path, &stat) || stat.is_dir || stat.size == 0) { return CODEC_ERR_FILE; }
    OsFile file = os_file_open(path);
    if (!file.v) { return CODEC_ERR_FILE; }

    Arena *arena = arena_alloc(CODEC_ARENA_RESERVE);
    Decoder *decoder = push_struct_zero(arena, Decoder);
    decoder->arena = arena;
    decoder->source.file = file;
    decoder->source.size = stat.size;
    decoder->source.chunk = push_array(arena, u8, CODEC_CHUNK_SIZE);

    u8 probe[CODEC_PROBE_BYTES];
    u64 probe_size = codec_source_read(&decoder->source, probe, sizeof(probe));
    decoder->source.pos = 0;

    CodecId id = codec_probe_bytes(str8(probe, probe_size));
    decoder->info.codec = (u32)id;

    CodecStatus status;
    switch (id) {
        case CODEC_MP3: status = codec_mp3_open(decoder); break;
        case CODEC_FLAC: status = codec_flac_open(decoder); break;
        case CODEC_WAV: status = codec_wav_open(decoder); break;
        case CODEC_AIFF: status = codec_aiff_open(decoder); break;
        case CODEC_OGG_VORBIS: status = codec_ogg_open(decoder); break;
        case CODEC_MP4:
        case CODEC_WMA: status = codec_mf_open(decoder, path); break;
        case CODEC_OPUS: status = CODEC_ERR_UNSUPPORTED; break;
        default: status = CODEC_ERR_FORMAT; break;
    }
    if (status != CODEC_OK) {
        os_file_close(file);
        arena_release(arena);
        return status;
    }
    // What every back end promises past this point, and what the rest of the
    // program is allowed to trust without checking again (ADR-012).
    AssertAlways(decoder->info.sample_rate != 0);
    AssertAlways(decoder->info.channels != 0 && decoder->info.channels <= CODEC_MAX_CHANNELS);
    *out = decoder;
    return CODEC_OK;
}

void codec_close(Decoder *decoder) {
    if (decoder->info.codec == CODEC_MP4 || decoder->info.codec == CODEC_WMA) {
        codec_mf_close(decoder);
    }
    os_file_close(decoder->source.file);
    // Everything the decoder and the vendored library allocated went here.
    arena_release(decoder->arena);
}

u32 codec_read_f32_planar(Decoder *decoder, f32 *const *channels, u32 frames) {
    Assert(frames <= CODEC_BLOCK_FRAMES);
    if (decoder->eos || frames == 0) { return 0; }
    u32 produced = 0;
    switch (decoder->info.codec) {
        case CODEC_MP3: produced = codec_mp3_read(decoder, channels, frames); break;
        case CODEC_FLAC: produced = codec_flac_read(decoder, channels, frames); break;
        case CODEC_WAV: produced = codec_wav_read(decoder, channels, frames); break;
        case CODEC_AIFF: produced = codec_aiff_read(decoder, channels, frames); break;
        case CODEC_OGG_VORBIS: produced = codec_ogg_read(decoder, channels, frames); break;
        default: produced = codec_mf_read(decoder, channels, frames); break;
    }
    if (produced == 0) { decoder->eos = 1; }
    decoder->frame_pos += produced;
    return produced;
}

b32 codec_seek(Decoder *decoder, u64 frame) {
    b32 ok;
    switch (decoder->info.codec) {
        case CODEC_MP3: ok = codec_mp3_seek(decoder, frame); break;
        case CODEC_FLAC: ok = codec_flac_seek(decoder, frame); break;
        case CODEC_WAV: ok = codec_wav_seek(decoder, frame); break;
        case CODEC_AIFF: ok = codec_aiff_seek(decoder, frame); break;
        case CODEC_OGG_VORBIS: ok = codec_ogg_seek(decoder, frame); break;
        default: ok = codec_mf_seek(decoder, frame); break;
    }
    if (ok) {
        decoder->frame_pos = frame;
        decoder->eos = 0;
    }
    return ok;
}

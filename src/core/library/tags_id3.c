// tags_id3.c - ID3v2.2/2.3/2.4, ID3v1/v1.1, and the MPEG audio header.
//
// A boundary (ADR-012). The frame walk is the dangerous part of the format:
// sizes are attacker controlled, v2.3 and v2.4 disagree on how they are
// encoded, and unsynchronisation rewrites the byte stream underneath. Every
// read goes through TagsReader, so the worst a malformed tag can do is end the
// walk early.

// --- MPEG tables -----------------------------------------------------------

static const u16 tags_mpeg_bitrate[2][3][16] = {
    // MPEG 2 / 2.5: layer I, then layer II, then layer III (II and III share).
    {{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0},
     {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
     {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}},
    // MPEG 1: layer I, layer II, layer III.
    {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0},
     {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},
     {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}},
};

static const u16 tags_mpeg_rate[4][3] = {
    {11025, 12000, 8000},  // MPEG 2.5
    {0, 0, 0},             // reserved
    {22050, 24000, 16000}, // MPEG 2
    {44100, 48000, 32000}, // MPEG 1
};

typedef struct TagsMpegFrame {
    u32 sample_rate;
    u32 bitrate_kbps;
    u32 samples_per_frame;
    u32 frame_bytes;
    u8 channels;
    u8 version_id;  // 3: MPEG1, 2: MPEG2, 0: MPEG2.5
    u8 layer;       // 1, 2, 3
    u8 mono;
} TagsMpegFrame;

// Decodes the four bytes at `p`. Returns 0 on anything that is not a plausible
// frame header - which is how the sync scan rejects a false 0xFF 0xE0.
static b32 tags_mpeg_header(const u8 *p, TagsMpegFrame *out) {
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) { return 0; }
    u32 version_id = (p[1] >> 3) & 3;
    u32 layer_bits = (p[1] >> 1) & 3;
    u32 bitrate_index = (p[2] >> 4) & 0xF;
    u32 rate_index = (p[2] >> 2) & 3;
    u32 padding = (p[2] >> 1) & 1;
    u32 mode = (p[3] >> 6) & 3;
    if (version_id == 1 || layer_bits == 0) { return 0; }
    if (bitrate_index == 0 || bitrate_index == 15 || rate_index == 3) { return 0; }

    u32 layer = 4 - layer_bits;                 // 01 -> III, 10 -> II, 11 -> I
    u32 is_mpeg1 = (version_id == 3) ? 1 : 0;
    u32 bitrate = tags_mpeg_bitrate[is_mpeg1][layer - 1][bitrate_index];
    u32 sample_rate = tags_mpeg_rate[version_id][rate_index];
    if (bitrate == 0 || sample_rate == 0) { return 0; }

    u32 samples;
    u32 frame_bytes;
    if (layer == 1) {
        samples = 384;
        frame_bytes = (12 * bitrate * 1000 / sample_rate + padding) * 4;
    } else {
        samples = (layer == 3 && !is_mpeg1) ? 576 : 1152;
        frame_bytes = (samples / 8) * bitrate * 1000 / sample_rate + padding;
    }

    out->sample_rate = sample_rate;
    out->bitrate_kbps = bitrate;
    out->samples_per_frame = samples;
    out->frame_bytes = frame_bytes;
    out->mono = (mode == 3) ? 1 : 0;
    out->channels = (u8)(out->mono ? 1 : 2);
    out->version_id = (u8)version_id;
    out->layer = (u8)layer;
    return 1;
}

// Xing/Info sits after the side information, VBRI at a fixed offset; both give
// the exact frame count, which is the only honest duration for a VBR file.
static b32 tags_mpeg_vbr_frames(String8 frame, const TagsMpegFrame *mpeg, u32 *frames_out) {
    u32 side_info = (mpeg->version_id == 3) ? (mpeg->mono ? 17u : 32u)
                                            : (mpeg->mono ? 9u : 17u);
    TagsReader r = tags_reader(frame);
    tags_skip(&r, 4 + side_info);
    String8 magic = tags_bytes(&r, 4);
    if (!r.fail && (tags_match(magic, "Xing") || tags_match(magic, "Info"))) {
        u32 flags = tags_u32be(&r);
        if (flags & 1) {
            u32 frames = tags_u32be(&r);
            if (!r.fail && frames) { *frames_out = frames; return 1; }
        }
        return 0;
    }
    r = tags_reader(frame);
    tags_skip(&r, 36);
    magic = tags_bytes(&r, 4);
    if (!r.fail && tags_match(magic, "VBRI")) {
        tags_skip(&r, 6);       // version, delay, quality
        tags_skip(&r, 4);       // stream bytes
        u32 frames = tags_u32be(&r);
        if (!r.fail && frames) { *frames_out = frames; return 1; }
    }
    return 0;
}

// `audio_begin` is where the tags stop, `audio_end` where the trailing ones
// start: everything between the two is what the bitrate has to account for.
static void tags_mpeg_scan(Tags *tags, const TagsFile *file, u64 audio_begin, u64 audio_end) {
    String8 window;
    u64 window_size = Min((u64)KB(8), file->head.size > audio_begin
                                          ? file->head.size - audio_begin : 0);
    if (window_size < 4 || !tags_file_slice(file, audio_begin, window_size, &window)) { return; }

    TagsMpegFrame mpeg;
    StructZero(&mpeg);
    u64 at = 0;
    for (; at + 4 <= window.size; at += 1) {
        if (window.str[at] != 0xFF) { continue; }
        if (tags_mpeg_header(window.str + at, &mpeg)) { break; }
    }
    if (at + 4 > window.size) { return; }

    tags->codec = LibCodec_MP3;
    tags->sample_rate = mpeg.sample_rate;
    tags->channels = mpeg.channels;

    u64 frame_begin = audio_begin + at;
    u32 frames = 0;
    String8 frame;
    u64 header_bytes = Min((u64)mpeg.frame_bytes, window.size - at);
    if (header_bytes >= 40 && tags_file_slice(file, frame_begin, header_bytes, &frame) &&
        tags_mpeg_vbr_frames(frame, &mpeg, &frames)) {
        u64 ms = (u64)frames * mpeg.samples_per_frame * 1000 / mpeg.sample_rate;
        tags->duration_ms = (u32)Min(ms, (u64)U32_MAX);
        return;
    }
    if (audio_end > frame_begin) {
        u64 bytes = audio_end - frame_begin;
        u64 ms = bytes * 8 / mpeg.bitrate_kbps;
        tags->duration_ms = (u32)Min(ms, (u64)U32_MAX);
    }
}

// --- ID3v1 -----------------------------------------------------------------
// 128 fixed bytes at the very end, spaces or zeros for padding. v1.1 steals the
// last two bytes of the comment for the track number.

static String8 tags_id3v1_field(String8 raw, u8 *out, u64 cap) {
    u64 size = raw.size;
    while (size && (raw.str[size - 1] == ' ' || raw.str[size - 1] == 0)) { size -= 1; }
    return tags_latin1_to_utf8(str8(raw.str, size), out, cap);
}

static b32 tags_id3v1_parse(Tags *tags, const TagsFile *file) {
    if (file->size < 128) { return 0; }
    String8 block;
    if (!tags_file_slice(file, file->size - 128, 128, &block)) { return 0; }
    TagsReader r = tags_reader(block);
    String8 magic = tags_bytes(&r, 3);
    if (r.fail || magic.str[0] != 'T' || magic.str[1] != 'A' || magic.str[2] != 'G') {
        return 0;
    }
    u8 buffer[TAGS_FIELD_MAX];
    tags_set_title(tags, tags_id3v1_field(tags_bytes(&r, 30), buffer, sizeof(buffer)));
    tags_set_artist(tags, tags_id3v1_field(tags_bytes(&r, 30), buffer, sizeof(buffer)));
    tags_set_album(tags, tags_id3v1_field(tags_bytes(&r, 30), buffer, sizeof(buffer)));
    String8 year = tags_bytes(&r, 4);
    String8 comment = tags_bytes(&r, 30);
    u8 genre = tags_u8(&r);
    if (r.fail) { return 0; }
    if (!tags->year) { tags->year = (u16)tags_parse_u32(year); }
    if (!tags->track_no && comment.str[28] == 0 && comment.str[29] != 0) {
        tags->track_no = comment.str[29];
    }
    tags_set_genre(tags, tags_genre_by_index(genre));
    return 1;
}

// --- ID3v2 -----------------------------------------------------------------

md_inline b32 tags_id3_syncsafe_ok(const u8 *p) {
    return (p[0] | p[1] | p[2] | p[3]) < 0x80;
}

md_inline u32 tags_id3_syncsafe(const u8 *p) {
    return ((u32)(p[0] & 0x7F) << 21) | ((u32)(p[1] & 0x7F) << 14) |
           ((u32)(p[2] & 0x7F) << 7) | (u32)(p[3] & 0x7F);
}

// Splits a frame payload at the terminator of the current encoding: TXXX and
// APIC both carry two strings back to back.
static void tags_id3_split(u8 encoding, String8 raw, String8 *first, String8 *rest) {
    u64 step = (encoding == 1 || encoding == 2) ? 2 : 1;
    for (u64 i = 0; i + step <= raw.size; i += step) {
        b32 zero = raw.str[i] == 0 && (step == 1 || raw.str[i + 1] == 0);
        if (zero) {
            *first = str8(raw.str, i);
            *rest = str8(raw.str + i + step, raw.size - i - step);
            return;
        }
    }
    *first = raw;
    *rest = str8(0, 0);
}

md_inline b32 tags_id3_is(String8 id, const char *name, u64 size) {
    for (u64 i = 0; i < size; i += 1) {
        if (id.str[i] != (u8)name[i]) { return 0; }
    }
    return 1;
}

static void tags_id3_picture(Tags *tags, String8 body, u64 file_offset, b32 v22) {
    TagsReader r = tags_reader(body);
    u8 encoding = tags_u8(&r);
    if (v22) {
        tags_skip(&r, 3);  // "JPG" / "PNG"
    } else {
        while (!r.fail && tags_u8(&r) != 0) {}  // MIME, latin1, nul terminated
    }
    tags_skip(&r, 1);  // picture type
    if (r.fail) { return; }
    String8 rest = tags_bytes(&r, tags_remaining(&r));
    String8 description, data;
    tags_id3_split(encoding, rest, &description, &data);
    if (r.fail || data.size == 0) { return; }
    tags->cover_offset = file_offset + (u64)(data.str - body.str);
    tags->cover_size = (u32)data.size;
    tags->cover_hashed = (u32)data.size;
    tags->cover_hash = hash64(data.str, data.size);
}

static void tags_id3_text_frame(Tags *tags, String8 id, u64 id_size, String8 body) {
    TagsReader r = tags_reader(body);
    u8 encoding = tags_u8(&r);
    String8 raw = tags_bytes(&r, tags_remaining(&r));
    if (r.fail) { return; }

    if (tags_id3_is(id, id_size == 3 ? "TXX" : "TXXX", id_size)) {
        String8 description, value;
        tags_id3_split(encoding, raw, &description, &value);
        u8 name_buffer[TAGS_FIELD_MAX];
        u8 value_buffer[TAGS_FIELD_MAX];
        String8 name = tags_decode(encoding, description, name_buffer, sizeof(name_buffer));
        String8 text = tags_decode(encoding, value, value_buffer, sizeof(value_buffer));
        if (tags_key_is(name, "replaygain_track_gain") &&
            tags->replaygain_track_db == LIB_REPLAYGAIN_NONE) {
            tags->replaygain_track_db = tags_parse_gain_db(text);
        } else if (tags_key_is(name, "replaygain_album_gain") &&
                   tags->replaygain_album_db == LIB_REPLAYGAIN_NONE) {
            tags->replaygain_album_db = tags_parse_gain_db(text);
        }
        return;
    }

    u8 buffer[TAGS_FIELD_MAX];
    String8 first, ignored;
    tags_id3_split(encoding, raw, &first, &ignored);  // v2.4 lists: keep the first
    String8 text = tags_decode(encoding, first, buffer, sizeof(buffer));
    if (!text.size) { return; }

    if (tags_id3_is(id, id_size == 3 ? "TT2" : "TIT2", id_size)) {
        tags_set_title(tags, text);
    } else if (tags_id3_is(id, id_size == 3 ? "TP1" : "TPE1", id_size)) {
        tags_set_artist(tags, text);
    } else if (tags_id3_is(id, id_size == 3 ? "TAL" : "TALB", id_size)) {
        tags_set_album(tags, text);
    } else if (tags_id3_is(id, id_size == 3 ? "TP2" : "TPE2", id_size)) {
        tags_set_album_artist(tags, text);
    } else if (tags_id3_is(id, id_size == 3 ? "TCO" : "TCON", id_size)) {
        // "17", "(17)" and "(17)Rock" all mean the numeric table.
        u64 at = (text.str[0] == '(') ? 1 : 0;
        if (at < text.size && text.str[at] >= '0' && text.str[at] <= '9') {
            String8 named = tags_genre_by_index(tags_parse_u32(str8_skip(text, at)));
            tags_set_genre(tags, named.size ? named : text);
        } else {
            tags_set_genre(tags, text);
        }
    } else if (tags_id3_is(id, id_size == 3 ? "TRK" : "TRCK", id_size)) {
        if (!tags->track_no) { tags_parse_pair(text, &tags->track_no); }
    } else if (tags_id3_is(id, id_size == 3 ? "TPA" : "TPOS", id_size)) {
        if (!tags->disc_no) { tags_parse_pair(text, &tags->disc_no); }
    } else if (tags_id3_is(id, id_size == 3 ? "TYE" : "TYER", id_size) ||
               tags_id3_is(id, id_size == 3 ? "TDR" : "TDRC", id_size)) {
        if (!tags->year) { tags->year = (u16)tags_parse_u32(text); }
    }
}

// 0xFF 0x00 -> 0xFF. The copy is what lets the frame walk stay a plain bounded
// read; the price is that a picture inside an unsynchronised tag loses its file
// offset, which we report as 0.
static String8 tags_id3_deunsync(Arena *arena, String8 body) {
    u8 *out = push_array(arena, u8, body.size);
    u64 count = 0;
    for (u64 i = 0; i < body.size; i += 1) {
        out[count++] = body.str[i];
        if (body.str[i] == 0xFF && i + 1 < body.size && body.str[i + 1] == 0x00) { i += 1; }
    }
    return str8(out, count);
}

// Returns the total size of the tag on disk (header included), 0 when there is
// no ID3v2 tag at `offset`.
static u64 tags_id3v2_parse(Tags *tags, const TagsFile *file, u64 offset) {
    String8 header;
    if (!tags_file_slice(file, offset, 10, &header)) { return 0; }
    if (header.str[0] != 'I' || header.str[1] != 'D' || header.str[2] != '3') { return 0; }
    u8 major = header.str[3];
    if (major < 2 || major > 4 || header.str[4] == 0xFF) { return 0; }
    if (!tags_id3_syncsafe_ok(header.str + 6)) { return 0; }
    u32 body_size = tags_id3_syncsafe(header.str + 6);
    u8 flags = header.str[5];
    u64 total = 10 + (u64)body_size + ((flags & 0x10) ? 10u : 0u);

    String8 body;
    if (!tags_file_slice(file, offset + 10, body_size, &body)) { return total; }

    ArenaTemp scratch = scratch_begin(0, 0);
    u64 body_offset = offset + 10;
    b32 unsynchronised = (flags & 0x80) != 0;
    if (unsynchronised) {
        body = tags_id3_deunsync(scratch.arena, body);
        body_offset = 0;  // offsets no longer map to the file
    }

    TagsReader r = tags_reader(body);
    if (flags & 0x40) {  // extended header: v2.3 plain size, v2.4 syncsafe and self inclusive
        String8 ext = tags_bytes(&r, 4);
        if (!r.fail) {
            if (major >= 4) {
                u32 ext_size = tags_id3_syncsafe(ext.str);
                tags_skip(&r, ext_size >= 4 ? ext_size - 4 : 0);
            } else {
                u32 ext_size = ((u32)ext.str[0] << 24) | ((u32)ext.str[1] << 16) |
                               ((u32)ext.str[2] << 8) | (u32)ext.str[3];
                tags_skip(&r, ext_size);
            }
        }
    }

    u64 id_size = (major == 2) ? 3 : 4;
    u64 header_size = (major == 2) ? 6 : 10;
    while (!r.fail && tags_remaining(&r) >= header_size) {
        String8 id = tags_bytes(&r, id_size);
        if (r.fail || id.str[0] == 0) { break; }
        b32 valid = 1;
        for (u64 i = 0; i < id_size; i += 1) {
            u8 c = id.str[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) { valid = 0; }
        }
        if (!valid) { break; }

        u32 size;
        u16 frame_flags = 0;
        if (major == 2) {
            size = tags_u24be(&r);
        } else {
            String8 raw_size = tags_bytes(&r, 4);
            if (r.fail) { break; }
            size = ((u32)raw_size.str[0] << 24) | ((u32)raw_size.str[1] << 16) |
                   ((u32)raw_size.str[2] << 8) | (u32)raw_size.str[3];
            // v2.4 says syncsafe; enough encoders write a plain u32 that we take
            // the syncsafe reading only when it is actually a legal one.
            if (major == 4 && tags_id3_syncsafe_ok(raw_size.str)) {
                size = tags_id3_syncsafe(raw_size.str);
            }
            frame_flags = tags_u16be(&r);
        }
        if (r.fail || size == 0 || size > tags_remaining(&r)) { break; }

        u64 payload_offset = body_offset ? body_offset + r.at : 0;
        String8 payload = tags_bytes(&r, size);
        if (r.fail) { break; }

        b32 skip = (major == 4) ? ((frame_flags & 0x000C) != 0)   // compressed / encrypted
                                : ((frame_flags & 0x00C0) != 0);
        if (major == 4 && (frame_flags & 0x0001)) {               // data length indicator
            payload = str8_skip(payload, 4);
            payload_offset = payload_offset ? payload_offset + 4 : 0;
        }
        if (major == 4 && (frame_flags & 0x0002)) {               // per frame unsync
            payload = tags_id3_deunsync(scratch.arena, payload);
            payload_offset = 0;
        }
        if (skip || payload.size == 0) { continue; }

        if (id.str[0] == 'T') {
            tags_id3_text_frame(tags, id, id_size, payload);
        } else if (tags_id3_is(id, id_size == 3 ? "PIC" : "APIC", id_size) &&
                   tags->cover_size == 0) {
            tags_id3_picture(tags, payload, payload_offset, id_size == 3);
        }
    }
    scratch_end(scratch);
    return total;
}

// The whole MP3 story: a leading ID3v2, the audio, then whatever trailing tags
// the file grew, and the frame header that gives the real sample rate.
b32 tags_mp3_parse(Tags *tags, const TagsFile *file) {
    u64 audio_begin = tags_id3v2_parse(tags, file, 0);
    if (audio_begin > file->size) { audio_begin = 0; }
    u64 audio_end = file->size;

    u64 ape_size = tags_ape_parse(tags, file);
    if (ape_size && ape_size <= audio_end) { audio_end -= ape_size; }
    if (tags_id3v1_parse(tags, file) && audio_end >= 128) { audio_end -= 128; }

    tags_mpeg_scan(tags, file, audio_begin, audio_end);
    if (tags->codec == LibCodec_Unknown && audio_begin == 0) { return 0; }
    tags->codec = LibCodec_MP3;
    return 1;
}

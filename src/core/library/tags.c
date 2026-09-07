// tags.c - the text helpers every parser shares, and the dispatcher.
//
// The dispatch is on the signature, never on the extension: a .mp3 that is
// really a FLAC is read as a FLAC, and a file whose extension lies about
// nothing still costs one comparison per format.

#include "tags.h"

// --- text ------------------------------------------------------------------

void tags_init(Tags *tags) {
    StructZero(tags);
    tags->replaygain_track_db = LIB_REPLAYGAIN_NONE;
    tags->replaygain_album_db = LIB_REPLAYGAIN_NONE;
}

// Trims, caps at one field, and copies into the struct's own bytes. A field
// that no longer fits is dropped rather than truncated mid codepoint.
String8 tags_text_push(Tags *tags, String8 utf8) {
    String8 trimmed = str8_trim(utf8);
    if (trimmed.size > TAGS_FIELD_MAX) { trimmed.size = TAGS_FIELD_MAX; }
    if (trimmed.size == 0 || tags->text_size + trimmed.size > TAGS_TEXT_CAPACITY) {
        return str8(0, 0);
    }
    u8 *at = tags->text + tags->text_size;
    mem_copy(at, trimmed.str, trimmed.size);
    tags->text_size += (u32)trimmed.size;
    return str8(at, trimmed.size);
}

// Canonical UTF-8 out of whatever was in the file: every sequence is decoded
// and re-encoded, so an invalid byte becomes U+FFFD instead of travelling
// through the library and into the renderer.
String8 tags_utf8_clean(String8 raw, u8 *out, u64 cap) {
    u64 count = 0;
    for (u64 i = 0; i < raw.size;) {
        if (raw.str[i] == 0) { break; }
        UnicodeDecode decoded = utf8_decode(raw.str + i, raw.size - i);
        i += decoded.advance ? decoded.advance : 1;
        if (count + 4 > cap) { break; }
        count += utf8_encode(out + count, decoded.codepoint);
    }
    return str8(out, count);
}

String8 tags_latin1_to_utf8(String8 raw, u8 *out, u64 cap) {
    u64 count = 0;
    for (u64 i = 0; i < raw.size && raw.str[i] != 0; i += 1) {
        if (count + 2 > cap) { break; }
        count += utf8_encode(out + count, raw.str[i]);
    }
    return str8(out, count);
}

String8 tags_utf16_to_utf8(String8 raw, b32 big_endian, u8 *out, u64 cap) {
    u64 count = 0;
    for (u64 i = 0; i + 2 <= raw.size; i += 2) {
        u16 units[2];
        u64 available = 0;
        for (u64 u = 0; u < 2 && i + u * 2 + 2 <= raw.size; u += 1) {
            u8 a = raw.str[i + u * 2];
            u8 b = raw.str[i + u * 2 + 1];
            units[u] = big_endian ? (u16)((a << 8) | b) : (u16)((b << 8) | a);
            available += 1;
        }
        if (units[0] == 0) { break; }
        UnicodeDecode decoded = utf16_decode(units, available);
        if (decoded.advance == 2) { i += 2; }
        if (count + 4 > cap) { break; }
        count += utf8_encode(out + count, decoded.codepoint);
    }
    return str8(out, count);
}

String8 tags_decode(u8 encoding, String8 raw, u8 *out, u64 cap) {
    if (encoding == 3) { return tags_utf8_clean(raw, out, cap); }
    if (encoding == 2) { return tags_utf16_to_utf8(raw, 1, out, cap); }
    if (encoding == 1) {
        b32 big_endian = 0;
        if (raw.size >= 2 && raw.str[0] == 0xFF && raw.str[1] == 0xFE) {
            raw = str8_skip(raw, 2);
        } else if (raw.size >= 2 && raw.str[0] == 0xFE && raw.str[1] == 0xFF) {
            raw = str8_skip(raw, 2);
            big_endian = 1;
        }
        return tags_utf16_to_utf8(raw, big_endian, out, cap);
    }
    return tags_latin1_to_utf8(raw, out, cap);
}

u32 tags_parse_u32(String8 s) {
    u64 value = 0;
    u64 i = 0;
    while (i < s.size && (s.str[i] == ' ' || s.str[i] == '\t')) { i += 1; }
    for (; i < s.size && s.str[i] >= '0' && s.str[i] <= '9'; i += 1) {
        value = value * 10 + (u64)(s.str[i] - '0');
        if (value > U32_MAX) { return U32_MAX; }
    }
    return (u32)value;
}

void tags_parse_pair(String8 s, u16 *number) {
    u32 value = tags_parse_u32(s);   // "3" and "3/12" both start with the number
    *number = (u16)Min(value, (u32)U16_MAX);
}

// "-7.25 dB" -> -1856. Fixed point on purpose: no float parsing, no rounding
// surprises, and the value lands straight in the library's i16 column.
i16 tags_parse_gain_db(String8 s) {
    u64 i = 0;
    while (i < s.size && (s.str[i] == ' ' || s.str[i] == '+')) { i += 1; }
    b32 negative = 0;
    if (i < s.size && s.str[i] == '-') { negative = 1; i += 1; }
    i64 whole = 0;
    for (; i < s.size && s.str[i] >= '0' && s.str[i] <= '9'; i += 1) {
        whole = whole * 10 + (s.str[i] - '0');
        if (whole > 512) { return LIB_REPLAYGAIN_NONE; }
    }
    i64 fraction = 0;
    i64 scale = 1;
    if (i < s.size && s.str[i] == '.') {
        i += 1;
        for (; i < s.size && s.str[i] >= '0' && s.str[i] <= '9' && scale < 10000; i += 1) {
            fraction = fraction * 10 + (s.str[i] - '0');
            scale *= 10;
        }
    }
    i64 value = whole * 256 + (fraction * 256) / scale;
    if (negative) { value = -value; }
    if (value <= -32768 || value >= 32767) { return LIB_REPLAYGAIN_NONE; }
    return (i16)value;
}

// The ID3v1 genre table, 0..125: one packed string rather than 126 pointers,
// because a table of pointers into .rdata is 1 KB of relocations for nothing.
static const char tags_genre_names[] =
    "Blues\nClassic Rock\nCountry\nDance\nDisco\nFunk\nGrunge\nHip-Hop\nJazz\nMetal\n"
    "New Age\nOldies\nOther\nPop\nR&B\nRap\nReggae\nRock\nTechno\nIndustrial\n"
    "Alternative\nSka\nDeath Metal\nPranks\nSoundtrack\nEuro-Techno\nAmbient\nTrip-Hop\n"
    "Vocal\nJazz+Funk\nFusion\nTrance\nClassical\nInstrumental\nAcid\nHouse\nGame\n"
    "Sound Clip\nGospel\nNoise\nAlternRock\nBass\nSoul\nPunk\nSpace\nMeditative\n"
    "Instrumental Pop\nInstrumental Rock\nEthnic\nGothic\nDarkwave\nTechno-Industrial\n"
    "Electronic\nPop-Folk\nEurodance\nDream\nSouthern Rock\nComedy\nCult\nGangsta\nTop 40\n"
    "Christian Rap\nPop/Funk\nJungle\nNative American\nCabaret\nNew Wave\nPsychadelic\nRave\n"
    "Showtunes\nTrailer\nLo-Fi\nTribal\nAcid Punk\nAcid Jazz\nPolka\nRetro\nMusical\n"
    "Rock & Roll\nHard Rock\nFolk\nFolk-Rock\nNational Folk\nSwing\nFast Fusion\nBebob\n"
    "Latin\nRevival\nCeltic\nBluegrass\nAvantgarde\nGothic Rock\nProgressive Rock\n"
    "Psychedelic Rock\nSymphonic Rock\nSlow Rock\nBig Band\nChorus\nEasy Listening\n"
    "Acoustic\nHumour\nSpeech\nChanson\nOpera\nChamber Music\nSonata\nSymphony\nBooty Bass\n"
    "Primus\nPorn Groove\nSatire\nSlow Jam\nClub\nTango\nSamba\nFolklore\nBallad\n"
    "Power Ballad\nRhythmic Soul\nFreestyle\nDuet\nPunk Rock\nDrum Solo\nA capella\n"
    "Euro-House\nDance Hall\n";

String8 tags_genre_by_index(u32 index) {
    u64 at = 0;
    for (u32 i = 0; i < index; i += 1) {
        while (tags_genre_names[at] && tags_genre_names[at] != '\n') { at += 1; }
        if (!tags_genre_names[at]) { return str8(0, 0); }
        at += 1;
    }
    u64 begin = at;
    while (tags_genre_names[at] && tags_genre_names[at] != '\n') { at += 1; }
    if (at == begin) { return str8(0, 0); }
    return str8((u8 *)tags_genre_names + begin, at - begin);
}

// --- the dispatcher --------------------------------------------------------

b32 tags_parse(Tags *tags, const TagsFile *file) {
    String8 magic;
    if (!tags_file_slice(file, 0, 12, &magic)) {
        // Under 12 bytes there is no container at all, only a possible ID3v1.
        return tags_file_slice(file, 0, 4, &magic) ? tags_mp3_parse(tags, file) : 0;
    }
    if (tags_match(magic, "fLaC")) { return tags_flac_parse(tags, file); }
    if (tags_match(magic, "OggS")) { return tags_ogg_parse(tags, file); }
    if (tags_match(magic, "RIFF")) { return tags_wav_parse(tags, file); }
    if (tags_match(magic, "FORM")) { return tags_aiff_parse(tags, file); }
    if (tags_match(str8_skip(magic, 4), "ftyp")) { return tags_mp4_parse(tags, file); }
    if (tags_match(magic, "ID3\x02") || tags_match(magic, "ID3\x03") ||
        tags_match(magic, "ID3\x04") || (magic.str[0] == 0xFF && (magic.str[1] & 0xE0) == 0xE0)) {
        return tags_mp3_parse(tags, file);
    }
    // No leading signature: the tag may still be at the end (APEv2 on a
    // WavPack, an ID3v1 on a stripped MP3).
    if (tags_ape_parse(tags, file)) { return 1; }
    return tags_mp3_parse(tags, file);
}

void tags_fallback_from_path(Tags *tags, String8 path) {
    String8 name = os_path_filename(path);
    String8 extension = os_path_extension(name);
    if (extension.size && name.size > extension.size) {
        name = str8_prefix(name, name.size - extension.size - 1);
    }
    tags_set_title(tags, name);
    String8 folder = os_path_parent(path);
    tags_set_album(tags, os_path_filename(folder));
    tags_set_artist(tags, os_path_filename(os_path_parent(folder)));
}

// The shared body: `capture` is 0 for the tag wave, a buffer for a cover job.
static b32 tags_read_file_capture(Tags *tags, String8 path, u64 size, u8 *head, u8 *tail,
                                  u8 *capture, u32 capture_size) {
    tags_init(tags);
    tags->cover_capture = capture;
    tags->cover_capture_size = capture_size;
    TagsFile file;
    StructZero(&file);
    file.size = size;

    OsFile handle = os_file_open(path);
    b32 opened = handle.v != 0;
    if (opened) {
        u64 head_size = Min(size, TAGS_BLOCK_SIZE);
        file.head = str8(head, os_file_read_at(handle, 0, head, head_size));
        if (size > head_size) {
            u64 tail_size = Min(size - head_size, TAGS_BLOCK_SIZE);
            file.tail_offset = size - tail_size;
            file.tail = str8(tail, os_file_read_at(handle, file.tail_offset, tail, tail_size));
        }
        os_file_close(handle);
    }
    // The size we were told and the bytes we actually got can disagree if the
    // file shrank between the scan and now: the windows are what is real.
    if (file.tail.size == 0 && file.head.size < file.size) { file.size = file.head.size; }

    b32 parsed = opened && file.head.size >= 4 && tags_parse(tags, &file);
    tags_fallback_from_path(tags, path);
    if (tags->codec == LibCodec_Unknown) {
        tags->codec = (u8)lib_codec_from_extension(os_path_extension(path));
    }
    return parsed;
}

b32 tags_read_file(Tags *tags, String8 path, u64 size, u8 *head, u8 *tail) {
    return tags_read_file_capture(tags, path, size, head, tail, 0, 0);
}

String8 tags_read_cover(Tags *tags, String8 path, u64 size, u8 *head, u8 *tail, u8 *cover,
                        u32 cover_capacity) {
    tags_read_file_capture(tags, path, size, head, tail, cover, cover_capacity);
    return str8(cover, tags->cover_captured);
}

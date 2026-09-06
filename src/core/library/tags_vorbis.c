// tags_vorbis.c - FLAC and Ogg (Vorbis, Opus). One comment format, two
// containers, and two very different ways of saying how long the stream is:
// FLAC states the sample count in STREAMINFO, Ogg only knows it from the
// granule position of its last page - which is why we read the tail at all.

// --- the shared comment ----------------------------------------------------
// "KEY=value", the key ASCII and case insensitive, the value UTF-8. Nothing
// here trusts the lengths: a comment claiming 4 GB simply ends the walk.

void tags_vorbis_comment(Tags *tags, String8 comment) {
    u64 equals = str8_find(comment, str8_lit("="), 0);
    if (equals >= comment.size) { return; }
    String8 key = str8_prefix(comment, equals);
    String8 raw = str8_skip(comment, equals + 1);
    u8 buffer[TAGS_FIELD_MAX];
    String8 value = tags_utf8_clean(raw, buffer, sizeof(buffer));
    if (!value.size) { return; }

    if (tags_key_is(key, "title")) { tags_set_title(tags, value); }
    else if (tags_key_is(key, "artist")) { tags_set_artist(tags, value); }
    else if (tags_key_is(key, "album")) { tags_set_album(tags, value); }
    else if (tags_key_is(key, "albumartist") || tags_key_is(key, "album artist")) {
        tags_set_album_artist(tags, value);
    } else if (tags_key_is(key, "genre")) { tags_set_genre(tags, value); }
    else if (tags_key_is(key, "tracknumber")) {
        if (!tags->track_no) { tags_parse_pair(value, &tags->track_no); }
    } else if (tags_key_is(key, "discnumber")) {
        if (!tags->disc_no) { tags_parse_pair(value, &tags->disc_no); }
    } else if (tags_key_is(key, "date") || tags_key_is(key, "year")) {
        if (!tags->year) { tags->year = (u16)tags_parse_u32(value); }
    } else if (tags_key_is(key, "replaygain_track_gain")) {
        if (tags->replaygain_track_db == LIB_REPLAYGAIN_NONE) {
            tags->replaygain_track_db = tags_parse_gain_db(value);
        }
    } else if (tags_key_is(key, "replaygain_album_gain")) {
        if (tags->replaygain_album_db == LIB_REPLAYGAIN_NONE) {
            tags->replaygain_album_db = tags_parse_gain_db(value);
        }
    }
}

// The comment block proper: vendor string, count, then count comments, all
// little endian.
static void tags_vorbis_comments(Tags *tags, String8 block) {
    TagsReader r = tags_reader(block);
    u32 vendor_size = tags_u32le(&r);
    tags_skip(&r, vendor_size);
    u32 count = tags_u32le(&r);
    for (u32 i = 0; i < count && !r.fail; i += 1) {
        u32 size = tags_u32le(&r);
        String8 comment = tags_bytes(&r, size);
        if (r.fail) { break; }
        tags_vorbis_comment(tags, comment);
    }
}

// --- FLAC ------------------------------------------------------------------

static void tags_flac_streaminfo(Tags *tags, String8 block) {
    TagsReader r = tags_reader(block);
    tags_skip(&r, 10);  // block sizes and frame sizes
    // 20 bits sample rate, 3 channels - 1, 5 bits per sample - 1, 36 total.
    u64 packed = (u64)tags_u32be(&r) << 32;
    packed |= tags_u32be(&r);
    if (r.fail) { return; }
    u32 sample_rate = (u32)(packed >> 44);
    u32 channels = (u32)((packed >> 41) & 7) + 1;
    u64 samples = packed & 0xFFFFFFFFFull;
    if (sample_rate == 0) { return; }
    tags->sample_rate = sample_rate;
    tags->channels = (u8)channels;
    tags->duration_ms = (u32)Min(samples * 1000 / sample_rate, (u64)U32_MAX);
}

static void tags_flac_picture(Tags *tags, String8 block, u64 block_offset) {
    TagsReader r = tags_reader(block);
    tags_skip(&r, 4);                       // picture type
    tags_skip(&r, tags_u32be(&r));          // MIME
    tags_skip(&r, tags_u32be(&r));          // description
    tags_skip(&r, 16);                      // width, height, depth, colours
    u32 size = tags_u32be(&r);
    String8 data = tags_bytes(&r, size);
    if (r.fail || !data.size) { return; }
    tags->cover_offset = block_offset + (u64)(data.str - block.str);
    tags->cover_size = size;
    tags->cover_hashed = (u32)data.size;
    tags->cover_hash = hash64(data.str, data.size);
}

b32 tags_flac_parse(Tags *tags, const TagsFile *file) {
    String8 magic;
    if (!tags_file_slice(file, 0, 4, &magic) || !tags_match(magic, "fLaC")) { return 0; }
    tags->codec = LibCodec_FLAC;

    u64 at = 4;
    for (u32 guard = 0; guard < 64; guard += 1) {
        String8 header;
        if (!tags_file_slice(file, at, 4, &header)) { break; }
        b32 last = (header.str[0] & 0x80) != 0;
        u32 type = header.str[0] & 0x7F;
        u32 size = ((u32)header.str[1] << 16) | ((u32)header.str[2] << 8) | header.str[3];
        String8 block;
        if (!tags_file_slice(file, at + 4, size, &block)) { break; }
        if (type == 0) { tags_flac_streaminfo(tags, block); }
        else if (type == 4) { tags_vorbis_comments(tags, block); }
        else if (type == 6 && tags->cover_size == 0) {
            tags_flac_picture(tags, block, at + 4);
        }
        at += 4 + (u64)size;
        if (last) { break; }
    }
    return 1;
}

// --- Ogg -------------------------------------------------------------------
// A page is a 27 byte header plus a segment table; the payload is the sum of
// the table. We only ever need the first two pages (identification, comment)
// and the last one (granule position).

typedef struct TagsOggPage {
    u64 granule;
    u32 serial;
    u64 payload_offset;
    u32 payload_size;
    u64 total_size;
} TagsOggPage;

static b32 tags_ogg_page(const TagsFile *file, u64 offset, TagsOggPage *out) {
    String8 header;
    if (!tags_file_slice(file, offset, 27, &header)) { return 0; }
    TagsReader r = tags_reader(header);
    String8 magic = tags_bytes(&r, 4);
    if (!tags_match(magic, "OggS")) { return 0; }
    if (tags_u8(&r) != 0) { return 0; }   // stream structure version
    tags_skip(&r, 1);                     // header type
    out->granule = tags_u64le(&r);
    out->serial = tags_u32le(&r);
    tags_skip(&r, 8);                     // page sequence, CRC
    u32 segments = tags_u8(&r);
    if (r.fail) { return 0; }
    String8 table;
    if (!tags_file_slice(file, offset + 27, segments, &table)) { return 0; }
    u32 payload = 0;
    for (u32 i = 0; i < segments; i += 1) { payload += table.str[i]; }
    out->payload_offset = offset + 27 + segments;
    out->payload_size = payload;
    out->total_size = 27 + (u64)segments + payload;
    return 1;
}

b32 tags_ogg_parse(Tags *tags, const TagsFile *file) {
    TagsOggPage page;
    if (!tags_ogg_page(file, 0, &page)) { return 0; }
    String8 payload;
    if (!tags_file_slice(file, page.payload_offset, page.payload_size, &payload)) { return 0; }

    b32 opus = payload.size >= 8 && tags_match(payload, "Opus") &&
               tags_match(str8_skip(payload, 4), "Head");
    b32 vorbis = payload.size >= 7 && payload.str[0] == 1 &&
                 tags_match(str8_skip(payload, 1), "vorb");
    if (!opus && !vorbis) { return 0; }

    u32 rate = 48000;
    u64 preskip = 0;
    TagsReader r = tags_reader(payload);
    if (opus) {
        tags->codec = LibCodec_OPUS;
        tags_skip(&r, 9);                 // "OpusHead", version
        tags->channels = tags_u8(&r);
        preskip = tags_u16le(&r);
    } else {
        tags->codec = LibCodec_OGG;
        tags_skip(&r, 11);                // packet type, "vorbis", version
        tags->channels = tags_u8(&r);
        rate = tags_u32le(&r);
    }
    if (r.fail || rate == 0) { return 1; }
    tags->sample_rate = opus ? 48000 : rate;

    // The comment header is the second packet; it starts on the next page and
    // may span several, so we only read what one page holds - which is all any
    // sane encoder writes.
    TagsOggPage comment_page;
    if (tags_ogg_page(file, page.total_size, &comment_page) &&
        tags_file_slice(file, comment_page.payload_offset, comment_page.payload_size, &payload)) {
        if (opus && payload.size > 8 && tags_match(payload, "Opus")) {
            tags_vorbis_comments(tags, str8_skip(payload, 8));
        } else if (!opus && payload.size > 7 && payload.str[0] == 3) {
            tags_vorbis_comments(tags, str8_skip(payload, 7));
        }
    }

    // The duration: the granule of the last page in the tail window. Scanning
    // backwards for "OggS" is what every player does; the serial number check
    // is what keeps a chained stream from lying to us.
    String8 tail = file->tail.size ? file->tail : file->head;
    u64 granule = 0;
    for (u64 i = tail.size; i >= 4; i -= 1) {
        String8 candidate = str8(tail.str + i - 4, 4);
        if (!tags_match(candidate, "OggS")) { continue; }
        u64 offset = (file->tail.size ? file->tail_offset : 0) + i - 4;
        TagsOggPage last;
        if (tags_ogg_page(file, offset, &last) && last.serial == page.serial) {
            granule = last.granule;
            break;
        }
    }
    if (granule && granule != U64_MAX) {
        u64 samples = granule > preskip ? granule - preskip : 0;
        tags->duration_ms = (u32)Min(samples * 1000 / tags->sample_rate, (u64)U32_MAX);
    }
    return 1;
}

// tags_riff.c - WAV (RIFF, little endian) and AIFF (FORM, big endian). Two
// chunked containers with opposite byte orders and the same shape: a walk that
// stops the moment a size does not fit inside its parent.

// --- WAV -------------------------------------------------------------------

static void tags_riff_info(Tags *tags, String8 list) {
    TagsReader r = tags_reader(list);
    tags_skip(&r, 4);  // "INFO"
    while (!r.fail && tags_remaining(&r) >= 8) {
        String8 id = tags_bytes(&r, 4);
        u32 size = tags_u32le(&r);
        String8 body = tags_bytes(&r, size);
        if (r.fail) { break; }
        if (size & 1) { tags_skip(&r, 1); }  // chunks are word aligned

        u64 length = body.size;
        while (length && body.str[length - 1] == 0) { length -= 1; }
        u8 buffer[TAGS_FIELD_MAX];
        String8 text = tags_latin1_to_utf8(str8(body.str, length), buffer, sizeof(buffer));
        if (!text.size) { continue; }
        if (tags_match(id, "INAM")) { tags_set_title(tags, text); }
        else if (tags_match(id, "IART")) { tags_set_artist(tags, text); }
        else if (tags_match(id, "IPRD")) { tags_set_album(tags, text); }
        else if (tags_match(id, "IGNR")) { tags_set_genre(tags, text); }
        else if (tags_match(id, "ITRK")) {
            if (!tags->track_no) { tags_parse_pair(text, &tags->track_no); }
        } else if (tags_match(id, "ICRD")) {
            if (!tags->year) { tags->year = (u16)tags_parse_u32(text); }
        }
    }
}

b32 tags_wav_parse(Tags *tags, const TagsFile *file) {
    String8 header;
    if (!tags_file_slice(file, 0, 12, &header)) { return 0; }
    if (!tags_match(header, "RIFF") || !tags_match(str8_skip(header, 8), "WAVE")) { return 0; }
    tags->codec = LibCodec_WAV;

    u32 byte_rate = 0;
    u64 at = 12;
    for (u32 guard = 0; guard < 64; guard += 1) {
        String8 chunk_header;
        if (!tags_file_slice(file, at, 8, &chunk_header)) { break; }
        TagsReader hr = tags_reader(chunk_header);
        String8 id = tags_bytes(&hr, 4);
        u32 size = tags_u32le(&hr);
        u64 body_offset = at + 8;

        if (tags_match(id, "fmt ") && size >= 16) {
            String8 body;
            if (tags_file_slice(file, body_offset, 16, &body)) {
                TagsReader r = tags_reader(body);
                tags_skip(&r, 2);  // format tag
                tags->channels = (u8)tags_u16le(&r);
                tags->sample_rate = tags_u32le(&r);
                byte_rate = tags_u32le(&r);
            }
        } else if (tags_match(id, "data")) {
            // The declared length, not what is left of the file: a WAV whose
            // tail was cut still knows how long it was meant to be.
            if (byte_rate) {
                tags->duration_ms = (u32)Min((u64)size * 1000 / byte_rate, (u64)U32_MAX);
            }
        } else if (tags_match(id, "LIST") && size >= 4) {
            String8 body;
            if (tags_file_slice(file, body_offset, Min((u64)size, TAGS_BLOCK_SIZE / 2), &body) &&
                tags_match(body, "INFO")) {
                tags_riff_info(tags, body);
            }
        }
        at = body_offset + size + (size & 1);
        if (at > file->size) { break; }
    }
    return 1;
}

// --- AIFF ------------------------------------------------------------------
// The sample rate is an 80 bit IEEE extended: a 15 bit exponent biased by
// 16383 and an explicit 64 bit mantissa. Audio rates are small integers, so
// shifting the mantissa by the exponent is exact and needs no float at all.

static u32 tags_aiff_rate(const u8 *p) {
    u32 exponent = (u32)(((u32)p[0] << 8) | p[1]) & 0x7FFF;
    u64 mantissa = 0;
    for (u32 i = 0; i < 8; i += 1) { mantissa = (mantissa << 8) | p[2 + i]; }
    if (exponent == 0 || exponent < 16383 || exponent > 16383 + 63) { return 0; }
    u32 shift = 63 - (exponent - 16383);
    return (u32)(mantissa >> shift);
}

b32 tags_aiff_parse(Tags *tags, const TagsFile *file) {
    String8 header;
    if (!tags_file_slice(file, 0, 12, &header)) { return 0; }
    if (!tags_match(header, "FORM")) { return 0; }
    String8 form = str8_skip(header, 8);
    if (!tags_match(form, "AIFF") && !tags_match(form, "AIFC")) { return 0; }
    tags->codec = LibCodec_AIFF;

    u64 at = 12;
    for (u32 guard = 0; guard < 64; guard += 1) {
        String8 chunk_header;
        if (!tags_file_slice(file, at, 8, &chunk_header)) { break; }
        TagsReader hr = tags_reader(chunk_header);
        String8 id = tags_bytes(&hr, 4);
        u32 size = tags_u32be(&hr);
        u64 body_offset = at + 8;

        if (tags_match(id, "COMM") && size >= 18) {
            String8 body;
            if (tags_file_slice(file, body_offset, 18, &body)) {
                TagsReader r = tags_reader(body);
                tags->channels = (u8)tags_u16be(&r);
                u32 frames = tags_u32be(&r);
                tags_skip(&r, 2);  // bits per sample
                String8 rate = tags_bytes(&r, 10);
                if (!r.fail) {
                    tags->sample_rate = tags_aiff_rate(rate.str);
                    if (tags->sample_rate) {
                        tags->duration_ms =
                            (u32)Min((u64)frames * 1000 / tags->sample_rate, (u64)U32_MAX);
                    }
                }
            }
        } else if (tags_match(id, "NAME") || tags_match(id, "AUTH")) {
            String8 body;
            if (tags_file_slice(file, body_offset, Min((u64)size, (u64)TAGS_FIELD_MAX), &body)) {
                u8 buffer[TAGS_FIELD_MAX];
                String8 text = tags_latin1_to_utf8(body, buffer, sizeof(buffer));
                if (tags_match(id, "NAME")) { tags_set_title(tags, text); }
                else { tags_set_artist(tags, text); }
            }
        }
        at = body_offset + size + (size & 1);
        if (at > file->size) { break; }
    }
    return 1;
}

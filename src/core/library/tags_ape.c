// tags_ape.c - APEv2, the tag that lives at the end of the file: WavPack and
// Musepack use it as their only tag, and an MP3 can carry one after its audio.
//
// Everything is little endian, keys are ASCII and nul terminated, values are
// UTF-8. The footer is the entry point, and its own size field is the only
// thing that says where the tag starts - so it is checked against the file.

#define TAGS_APE_FOOTER 32

// Returns the number of bytes the tag occupies at the end of the file (footer
// and, when present, header included), 0 when there is none.
u64 tags_ape_parse(Tags *tags, const TagsFile *file) {
    if (file->size < TAGS_APE_FOOTER) { return 0; }
    String8 footer;
    if (!tags_file_slice(file, file->size - TAGS_APE_FOOTER, TAGS_APE_FOOTER, &footer)) {
        return 0;
    }
    if (!tags_match(footer, "APET") || !tags_match(str8_skip(footer, 4), "AGEX")) { return 0; }

    TagsReader r = tags_reader(footer);
    tags_skip(&r, 8);
    u32 version = tags_u32le(&r);
    u32 tag_size = tags_u32le(&r);   // items + this footer, header excluded
    u32 item_count = tags_u32le(&r);
    u32 flags = tags_u32le(&r);
    if (r.fail || version < 1000 || tag_size < TAGS_APE_FOOTER || tag_size > file->size) {
        return 0;
    }
    b32 has_header = (flags & 0x80000000u) != 0;
    u64 total = (u64)tag_size + (has_header ? TAGS_APE_FOOTER : 0);
    if (total > file->size) { return 0; }

    u32 items_size = tag_size - TAGS_APE_FOOTER;
    String8 items;
    if (!tags_file_slice(file, file->size - tag_size, items_size, &items)) { return total; }

    TagsReader ir = tags_reader(items);
    for (u32 i = 0; i < item_count && !ir.fail; i += 1) {
        u32 value_size = tags_u32le(&ir);
        u32 item_flags = tags_u32le(&ir);
        u64 key_begin = ir.at;
        while (!ir.fail && tags_u8(&ir) != 0) {}
        if (ir.fail) { break; }
        String8 key = str8(items.str + key_begin, ir.at - key_begin - 1);
        String8 value = tags_bytes(&ir, value_size);
        if (ir.fail) { break; }
        if (((item_flags >> 1) & 3) != 0) { continue; }  // binary or locator, not text

        u8 buffer[TAGS_FIELD_MAX];
        String8 text = tags_utf8_clean(value, buffer, sizeof(buffer));
        if (!text.size) { continue; }
        if (tags_key_is(key, "title")) { tags_set_title(tags, text); }
        else if (tags_key_is(key, "artist")) { tags_set_artist(tags, text); }
        else if (tags_key_is(key, "album")) { tags_set_album(tags, text); }
        else if (tags_key_is(key, "album artist") || tags_key_is(key, "albumartist")) {
            tags_set_album_artist(tags, text);
        } else if (tags_key_is(key, "genre")) { tags_set_genre(tags, text); }
        else if (tags_key_is(key, "track")) {
            if (!tags->track_no) { tags_parse_pair(text, &tags->track_no); }
        } else if (tags_key_is(key, "disc")) {
            if (!tags->disc_no) { tags_parse_pair(text, &tags->disc_no); }
        } else if (tags_key_is(key, "year") || tags_key_is(key, "date")) {
            if (!tags->year) { tags->year = (u16)tags_parse_u32(text); }
        } else if (tags_key_is(key, "replaygain_track_gain")) {
            if (tags->replaygain_track_db == LIB_REPLAYGAIN_NONE) {
                tags->replaygain_track_db = tags_parse_gain_db(text);
            }
        } else if (tags_key_is(key, "replaygain_album_gain")) {
            if (tags->replaygain_album_db == LIB_REPLAYGAIN_NONE) {
                tags->replaygain_album_db = tags_parse_gain_db(text);
            }
        }
    }
    return total;
}

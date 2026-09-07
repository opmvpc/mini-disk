// tags_mp4.c - ISO base media: moov/udta/meta/ilst for the tags, mvhd for the
// duration, stsd for the codec.
//
// The one structural surprise is that `moov` is often written after `mdat`,
// at the end of a file we only hold the two ends of. That costs nothing here:
// the walk asks TagsFile for each atom header, so it steps over a 40 MB `mdat`
// in the head and lands on a `moov` that lives in the tail.

typedef struct TagsAtom {
    String8 type;    // four bytes, borrowed
    u64 body_offset;
    u64 body_size;
    u64 total_size;
} TagsAtom;

static b32 tags_mp4_atom(const TagsFile *file, u64 offset, u64 limit, TagsAtom *out) {
    if (offset + 8 > limit) { return 0; }
    String8 header;
    if (!tags_file_slice(file, offset, 8, &header)) { return 0; }
    TagsReader r = tags_reader(header);
    u64 size = tags_u32be(&r);
    out->type = tags_bytes(&r, 4);
    u64 header_size = 8;
    if (size == 1) {
        String8 large;
        if (!tags_file_slice(file, offset + 8, 8, &large)) { return 0; }
        TagsReader lr = tags_reader(large);
        size = tags_u64be(&lr);
        header_size = 16;
    } else if (size == 0) {
        size = limit - offset;  // "to the end of the enclosing box"
    }
    if (size < header_size || offset + size > limit) { return 0; }
    out->body_offset = offset + header_size;
    out->body_size = size - header_size;
    out->total_size = size;
    return 1;
}

md_inline b32 tags_mp4_is(String8 type, const char *name) {
    return type.size == 4 && type.str[0] == (u8)name[0] && type.str[1] == (u8)name[1] &&
           type.str[2] == (u8)name[2] && type.str[3] == (u8)name[3];
}

// "©nam" as written on disk: 0xA9 then three ASCII bytes.
md_inline b32 tags_mp4_is_c(String8 type, const char *three) {
    return type.size == 4 && type.str[0] == 0xA9 && type.str[1] == (u8)three[0] &&
           type.str[2] == (u8)three[1] && type.str[3] == (u8)three[2];
}

// Every ilst item holds a `data` atom: version/flags then four reserved bytes,
// then the payload. Flag 1 means UTF-8 text, 0 means an integer blob.
static b32 tags_mp4_data(const TagsFile *file, const TagsAtom *item, String8 *out,
                         u32 *type_out, u64 *offset_out) {
    u64 at = item->body_offset;
    u64 end = item->body_offset + item->body_size;
    TagsAtom child;
    while (tags_mp4_atom(file, at, end, &child)) {
        if (tags_mp4_is(child.type, "data") && child.body_size >= 8) {
            String8 body;
            if (!tags_file_slice(file, child.body_offset, child.body_size, &body)) { return 0; }
            TagsReader r = tags_reader(body);
            *type_out = tags_u32be(&r) & 0xFFFFFF;
            tags_skip(&r, 4);
            *out = tags_bytes(&r, tags_remaining(&r));
            *offset_out = child.body_offset + 8;
            return !r.fail;
        }
        at += child.total_size;
    }
    return 0;
}

// The freeform "----" item: a `name` atom says which tag it is, `data` carries
// the value. It is how ReplayGain reaches an M4A.
static void tags_mp4_freeform(Tags *tags, const TagsFile *file, const TagsAtom *item) {
    u64 at = item->body_offset;
    u64 end = item->body_offset + item->body_size;
    String8 name = str8(0, 0);
    TagsAtom child;
    while (tags_mp4_atom(file, at, end, &child)) {
        if (tags_mp4_is(child.type, "name") && child.body_size > 4) {
            String8 body;
            if (tags_file_slice(file, child.body_offset + 4, child.body_size - 4, &body)) {
                name = body;
            }
        }
        at += child.total_size;
    }
    if (!tags_key_is(str8_prefix(name, 11), "replaygain_")) { return; }
    String8 value;
    u32 type = 0;
    u64 offset = 0;
    if (!tags_mp4_data(file, item, &value, &type, &offset)) { return; }
    u8 buffer[TAGS_FIELD_MAX];
    String8 text = tags_utf8_clean(value, buffer, sizeof(buffer));
    if (tags_key_is(name, "replaygain_track_gain") &&
        tags->replaygain_track_db == LIB_REPLAYGAIN_NONE) {
        tags->replaygain_track_db = tags_parse_gain_db(text);
    } else if (tags_key_is(name, "replaygain_album_gain") &&
               tags->replaygain_album_db == LIB_REPLAYGAIN_NONE) {
        tags->replaygain_album_db = tags_parse_gain_db(text);
    }
}

static void tags_mp4_ilst(Tags *tags, const TagsFile *file, u64 offset, u64 size) {
    u64 at = offset;
    u64 end = offset + size;
    TagsAtom item;
    while (tags_mp4_atom(file, at, end, &item)) {
        at += item.total_size;
        if (tags_mp4_is(item.type, "----")) {
            tags_mp4_freeform(tags, file, &item);
            continue;
        }
        String8 value;
        u32 type = 0;
        u64 value_offset = 0;
        if (!tags_mp4_data(file, &item, &value, &type, &value_offset)) { continue; }

        if (tags_mp4_is(item.type, "covr")) {
            if (tags->cover_size == 0 && value.size) {
                tags->cover_offset = value_offset;
                tags->cover_size = (u32)value.size;
                tags->cover_hashed = (u32)value.size;
                tags->cover_hash = hash64(value.str, value.size);
                tags_cover_capture(tags, value);
            }
            continue;
        }
        if (tags_mp4_is(item.type, "trkn") || tags_mp4_is(item.type, "disk")) {
            if (value.size >= 4) {
                u16 number = (u16)(((u16)value.str[2] << 8) | value.str[3]);
                if (tags_mp4_is(item.type, "trkn")) {
                    if (!tags->track_no) { tags->track_no = number; }
                } else if (!tags->disc_no) {
                    tags->disc_no = number;
                }
            }
            continue;
        }

        u8 buffer[TAGS_FIELD_MAX];
        String8 text = tags_utf8_clean(value, buffer, sizeof(buffer));
        if (!text.size) { continue; }
        if (tags_mp4_is_c(item.type, "nam")) { tags_set_title(tags, text); }
        else if (tags_mp4_is_c(item.type, "ART")) { tags_set_artist(tags, text); }
        else if (tags_mp4_is_c(item.type, "alb")) { tags_set_album(tags, text); }
        else if (tags_mp4_is(item.type, "aART")) { tags_set_album_artist(tags, text); }
        else if (tags_mp4_is_c(item.type, "gen")) { tags_set_genre(tags, text); }
        else if (tags_mp4_is(item.type, "gnre")) {
            if (value.size >= 2) {
                u32 index = (u32)(((u32)value.str[0] << 8) | value.str[1]);
                tags_set_genre(tags, tags_genre_by_index(index ? index - 1 : 0));
            }
        } else if (tags_mp4_is_c(item.type, "day")) {
            if (!tags->year) { tags->year = (u16)tags_parse_u32(text); }
        }
    }
}

static void tags_mp4_mvhd(Tags *tags, const TagsFile *file, u64 offset, u64 size) {
    String8 body;
    if (!tags_file_slice(file, offset, Min(size, (u64)32), &body)) { return; }
    TagsReader r = tags_reader(body);
    u8 version = tags_u8(&r);
    tags_skip(&r, 3);
    u64 timescale, duration;
    if (version == 1) {
        tags_skip(&r, 16);
        timescale = tags_u32be(&r);
        duration = tags_u64be(&r);
    } else {
        tags_skip(&r, 8);
        timescale = tags_u32be(&r);
        duration = tags_u32be(&r);
    }
    if (r.fail || timescale == 0 || duration == U32_MAX) { return; }
    tags->duration_ms = (u32)Min(duration * 1000 / timescale, (u64)U32_MAX);
}

static void tags_mp4_stsd(Tags *tags, const TagsFile *file, u64 offset, u64 size) {
    String8 body;
    if (!tags_file_slice(file, offset, Min(size, (u64)64), &body)) { return; }
    TagsReader r = tags_reader(body);
    tags_skip(&r, 8);              // version/flags, entry count
    tags_skip(&r, 4);              // entry size
    String8 format = tags_bytes(&r, 4);
    tags_skip(&r, 8 + 8);          // reserved, data reference index, reserved
    tags->channels = (u8)tags_u16be(&r);
    tags_skip(&r, 2 + 2 + 2);      // sample size, pre defined, reserved
    u32 rate = tags_u32be(&r);     // 16.16 fixed point
    if (r.fail) { return; }
    tags->sample_rate = rate >> 16;
    if (tags_mp4_is(format, "alac")) { tags->codec = LibCodec_ALAC; }
    else if (tags_mp4_is(format, "mp4a")) { tags->codec = LibCodec_M4A; }
}

// A depth first walk over the containers we care about, and nothing else: the
// list of parents is the whole recursion policy.
static void tags_mp4_walk(Tags *tags, const TagsFile *file, u64 offset, u64 end, u32 depth) {
    if (depth > 6) { return; }
    TagsAtom atom;
    u64 at = offset;
    while (tags_mp4_atom(file, at, end, &atom)) {
        at += atom.total_size;
        if (tags_mp4_is(atom.type, "moov") || tags_mp4_is(atom.type, "udta") ||
            tags_mp4_is(atom.type, "trak") || tags_mp4_is(atom.type, "mdia") ||
            tags_mp4_is(atom.type, "minf") || tags_mp4_is(atom.type, "stbl")) {
            tags_mp4_walk(tags, file, atom.body_offset, atom.body_offset + atom.body_size,
                          depth + 1);
        } else if (tags_mp4_is(atom.type, "meta")) {
            // `meta` is a full box: four bytes of version and flags first.
            if (atom.body_size > 4) {
                tags_mp4_walk(tags, file, atom.body_offset + 4,
                              atom.body_offset + atom.body_size, depth + 1);
            }
        } else if (tags_mp4_is(atom.type, "ilst")) {
            tags_mp4_ilst(tags, file, atom.body_offset, atom.body_size);
        } else if (tags_mp4_is(atom.type, "mvhd")) {
            tags_mp4_mvhd(tags, file, atom.body_offset, atom.body_size);
        } else if (tags_mp4_is(atom.type, "stsd")) {
            tags_mp4_stsd(tags, file, atom.body_offset, atom.body_size);
        }
    }
}

b32 tags_mp4_parse(Tags *tags, const TagsFile *file) {
    String8 brand;
    if (!tags_file_slice(file, 4, 4, &brand) || !tags_match(brand, "ftyp")) { return 0; }
    tags->codec = LibCodec_M4A;
    tags_mp4_walk(tags, file, 0, file->size, 0);
    return 1;
}

// tags.h - reading metadata and stream headers without decoding a single frame.
//
// Every parser in this module is a boundary (ADR-012): it sees two byte blocks
// that came off a disk somebody else filled, so it validates every length and
// never reads outside them. What it publishes is canonical - valid UTF-8,
// trimmed, numbers in range - and the rest of the program trusts it.
//
// The whole cost of a file is two reads of 64 KB: the head (containers put
// their tables there) and the tail (ID3v1, APEv2, the last Ogg page, a `moov`
// written after `mdat`). Nothing else is ever read, and nothing is allocated:
// the strings live in the Tags struct itself.
#ifndef TAGS_H
#define TAGS_H

#include "../../base/base.h"
#include "../../base/base_hash.h"
#include "../../base/base_string.h"
#include "../../platform/platform.h"
#include "lib_model.h"

#define TAGS_BLOCK_SIZE    KB(64)
#define TAGS_TEXT_CAPACITY 1024  // all five text fields together
#define TAGS_FIELD_MAX     256   // one field, after decoding

// The two windows a parser is allowed to look at. When the file is smaller
// than one block the tail is empty and the head is the whole file.
typedef struct TagsFile {
    String8 head;      // bytes [0, head.size)
    String8 tail;      // bytes [tail_offset, tail_offset + tail.size)
    u64 tail_offset;
    u64 size;          // the real size on disk
} TagsFile;

typedef struct Tags {
    String8 title, artist, album, album_artist, genre;  // slices into `text`
    u32 duration_ms;
    u32 sample_rate;
    u16 track_no, disc_no, year;
    i16 replaygain_track_db;  // 1/256 dB, LIB_REPLAYGAIN_NONE when absent
    i16 replaygain_album_db;
    u8 channels;
    u8 codec;                 // LibCodec
    u64 cover_hash;           // 0: no embedded picture
    u64 cover_offset;         // absolute file offset of the picture bytes
    u32 cover_size;
    u32 cover_hashed;         // bytes of it we had in the window and hashed
    u8 text[TAGS_TEXT_CAPACITY];
    u32 text_size;
} Tags;

void tags_init(Tags *tags);
// Dispatches on the signature, never on the extension. Returns 0 when nothing
// recognised the bytes; the Tags are still canonical (empty) in that case.
b32  tags_parse(Tags *tags, const TagsFile *file);
// title = file name without extension, album = parent folder, artist =
// grandparent folder. Only fills what the tags left empty.
void tags_fallback_from_path(Tags *tags, String8 path);
// The whole job for one file: at most two reads of TAGS_BLOCK_SIZE.
// `head` and `tail` are caller owned buffers of TAGS_BLOCK_SIZE bytes each.
b32  tags_read_file(Tags *tags, String8 path, u64 size, u8 *head, u8 *tail);

// --- the format parsers ----------------------------------------------------
// One per container, each its own boundary. They are declared here so that the
// unity build does not have to care in which order the .c files are included.
b32 tags_mp3_parse(Tags *tags, const TagsFile *file);
b32 tags_flac_parse(Tags *tags, const TagsFile *file);
b32 tags_ogg_parse(Tags *tags, const TagsFile *file);
b32 tags_mp4_parse(Tags *tags, const TagsFile *file);
b32 tags_wav_parse(Tags *tags, const TagsFile *file);
b32 tags_aiff_parse(Tags *tags, const TagsFile *file);
// Returns the size of the APEv2 tag sitting at the end of the file, 0 when
// there is none: an MP3 has to subtract it from the audio length.
u64 tags_ape_parse(Tags *tags, const TagsFile *file);
// A "KEY=value" Vorbis comment; shared by FLAC, Ogg Vorbis and Opus.
void tags_vorbis_comment(Tags *tags, String8 comment);

// --- the bounded reader ----------------------------------------------------
// Out of range never reads: it raises `fail` and returns zero, so a parser can
// run to the end and ask once whether anything it used was real.

typedef struct TagsReader {
    const u8 *data;
    u64 size;
    u64 at;
    b32 fail;
} TagsReader;

md_inline TagsReader tags_reader(String8 s) {
    TagsReader r;
    r.data = s.str;
    r.size = s.size;
    r.at = 0;
    r.fail = 0;
    return r;
}

md_inline b32 tags_has(TagsReader *r, u64 count) {
    return !r->fail && count <= r->size && r->at <= r->size - count;
}

md_inline u8 tags_u8(TagsReader *r) {
    if (!tags_has(r, 1)) { r->fail = 1; return 0; }
    return r->data[r->at++];
}

md_inline u16 tags_u16be(TagsReader *r) {
    u16 hi = tags_u8(r);
    return (u16)((hi << 8) | tags_u8(r));
}

md_inline u32 tags_u24be(TagsReader *r) {
    u32 v = (u32)tags_u8(r) << 16;
    v |= (u32)tags_u8(r) << 8;
    return v | tags_u8(r);
}

md_inline u32 tags_u32be(TagsReader *r) {
    u32 v = (u32)tags_u16be(r) << 16;
    return v | tags_u16be(r);
}

md_inline u64 tags_u64be(TagsReader *r) {
    u64 v = (u64)tags_u32be(r) << 32;
    return v | tags_u32be(r);
}

md_inline u16 tags_u16le(TagsReader *r) {
    u16 lo = tags_u8(r);
    return (u16)(lo | ((u16)tags_u8(r) << 8));
}

md_inline u32 tags_u32le(TagsReader *r) {
    u32 v = tags_u16le(r);
    return v | ((u32)tags_u16le(r) << 16);
}

md_inline u64 tags_u64le(TagsReader *r) {
    u64 v = tags_u32le(r);
    return v | ((u64)tags_u32le(r) << 32);
}

// A slice of the input, or an empty one plus `fail`. Never a copy.
md_inline String8 tags_bytes(TagsReader *r, u64 count) {
    if (!tags_has(r, count)) { r->fail = 1; return str8(0, 0); }
    String8 result = str8((u8 *)r->data + r->at, count);
    r->at += count;
    return result;
}

md_inline void tags_skip(TagsReader *r, u64 count) {
    if (!tags_has(r, count)) { r->fail = 1; return; }
    r->at += count;
}

md_inline u64 tags_remaining(TagsReader *r) {
    return (r->fail || r->at > r->size) ? 0 : r->size - r->at;
}

// ASCII case insensitive compare against a C literal: tag keys are ASCII in
// every format we read.
md_inline b32 tags_key_is(String8 key, const char *name) {
    u64 size = 0;
    while (name[size]) { size += 1; }
    if (key.size != size) { return 0; }
    for (u64 i = 0; i < size; i += 1) {
        u8 a = key.str[i];
        u8 b = (u8)name[i];
        if (a >= 'A' && a <= 'Z') { a = (u8)(a + 32); }
        if (b >= 'A' && b <= 'Z') { b = (u8)(b + 32); }
        if (a != b) { return 0; }
    }
    return 1;
}

md_inline b32 tags_match(String8 s, const char *four) {
    if (s.size < 4) { return 0; }
    return s.str[0] == (u8)four[0] && s.str[1] == (u8)four[1] &&
           s.str[2] == (u8)four[2] && s.str[3] == (u8)four[3];
}

// The window a parser may read: fails unless [offset, offset+size) sits wholly
// inside the head or wholly inside the tail.
md_inline b32 tags_file_slice(const TagsFile *file, u64 offset, u64 size, String8 *out) {
    if (size > file->size || offset > file->size - size) { return 0; }
    if (offset + size <= file->head.size) {
        *out = str8(file->head.str + offset, size);
        return 1;
    }
    if (file->tail.size && offset >= file->tail_offset &&
        offset - file->tail_offset + size <= file->tail.size) {
        *out = str8(file->tail.str + (offset - file->tail_offset), size);
        return 1;
    }
    return 0;
}

// --- text ------------------------------------------------------------------

String8 tags_text_push(Tags *tags, String8 utf8);          // canonical UTF-8 in
String8 tags_utf8_clean(String8 raw, u8 *out, u64 cap);    // invalid -> U+FFFD
String8 tags_latin1_to_utf8(String8 raw, u8 *out, u64 cap);
String8 tags_utf16_to_utf8(String8 raw, b32 big_endian, u8 *out, u64 cap);
// ID3 text encoding byte: 0 latin1, 1 UTF-16 with BOM, 2 UTF-16BE, 3 UTF-8.
String8 tags_decode(u8 encoding, String8 raw, u8 *out, u64 cap);
u32     tags_parse_u32(String8 s);                 // leading digits, 0 when none
void    tags_parse_pair(String8 s, u16 *number);   // "3" or "3/12"
i16     tags_parse_gain_db(String8 s);             // "-7.25 dB" -> 1/256 dB
String8 tags_genre_by_index(u32 index);            // ID3v1 table, empty when out

md_inline void tags_set_title(Tags *t, String8 s) {
    if (!t->title.size) { t->title = tags_text_push(t, s); }
}
md_inline void tags_set_artist(Tags *t, String8 s) {
    if (!t->artist.size) { t->artist = tags_text_push(t, s); }
}
md_inline void tags_set_album(Tags *t, String8 s) {
    if (!t->album.size) { t->album = tags_text_push(t, s); }
}
md_inline void tags_set_album_artist(Tags *t, String8 s) {
    if (!t->album_artist.size) { t->album_artist = tags_text_push(t, s); }
}
md_inline void tags_set_genre(Tags *t, String8 s) {
    if (!t->genre.size) { t->genre = tags_text_push(t, s); }
}

#endif // TAGS_H

// netmd_disc.c - see netmd_disc.h. The reply bytes are the validation boundary:
// every scan below can fail, and a failed scan is a disc we refuse to describe
// rather than a disc we describe wrongly.
#include "netmd_disc.h"

#include "netmd_charset.h"
#include "netmd_models.h"

// --- Shift-JIS -> UTF-8 (s3.11, read direction) -----------------------------

static u32 netmd_charset_find(u16 sjis) {
    u32 low = 0, high = (u32)NETMD_CHARSET_COUNT;
    while (low < high) {
        u32 mid = (low + high) / 2u;
        u16 at = netmd_charset_map[mid].sjis;
        if (at == sjis) { return netmd_charset_map[mid].cp; }
        if (at < sjis) {
            low = mid + 1u;
        } else {
            high = mid;
        }
    }
    return 0;
}

u64 netmd_sjis_to_utf8(const u8 *in, u64 size, u8 *out, u64 capacity) {
    u64 written = 0;
    for (u64 i = 0; i < size;) {
        u8 byte = in[i];
        u32 cp;
        if (byte == 0) { break; }  // the TOC pads with NUL, it is not content
        if (byte < 0x80u) {
            cp = byte;
            i += 1;
        } else if (byte >= 0xA1u && byte <= 0xDFu) {
            // The half-width katakana block, the one thing a MiniDisc title is
            // really made of once it leaves ASCII (s3.8).
            cp = 0xFF61u + (u32)(byte - 0xA1u);
            i += 1;
        } else if (i + 1 < size) {
            cp = netmd_charset_find((u16)(((u32)byte << 8) | in[i + 1]));
            if (cp == 0) { cp = '?'; }  // a kanji, or a byte pair we do not map
            i += 2;
        } else {
            cp = '?';  // a lead byte with nothing behind it: a truncated title
            i += 1;
        }
        u8 encoded[4];
        u32 length = utf8_encode(encoded, cp);
        if (written + length > capacity) { break; }
        mem_copy(out + written, encoded, length);
        written += length;
    }
    return written;
}

// --- time --------------------------------------------------------------------

NetmdTime netmd_time_make(u32 hours, u32 minutes, u32 seconds, u32 frames) {
    NetmdTime time;
    time.hours = hours;
    time.minutes = minutes;
    time.seconds = seconds;
    time.frames = frames;
    time.total_frames = ((hours * 60u + minutes) * 60u + seconds) * NETMD_FRAMES_PER_SECOND +
                        frames;
    time.ms = ((u64)time.total_frames * 1000u) / NETMD_FRAMES_PER_SECOND;
    return time;
}

static NetmdTime netmd_time_from_frames(u32 total) {
    u32 rest = total;
    u32 frames = rest % NETMD_FRAMES_PER_SECOND;
    rest /= NETMD_FRAMES_PER_SECOND;
    u32 seconds = rest % 60u;
    rest /= 60u;
    u32 minutes = rest % 60u;
    rest /= 60u;
    return netmd_time_make(rest, minutes, seconds, frames);
}

// --- the group syntax (s3.10) ------------------------------------------------

// A group separator, a range dash, a digit or the ';' - in either width. Full
// width service characters are what a full-width disc title uses, and they are
// the same convention (s3.10).
static u32 netmd_service_char(u32 cp) {
    if (cp < 0x80u) { return cp; }
    if (cp >= 0xFF10u && cp <= 0xFF19u) { return '0' + (cp - 0xFF10u); }
    if (cp == 0xFF0Du) { return '-'; }
    if (cp == 0xFF0Fu) { return '/'; }
    if (cp == 0xFF1Bu) { return ';'; }
    return 0;
}

typedef struct NetmdCursor {
    String8 text;
    u64 at;
    u32 service;  // the last decoded codepoint, folded to its ASCII meaning
    u32 advance;
} NetmdCursor;

static b32 netmd_cursor_peek(const String8 *text, u64 at, u32 *out_service, u32 *out_advance) {
    if (at >= text->size) { return 0; }
    UnicodeDecode decoded = utf8_decode(text->str + at, text->size - at);
    *out_service = netmd_service_char(decoded.codepoint);
    *out_advance = decoded.advance;
    return 1;
}

// The next "//" at or after `from`, in either width; text.size when there is
// none. `out_end` is where what follows begins.
static u64 netmd_find_delim(String8 text, u64 from, u64 *out_end) {
    u64 at = from;
    while (at < text.size) {
        u32 service = 0, advance = 0;
        if (!netmd_cursor_peek(&text, at, &service, &advance)) { break; }
        if (service == '/') {
            u32 next_service = 0, next_advance = 0;
            if (netmd_cursor_peek(&text, at + advance, &next_service, &next_advance) &&
                next_service == '/') {
                *out_end = at + advance + next_advance;
                return at;
            }
        }
        at += advance;
    }
    *out_end = text.size;
    return text.size;
}

// "1-4" or "7": 1-based and inclusive. Returns 0 when the segment does not open
// with a range, which is how a segment that is not a group is recognised.
static b32 netmd_parse_range(String8 segment, u64 *at, u32 *out_first, u32 *out_last) {
    u32 first = 0, last = 0;
    u64 cursor = *at;
    u32 digits = 0;
    for (;;) {
        u32 service = 0, advance = 0;
        if (!netmd_cursor_peek(&segment, cursor, &service, &advance)) { break; }
        if (service < '0' || service > '9') { break; }
        first = first * 10u + (service - '0');
        digits += 1;
        cursor += advance;
    }
    if (digits == 0) { return 0; }
    last = first;
    u32 service = 0, advance = 0;
    if (netmd_cursor_peek(&segment, cursor, &service, &advance) && service == '-') {
        cursor += advance;
        last = 0;
        digits = 0;
        for (;;) {
            if (!netmd_cursor_peek(&segment, cursor, &service, &advance)) { break; }
            if (service < '0' || service > '9') { break; }
            last = last * 10u + (service - '0');
            digits += 1;
            cursor += advance;
        }
        if (digits == 0) { return 0; }
    }
    *at = cursor;
    *out_first = first;
    *out_last = last;
    return 1;
}

static void netmd_layout_set_title(DiscLayout *layout, b32 wide, String8 title) {
    u8 *dst = wide ? layout->title_full : layout->title;
    u64 size = Min(title.size, (u64)NETMD_DISC_TITLE_MAX);
    if (size != 0) { mem_copy(dst, title.str, size); }
    if (wide) {
        layout->title_full_size = (u16)size;
    } else {
        layout->title_size = (u16)size;
    }
}

void netmd_parse_groups(DiscLayout *layout, String8 raw) {
    layout->group_count = 0;
    layout->ungrouped_count = layout->track_count;
    for (u32 i = 0; i < layout->track_count; i += 1) { layout->tracks[i].group = NETMD_NO_GROUP; }
    layout->raw_title_size = (u16)Min(raw.size, (u64)NETMD_DISC_TITLE_MAX);
    if (layout->raw_title_size != 0) {
        mem_copy(layout->raw_title, raw.str, layout->raw_title_size);
    }

    u64 first_end = 0;
    u64 first_delim = netmd_find_delim(raw, 0, &first_end);
    if (first_delim == raw.size) {
        // s3.10: no "//" at all means the whole string is the disc title and
        // there are no groups. The commonest disc there is.
        netmd_layout_set_title(layout, 0, raw);
        return;
    }

    // s3.10, title extraction: only a title that *ends* with the delimiter is a
    // group string. Otherwise the "//" is part of somebody's title.
    b32 ends_with_delim = 0;
    {
        u64 probe = first_delim, probe_end = first_end;
        while (probe != raw.size) {
            ends_with_delim = (probe_end == raw.size);
            probe = netmd_find_delim(raw, probe_end, &probe_end);
        }
    }
    if (!ends_with_delim) {
        netmd_layout_set_title(layout, 0, raw);
        return;
    }

    String8 first_segment = str8_prefix(raw, first_delim);
    // "0;" in front of the first segment is the disc title marker; a group
    // string with no "0;" segment simply has no disc title.
    {
        u64 at = 0;
        u32 service = 0, advance = 0;
        b32 is_marker = 0;
        if (netmd_cursor_peek(&first_segment, at, &service, &advance) && service == '0') {
            u64 after = at + advance;
            u32 next_service = 0, next_advance = 0;
            if (netmd_cursor_peek(&first_segment, after, &next_service, &next_advance) &&
                next_service == ';') {
                is_marker = 1;
                netmd_layout_set_title(layout, 0, str8_skip(first_segment,
                                                            after + next_advance));
            }
        }
        if (!is_marker) { netmd_layout_set_title(layout, 0, str8(0, 0)); }
    }

    // Every segment is a group candidate, the first one included: "1;Intro//"
    // is a group over track 1 and there is no disc title at all. Only a segment
    // whose range is 0 is the disc title, and that is what the check below is
    // (s3.10, "ignore a segment starting with 0;").
    u64 cursor = 0;
    while (cursor < raw.size && layout->group_count < NETMD_GROUP_MAX) {
        u64 end = 0;
        u64 delim = netmd_find_delim(raw, cursor, &end);
        String8 segment = str8_substr(raw, cursor, delim - cursor);
        cursor = end;
        if (segment.size == 0) { continue; }  // s3.10: empty segments are skipped

        u64 at = 0;
        u32 first = 0, last = 0;
        if (!netmd_parse_range(segment, &at, &first, &last)) { continue; }
        u32 service = 0, advance = 0;
        if (!netmd_cursor_peek(&segment, at, &service, &advance) || service != ';') { continue; }
        at += advance;
        if (first == 0) { continue; }  // "0;" is the disc title, never a group

        // s3.10: a range is not rewritten when a track is erased, so it can
        // point past the end of the disc. Clamp instead of trusting it.
        if (first > layout->track_count) { continue; }
        if (last > layout->track_count) { last = layout->track_count; }
        if (last < first) { continue; }

        NetmdGroup *group = &layout->groups[layout->group_count];
        StructZero(group);
        group->first = (u16)(first - 1u);
        group->count = (u16)(last - first + 1u);
        String8 name = str8_skip(segment, at);
        group->name_size = (u16)Min(name.size, (u64)NETMD_TITLE_MAX);
        if (group->name_size != 0) { mem_copy(group->name, name.str, group->name_size); }

        // A track belongs to one group only; an overlap is a corrupt title, and
        // the first group to claim a track keeps it.
        b32 overlaps = 0;
        for (u32 i = 0; i < group->count; i += 1) {
            if (layout->tracks[group->first + i].group != NETMD_NO_GROUP) { overlaps = 1; }
        }
        if (overlaps) { continue; }
        for (u32 i = 0; i < group->count; i += 1) {
            layout->tracks[group->first + i].group = (u8)layout->group_count;
            layout->ungrouped_count -= 1;
        }
        layout->group_count += 1;
    }
}

// --- descriptors, in and out of a batch --------------------------------------

static u32 netmd_desc_enter(NetmdSession *session, Arena *arena, const NetmdDescriptor *desc) {
    if (session->open_desc == desc) { return NetmdResult_Ok; }
    if (session->open_desc) {
        netmd_descriptor_close(session, arena, session->open_desc);
        session->open_desc = 0;
    }
    // s3.4: some machines reject opening what is already open. The open is
    // advisory; only the close is mandatory.
    netmd_descriptor_open(session, arena, desc, NETMD_DESC_OPEN_READ);
    if (session->batch) { session->open_desc = desc; }
    return NetmdResult_Ok;
}

static void netmd_desc_leave(NetmdSession *session, Arena *arena, const NetmdDescriptor *desc) {
    if (session->batch) { return; }  // the batch closes it when it ends
    netmd_descriptor_close(session, arena, desc);
    session->open_desc = 0;
}

static void netmd_batch_begin(NetmdSession *session) { session->batch = 1; }

static void netmd_batch_end(NetmdSession *session, Arena *arena) {
    session->batch = 0;
    if (session->open_desc) {
        netmd_descriptor_close(session, arena, session->open_desc);
        session->open_desc = 0;
    }
}

// --- identification (s3.6) ---------------------------------------------------

u32 netmd_get_device_level(NetmdSession *session, Arena *arena, u32 *out_level) {
    *out_level = 0;
    ArenaTemp scratch = arena_temp_begin(arena);
    netmd_desc_enter(session, arena, &netmd_desc_subunit);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                               "00 1809 00 ff00 0000 0000");
    if (result == NetmdResult_Ok) {
        // The subunit descriptor header of s3.6: a length, four sizes, and how
        // many root object lists precede the media type table.
        u32 descriptor_length = 0, generation = 0, size_of_list_id = 0, size_of_object_id = 0;
        u32 size_of_position = 0, amount_of_lists = 0;
        String8 rest;
        if (netmd_scan(reply, "09 1809 00 1000 %?%? %?%? %w %b %b %b %b %w %*",
                       &descriptor_length, &generation, &size_of_list_id, &size_of_object_id,
                       &size_of_position, &amount_of_lists, &rest)) {
            // The media type list of s3.6: skip the root object lists, then the
            // two lengths and the three bytes in front of the type count.
            u64 at = (u64)size_of_list_id * amount_of_lists;
            at += 2 + 2 + 1 + 1;  // subunitDependent, subunitFields, attributes, version
            if (at < rest.size) {
                u32 types = rest.str[at];
                at += 1;
                for (u32 i = 0; i < types && at + 8 <= rest.size; i += 1) {
                    u32 media = ((u32)rest.str[at] << 8) | rest.str[at + 1];
                    // 0x0301 is MiniDisc audio; its profile id is the NetMD
                    // level, and a device without it is not a recorder at all.
                    if (media == 0x0301u) { *out_level = rest.str[at + 2]; }
                    at += 8;
                }
            }
        } else {
            result = NetmdResult_Malformed;
        }
    }
    netmd_desc_leave(session, arena, &netmd_desc_subunit);
    arena_temp_end(scratch);
    return result;
}

String8 netmd_get_device_name(NetmdSession *session, Arena *arena) {
    // The bus calls every Sony portable "Net MD Walkman"; the PID table is the
    // only thing that knows which one it is (research/01 s1.1).
    String8 name = netmd_model_name(session->vid, session->pid);
    if (name.size == 0) { name = str8_lit("NetMD"); }
    u32 level = 0;
    if (netmd_get_device_level(session, arena, &level) != NetmdResult_Ok || level == 0) {
        return str8_copy(arena, name);
    }
    const char *level_name = (level >= 0x70u)   ? "Level 3"
                             : (level >= 0x50u) ? "Level 2"
                                                : "Level 1";
    return str8f(arena, "%S \xC2\xB7 %s", name, level_name);
}

// --- the disc ----------------------------------------------------------------

u32 netmd_get_disc_present(NetmdSession *session, Arena *arena, b32 *out_present) {
    *out_present = 0;
    ArenaTemp scratch = arena_temp_begin(arena);
    netmd_desc_enter(session, arena, &netmd_desc_op_status);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                               "00 1809 8001 0230 8800 0030 8804 00 ff00 00000000");
    if (result == NetmdResult_Ok) {
        String8 status;
        if (netmd_scan(reply, "09 1809 8001 0230 8800 0030 8804 00 1000 00090000 %x", &status) &&
            status.size > 4) {
            // s3.14: 0x40 disc present, 0x80 no disc.
            *out_present = (status.str[4] != 0x80u);
        } else {
            result = NetmdResult_Malformed;
        }
    }
    netmd_desc_leave(session, arena, &netmd_desc_op_status);
    arena_temp_end(scratch);
    return result;
}

u32 netmd_get_disc_flags(NetmdSession *session, Arena *arena, u32 *out_flags) {
    *out_flags = 0;
    ArenaTemp scratch = arena_temp_begin(arena);
    netmd_desc_enter(session, arena, &netmd_desc_root);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                               "00 1806 01101000 ff00 0001000b");
    if (result == NetmdResult_Ok) {
        u32 raw = 0;
        if (netmd_scan(reply, "09 1806 01101000 1000 0001000b %b", &raw)) {
            if (raw & 0x10u) { *out_flags |= NetmdDiscFlag_Writable; }
            if (raw & 0x40u) { *out_flags |= NetmdDiscFlag_WriteProtected; }
        } else {
            result = NetmdResult_Malformed;
        }
    }
    netmd_desc_leave(session, arena, &netmd_desc_root);
    arena_temp_end(scratch);
    return result;
}

u32 netmd_get_disc_capacity(NetmdSession *session, Arena *arena, NetmdCapacity *out) {
    StructZero(out);
    ArenaTemp scratch = arena_temp_begin(arena);
    netmd_desc_enter(session, arena, &netmd_desc_root);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                               "00 1806 02101000 3080 0300 ff00 00000000");
    if (result == NetmdResult_Ok) {
        u32 t[12];
        mem_set(t, 0, sizeof(t));
        // The %?03 is the s3.7.3 workaround: most machines answer 8003 and
        // Panasonic answers 0803, so that byte is never checked.
        if (netmd_scan(reply,
                       "09 1806 02101000 3080 0300 1000 001d0000 001b %?03 0017 8000"
                       " 0005 %W %B %B %B"
                       " 0005 %W %B %B %B"
                       " 0005 %W %B %B %B",
                       &t[0], &t[1], &t[2], &t[3], &t[4], &t[5], &t[6], &t[7], &t[8], &t[9],
                       &t[10], &t[11])) {
            out->recorded = netmd_time_make(t[0], t[1], t[2], t[3]);
            out->total = netmd_time_make(t[4], t[5], t[6], t[7]);
            out->available = netmd_time_make(t[8], t[9], t[10], t[11]);
            // s3.7.3, the Sharp correction: more than 82 minutes of SP does not
            // exist, so the device is reporting in its current LP mode.
            while (out->total.total_frames > NETMD_FRAMES_SP_MAX) {
                out->recorded = netmd_time_from_frames(out->recorded.total_frames / 2u);
                out->total = netmd_time_from_frames(out->total.total_frames / 2u);
                out->available = netmd_time_from_frames(out->available.total_frames / 2u);
                out->halved = 1;
            }
        } else {
            result = NetmdResult_Malformed;
        }
    }
    netmd_desc_leave(session, arena, &netmd_desc_root);
    arena_temp_end(scratch);
    return result;
}

u32 netmd_get_track_count(NetmdSession *session, Arena *arena, u32 *out_count) {
    *out_count = 0;
    ArenaTemp scratch = arena_temp_begin(arena);
    netmd_desc_enter(session, arena, &netmd_desc_audio_contents);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                               "00 1806 02101001 3000 1000 ff00 00000000");
    if (result == NetmdResult_Ok) {
        u32 count = 0;
        if (netmd_scan(reply,
                       "09 1806 02101001 %?%? %?%? 1000 00%?0000 0006 0010000200 %b", &count)) {
            *out_count = Min(count, (u32)NETMD_TRACK_MAX);
        } else {
            result = NetmdResult_Malformed;
        }
    }
    netmd_desc_leave(session, arena, &netmd_desc_audio_contents);
    arena_temp_end(scratch);
    return result;
}

// The generic per track query of s3.7.4; `p1`/`p2` pick which attribute.
static u32 netmd_track_attribute(NetmdSession *session, Arena *arena, u32 track, u32 p1, u32 p2,
                                 String8 *out) {
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                               "00 1806 02201001 %w %w %w ff00 00000000", track, p1, p2);
    if (result != NetmdResult_Ok) { return result; }
    if (!netmd_scan(reply, "09 1806 02201001 %?%? %?%? %?%? 1000 00%?0000 %x", out)) {
        return NetmdResult_Malformed;
    }
    return NetmdResult_Ok;
}

u32 netmd_get_track_info(NetmdSession *session, Arena *arena, u32 track, NetmdTrack *out) {
    ArenaTemp scratch = arena_temp_begin(arena);
    netmd_desc_enter(session, arena, &netmd_desc_audio_contents);

    String8 payload;
    u32 result = netmd_track_attribute(session, arena, track, 0x3000u, 0x0100u, &payload);
    if (result == NetmdResult_Ok) {
        u32 hours = 0, minutes = 0, seconds = 0, frames = 0;
        if (netmd_scan(payload, "0001 0006 0000 %B %B %B %B", &hours, &minutes, &seconds,
                       &frames)) {
            NetmdTime time = netmd_time_make(hours, minutes, seconds, frames);
            out->frames = time.total_frames;
            out->duration_ms = time.ms;
        } else {
            result = NetmdResult_Malformed;
        }
    }

    if (result == NetmdResult_Ok) {
        result = netmd_track_attribute(session, arena, track, 0x3080u, 0x0700u, &payload);
        if (result == NetmdResult_Ok) {
            u32 encoding = 0, channels = 0;
            if (netmd_scan(payload, "8007 0004 0110 %b %b", &encoding, &channels)) {
                out->encoding = (encoding == 0x90u)   ? (u8)NetmdEncoding_SP
                                : (encoding == 0x92u) ? (u8)NetmdEncoding_LP2
                                : (encoding == 0x93u) ? (u8)NetmdEncoding_LP4
                                                      : (u8)NetmdEncoding_Unknown;
                out->mono = (channels == 0x01u) ? 1u : 0u;
            } else {
                result = NetmdResult_Malformed;
            }
        }
    }

    if (result == NetmdResult_Ok) {
        String8 reply;
        u32 flags_result = netmd_command(session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                                         "00 1806 01201001 %w ff00 00010008", track);
        u32 flags = 0;
        if (flags_result == NetmdResult_Ok &&
            netmd_scan(reply, "09 1806 01201001 %?%? 10 00 00010008 %b", &flags)) {
            out->protect = (flags == 0x03u) ? 1u : 0u;
        }
        // A machine that does not answer the flag query is not a broken disc:
        // the track is simply reported as unprotected (s3.7.4c).
    }

    netmd_desc_leave(session, arena, &netmd_desc_audio_contents);
    arena_temp_end(scratch);
    return result;
}

u32 netmd_get_track_title(NetmdSession *session, Arena *arena, u32 track, b32 wide,
                          String8 *out) {
    *out = str8(0, 0);
    const NetmdDescriptor *desc = wide ? &netmd_desc_utoc4 : &netmd_desc_utoc1;
    // s3.8.2: the wchar of a *track* title is 0x02 / 0x03, not the 0x00 / 0x01
    // of the disc title. Getting this wrong reads the wrong title space.
    u32 wchar = wide ? 0x03u : 0x02u;
    ArenaTemp scratch = arena_temp_begin(arena);
    netmd_desc_enter(session, arena, desc);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                               "00 1806 022018%b %w 3000 0a00 ff00 00000000", wchar, track);
    String8 sjis = str8(0, 0);
    if (result == NetmdResult_Ok) {
        if (!netmd_scan(reply,
                        "09 1806 022018%? %?%? %?%? %?%? 1000 00%?0000 00%?000a %x", &sjis)) {
            result = NetmdResult_Malformed;
        }
    } else if (result == NetmdResult_Rejected) {
        // s3.8.2: an untitled track is REJECTED, not an error.
        result = NetmdResult_Ok;
    }
    netmd_desc_leave(session, arena, desc);

    if (result == NetmdResult_Ok && sjis.size != 0) {
        // The scan points into the reply, which the temp arena is about to take
        // back: the bytes are copied out first, and a reply can never be longer
        // than the poll's one length byte allows.
        u8 raw[NETMD_REPLY_MAX];
        u64 raw_size = Min(sjis.size, (u64)sizeof(raw));
        mem_copy(raw, sjis.str, raw_size);
        arena_temp_end(scratch);
        u64 capacity = raw_size * 3u + 1u;  // half-width katakana is 3 UTF-8 bytes
        u8 *text = push_array(arena, u8, capacity);
        *out = str8(text, netmd_sjis_to_utf8(raw, raw_size, text, capacity));
        return NetmdResult_Ok;
    }
    arena_temp_end(scratch);
    return result;
}

u32 netmd_get_disc_title(NetmdSession *session, Arena *arena, b32 wide, String8 *out) {
    *out = str8(0, 0);
    // s3.8.1 opens audioContentsTD as well as discTitleTD; the batch would only
    // hold one, so both are opened by hand here and closed in reverse.
    if (session->open_desc) {
        netmd_descriptor_close(session, arena, session->open_desc);
        session->open_desc = 0;
    }
    netmd_descriptor_open(session, arena, &netmd_desc_audio_contents, NETMD_DESC_OPEN_READ);
    netmd_descriptor_open(session, arena, &netmd_desc_disc_title, NETMD_DESC_OPEN_READ);

    u8 *sjis = push_array(arena, u8, NETMD_DISC_TITLE_MAX);
    u64 done = 0;
    u64 total = 1;  // one page is always read; the first one says how many follow
    u32 result = NetmdResult_Ok;
    while (done < total) {
        ArenaTemp scratch = arena_temp_begin(arena);
        String8 reply;
        u32 remaining = (u32)(total - done);
        if (done == 0) { remaining = 0; }
        result = netmd_command(session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                               "00 1806 02201801 00%b 3000 0a00 ff00 %w%w", wide ? 1u : 0u,
                               remaining, (u32)done);
        if (result == NetmdResult_Rejected && done == 0) {
            // An untitled disc answers REJECTED, exactly like an untitled track.
            result = NetmdResult_Ok;
            arena_temp_end(scratch);
            break;
        }
        if (result != NetmdResult_Ok) {
            arena_temp_end(scratch);
            break;
        }
        u32 chunk_size = 0;
        String8 chunk;
        if (done == 0) {
            u32 announced = 0;
            if (!netmd_scan(reply,
                            "09 1806 02201801 00%? 3000 0a00 1000 %w0000 %?%?000a %w %*",
                            &chunk_size, &announced, &chunk)) {
                result = NetmdResult_Malformed;
                arena_temp_end(scratch);
                break;
            }
            // s3.8.1: the first chunk's size counts its own six byte header.
            if (chunk_size < 6) {
                result = NetmdResult_Malformed;
                arena_temp_end(scratch);
                break;
            }
            chunk_size -= 6;
            total = announced;
        } else {
            if (!netmd_scan(reply, "09 1806 02201801 00%? 3000 0a00 1000 %w%?%? %*", &chunk_size,
                            &chunk)) {
                result = NetmdResult_Malformed;
                arena_temp_end(scratch);
                break;
            }
        }
        if (chunk_size > chunk.size) { chunk_size = (u32)chunk.size; }
        u64 room = NETMD_DISC_TITLE_MAX - done;
        u64 copied = Min((u64)chunk_size, room);
        if (copied != 0) { mem_copy(sjis + done, chunk.str, copied); }
        done += copied;
        arena_temp_end(scratch);
        // A device answering an empty chunk would loop forever otherwise.
        if (copied == 0) { break; }
        if (done >= NETMD_DISC_TITLE_MAX) { break; }
    }

    netmd_descriptor_close(session, arena, &netmd_desc_disc_title);
    netmd_descriptor_close(session, arena, &netmd_desc_audio_contents);
    if (result != NetmdResult_Ok) { return result; }

    u64 capacity = done * 3u + 1u;
    u8 *text = push_array(arena, u8, capacity);
    *out = str8(text, netmd_sjis_to_utf8(sjis, done, text, capacity));
    return NetmdResult_Ok;
}

u32 netmd_read_disc(NetmdSession *session, Arena *arena, DiscLayout *out) {
    StructZero(out);
    ArenaTemp scratch = arena_temp_begin(arena);

    b32 present = 0;
    u32 result = netmd_get_disc_present(session, arena, &present);
    if (result != NetmdResult_Ok) {
        arena_temp_end(scratch);
        return result;
    }
    if (!present) {
        arena_temp_end(scratch);
        return NetmdResult_Ok;  // no disc is an answer, not a failure
    }
    out->flags |= NetmdDiscFlag_Present;

    netmd_batch_begin(session);
    u32 flags = 0;
    result = netmd_get_disc_flags(session, arena, &flags);
    if (result == NetmdResult_Ok) { out->flags |= flags; }
    if (result == NetmdResult_Ok) {
        result = netmd_get_disc_capacity(session, arena, &out->capacity);
    }
    if (result == NetmdResult_Ok) {
        result = netmd_get_track_count(session, arena, &out->track_count);
    }
    netmd_batch_end(session, arena);
    if (result != NetmdResult_Ok) {
        arena_temp_end(scratch);
        return result;
    }
    if (out->track_count == 0) { out->flags |= NetmdDiscFlag_Empty; }

    String8 raw_title = str8(0, 0);
    String8 raw_title_full = str8(0, 0);
    result = netmd_get_disc_title(session, arena, 0, &raw_title);
    if (result == NetmdResult_Ok) {
        // The full-width title space is independent (s3.8); a device that has
        // nothing there answers REJECTED and that is not a failure either.
        netmd_get_disc_title(session, arena, 1, &raw_title_full);
    }
    if (result != NetmdResult_Ok) {
        arena_temp_end(scratch);
        return result;
    }

    // Three passes and not one loop over the tracks: each pass needs a different
    // descriptor, and interleaving them would close and reopen three descriptors
    // per track. On a ten track disc that is sixty extra commands for nothing
    // (s3.4), and the whole read has two seconds to happen in.
    netmd_batch_begin(session);
    for (u32 track = 0; track < out->track_count && result == NetmdResult_Ok; track += 1) {
        result = netmd_get_track_info(session, arena, track, &out->tracks[track]);
    }
    for (u32 track = 0; track < out->track_count && result == NetmdResult_Ok; track += 1) {
        String8 title;
        result = netmd_get_track_title(session, arena, track, 0, &title);
        if (result != NetmdResult_Ok) { break; }
        out->tracks[track].title_size = (u16)Min(title.size, (u64)NETMD_TITLE_MAX);
        if (out->tracks[track].title_size != 0) {
            mem_copy(out->tracks[track].title, title.str, out->tracks[track].title_size);
        }
    }
    for (u32 track = 0; track < out->track_count && result == NetmdResult_Ok; track += 1) {
        String8 title;
        result = netmd_get_track_title(session, arena, track, 1, &title);
        if (result != NetmdResult_Ok) { break; }
        out->tracks[track].title_full_size = (u16)Min(title.size, (u64)NETMD_TITLE_MAX);
        if (out->tracks[track].title_full_size != 0) {
            mem_copy(out->tracks[track].title_full, title.str,
                     out->tracks[track].title_full_size);
        }
    }
    netmd_batch_end(session, arena);
    if (result != NetmdResult_Ok) {
        arena_temp_end(scratch);
        return result;
    }

    netmd_parse_groups(out, raw_title);
    if (raw_title_full.size != 0) {
        // The full-width string carries the same group syntax; only its disc
        // title is kept, the groups of the half-width one are the real ones.
        DiscLayout *shadow = push_struct(arena, DiscLayout);
        StructZero(shadow);
        shadow->track_count = out->track_count;
        netmd_parse_groups(shadow, raw_title_full);
        out->title_full_size = shadow->title_size;
        if (out->title_full_size != 0) {
            mem_copy(out->title_full, shadow->title, out->title_full_size);
        }
    }
    arena_temp_end(scratch);
    return NetmdResult_Ok;
}

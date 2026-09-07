// netmd_backup.c - see netmd_backup.h.
#include "netmd_backup.h"

u64 netmd_disc_id(const DiscLayout *layout) {
    u64 id = hash64(layout->title, layout->title_size);
    id = hash64_combine(id, hash64_mix(layout->track_count));
    id = hash64_combine(id, hash64_mix(layout->capacity.total.total_frames));
    return id;
}

// --- writing -------------------------------------------------------------------

static void netmd_backup_line(Arena *arena, String8List *lines, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    str8_list_push(arena, lines, str8fv(arena, fmt, args));
    va_end(args);
}

String8 netmd_backup_serialize(Arena *arena, const DiscLayout *layout) {
    String8List lines;
    StructZero(&lines);
    netmd_backup_line(arena, &lines, "minidisk-toc %u", (u32)NETMD_BACKUP_VERSION);
    netmd_backup_line(arena, &lines, "disc-id %016llx", netmd_disc_id(layout));
    netmd_backup_line(arena, &lines, "flags %u", layout->flags);
    netmd_backup_line(arena, &lines, "capacity %u %u %u", layout->capacity.recorded.total_frames,
                      layout->capacity.total.total_frames,
                      layout->capacity.available.total_frames);
    netmd_backup_line(arena, &lines, "title %S", str8((u8 *)layout->title, layout->title_size));
    netmd_backup_line(arena, &lines, "title-full %S",
                      str8((u8 *)layout->title_full, layout->title_full_size));
    netmd_backup_line(arena, &lines, "tracks %u", layout->track_count);
    for (u32 i = 0; i < layout->group_count; i += 1) {
        const NetmdGroup *group = &layout->groups[i];
        netmd_backup_line(arena, &lines, "group %u %u %S", group->first, group->count,
                          str8((u8 *)group->name, group->name_size));
    }
    for (u32 i = 0; i < layout->track_count; i += 1) {
        const NetmdTrack *track = &layout->tracks[i];
        // One line per track, the title last: it is the only field that may hold
        // a space, so the parser can take the rest of the line for it.
        netmd_backup_line(arena, &lines, "track %u %u %u %u %u %llu %S", i, track->encoding,
                          (u32)track->mono, (u32)track->protect, track->frames,
                          track->duration_ms, str8((u8 *)track->title, track->title_size));
    }
    str8_list_push(arena, &lines, str8_lit(""));  // the trailing newline
    return str8_list_join(arena, &lines, str8_lit("\n"));
}

// --- reading (a validation boundary: this file may have been edited by hand) ---

static u64 netmd_backup_u64(String8 text, u64 *at) {
    u64 value = 0;
    while (*at < text.size && text.str[*at] == ' ') { *at += 1; }
    while (*at < text.size && text.str[*at] >= '0' && text.str[*at] <= '9') {
        value = value * 10u + (u64)(text.str[*at] - '0');
        *at += 1;
    }
    return value;
}

// The rest of the line after one separating space, kept as it is: a title may
// begin with a space and it would be wrong to eat it.
static String8 netmd_backup_rest(String8 text, u64 at) {
    if (at < text.size && text.str[at] == ' ') { at += 1; }
    return str8_skip(text, at);
}

static void netmd_backup_copy(u8 *dst, u16 *dst_size, String8 value, u64 cap) {
    u64 size = Min(value.size, cap);
    if (size != 0) { mem_copy(dst, value.str, size); }
    *dst_size = (u16)size;
}

b32 netmd_backup_parse(String8 text, DiscLayout *out) {
    StructZero(out);
    if (!str8_starts_with(text, str8_lit("minidisk-toc "))) { return 0; }
    u64 line_start = 0;
    u32 announced_tracks = 0;
    u32 groups = 0;
    while (line_start < text.size) {
        u64 end = line_start;
        while (end < text.size && text.str[end] != '\n') { end += 1; }
        String8 line = str8_substr(text, line_start, end - line_start);
        line_start = end + 1;
        if (line.size != 0 && line.str[line.size - 1] == '\r') {
            line = str8_prefix(line, line.size - 1);
        }
        if (line.size == 0 || line.str[0] == '#') { continue; }

        if (str8_starts_with(line, str8_lit("flags "))) {
            u64 at = 5;
            out->flags = (u32)netmd_backup_u64(line, &at);
        } else if (str8_starts_with(line, str8_lit("capacity "))) {
            u64 at = 8;
            u32 recorded = (u32)netmd_backup_u64(line, &at);
            u32 total = (u32)netmd_backup_u64(line, &at);
            u32 available = (u32)netmd_backup_u64(line, &at);
            out->capacity.recorded = netmd_time_from_frames(recorded);
            out->capacity.total = netmd_time_from_frames(total);
            out->capacity.available = netmd_time_from_frames(available);
        } else if (str8_starts_with(line, str8_lit("title-full"))) {
            netmd_backup_copy(out->title_full, &out->title_full_size,
                              netmd_backup_rest(line, 10), NETMD_DISC_TITLE_MAX);
        } else if (str8_starts_with(line, str8_lit("title"))) {
            netmd_backup_copy(out->title, &out->title_size, netmd_backup_rest(line, 5),
                              NETMD_DISC_TITLE_MAX);
        } else if (str8_starts_with(line, str8_lit("tracks "))) {
            u64 at = 6;
            announced_tracks = (u32)netmd_backup_u64(line, &at);
        } else if (str8_starts_with(line, str8_lit("group "))) {
            u64 at = 5;
            u32 first = (u32)netmd_backup_u64(line, &at);
            u32 count = (u32)netmd_backup_u64(line, &at);
            if (groups < NETMD_GROUP_MAX) {
                NetmdGroup *group = &out->groups[groups];
                group->first = (u16)first;
                group->count = (u16)count;
                netmd_backup_copy(group->name, &group->name_size, netmd_backup_rest(line, at),
                                  NETMD_TITLE_MAX);
                groups += 1;
            }
        } else if (str8_starts_with(line, str8_lit("track "))) {
            u64 at = 5;
            u32 index = (u32)netmd_backup_u64(line, &at);
            u32 encoding = (u32)netmd_backup_u64(line, &at);
            u32 mono = (u32)netmd_backup_u64(line, &at);
            u32 protect = (u32)netmd_backup_u64(line, &at);
            u32 frames = (u32)netmd_backup_u64(line, &at);
            u64 duration = netmd_backup_u64(line, &at);
            if (index < NETMD_TRACK_MAX) {
                NetmdTrack *track = &out->tracks[index];
                track->encoding = (u8)Min(encoding, (u32)NetmdEncoding_Unknown);
                track->mono = (u8)(mono != 0);
                track->protect = (u8)(protect != 0);
                track->frames = frames;
                track->duration_ms = duration;
                track->group = NETMD_NO_GROUP;
                netmd_backup_copy(track->title, &track->title_size, netmd_backup_rest(line, at),
                                  NETMD_TITLE_MAX);
                if (index + 1 > out->track_count) { out->track_count = index + 1; }
            }
        }
    }
    if (announced_tracks > out->track_count && announced_tracks <= NETMD_TRACK_MAX) {
        out->track_count = announced_tracks;  // trailing untitled tracks
    }
    out->group_count = groups;
    // The membership the file does not store, rebuilt from the ranges exactly
    // the way netmd_parse_groups does after a read (s3.10).
    out->ungrouped_count = out->track_count;
    for (u32 g = 0; g < groups; g += 1) {
        const NetmdGroup *group = &out->groups[g];
        for (u32 i = group->first; i < (u32)group->first + group->count &&
                                   i < out->track_count; i += 1) {
            if (out->tracks[i].group != NETMD_NO_GROUP) { continue; }
            out->tracks[i].group = (u8)g;
            out->ungrouped_count -= 1;
        }
    }
    return 1;
}

// --- where it goes ---------------------------------------------------------------

String8 netmd_backup_dir(Arena *arena) {
    ArenaTemp scratch = scratch_begin(&arena, 1);
    String8 root = os_known_folder(scratch.arena, OsKnownFolder_LocalAppData);
    if (root.size == 0) { root = os_known_folder(scratch.arena, OsKnownFolder_Temp); }
    String8 app_dir = os_path_join(scratch.arena, root, str8_lit("minidisk"));
    String8 dir = os_path_join(arena, app_dir, str8_lit("toc-backups"));
    scratch_end(scratch);
    return dir;
}

String8 netmd_backup_path(Arena *arena, String8 dir, const DiscLayout *layout,
                          const OsWallClock *now) {
    return str8f(arena, "%S\\%016llx-%04u%02u%02u-%02u%02u%02u.txt", dir, netmd_disc_id(layout),
                 now->year, now->month, now->day, now->hour, now->minute, now->second);
}

b32 netmd_backup_write(Arena *arena, String8 dir, const DiscLayout *layout, String8 *out_path) {
    // Both levels: the application folder may not exist on a first run either.
    os_dir_create(os_path_parent(dir));
    os_dir_create(dir);
    OsWallClock now;
    StructZero(&now);
    os_time_local(&now);
    String8 path = netmd_backup_path(arena, dir, layout, &now);
    String8 text = netmd_backup_serialize(arena, layout);
    String8 temp_path = str8_cat(arena, path, str8_lit(".tmp"));
    b32 ok = os_file_write_all(temp_path, text) && os_file_move_replace(temp_path, path);
    if (ok && out_path) { *out_path = path; }
    return ok;
}

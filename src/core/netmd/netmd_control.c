// netmd_control.c - see netmd_control.h.
#include "netmd_control.h"

#include "netmd_disc.h"

static u32 netmd_simple(NetmdSession *session, Arena *arena, u32 budget_ms, const char *fmt) {
    ArenaTemp scratch = arena_temp_begin(arena);
    String8 reply;
    u32 result = netmd_command(session, arena, budget_ms, &reply, fmt);
    arena_temp_end(scratch);
    return result;
}

u32 netmd_play(NetmdSession *session, Arena *arena) {
    return netmd_simple(session, arena, NETMD_BUDGET_TRANSPORT_MS, "00 18c3 ff 75 000000");
}

u32 netmd_pause(NetmdSession *session, Arena *arena) {
    return netmd_simple(session, arena, NETMD_BUDGET_TRANSPORT_MS, "00 18c3 ff 7d 000000");
}

u32 netmd_stop(NetmdSession *session, Arena *arena) {
    // s3.13: stop fails on the LAM-1 series. The caller gets the result, and the
    // device thread treats it as advisory - a player that will not stop is not
    // a reason to tear the session down.
    return netmd_simple(session, arena, NETMD_BUDGET_TRANSPORT_MS, "00 18c5 ff 00000000");
}

// s3.13: 0x8001 next, 0x0002 previous, 0x0001 restart the current track.
static u32 netmd_track_step(NetmdSession *session, Arena *arena, u32 direction) {
    ArenaTemp scratch = arena_temp_begin(arena);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_TRANSPORT_MS, &reply,
                               "00 1850 ff10 00000000 %w", direction);
    arena_temp_end(scratch);
    return result;
}

u32 netmd_next(NetmdSession *session, Arena *arena) {
    return netmd_track_step(session, arena, 0x8001u);
}

u32 netmd_prev(NetmdSession *session, Arena *arena) {
    return netmd_track_step(session, arena, 0x0002u);
}

u32 netmd_goto_track(NetmdSession *session, Arena *arena, u32 track) {
    ArenaTemp scratch = arena_temp_begin(arena);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_TRANSPORT_MS, &reply,
                               "00 1850 ff010000 0000 %w", track);
    arena_temp_end(scratch);
    return result;
}

u32 netmd_eject(NetmdSession *session, Arena *arena) {
    return netmd_simple(session, arena, NETMD_BUDGET_EJECT_MS, "00 18c1 ff 6000");
}

u32 netmd_can_eject(NetmdSession *session, Arena *arena, b32 *out_can) {
    // The STATUS ctype of s3.1: "do you know this command?" without running it.
    u32 result = netmd_simple(session, arena, NETMD_BUDGET_QUERY_MS, "01 18c1 ff 6000");
    *out_can = (result == NetmdResult_Ok);
    // Not knowing how to eject is an answer about the machine, not a failure.
    return (result == NetmdResult_NotImplemented || result == NetmdResult_Rejected)
                   ? NetmdResult_Ok
                   : result;
}

u32 netmd_get_state(NetmdSession *session, Arena *arena, u32 *out_state, u32 *out_raw) {
    *out_state = NetmdState_Unknown;
    *out_raw = 0;
    ArenaTemp scratch = arena_temp_begin(arena);
    netmd_descriptor_open(session, arena, &netmd_desc_op_status, NETMD_DESC_OPEN_READ);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                               "00 1809 8001 0330 8802 0030 8805 0030 8806 00 ff00 00000000");
    if (result == NetmdResult_Ok) {
        u32 status_mode = 0;
        String8 payload;
        if (netmd_scan(reply,
                       "09 1809 8001 0330 8802 0030 8805 0030 8806 00 1000 00%?0000 00%b 8806 %x",
                       &status_mode, &payload) &&
            payload.size >= 2) {
            u32 raw = ((u32)payload.str[0] << 8) | payload.str[1];
            *out_raw = raw;
            switch (raw) {
                case 0xC5FFu: *out_state = NetmdState_Ready; break;
                case 0xC375u: *out_state = NetmdState_Playing; break;
                case 0xC37Du: *out_state = NetmdState_Paused; break;
                case 0xC33Fu: *out_state = NetmdState_Forward; break;
                case 0xC34Fu: *out_state = NetmdState_Rewind; break;
                case 0xFF23u: *out_state = NetmdState_ReadingToc; break;
                case 0xFF10u: *out_state = NetmdState_NoDisc; break;
                case 0xFFFFu: *out_state = NetmdState_DiscBlank; break;
                case 0xC275u:
                case 0xC27Du: *out_state = NetmdState_Recording; break;
                default: *out_state = NetmdState_Unknown; break;
            }
        } else {
            result = NetmdResult_Malformed;
        }
    }
    netmd_descriptor_close(session, arena, &netmd_desc_op_status);
    arena_temp_end(scratch);
    return result;
}

u32 netmd_get_position(NetmdSession *session, Arena *arena, NetmdPosition *out) {
    StructZero(out);
    ArenaTemp scratch = arena_temp_begin(arena);
    netmd_descriptor_open(session, arena, &netmd_desc_op_status, NETMD_DESC_OPEN_READ);
    String8 reply;
    u32 result = netmd_command(
            session, arena, NETMD_BUDGET_QUERY_MS, &reply,
            "00 1809 8001 0430 8802 0030 8805 0030 0003 0030 0002 00 ff00 00000000");
    if (result == NetmdResult_Ok) {
        u32 track = 0, hours = 0, minutes = 0, seconds = 0, frames = 0;
        if (netmd_scan(reply,
                       "09 1809 8001 0430 %?%? %?%? %?%? %?%? %?%? %?%? %?%? %? %?00 00%?0000"
                       " 000b 0002 0007 00 %w %B %B %B %B",
                       &track, &hours, &minutes, &seconds, &frames)) {
            out->valid = 1;
            out->track = track;
            out->hours = hours;
            out->minutes = minutes;
            out->seconds = seconds;
            out->frames = frames;
            out->ms = netmd_time_make(hours, minutes, seconds, frames).ms;
        } else {
            result = NetmdResult_Malformed;
        }
    } else if (result == NetmdResult_Rejected) {
        // s3.14: a stopped device has no position to report. Not an error.
        result = NetmdResult_Ok;
    }
    netmd_descriptor_close(session, arena, &netmd_desc_op_status);
    arena_temp_end(scratch);
    return result;
}

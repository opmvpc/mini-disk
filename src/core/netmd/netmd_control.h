// netmd_control.h - transport control and where the head is (research/01
// s3.13-3.15). The whole file is six commands and two queries; it is separate
// from netmd_disc.c because pressing play must not depend on having read a disc.
#ifndef NETMD_CONTROL_H
#define NETMD_CONTROL_H

#include "../../base/base.h"
#include "netmd_proto.h"

// The operating status values of s3.14, as the device reports them.
typedef enum NetmdState {
    NetmdState_Unknown = 0,
    NetmdState_Ready,     // 0xC5FF, stopped
    NetmdState_Playing,   // 0xC375
    NetmdState_Paused,    // 0xC37D
    NetmdState_Forward,   // 0xC33F
    NetmdState_Rewind,    // 0xC34F
    NetmdState_ReadingToc,// 0xFF23
    NetmdState_NoDisc,    // 0xFF10
    NetmdState_DiscBlank, // 0xFFFF
    NetmdState_Recording, // 0xC275
    NetmdState_COUNT
} NetmdState;

typedef struct NetmdPosition {
    b32 valid;  // a stopped device answers REJECTED: no position, not an error
    u32 track;  // 0-based
    u32 hours;
    u32 minutes;
    u32 seconds;
    u32 frames;
    u64 ms;
} NetmdPosition;

u32 netmd_play(NetmdSession *session, Arena *arena);
u32 netmd_pause(NetmdSession *session, Arena *arena);
u32 netmd_stop(NetmdSession *session, Arena *arena);
u32 netmd_next(NetmdSession *session, Arena *arena);
u32 netmd_prev(NetmdSession *session, Arena *arena);
u32 netmd_goto_track(NetmdSession *session, Arena *arena, u32 track);
u32 netmd_eject(NetmdSession *session, Arena *arena);
// s3.15: on a portable there is nothing to eject and the command answers NOT
// IMPLEMENTED. Asked once, so the button is never offered where it does nothing.
u32 netmd_can_eject(NetmdSession *session, Arena *arena, b32 *out_can);

u32 netmd_get_state(NetmdSession *session, Arena *arena, u32 *out_state, u32 *out_raw);
u32 netmd_get_position(NetmdSession *session, Arena *arena, NetmdPosition *out);

#endif  // NETMD_CONTROL_H

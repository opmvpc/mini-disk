// netmd_disc.h - what is on the disc, read back into one struct the UI can draw
// without asking anything else (research/01 s3.6-3.8, s3.10, s3.14, s7).
//
// A MiniDisc has no notion of a group: groups are a Sony convention encoded in
// the disc title itself ("0;Album//1-4;Face A//"), and taking that string apart
// is as much a part of reading a disc as any USB command. DiscLayout is the
// result of both: the clean title, the groups, the tracks, and the capacity.
//
// The reply bytes are the ONE validation boundary of this module (ADR-012):
// they are untrusted input, exactly like a tag. Everything downstream trusts
// the DiscLayout.
#ifndef NETMD_DISC_H
#define NETMD_DISC_H

#include "../../base/base.h"
#include "../../base/base_string.h"
#include "netmd_proto.h"

// The TOC holds 255 fragments, and 254 tracks is the practical limit every
// piece of software uses (research/01 s7.3).
#define NETMD_TRACK_MAX 255
#define NETMD_GROUP_MAX 32
// One title in UTF-8. The whole TOC budget is 1785 half-width characters
// (s7.4), so a single title can never need more than a fraction of this.
#define NETMD_TITLE_MAX 128
// The disc title carries the group syntax as well, so it gets the whole budget
// plus room for the UTF-8 expansion of half-width katakana (3 bytes each).
#define NETMD_DISC_TITLE_MAX 1024

// 1 second = 512 TOC frames (s7.2). Not to be confused with an audio frame.
#define NETMD_FRAMES_PER_SECOND 512u
// s3.7.3: more than 82 minutes of SP is physically impossible, so a device
// reporting it is reporting in the current recording mode (the Sharp quirk).
#define NETMD_FRAMES_SP_MAX (NETMD_FRAMES_PER_SECOND * 60u * 82u)

typedef enum NetmdEncoding {
    NetmdEncoding_SP = 0,  // ATRAC1, 0x90
    NetmdEncoding_LP2,     // ATRAC3 132 kbit/s, 0x92
    NetmdEncoding_LP4,     // ATRAC3 66 kbit/s, 0x93
    NetmdEncoding_Unknown,
    NetmdEncoding_COUNT
} NetmdEncoding;

typedef enum NetmdDiscFlag {
    NetmdDiscFlag_Present = 1 << 0,
    NetmdDiscFlag_Writable = 1 << 1,       // 0x10: recordable, not pre-mastered
    NetmdDiscFlag_WriteProtected = 1 << 2, // 0x40: the tab is closed
    NetmdDiscFlag_Empty = 1 << 3,          // no track at all
} NetmdDiscFlag;

#define NETMD_NO_GROUP 0xFFu

// hh:mm:ss:ff as the TOC stores it, plus what the UI actually wants.
typedef struct NetmdTime {
    u32 hours;
    u32 minutes;
    u32 seconds;
    u32 frames;  // 0..511
    u32 total_frames;
    u64 ms;
} NetmdTime;

typedef struct NetmdCapacity {
    NetmdTime recorded;
    NetmdTime total;
    NetmdTime available;
    b32 halved;  // the s3.7.3 correction fired: the device reported in LP
} NetmdCapacity;

typedef struct NetmdTrack {
    u64 duration_ms;
    u32 frames;
    u8 encoding;   // NetmdEncoding
    u8 mono;       // channels == 0x01
    u8 protect;    // track flags == 0x03: checked out, not erasable
    u8 group;      // NETMD_NO_GROUP when the track is in none
    u16 title_size;
    u16 title_full_size;
    u8 title[NETMD_TITLE_MAX];       // half-width, decoded to UTF-8
    u8 title_full[NETMD_TITLE_MAX];  // full-width, decoded to UTF-8
} NetmdTrack;

typedef struct NetmdGroup {
    u16 first;  // 0-based track index
    u16 count;
    u16 name_size;
    u8 name[NETMD_TITLE_MAX];
} NetmdGroup;

typedef struct DiscLayout {
    u32 flags;  // NetmdDiscFlag
    NetmdCapacity capacity;
    u32 track_count;
    u32 group_count;
    u32 ungrouped_count;  // tracks belonging to no group at all
    u16 title_size;
    u16 title_full_size;
    u16 raw_title_size;
    u8 title[NETMD_DISC_TITLE_MAX];       // the clean title, groups stripped
    u8 title_full[NETMD_DISC_TITLE_MAX];  // the full-width one, likewise
    u8 raw_title[NETMD_DISC_TITLE_MAX];   // exactly what the TOC holds
    NetmdGroup groups[NETMD_GROUP_MAX];
    NetmdTrack tracks[NETMD_TRACK_MAX];
} DiscLayout;

// --- charset (s3.11, read direction) ----------------------------------------
// Shift-JIS as the TOC stores it -> UTF-8. Returns bytes written. Anything the
// generated table does not know (a kanji, a corrupt byte) becomes '?': a title
// is display data, and refusing to show a disc because one byte is odd would be
// the wrong answer.
u64 netmd_sjis_to_utf8(const u8 *in, u64 size, u8 *out, u64 capacity);

// --- time --------------------------------------------------------------------
NetmdTime netmd_time_make(u32 hours, u32 minutes, u32 seconds, u32 frames);
// The other way round: a TOC frame count back into hh:mm:ss:ff (netmd_backup.c
// stores frames, because that is the one lossless form).
NetmdTime netmd_time_from_frames(u32 total);

// --- the group syntax of s3.10 ----------------------------------------------
// Takes the raw disc title (UTF-8, half or full width) and fills in `title`,
// `groups` and every track's `group`. `track_count` bounds the ranges: groups
// are not rewritten when a track is erased, so a range can point past the end.
void netmd_parse_groups(DiscLayout *layout, String8 raw);

// --- the queries -------------------------------------------------------------
// Every one of these opens the descriptor it needs and closes it again, whatever
// happens in between (s3.4).
String8 netmd_get_device_name(NetmdSession *session, Arena *arena);
u32 netmd_get_device_level(NetmdSession *session, Arena *arena, u32 *out_level);
u32 netmd_get_disc_present(NetmdSession *session, Arena *arena, b32 *out_present);
u32 netmd_get_disc_flags(NetmdSession *session, Arena *arena, u32 *out_flags);
u32 netmd_get_disc_capacity(NetmdSession *session, Arena *arena, NetmdCapacity *out);
u32 netmd_get_track_count(NetmdSession *session, Arena *arena, u32 *out_count);
u32 netmd_get_track_info(NetmdSession *session, Arena *arena, u32 track, NetmdTrack *out);
// wide: 0 half-width, 1 full-width. A track with no title answers REJECTED,
// which is not an error - it is an empty title (s3.8.2).
u32 netmd_get_track_title(NetmdSession *session, Arena *arena, u32 track, b32 wide, String8 *out);
// Paginated by 255 bytes, because that is all a poll length byte can say (s3.8.1).
u32 netmd_get_disc_title(NetmdSession *session, Arena *arena, b32 wide, String8 *out);
// The same two, plus the raw Shift-JIS bytes the TOC holds. Only a writer wants
// them: `oldLen` counts those bytes and nothing else (s3.9, netmd_edit.c).
u32 netmd_get_track_title_ex(NetmdSession *session, Arena *arena, u32 track, b32 wide,
                             String8 *out, String8 *out_sjis);
u32 netmd_get_disc_title_ex(NetmdSession *session, Arena *arena, b32 wide, String8 *out,
                            String8 *out_sjis);

// The whole disc in one call: what the device thread runs on ReadDisc.
u32 netmd_read_disc(NetmdSession *session, Arena *arena, DiscLayout *out);

#endif  // NETMD_DISC_H

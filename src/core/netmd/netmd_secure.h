// netmd_secure.h - the download side of the protocol: the secure session, the
// key exchange, and the audio packets (research/01 s4).
//
// Everything here is one shape: a nine byte signature, a command byte, a
// placeholder, and a payload, sent through the same EP0 channel as every other
// command (s4.1). What makes it different is the state: from enterSecureSession
// to leaveSecureSession the device holds a session key, and a session left open
// by a crash makes the *next* run fail with an opaque REJECTED that no amount of
// unplugging the cable explains to a user. So the entry point runs the
// preventive teardown of s4.14 first, and every exit path - success, error,
// cancellation - runs the teardown again. There is no way to leave this file
// with a session still open.
//
// The host nonce and the packet key are injectable (`random`), which is what
// makes a transcript of a download replayable byte for byte: with a fixed nonce
// the session key is fixed, so every encrypted byte on the wire is fixed too.
#ifndef NETMD_SECURE_H
#define NETMD_SECURE_H

#include "../../base/base.h"
#include "../../base/base_arena.h"
#include "netmd_des.h"
#include "netmd_proto.h"

// s4.1: invariant for every secure command but "enter HiMD mode".
#define NETMD_SECURE_HDR_SIZE 9
extern const u8 netmd_secure_hdr[NETMD_SECURE_HDR_SIZE];

// s4.2, the commands v1 sends.
#define NETMD_SEC_LEAF_ID        0x11u
#define NETMD_SEC_SEND_KEY_DATA  0x12u
#define NETMD_SEC_KEY_EXCHANGE   0x20u
#define NETMD_SEC_KEY_FORGET     0x21u
#define NETMD_SEC_SETUP_DOWNLOAD 0x22u
#define NETMD_SEC_SEND_TRACK     0x28u
#define NETMD_SEC_TRACK_PROTECT  0x2Bu
#define NETMD_SEC_COMMIT_TRACK   0x48u
#define NETMD_SEC_ENTER_SESSION  0x80u
#define NETMD_SEC_LEAVE_SESSION  0x81u

// s4.8. v1 writes SP only: PCM on the wire, the device does the ATRAC1.
#define NETMD_WIREFORMAT_PCM  0x00u
#define NETMD_WIREFORMAT_LP2  0x94u
#define NETMD_WIREFORMAT_LP4  0xA8u
#define NETMD_DISCFORMAT_LP4      0u
#define NETMD_DISCFORMAT_LP2      2u
#define NETMD_DISCFORMAT_SP_MONO  4u
#define NETMD_DISCFORMAT_SP       6u

// s4.10-4.11: the first packet carries 24 bytes of header, and totalBytes
// counts them.
#define NETMD_PACKET_HEADER_BYTES 24u
// s4.10 leaves the choice between 1 MiB (netmd-js) and 8 MiB (libnetmd). Both
// are far too coarse here: SP runs at 176 KB/s, so a 1 MiB packet is six
// seconds between two cancellation checks and six seconds of frozen progress
// bar. 256 KB is 1.5 s, and the device cannot tell the difference - the packets
// are a host side cut of one continuous CBC stream, not a wire framing.
#define NETMD_PACKET_BYTES KB(256)

// s6.2 / s4.11: the Sharps are slow, and 200 ms around sendTrack is free.
#define NETMD_SECURE_SEND_HOLD_MS 200
#define NETMD_SECURE_READY_TRIES  50   // 200 ms apart: 10 s to become ready
#define NETMD_BUDGET_SECURE_MS    10000
// The device encodes ATRAC1 in real time while it swallows the audio, so the
// final ACCEPTED of a four minute track shows up four minutes later.
#define NETMD_BUDGET_TRACK_MS     900000

// s4.5: the EKB the community uses, and its root key.
typedef struct NetmdEkb {
    u32 id;
    u32 depth;
    u32 chain_length;
    const u8 *chain;      // chain_length * 16 bytes
    const u8 *signature;  // 24 bytes
    const u8 *root_key;   // 16 bytes
} NetmdEkb;

const NetmdEkb *netmd_ekb_select(const u8 leaf_id[8], u16 vid, u16 pid);
// The 16 byte root key of the open source EKB - the retail MAC's key (s4.6).
const u8 *netmd_ekb_open_source_root_key(void);

// s4.7: the two constants every implementation sends.
extern const u8 netmd_content_id[20];
extern const u8 netmd_kek[8];

// Injectable so a replay test is deterministic (see the file comment).
typedef void NetmdRandomFn(void *user, u8 *dst, u64 size);

typedef struct NetmdSecure {
    NetmdSession *session;
    Arena *arena;
    NetmdRandomFn *random;
    void *random_user;

    const NetmdEkb *ekb;
    u8 leaf_id[8];
    u8 host_nonce[8];
    u8 dev_nonce[8];
    u8 session_key[8];
    DesKey session_des;
    DesKey kek_des;

    b32 in_session;   // enterSecureSession succeeded and leave has not run yet
    b32 acquired;     // s3.5, released by netmd_secure_end
    u32 tracks_sent;  // committed in this session
} NetmdSecure;

// One byte source for one track, already padded to a whole number of frames.
// T-042 renders the pipeline into an arena block; T-043 will bind a cache file
// behind the same two fields and nothing else in this file will change.
typedef struct NetmdAudioSource {
    u64 (*read)(void *user, u8 *dst, u64 size);  // sequential, returns bytes
    void *user;
    u64 total_bytes;  // a multiple of the wireformat's frame size
} NetmdAudioSource;

// Called after every packet reaches the pipe, on the device thread. It is what
// makes a four minute track show a moving bar instead of one jump at the end.
typedef void NetmdPacketFn(void *user, u64 bytes_sent);

typedef struct NetmdTrackSend {
    NetmdAudioSource source;
    u8 wireformat;
    u8 discformat;
    volatile u32 *cancel;          // read between two packets, never inside one
    volatile long long *progress;  // payload bytes of THIS track, atomic
    NetmdPacketFn *on_packet;
    void *packet_user;
    // out
    u32 track;          // the number the device assigned, 0 based
    u8 uuid[8];
    u8 content_id[20];
    u64 bytes_sent;
} NetmdTrackSend;

u32 netmd_secure_frame_size(u8 wireformat);

void netmd_secure_init(NetmdSecure *secure, NetmdSession *session, Arena *arena);
// The default source of entropy. A test overrides it with a counter.
void netmd_secure_set_random(NetmdSecure *secure, NetmdRandomFn *fn, void *user);

// One raw secure command, for the callers that only need one (and for tests).
// `out_data` is the payload after the placeholder byte.
u32 netmd_secure_command(NetmdSecure *secure, u8 command, const u8 *data, u64 size,
                         String8 *out_data);

// s4.3 steps 1-12: preflight, enter, leaf id, EKB, nonces, session key.
u32 netmd_secure_begin(NetmdSecure *secure);
// s4.14. Always safe to call, always ignores its own errors, always leaves the
// device in a state the next run can enter.
void netmd_secure_end(NetmdSecure *secure);

// s4.14: wait for operatingStatus in { ready, discBlank } before setup.
u32 netmd_secure_wait_ready(NetmdSecure *secure);
u32 netmd_secure_setup_download(NetmdSecure *secure);   // s4.7
u32 netmd_secure_send_track(NetmdSecure *secure, NetmdTrackSend *send);  // s4.10-4.11
u32 netmd_secure_commit_track(NetmdSecure *secure, u32 track);           // s4.12

#endif  // NETMD_SECURE_H

// netmd_proto.h - the AV/C command layer: one frame out, one frame in, and the
// mini language that describes both (research/01 s2.4-2.7, s3.1-3.5).
//
// Everything above this file writes commands as the research document writes
// them - "00 1806 02201001 %w %w %w ff00 00000000" - and reads replies the same
// way. That is not sugar: a frame that is spelled the way the reference text
// spells it can be checked against the reference text by eye, and the tests do
// exactly that against the hex examples of s3.
//
// Nothing here allocates outside the arena it is handed, and nothing here knows
// what a disc is: that is netmd_disc.c.
#ifndef NETMD_PROTO_H
#define NETMD_PROTO_H

#include "../../base/base.h"
#include "../../base/base_arena.h"
#include "../../base/base_string.h"
#include "netmd_transport.h"

// AV/C ctype, the first byte of a command (research/01 s3.1). CONTROL is what
// almost everything is; STATUS asks whether a command exists at all.
#define NETMD_CTYPE_CONTROL 0x00u
#define NETMD_CTYPE_STATUS 0x01u

// EP0 carries a few hundred bytes at most (s2.8) and a reply never exceeds the
// single length byte of the poll, so one buffer of this size covers both.
#define NETMD_FRAME_MAX 256

// The budgets of s2.6. They are per command and they are the only thing that
// stops a wedged device from hanging the device thread forever.
#define NETMD_BUDGET_QUERY_MS 3000
#define NETMD_BUDGET_DESCRIPTOR_MS 3000
#define NETMD_BUDGET_TRANSPORT_MS 5000
#define NETMD_BUDGET_EJECT_MS 10000

#define NETMD_TRANSFER_TIMEOUT_MS 1000
#define NETMD_POLL_DELAY_MIN_MS 5
#define NETMD_POLL_DELAY_MAX_MS 200
// s3.2: an INTERIM says "understood, it will take a while". Four more waits is
// what the reference implementations allow before calling the device stuck.
#define NETMD_INTERIM_RETRIES 4

typedef enum NetmdResult {
    NetmdResult_Ok = 0,
    NetmdResult_Rejected,        // 0x0A: wrong state, no such track, protected disc
    NetmdResult_NotImplemented,  // 0x08: this machine does not know the command
    NetmdResult_Timeout,         // the budget ran out with no reply
    NetmdResult_Usb,             // the transport failed; `usb_error` says how
    NetmdResult_Malformed,       // the reply does not fit the pattern
    NetmdResult_COUNT
} NetmdResult;

// The session owns the transport and the one piece of state a NetMD really has
// on the host side: when the device may next be spoken to (s6.2).
typedef struct NetmdSession {
    UsbTransport transport;
    u16 vid;
    u16 pid;
    u64 quiet_until_us;  // s6.2: the device is still writing, do not interrupt
    i32 usb_error;       // the OsUsbError behind the last NetmdResult_Usb
    u32 exchanges;       // commands actually sent, for the timing measurements
    u8 last_status;      // the AV/C status byte of the last reply
    // A batch keeps one descriptor open across a run of queries instead of
    // opening and closing it around each one: reading a 10 track disc is 50
    // queries, and 100 descriptor commands on top of them is the difference
    // between well under the two second budget and nowhere near it (s3.4).
    u8 batch;
    const struct NetmdDescriptor *open_desc;
} NetmdSession;

void netmd_session_init(NetmdSession *session, const UsbTransport *transport, u16 vid, u16 pid);
// The mandatory quiet time of s6.2, declared by whoever knows the command just
// sent: the next exchange waits it out instead of collecting a REJECTED.
void netmd_session_hold(NetmdSession *session, u32 ms);

// --- the mini language of s3.3 ----------------------------------------------
// Two hex digits are a literal byte, whitespace is ignored, and:
//   %b %w %d %q   1 / 2 / 4 / 8 bytes big-endian   (u32, u32, u32, u64)
//   %<b %<w %<d %<q            the same, little-endian
//   %B %W         1 / 2 bytes BCD, a decimal value both ways      (u32)
//   %x %z         a buffer behind a 2 / 1 byte length             (u8*, u32)
//   %s            like %x but the length counts a trailing NUL    (u8*, u32)
//   %*            the raw rest                                    (u8*, u32)
// and on the scan side only:
//   %?            skip one byte without checking or returning it
//   %#            the rest, alias of %*
// Scan outputs are pointers: u32*, u64*, String8*. A scan must consume the
// whole reply - a leftover byte means the device answered a different shape,
// which is the best malformed-reply detector there is (s3.3).
String8 netmd_query(Arena *arena, const char *fmt, ...);
String8 netmd_queryv(Arena *arena, const char *fmt, va_list args);
b32 netmd_scan(String8 reply, const char *fmt, ...);
b32 netmd_scanv(String8 reply, const char *fmt, va_list args);

// BCD, the encoding every time field on a MiniDisc uses (s3.3).
md_inline u8 netmd_bcd_from_u8(u32 v) { return (u8)(((v / 10u) << 4) | (v % 10u)); }
md_inline u32 netmd_bcd_to_u8(u8 b) { return (u32)((b >> 4) * 10u + (b & 0x0Fu)); }

// --- one command ------------------------------------------------------------
// Send, poll with the capped backoff of s2.6, read, follow an INTERIM. The
// reply keeps its status byte, so a scan format reads exactly like the research
// document's ("09 1806 ...").
u32 netmd_exchange(NetmdSession *session, Arena *arena, String8 request, u32 budget_ms,
                   String8 *out_reply);
// The same, building the request from a format string in one call.
u32 netmd_command(NetmdSession *session, Arena *arena, u32 budget_ms, String8 *out_reply,
                  const char *fmt, ...);

// --- descriptors (s3.4) ------------------------------------------------------
#define NETMD_DESC_OPEN_READ 0x01u
#define NETMD_DESC_OPEN_WRITE 0x03u
#define NETMD_DESC_CLOSE 0x00u

typedef struct NetmdDescriptor {
    u8 bytes[3];
    u8 size;
} NetmdDescriptor;

extern const NetmdDescriptor netmd_desc_disc_title;      // 10 1801, half-width disc title
extern const NetmdDescriptor netmd_desc_utoc1;           // 10 1802, half-width track titles
extern const NetmdDescriptor netmd_desc_utoc4;           // 10 1803, full-width track titles
extern const NetmdDescriptor netmd_desc_audio_contents;  // 10 1001, track count and lengths
extern const NetmdDescriptor netmd_desc_root;            // 10 1000, disc flags and capacity
extern const NetmdDescriptor netmd_desc_subunit;         // 00, the identification
extern const NetmdDescriptor netmd_desc_op_status;       // 80 00, operating status

// s3.4: an open that fails is not fatal (some machines reject re-opening what is
// already open), a close that never happens *is* - it wedges every later
// command. So the open is advisory and the close is always emitted.
u32 netmd_descriptor_open(NetmdSession *session, Arena *arena, const NetmdDescriptor *desc,
                          u8 action);
u32 netmd_descriptor_close(NetmdSession *session, Arena *arena, const NetmdDescriptor *desc);

// s3.5: acquire silences the player's own buttons for the length of a session.
// An acquire without a release leaves the machine showing "PC --> MD" until it
// is unplugged, so every error path releases.
u32 netmd_acquire(NetmdSession *session, Arena *arena);
u32 netmd_release(NetmdSession *session, Arena *arena);

#endif  // NETMD_PROTO_H

// netmd_replay.h - a UsbTransport that answers from a recorded session instead
// of a device (ADR-008). It is what makes the protocol testable with nothing
// plugged in, and what turns a protocol regression into a failing test rather
// than a corrupted TOC.
//
// The transcript is a text file, one exchange per pair of lines:
//
//     # a comment, and blank lines, are ignored
//     > c1 01 00 00 00 00 04 00      the bytes the host is expected to send
//     < 00 81 00 00                  the bytes the device answers
//     ! timeout                      the next exchange times out instead
//
// A request is serialised the one way the transport can see it:
//   * control - the eight byte setup packet (bmRequestType, bRequest, wValue
//     and wIndex little endian, wLength) followed by the payload of an OUT,
//   * bulk    - the endpoint address, then the payload.
// A `>` line that does not match byte for byte is a divergence: the replay
// fails there and stays failed, which is the whole point (a request that drifts
// must not be answered as if it had not).
#ifndef NETMD_REPLAY_H
#define NETMD_REPLAY_H

#include "../../base/base.h"
#include "../../base/base_string.h"
#include "../../platform/platform.h"
#include "netmd_transport.h"

// The longest frame v1 sends is sendKeyData at ~90 bytes; 512 leaves room for
// the bulk headers of T-021 without making the struct heavy.
#define NETMD_REPLAY_MAX_BYTES 512

typedef enum NetmdReplayFail {
    NetmdReplayFail_None = 0,
    NetmdReplayFail_Diverged,   // the request is not the one the transcript has
    NetmdReplayFail_Exhausted,  // more requests than the transcript recorded
    NetmdReplayFail_Malformed,  // the transcript itself is not readable
} NetmdReplayFail;

typedef struct NetmdReplay {
    String8 trace;
    u64 at;    // parse cursor, bytes into the trace
    u64 line;  // 1 based, for the failure report
    u32 exchanges;
    u32 fail;       // NetmdReplayFail
    u64 fail_line;  // where it went wrong
    // The two frames of the divergence, kept so a test can say what differed.
    u8 expected[NETMD_REPLAY_MAX_BYTES];
    u64 expected_size;
    u8 actual[NETMD_REPLAY_MAX_BYTES];
    u64 actual_size;
} NetmdReplay;

void netmd_replay_init(NetmdReplay *replay, String8 trace);
// Reads the file through the platform layer; 0 when it is unreadable, which is
// a test fixture problem and reported as one.
b32  netmd_replay_load(NetmdReplay *replay, Arena *arena, String8 path);
void netmd_replay_transport(NetmdReplay *replay, UsbTransport *out);

// --- the other direction: recording a session (T-021) -----------------------
// The same file format, written instead of read. It wraps a real transport and
// logs every control and bulk exchange, which is what `--netmd-trace <file>`
// turns on: the transcripts under tests/netmd are meant to be *captured*, and a
// capture that cannot be replayed byte for byte would be worthless.
typedef struct NetmdTrace {
    UsbTransport inner;
    Arena *arena;
    String8List lines;
    u32 exchanges;
    b32 bound;
} NetmdTrace;

void netmd_trace_init(NetmdTrace *trace, Arena *arena, const UsbTransport *inner);
void netmd_trace_transport(NetmdTrace *trace, UsbTransport *out);
void netmd_trace_comment(NetmdTrace *trace, String8 text);
// Writes the whole transcript out in one go. There is no incremental append in
// the platform layer, and a session is a few hundred lines: buffering it costs
// nothing and keeps the file consistent if the device is unplugged mid capture.
b32 netmd_trace_write(NetmdTrace *trace, String8 path);

md_inline b32 netmd_replay_ok(const NetmdReplay *replay) { return replay->fail == 0; }
// Every recorded exchange was played: a session that stops early is a test that
// forgot half of what it meant to check.
b32 netmd_replay_done(const NetmdReplay *replay);

#endif  // NETMD_REPLAY_H

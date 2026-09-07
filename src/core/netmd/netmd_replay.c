// netmd_replay.c - the transcript transport of ADR-008. See netmd_replay.h for
// the file format.
#include "netmd_replay.h"

typedef struct NetmdReplayLine {
    u8 kind;  // '>', '<', '!' or 0 at the end of the transcript
    String8 body;
} NetmdReplayLine;

static u32 netmd_hex_value(u8 c) {
    if (c >= '0' && c <= '9') { return (u32)(c - '0'); }
    if (c >= 'a' && c <= 'f') { return (u32)(c - 'a') + 10; }
    if (c >= 'A' && c <= 'F') { return (u32)(c - 'A') + 10; }
    return U32_MAX;
}

// Hex bytes, whitespace anywhere: "c1 01 0000" and "c101 0000" are the same
// eight nibbles. An odd nibble count or a stray character is malformed.
static b32 netmd_replay_hex(String8 text, u8 *out, u64 capacity, u64 *out_size) {
    u64 count = 0;
    u32 high = U32_MAX;
    for (u64 i = 0; i < text.size; i += 1) {
        u8 c = text.str[i];
        if (c == ' ' || c == '\t' || c == '\r') { continue; }
        u32 nibble = netmd_hex_value(c);
        if (nibble == U32_MAX) { return 0; }
        if (high == U32_MAX) {
            high = nibble;
        } else {
            if (count >= capacity) { return 0; }
            out[count] = (u8)((high << 4) | nibble);
            count += 1;
            high = U32_MAX;
        }
    }
    if (high != U32_MAX) { return 0; }
    *out_size = count;
    return 1;
}

// The next line that means something: comments and blank lines are skipped
// here so nothing downstream has to know they exist.
static NetmdReplayLine netmd_replay_next_line(NetmdReplay *replay) {
    NetmdReplayLine result;
    result.kind = 0;
    result.body = str8(0, 0);
    while (replay->at < replay->trace.size) {
        u64 start = replay->at;
        u64 end = start;
        while (end < replay->trace.size && replay->trace.str[end] != '\n') { end += 1; }
        replay->at = (end < replay->trace.size) ? end + 1 : end;
        replay->line += 1;
        String8 line = str8_trim(str8_substr(replay->trace, start, end - start));
        if (line.size == 0 || line.str[0] == '#') { continue; }
        result.kind = line.str[0];
        result.body = str8_trim(str8_skip(line, 1));
        return result;
    }
    return result;
}

static i32 netmd_replay_fail(NetmdReplay *replay, u32 reason) {
    replay->fail = reason;
    replay->fail_line = replay->line;
    return OsUsbError_Divergence;
}

// One exchange: match the request against the transcript, hand back what the
// device answered. `buffer` is only written for a device to host transfer, and
// only up to `length`, exactly as a real short read would.
static i32 netmd_replay_exchange(NetmdReplay *replay, const u8 *request, u64 request_size,
                                 void *buffer, u32 length) {
    if (replay->fail != NetmdReplayFail_None) { return OsUsbError_Divergence; }

    b32 timeout = 0;
    NetmdReplayLine line = netmd_replay_next_line(replay);
    while (line.kind == '!') {
        if (str8_eq(line.body, str8_lit("timeout"))) {
            timeout = 1;
        } else {
            return netmd_replay_fail(replay, NetmdReplayFail_Malformed);
        }
        line = netmd_replay_next_line(replay);
    }
    if (line.kind == 0) { return netmd_replay_fail(replay, NetmdReplayFail_Exhausted); }
    if (line.kind != '>') { return netmd_replay_fail(replay, NetmdReplayFail_Malformed); }

    if (!netmd_replay_hex(line.body, replay->expected, sizeof(replay->expected),
                          &replay->expected_size)) {
        return netmd_replay_fail(replay, NetmdReplayFail_Malformed);
    }
    replay->actual_size = Min(request_size, sizeof(replay->actual));
    mem_copy(replay->actual, request, replay->actual_size);
    if (replay->expected_size != request_size ||
        mem_cmp(replay->expected, request, request_size) != 0) {
        return netmd_replay_fail(replay, NetmdReplayFail_Diverged);
    }
    replay->exchanges += 1;

    // The answer is optional: an OUT transfer has nothing to say back.
    u64 before = replay->at;
    u64 before_line = replay->line;
    NetmdReplayLine answer = netmd_replay_next_line(replay);
    u8 bytes[NETMD_REPLAY_MAX_BYTES];
    u64 size = 0;
    if (answer.kind == '<') {
        if (!netmd_replay_hex(answer.body, bytes, sizeof(bytes), &size)) {
            return netmd_replay_fail(replay, NetmdReplayFail_Malformed);
        }
    } else {
        replay->at = before;  // not ours: leave it for the next exchange
        replay->line = before_line;
    }
    // A timeout is reported *after* the request was checked: the host did emit
    // the right frame, the device simply never answered (research/01 s2.9).
    if (timeout) { return OsUsbError_Timeout; }
    if (answer.kind != '<') { return (i32)length; }

    u64 copied = Min(size, (u64)length);
    if (copied != 0 && buffer) { mem_copy(buffer, bytes, copied); }
    return (i32)copied;
}

static i32 netmd_replay_control(void *user, u8 request_type, u8 request, u16 value, u16 index,
                                void *buffer, u32 length, u32 timeout_ms) {
    Unused(timeout_ms);
    NetmdReplay *replay = (NetmdReplay *)user;
    u8 frame[8 + NETMD_REPLAY_MAX_BYTES];
    frame[0] = request_type;
    frame[1] = request;
    frame[2] = (u8)(value & 0xFFu);
    frame[3] = (u8)(value >> 8);
    frame[4] = (u8)(index & 0xFFu);
    frame[5] = (u8)(index >> 8);
    frame[6] = (u8)(length & 0xFFu);
    frame[7] = (u8)(length >> 8);
    u64 size = 8;
    // Host to device: the payload is part of the request, and part of what the
    // transcript pins down.
    if ((request_type & 0x80u) == 0 && length != 0 && buffer) {
        u64 payload = Min((u64)length, (u64)NETMD_REPLAY_MAX_BYTES);
        mem_copy(frame + 8, buffer, payload);
        size += payload;
    }
    return netmd_replay_exchange(replay, frame, size, buffer, length);
}

static i32 netmd_replay_bulk_write(void *user, u8 endpoint, const void *data, u32 length,
                                   u32 timeout_ms) {
    Unused(timeout_ms);
    NetmdReplay *replay = (NetmdReplay *)user;
    u8 frame[1 + NETMD_REPLAY_MAX_BYTES];
    frame[0] = endpoint;
    u64 payload = Min((u64)length, (u64)NETMD_REPLAY_MAX_BYTES);
    if (payload != 0 && data) { mem_copy(frame + 1, data, payload); }
    return netmd_replay_exchange(replay, frame, payload + 1, 0, length);
}

static i32 netmd_replay_bulk_read(void *user, u8 endpoint, void *data, u32 length,
                                  u32 timeout_ms) {
    Unused(timeout_ms);
    NetmdReplay *replay = (NetmdReplay *)user;
    u8 frame[1];
    frame[0] = endpoint;
    return netmd_replay_exchange(replay, frame, 1, data, length);
}

void netmd_replay_init(NetmdReplay *replay, String8 trace) {
    StructZero(replay);
    replay->trace = trace;
}

b32 netmd_replay_load(NetmdReplay *replay, Arena *arena, String8 path) {
    String8 text = os_file_read_all(arena, path);
    netmd_replay_init(replay, text);
    return text.size != 0;
}

void netmd_replay_transport(NetmdReplay *replay, UsbTransport *out) {
    out->control = netmd_replay_control;
    out->bulk_write = netmd_replay_bulk_write;
    out->bulk_read = netmd_replay_bulk_read;
    out->user = replay;
}

b32 netmd_replay_done(const NetmdReplay *replay) {
    if (replay->fail != NetmdReplayFail_None) { return 0; }
    // A copy walks what is left: asking the question must not consume it.
    // mem_copy and not an assignment: /GL turns a struct copy into a memcpy the
    // no-CRT link cannot resolve (CONVENTIONS, T-004).
    NetmdReplay tail;
    mem_copy(&tail, replay, sizeof(tail));
    NetmdReplayLine line = netmd_replay_next_line(&tail);
    return line.kind == 0;
}

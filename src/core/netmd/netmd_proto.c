// netmd_proto.c - see netmd_proto.h for the contract.
#include "netmd_proto.h"

const NetmdDescriptor netmd_desc_disc_title = {{0x10, 0x18, 0x01}, 3};
const NetmdDescriptor netmd_desc_utoc1 = {{0x10, 0x18, 0x02}, 3};
const NetmdDescriptor netmd_desc_utoc4 = {{0x10, 0x18, 0x03}, 3};
const NetmdDescriptor netmd_desc_audio_contents = {{0x10, 0x10, 0x01}, 3};
const NetmdDescriptor netmd_desc_root = {{0x10, 0x10, 0x00}, 3};
const NetmdDescriptor netmd_desc_subunit = {{0x00, 0x00, 0x00}, 1};
const NetmdDescriptor netmd_desc_op_status = {{0x80, 0x00, 0x00}, 2};

// --- the mini language ------------------------------------------------------

static u32 netmd_hex_digit(u8 c) {
    if (c >= '0' && c <= '9') { return (u32)(c - '0'); }
    if (c >= 'a' && c <= 'f') { return (u32)(c - 'a') + 10u; }
    if (c >= 'A' && c <= 'F') { return (u32)(c - 'A') + 10u; }
    return U32_MAX;
}

// A cursor over the format string, shared by the writer and the reader so the
// two can never disagree on what a token is.
typedef struct NetmdFmt {
    const char *at;
    u8 wide;    // 'b' 'w' 'd' 'q' 'B' 'W' 'x' 'z' 's' '*' '#' '?' or 0 for a literal
    u8 little;  // the '<' modifier was present
    u8 literal;
    u8 done;
} NetmdFmt;

// The next token: a literal byte, a placeholder, or the end. Whitespace never
// reaches the caller, and an unpaired hex digit is a bug in *our* format string
// - the only kind of bad format that can exist, since they are all literals in
// our own source.
static NetmdFmt netmd_fmt_next(const char *at) {
    NetmdFmt result;
    StructZero(&result);
    while (*at == ' ' || *at == '\t' || *at == '\n') { at += 1; }
    if (*at == 0) {
        result.at = at;
        result.done = 1;
        return result;
    }
    if (*at == '%') {
        at += 1;
        if (*at == '<') {
            result.little = 1;
            at += 1;
        }
        result.wide = (u8)*at;
        result.at = at + 1;
        return result;
    }
    u32 high = netmd_hex_digit((u8)at[0]);
    u32 low = netmd_hex_digit((u8)at[1]);
    AssertAlways(high != U32_MAX && low != U32_MAX);  // our own format strings
    result.literal = (u8)((high << 4) | low);
    result.wide = 0;
    result.at = at + 2;
    return result;
}

static u32 netmd_fmt_width(u8 spec) {
    switch (spec) {
        case 'b': case 'B': return 1;
        case 'w': case 'W': return 2;
        case 'd': return 4;
        case 'q': return 8;
        default: return 0;
    }
}

static void netmd_put_int(u8 *out, u64 value, u32 width, b32 little) {
    for (u32 i = 0; i < width; i += 1) {
        u32 shift = little ? (i * 8u) : ((width - 1u - i) * 8u);
        out[i] = (u8)(value >> shift);
    }
}

static u64 netmd_get_int(const u8 *in, u32 width, b32 little) {
    u64 value = 0;
    for (u32 i = 0; i < width; i += 1) {
        u32 shift = little ? (i * 8u) : ((width - 1u - i) * 8u);
        value |= (u64)in[i] << shift;
    }
    return value;
}

String8 netmd_queryv(Arena *arena, const char *fmt, va_list args) {
    u8 *out = push_array(arena, u8, NETMD_FRAME_MAX);
    u64 size = 0;
    for (NetmdFmt token = netmd_fmt_next(fmt);; token = netmd_fmt_next(token.at)) {
        if (token.done) { break; }
        if (token.wide == 0) {
            AssertAlways(size < NETMD_FRAME_MAX);
            out[size] = token.literal;
            size += 1;
            continue;
        }
        u32 width = netmd_fmt_width(token.wide);
        if (width != 0) {
            u64 value;
            if (token.wide == 'q') {
                value = va_arg(args, u64);
            } else {
                value = va_arg(args, u32);
            }
            if (token.wide == 'B') { value = netmd_bcd_from_u8((u32)value); }
            if (token.wide == 'W') {
                // Two BCD bytes: hundreds and units of the same decimal number,
                // which is how an hour count over 99 could never be written.
                u32 decimal = (u32)value;
                value = ((u64)netmd_bcd_from_u8(decimal / 100u) << 8) |
                        netmd_bcd_from_u8(decimal % 100u);
            }
            AssertAlways(size + width <= NETMD_FRAME_MAX);
            netmd_put_int(out + size, value, width, token.little);
            size += width;
            continue;
        }
        // The buffer forms: the length prefix differs, the payload does not.
        const u8 *data = va_arg(args, const u8 *);
        u32 length = va_arg(args, u32);
        u32 prefix = 0;
        u32 declared = length;
        if (token.wide == 'x') { prefix = 2; }
        if (token.wide == 'z') { prefix = 1; }
        if (token.wide == 's') {
            prefix = 2;
            declared = length + 1;  // s3.3: %s counts the NUL it appends
        }
        AssertAlways(size + prefix + declared <= NETMD_FRAME_MAX);
        if (prefix != 0) { netmd_put_int(out + size, declared, prefix, 0); }
        size += prefix;
        if (length != 0) { mem_copy(out + size, data, length); }
        size += length;
        if (token.wide == 's') {
            out[size] = 0;
            size += 1;
        }
    }
    return str8(out, size);
}

String8 netmd_query(Arena *arena, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    String8 result = netmd_queryv(arena, fmt, args);
    va_end(args);
    return result;
}

b32 netmd_scanv(String8 reply, const char *fmt, va_list args) {
    u64 at = 0;
    for (NetmdFmt token = netmd_fmt_next(fmt);; token = netmd_fmt_next(token.at)) {
        if (token.done) { break; }
        if (token.wide == 0) {
            // A literal that does not match is the whole point of the language:
            // the device answered a shape we do not know (s3.3).
            if (at >= reply.size || reply.str[at] != token.literal) { return 0; }
            at += 1;
            continue;
        }
        if (token.wide == '?') {
            if (at >= reply.size) { return 0; }
            at += 1;
            continue;
        }
        if (token.wide == '*' || token.wide == '#') {
            String8 *out = va_arg(args, String8 *);
            *out = str8(reply.str + at, reply.size - at);
            at = reply.size;
            continue;
        }
        u32 width = netmd_fmt_width(token.wide);
        if (width != 0) {
            if (at + width > reply.size) { return 0; }
            u64 value = netmd_get_int(reply.str + at, width, token.little);
            at += width;
            if (token.wide == 'B') { value = netmd_bcd_to_u8((u8)value); }
            if (token.wide == 'W') {
                value = netmd_bcd_to_u8((u8)(value >> 8)) * 100u + netmd_bcd_to_u8((u8)value);
            }
            if (token.wide == 'q') {
                u64 *out = va_arg(args, u64 *);
                *out = value;
            } else {
                u32 *out = va_arg(args, u32 *);
                *out = (u32)value;
            }
            continue;
        }
        u32 prefix = (token.wide == 'z') ? 1u : 2u;
        if (at + prefix > reply.size) { return 0; }
        u64 length = netmd_get_int(reply.str + at, prefix, 0);
        at += prefix;
        if (token.wide == 's') {
            if (length == 0) { return 0; }
            length -= 1;  // the NUL the writer appended
        }
        if (at + length > reply.size) { return 0; }
        String8 *out = va_arg(args, String8 *);
        *out = str8(reply.str + at, length);
        at += length;
        if (token.wide == 's') { at += 1; }
    }
    // s3.3: leftover bytes mean a different reply model, not a longer one.
    return at == reply.size;
}

b32 netmd_scan(String8 reply, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    b32 result = netmd_scanv(reply, fmt, args);
    va_end(args);
    return result;
}

// --- the exchange -----------------------------------------------------------

void netmd_session_init(NetmdSession *session, const UsbTransport *transport, u16 vid, u16 pid) {
    StructZero(session);
    mem_copy(&session->transport, transport, sizeof(UsbTransport));
    session->vid = vid;
    session->pid = pid;
}

void netmd_session_hold(NetmdSession *session, u32 ms) {
    session->quiet_until_us = os_time_now_us() + (u64)ms * 1000u;
}

static void netmd_session_wait_quiet(NetmdSession *session) {
    u64 now = os_time_now_us();
    if (now >= session->quiet_until_us) { return; }
    os_sleep_us(session->quiet_until_us - now);
    session->quiet_until_us = 0;
}

static i32 netmd_poll(NetmdSession *session, u8 *out4) {
    const UsbTransport *transport = &session->transport;
    return transport->control(transport->user, NETMD_RT_IN, NETMD_REQ_POLL, 0, 0, out4, 4,
                              NETMD_TRANSFER_TIMEOUT_MS);
}

static u32 netmd_usb_fail(NetmdSession *session, i32 error) {
    session->usb_error = error;
    return (error == OsUsbError_Timeout) ? NetmdResult_Timeout : NetmdResult_Usb;
}

// The poll loop of s2.6: capped exponential backoff against an explicit budget.
// poll[0] == 0 is "not yet" and never an error.
static u32 netmd_wait_reply(NetmdSession *session, u32 budget_ms, u8 *out_request,
                            u8 *out_length) {
    u32 waited = 0;
    u32 delay = NETMD_POLL_DELAY_MIN_MS;
    for (;;) {
        u8 poll[4];
        i32 got = netmd_poll(session, poll);
        if (got < 0) { return netmd_usb_fail(session, got); }
        if (got >= 4 && poll[0] != 0) {
            *out_request = poll[1];
            *out_length = poll[2];
            return NetmdResult_Ok;
        }
        if (waited >= budget_ms) { return NetmdResult_Timeout; }
        os_sleep_us((u64)delay * 1000u);
        waited += delay;
        delay = (delay < NETMD_POLL_DELAY_MAX_MS) ? delay * 2u : NETMD_POLL_DELAY_MAX_MS;
    }
}

static u32 netmd_read_reply(NetmdSession *session, Arena *arena, u8 request, u8 length,
                            String8 *out) {
    if (length == 0) { return NetmdResult_Malformed; }
    const UsbTransport *transport = &session->transport;
    u8 *buffer = push_array(arena, u8, length);
    // poll[1] and not a hard coded 0x81: the factory channel answers on another
    // request, and a device that asks for one is not asking for fun (s2.5).
    i32 got = transport->control(transport->user, NETMD_RT_IN, request, 0, 0, buffer, length,
                                 NETMD_TRANSFER_TIMEOUT_MS);
    if (got < 0) { return netmd_usb_fail(session, got); }
    if (got == 0) { return NetmdResult_Malformed; }
    *out = str8(buffer, (u64)got);
    return NetmdResult_Ok;
}

static u32 netmd_status_result(u8 status) {
    switch (status) {
        case NetmdStatus_Accepted:
        case NetmdStatus_Implemented:
        case NetmdStatus_Changed: return NetmdResult_Ok;
        case NetmdStatus_NotImplemented: return NetmdResult_NotImplemented;
        default: return NetmdResult_Rejected;
    }
}

u32 netmd_exchange(NetmdSession *session, Arena *arena, String8 request, u32 budget_ms,
                   String8 *out_reply) {
    if (!netmd_transport_bound(&session->transport)) {
        session->usb_error = OsUsbError_NotOpen;
        return NetmdResult_Usb;
    }
    netmd_session_wait_quiet(session);

    // s2.5 invariant 1: a reply still pending means the previous command was
    // never collected. Sending now would answer *that* one and desynchronise
    // the state machine for the rest of the session, so it is drained first.
    u8 poll[4];
    i32 got = netmd_poll(session, poll);
    if (got < 0) { return netmd_usb_fail(session, got); }
    if (got >= 4 && poll[2] != 0) {
        ArenaTemp orphan = arena_temp_begin(arena);
        String8 dropped;
        netmd_read_reply(session, arena, poll[1], poll[2], &dropped);
        arena_temp_end(orphan);
    }

    const UsbTransport *transport = &session->transport;
    // The frame is copied: a transport may write into the buffer it is given
    // (the replay one does), and `request` may live in a caller's read only
    // table.
    u8 frame[NETMD_FRAME_MAX];
    AssertAlways(request.size <= sizeof(frame));
    mem_copy(frame, request.str, request.size);
    got = transport->control(transport->user, NETMD_RT_OUT, NETMD_REQ_SEND, 0, 0, frame,
                             (u32)request.size, NETMD_TRANSFER_TIMEOUT_MS);
    if (got < 0) { return netmd_usb_fail(session, got); }
    session->exchanges += 1;

    // s3.2: an INTERIM is not an answer, it is a promise of one. Poll again for
    // the real reply rather than resending, which would queue a second command.
    for (u32 attempt = 0;; attempt += 1) {
        u8 reply_request = NETMD_REQ_READ;
        u8 reply_length = 0;
        u32 result = netmd_wait_reply(session, budget_ms, &reply_request, &reply_length);
        if (result != NetmdResult_Ok) { return result; }
        String8 reply;
        result = netmd_read_reply(session, arena, reply_request, reply_length, &reply);
        if (result != NetmdResult_Ok) { return result; }
        session->last_status = reply.str[0];
        if (reply.str[0] != NetmdStatus_Interim && reply.str[0] != NetmdStatus_InTransition) {
            *out_reply = reply;
            return netmd_status_result(reply.str[0]);
        }
        if (attempt >= NETMD_INTERIM_RETRIES) {
            *out_reply = reply;
            return NetmdResult_Timeout;
        }
        // s3.2: 100 * (2^n - 1) ms between attempts - 0, 100, 300, 700, 1500.
        os_sleep_us((u64)100000u * (((u64)1 << attempt) - 1u));
    }
}

u32 netmd_command(NetmdSession *session, Arena *arena, u32 budget_ms, String8 *out_reply,
                  const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    String8 request = netmd_queryv(arena, fmt, args);
    va_end(args);
    return netmd_exchange(session, arena, request, budget_ms, out_reply);
}

// --- descriptors, acquire / release -----------------------------------------

static u32 netmd_descriptor_state(NetmdSession *session, Arena *arena,
                                  const NetmdDescriptor *desc, u8 action) {
    ArenaTemp scratch = arena_temp_begin(arena);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_DESCRIPTOR_MS, &reply,
                               "00 1808 %* %b 00", desc->bytes, (u32)desc->size, (u32)action);
    arena_temp_end(scratch);
    return result;
}

u32 netmd_descriptor_open(NetmdSession *session, Arena *arena, const NetmdDescriptor *desc,
                          u8 action) {
    return netmd_descriptor_state(session, arena, desc, action);
}

u32 netmd_descriptor_close(NetmdSession *session, Arena *arena, const NetmdDescriptor *desc) {
    return netmd_descriptor_state(session, arena, desc, NETMD_DESC_CLOSE);
}

u32 netmd_acquire(NetmdSession *session, Arena *arena) {
    ArenaTemp scratch = arena_temp_begin(arena);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                               "00 ff 010c ffff ffff ffff ffff ffff ffff");
    arena_temp_end(scratch);
    return result;
}

u32 netmd_release(NetmdSession *session, Arena *arena) {
    ArenaTemp scratch = arena_temp_begin(arena);
    String8 reply;
    u32 result = netmd_command(session, arena, NETMD_BUDGET_QUERY_MS, &reply,
                               "00 ff 0100 ffff ffff ffff ffff ffff ffff");
    arena_temp_end(scratch);
    return result;
}

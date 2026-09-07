// netmd_secure.c - see netmd_secure.h. Every frame here is quoted from
// research/01 s4; nothing is invented.
#include "netmd_secure.h"

#include "netmd_control.h"

const u8 netmd_secure_hdr[NETMD_SECURE_HDR_SIZE] = {0x18, 0x00, 0x08, 0x00, 0x46,
                                                    0xF0, 0x03, 0x01, 0x03};

// s4.5, the open source EKB. These are published community constants, not Sony
// code: an EKB is a block of key material, and this one is the block every
// non-revoked NetMD accepts.
global const u8 netmd_ekb_open_chain[32] = {
    0x25, 0x45, 0x06, 0x4D, 0xEA, 0xCA, 0x14, 0xF9, 0x96, 0xBD, 0xC8,
    0xA4, 0x06, 0xC2, 0x2B, 0x81, 0xFB, 0x60, 0xBD, 0xDD, 0x0D, 0xBC,
    0xAB, 0x84, 0x8A, 0x00, 0x5E, 0x03, 0x19, 0x4D, 0x3E, 0xDA};
global const u8 netmd_ekb_open_signature[24] = {
    0x8F, 0x2B, 0xC3, 0x52, 0xE8, 0x6C, 0x5E, 0xD3, 0x06, 0xDC, 0xAE, 0x18,
    0xD2, 0xF3, 0x8C, 0x7F, 0x89, 0xB5, 0xE1, 0x85, 0x55, 0xA1, 0x05, 0xEA};
global const u8 netmd_ekb_open_root[16] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
                                           0x0F, 0xED, 0xCB, 0xA9, 0x87, 0x65, 0x43, 0x21};

// s4.5, the deck with the erased EEPROM: leaf id FF x8 on 054c:0081.
global const u8 netmd_ekb_deck_chain[16] = {0xB1, 0xD4, 0xAF, 0xFA, 0x80, 0xA0, 0xC9, 0x03,
                                            0xC2, 0x58, 0x4B, 0x1B, 0x44, 0xAF, 0xC4, 0xA6};
global const u8 netmd_ekb_deck_signature[24] = {
    0x6C, 0x2B, 0xC2, 0x8C, 0x45, 0x2B, 0x54, 0xF1, 0xC3, 0x59, 0x72, 0x3B,
    0xE3, 0x19, 0x1F, 0x55, 0x17, 0x25, 0x64, 0x0E, 0x65, 0x8C, 0x81, 0x0B};
// "WMDPWMDPMiniDisc" - the key is ASCII, which is how it was found.
global const u8 netmd_ekb_deck_root[16] = {0x57, 0x4D, 0x44, 0x50, 0x57, 0x4D, 0x44, 0x50,
                                           0x4D, 0x69, 0x6E, 0x69, 0x44, 0x69, 0x73, 0x63};

global const NetmdEkb netmd_ekb_open_source = {
    0x26422642u, 9u, 2u, netmd_ekb_open_chain, netmd_ekb_open_signature, netmd_ekb_open_root};
global const NetmdEkb netmd_ekb_corrupted_deck = {
    0x13371337u, 9u, 1u, netmd_ekb_deck_chain, netmd_ekb_deck_signature, netmd_ekb_deck_root};

const u8 netmd_content_id[20] = {0x01, 0x0F, 0x50, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x48,
                                 0xA2, 0x8D, 0x3E, 0x1A, 0x3B, 0x0C, 0x44, 0xAF, 0x2F, 0xA0};
const u8 netmd_kek[8] = {0x14, 0xE3, 0x83, 0x4E, 0xE2, 0xD3, 0xCC, 0xA5};

const NetmdEkb *netmd_ekb_select(const u8 leaf_id[8], u16 vid, u16 pid) {
    b32 all_ff = 1;
    for (u32 i = 0; i < 8; i += 1) {
        if (leaf_id[i] != 0xFF) { all_ff = 0; }
    }
    if (all_ff && vid == 0x054Cu && pid == 0x0081u) { return &netmd_ekb_corrupted_deck; }
    return &netmd_ekb_open_source;
}

const u8 *netmd_ekb_open_source_root_key(void) { return netmd_ekb_open_root; }

u32 netmd_secure_frame_size(u8 wireformat) {
    switch (wireformat) {
        case NETMD_WIREFORMAT_LP2: return 192u;
        case NETMD_WIREFORMAT_LP4: return 96u;
        default: return 2048u;  // PCM, s4.8
    }
}

// --- one secure command ------------------------------------------------------

static void netmd_secure_default_random(void *user, u8 *dst, u64 size) {
    Unused(user);
    os_random_bytes(dst, size);
}

void netmd_secure_init(NetmdSecure *secure, NetmdSession *session, Arena *arena) {
    StructZero(secure);
    secure->session = session;
    secure->arena = arena;
    secure->random = netmd_secure_default_random;
    secure->ekb = &netmd_ekb_open_source;
}

void netmd_secure_set_random(NetmdSecure *secure, NetmdRandomFn *fn, void *user) {
    secure->random = fn;
    secure->random_user = user;
}

// s4.1: the reply repeats the signature and the command, with a placeholder in
// place of the 0xFF. The placeholder is 0x00 everywhere except sendKeyData
// (0x01) and, on a Panasonic SJ-MR270, anything at all after 0x20 - so it is
// read past rather than checked. The signature and the command byte are what
// prove this is the answer to the command that was sent.
static u32 netmd_secure_check(String8 reply, u8 command, String8 *out_data) {
    // status, the nine byte signature, the command, the placeholder, then data.
    if (reply.size < 3 + NETMD_SECURE_HDR_SIZE) { return NetmdResult_Malformed; }
    if (mem_cmp(reply.str + 1, netmd_secure_hdr, NETMD_SECURE_HDR_SIZE) != 0) {
        return NetmdResult_Malformed;
    }
    if (reply.str[1 + NETMD_SECURE_HDR_SIZE] != command) { return NetmdResult_Malformed; }
    u64 at = 3 + NETMD_SECURE_HDR_SIZE;
    if (out_data) { *out_data = str8(reply.str + at, reply.size - at); }
    return NetmdResult_Ok;
}

u32 netmd_secure_command(NetmdSecure *secure, u8 command, const u8 *data, u64 size,
                         String8 *out_data) {
    ArenaTemp scratch = arena_temp_begin(secure->arena);
    String8 request = netmd_query(secure->arena, "00 %* %b ff %*", netmd_secure_hdr,
                                  (u32)NETMD_SECURE_HDR_SIZE, (u32)command, data, (u32)size);
    String8 reply;
    u32 result = netmd_exchange(secure->session, secure->arena, request, NETMD_BUDGET_SECURE_MS,
                                &reply);
    if (result == NetmdResult_Ok) {
        String8 payload;
        result = netmd_secure_check(reply, command, &payload);
        if (result == NetmdResult_Ok && out_data) {
            // Copied out of the scratch region: the caller keeps it after the
            // arena is rewound, and every payload here is a handful of bytes.
            u8 *bytes = push_array(secure->arena, u8, Max(payload.size, (u64)1));
            mem_copy(bytes, payload.str, payload.size);
            arena_temp_end(scratch);
            *out_data = str8(bytes, payload.size);
            return NetmdResult_Ok;
        }
    }
    arena_temp_end(scratch);
    if (out_data) { *out_data = str8(0, 0); }
    return result;
}

// s4.6: sessionKeyForget carries three zero bytes, unlike the other bare
// commands. Sending it without them gets a REJECTED that looks like a state
// problem and is not.
static u32 netmd_secure_forget(NetmdSecure *secure) {
    u8 payload[3];
    mem_zero(payload, sizeof(payload));
    return netmd_secure_command(secure, NETMD_SEC_KEY_FORGET, payload, sizeof(payload), 0);
}

// --- the session -------------------------------------------------------------

static u32 netmd_secure_leaf_id(NetmdSecure *secure) {
    String8 data;
    u32 result = netmd_secure_command(secure, NETMD_SEC_LEAF_ID, 0, 0, &data);
    if (result != NetmdResult_Ok) { return result; }
    if (data.size < 8) { return NetmdResult_Malformed; }
    mem_copy(secure->leaf_id, data.str, 8);
    return NetmdResult_Ok;
}

// s4.5. databytes = 16 + 16 * chainLength + 24, written twice, and the whole
// payload after it is fixed length - which is why it is spelled out rather than
// assembled from a struct.
static u32 netmd_secure_send_key_data(NetmdSecure *secure) {
    const NetmdEkb *ekb = secure->ekb;
    u32 databytes = 16u + 16u * ekb->chain_length + 24u;
    ArenaTemp scratch = arena_temp_begin(secure->arena);
    String8 payload = netmd_query(secure->arena, "%w 0000 %w %d %d %d 00000000 %* %*",
                                  databytes, databytes, ekb->chain_length, ekb->depth, ekb->id,
                                  ekb->chain, ekb->chain_length * 16u, ekb->signature, 24u);
    u32 result = netmd_secure_command(secure, NETMD_SEC_SEND_KEY_DATA, payload.str, payload.size,
                                      0);
    arena_temp_end(scratch);
    return result;
}

// s4.6: 8 host bytes out, 8 device bytes back, and the session key is the
// retail MAC of the pair under the EKB's root key.
static u32 netmd_secure_exchange_nonces(NetmdSecure *secure) {
    secure->random(secure->random_user, secure->host_nonce, 8);
    u8 payload[3 + 8];
    payload[0] = 0;
    payload[1] = 0;
    payload[2] = 0;
    mem_copy(payload + 3, secure->host_nonce, 8);
    String8 data;
    u32 result = netmd_secure_command(secure, NETMD_SEC_KEY_EXCHANGE, payload, sizeof(payload),
                                      &data);
    if (result != NetmdResult_Ok) { return result; }
    if (data.size < 3 + 8) { return NetmdResult_Malformed; }
    mem_copy(secure->dev_nonce, data.str + 3, 8);

    u8 nonces[16];
    mem_copy(nonces, secure->host_nonce, 8);
    mem_copy(nonces + 8, secure->dev_nonce, 8);
    des_retail_mac(secure->ekb->root_key, nonces, sizeof(nonces), secure->session_key);
    des_key_init(&secure->session_des, secure->session_key);
    des_key_init(&secure->kek_des, netmd_kek);
    return NetmdResult_Ok;
}

u32 netmd_secure_begin(NetmdSecure *secure) {
    NetmdSession *session = secure->session;
    Arena *arena = secure->arena;
    // s4.14, the preventive half: a session left open by a crash makes
    // enterSecureSession answer REJECTED, and the only cure a user finds on
    // their own is unplugging the device. Both errors are ignored on purpose.
    (void)netmd_secure_forget(secure);
    (void)netmd_secure_command(secure, NETMD_SEC_LEAVE_SESSION, 0, 0, 0);
    u32 result = netmd_acquire(session, arena);
    if (result != NetmdResult_Ok) { return result; }
    secure->acquired = 1;
    // s4.13: new tracks are checked out and unerasable unless this is sent, and
    // it fails on Sharp - which is not our problem, so the error is dropped.
    u8 protect[2] = {0x00, 0x01};
    (void)netmd_secure_command(secure, NETMD_SEC_TRACK_PROTECT, protect, sizeof(protect), 0);

    result = netmd_secure_command(secure, NETMD_SEC_ENTER_SESSION, 0, 0, 0);
    if (result != NetmdResult_Ok) { return result; }
    secure->in_session = 1;

    result = netmd_secure_leaf_id(secure);
    if (result != NetmdResult_Ok) { return result; }
    secure->ekb = netmd_ekb_select(secure->leaf_id, session->vid, session->pid);
    result = netmd_secure_send_key_data(secure);
    if (result != NetmdResult_Ok) { return result; }
    return netmd_secure_exchange_nonces(secure);
}

void netmd_secure_end(NetmdSecure *secure) {
    // s4.14: unconditional, in this order, errors ignored. Called from every
    // exit path including the ones that already failed.
    (void)netmd_secure_forget(secure);
    (void)netmd_secure_command(secure, NETMD_SEC_LEAVE_SESSION, 0, 0, 0);
    secure->in_session = 0;
    if (secure->acquired) {
        (void)netmd_release(secure->session, secure->arena);
        secure->acquired = 0;
    }
    // The status read that proves the machine came back: a device still stuck
    // in a transfer answers nothing here, and the caller reports that rather
    // than pretending the disc is fine.
    ArenaTemp scratch = arena_temp_begin(secure->arena);
    u32 state = 0, raw = 0;
    (void)netmd_get_state(secure->session, secure->arena, &state, &raw);
    arena_temp_end(scratch);
}

u32 netmd_secure_wait_ready(NetmdSecure *secure) {
    for (u32 attempt = 0; attempt < NETMD_SECURE_READY_TRIES; attempt += 1) {
        ArenaTemp scratch = arena_temp_begin(secure->arena);
        u32 state = 0, raw = 0;
        u32 result = netmd_get_state(secure->session, secure->arena, &state, &raw);
        arena_temp_end(scratch);
        // A stopped device answers Ready; a blank disc answers DiscBlank, which
        // is exactly the disc we most want to write to (s4.14).
        if (result == NetmdResult_Ok &&
            (state == NetmdState_Ready || state == NetmdState_DiscBlank)) {
            return NetmdResult_Ok;
        }
        if (result == NetmdResult_Usb) { return result; }
        os_sleep_us((u64)NETMD_SECURE_SEND_HOLD_MS * 1000u);
    }
    return NetmdResult_Timeout;
}

u32 netmd_secure_setup_download(NetmdSecure *secure) {
    // s4.7: 01010101 || contentID || KEK, DES-CBC under the session key, IV 0.
    u8 plain[32];
    plain[0] = plain[1] = plain[2] = plain[3] = 0x01;
    mem_copy(plain + 4, netmd_content_id, 20);
    mem_copy(plain + 24, netmd_kek, 8);
    u8 iv[8];
    mem_zero(iv, sizeof(iv));
    u8 payload[2 + 32];
    payload[0] = 0;
    payload[1] = 0;
    des_cbc_encrypt(&secure->session_des, iv, plain, payload + 2, 32);
    return netmd_secure_command(secure, NETMD_SEC_SETUP_DOWNLOAD, payload, sizeof(payload), 0);
}

// --- the audio ---------------------------------------------------------------

md_inline void netmd_put_u32_be(u8 *p, u32 v) {
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

u32 netmd_secure_send_track(NetmdSecure *secure, NetmdTrackSend *send) {
    NetmdSession *session = secure->session;
    const UsbTransport *transport = &session->transport;
    u32 frame_size = netmd_secure_frame_size(send->wireformat);
    u64 total = send->source.total_bytes;
    AssertAlways(total != 0 && (total % frame_size) == 0);
    u32 frames = (u32)(total / frame_size);
    u32 total_bytes = (u32)(total + NETMD_PACKET_HEADER_BYTES);  // s4.11
    send->bytes_sent = 0;

    ArenaTemp scratch = arena_temp_begin(secure->arena);
    // s4.11: "the Sharps are slow", 200 ms on either side of the command.
    os_sleep_us((u64)NETMD_SECURE_SEND_HOLD_MS * 1000u);
    String8 request = netmd_query(secure->arena, "00 %* 28 ff 000100 1001 ffff 00 %b %b %d %d",
                                  netmd_secure_hdr, (u32)NETMD_SECURE_HDR_SIZE,
                                  (u32)send->wireformat, (u32)send->discformat, frames,
                                  total_bytes);
    u32 result = netmd_send_frame(session, secure->arena, request);
    if (result != NetmdResult_Ok) {
        arena_temp_end(scratch);
        return result;
    }
    String8 interim;
    result = netmd_receive_frame(session, secure->arena, NETMD_BUDGET_SECURE_MS, 0, &interim);
    if (result != NetmdResult_Ok) {
        arena_temp_end(scratch);
        return result;
    }
    if (session->last_status != NetmdStatus_Interim) {
        // Not the go-ahead: the device refused the track (no space, wrong
        // state) and nothing has been written.
        arena_temp_end(scratch);
        return NetmdResult_Rejected;
    }
    os_sleep_us((u64)NETMD_SECURE_SEND_HOLD_MS * 1000u);

    // s4.10: the key that encrypts is random, and what travels is that key
    // *decrypted* under the KEK - the device encrypts it back.
    u8 raw_key[8];
    u8 data_key[8];
    secure->random(secure->random_user, raw_key, sizeof(raw_key));
    des_decrypt_block(&secure->kek_des, raw_key, data_key);
    DesKey packet_key;
    des_key_init(&packet_key, raw_key);

    u8 iv[8];
    mem_zero(iv, sizeof(iv));  // s4.10: zero, and travelling in the clear anyway
    u8 *plain = push_array(secure->arena, u8, NETMD_PACKET_BYTES);
    u8 *cipher = push_array(secure->arena, u8, NETMD_PACKET_BYTES);

    u64 offset = 0;
    b32 first = 1;
    while (offset < total) {
        if (send->cancel && os_atomic_load_u32(send->cancel)) {
            arena_temp_end(scratch);
            return NetmdResult_Cancelled;
        }
        // s4.10: the first packet is 24 bytes shorter, because the header eats
        // into it. Every packet after it is a full chunk.
        u64 want = first ? (NETMD_PACKET_BYTES - NETMD_PACKET_HEADER_BYTES) : NETMD_PACKET_BYTES;
        want = Min(want, total - offset);
        u64 got = send->source.read(send->source.user, plain, want);
        if (got != want) {
            arena_temp_end(scratch);
            return NetmdResult_Malformed;  // the renderer lied about its length
        }
        des_cbc_encrypt(&packet_key, iv, plain, cipher, want);

        if (first) {
            // s4.10: a big-endian quadword holding the payload length, the key
            // the device must decrypt, and the IV of the first block.
            u8 header[NETMD_PACKET_HEADER_BYTES];
            mem_zero(header, 4);
            netmd_put_u32_be(header + 4, (u32)total);
            mem_copy(header + 8, data_key, 8);
            mem_zero(header + 16, 8);  // the IV the CBC stream started from
            i32 written = transport->bulk_write(transport->user, NETMD_EP_BULK_OUT, header,
                                                (u32)sizeof(header), NETMD_BUDGET_SECURE_MS);
            if (written < 0) {
                session->usb_error = written;
                arena_temp_end(scratch);
                return NetmdResult_Usb;
            }
            first = 0;
        }
        i32 written = transport->bulk_write(transport->user, NETMD_EP_BULK_OUT, cipher, (u32)want,
                                            NETMD_BUDGET_TRACK_MS);
        if (written < 0) {
            session->usb_error = written;
            arena_temp_end(scratch);
            return NetmdResult_Usb;
        }
        offset += want;
        send->bytes_sent = offset;
        if (send->progress) { os_atomic_store_u64((volatile u64 *)send->progress, offset); }
        if (send->on_packet) { send->on_packet(send->packet_user, offset); }
    }

    // s4.11: the device has been encoding ATRAC1 the whole time and only
    // answers when the last frame is on the disc.
    String8 reply;
    result = netmd_receive_frame(session, secure->arena, NETMD_BUDGET_TRACK_MS, 1, &reply);
    if (result != NetmdResult_Ok) {
        arena_temp_end(scratch);
        return result;
    }
    String8 data;
    result = netmd_secure_check(reply, NETMD_SEC_SEND_TRACK, &data);
    if (result != NetmdResult_Ok) {
        arena_temp_end(scratch);
        return result;
    }
    // 000100 1001 %w 00 then eight skipped bytes and the 32 byte blob.
    if (data.size < 5 + 2 + 32) {
        arena_temp_end(scratch);
        return NetmdResult_Malformed;
    }
    send->track = ((u32)data.str[5] << 8) | data.str[6];
    u8 blob[32];
    mem_copy(blob, data.str + data.size - 32, 32);
    u8 blob_iv[8];
    mem_zero(blob_iv, sizeof(blob_iv));
    des_cbc_decrypt(&secure->session_des, blob_iv, blob, blob, sizeof(blob));
    mem_copy(send->uuid, blob, 8);
    mem_copy(send->content_id, blob + 12, 20);
    arena_temp_end(scratch);

    // s4.11: one more poll. netmd-js calls getReplyLength once more here, and
    // leaving it out desynchronises the device on some models - the next
    // command reads this leftover instead of its own answer.
    ArenaTemp drain = arena_temp_begin(secure->arena);
    String8 leftover;
    // Budget 0 means exactly one poll and no waiting: there is nothing to wait
    // for, the point is to consume whatever the device left behind.
    (void)netmd_receive_frame(session, secure->arena, 0, 0, &leftover);
    arena_temp_end(drain);
    return NetmdResult_Ok;
}

u32 netmd_secure_commit_track(NetmdSecure *secure, u32 track) {
    // s4.12: the proof that we hold the session key is the encryption of eight
    // zero bytes under it.
    u8 zero[8];
    mem_zero(zero, sizeof(zero));
    u8 auth[8];
    des_encrypt_block(&secure->session_des, zero, auth);
    u8 payload[3 + 2 + 8];
    payload[0] = 0x00;
    payload[1] = 0x10;
    payload[2] = 0x01;
    payload[3] = (u8)(track >> 8);
    payload[4] = (u8)track;
    mem_copy(payload + 5, auth, 8);
    u32 result = netmd_secure_command(secure, NETMD_SEC_COMMIT_TRACK, payload, sizeof(payload), 0);
    if (result == NetmdResult_Ok) {
        secure->tracks_sent += 1;
        // s6.2: the TOC is being written; the next command waits it out.
        netmd_session_hold(secure->session, 500);
    }
    return result;
}

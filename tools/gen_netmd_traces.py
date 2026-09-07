#!/usr/bin/env python3
# gen_netmd_traces.py - writes the tests/netmd/mzn505_*.trace fixtures.
#
# THESE ARE SYNTHETIC. The MZ-N505 on the bench has no WinUSB driver bound yet
# (P-001, ProblemCode 28), so nothing could be captured; every frame below is
# assembled from the hex examples and the scan formats of
# docs/research/01-netmd-protocol.md, section by section, and from nothing else
# (ADR-008: no line of any other implementation is involved).
#
# When the driver is installed, `minidisk.exe --netmd-trace <file>` writes the
# same format from the real device and the fixtures are replaced by the capture:
# the tests do not change, only the bytes do. That is the point of the format.
#
# Usage: python tools/gen_netmd_traces.py [output_dir]

import os
import sys

POLL_IDLE = "00 81 00 00"


def hexs(data):
    return " ".join("%02x" % b for b in data)


class Trace:
    def __init__(self, title):
        self.lines = ["# %s" % title,
                      "# synthetic, from docs/research/01-netmd-protocol.md (see"
                      " tools/gen_netmd_traces.py)"]

    def comment(self, text):
        self.lines.append("# %s" % text)

    def command(self, request, reply):
        """One full exchange as netmd_exchange emits it (research/01 s2.7)."""
        # The poll before sending: nothing pending, we may talk (s2.5).
        self.lines.append("> c1 01 00 00 00 00 04 00")
        self.lines.append("< " + POLL_IDLE)
        # The command itself.
        self.lines.append("> 41 80 00 00 00 00 %02x 00 %s" % (len(request), hexs(request)))
        # The poll that says the answer is ready, then the read.
        assert len(reply) <= 255, (len(reply), hexs(reply))
        self.lines.append("> c1 01 00 00 00 00 04 00")
        self.lines.append("< 01 81 %02x 00" % len(reply))
        self.lines.append("> c1 81 00 00 00 00 %02x 00" % len(reply))
        self.lines.append("< " + hexs(reply))

    def text(self):
        return "\n".join(self.lines) + "\n"


def b(*values):
    return bytes(values)


def u16(value):
    return bytes(((value >> 8) & 0xFF, value & 0xFF))


def bcd(value):
    assert 0 <= value <= 99, value
    return bytes((((value // 10) << 4) | (value % 10),))


def bcd16(value):
    return bcd(value // 100) + bcd(value % 100)


def hms(hours, minutes, seconds, frames):
    """Capacity times: the hour field is two BCD bytes (%W, s3.7.3)."""
    return bcd16(hours) + bcd(minutes) + bcd(seconds) + bcd(frames)


def hms_short(hours, minutes, seconds, frames):
    """Track lengths and positions: the hour field is one BCD byte (%B, s3.7.4)."""
    return bcd(hours) + bcd(minutes) + bcd(seconds) + bcd(frames)


# --- descriptors (s3.4) ------------------------------------------------------
DESC = {
    "root": b(0x10, 0x10, 0x00),
    "contents": b(0x10, 0x10, 0x01),
    "disc_title": b(0x10, 0x18, 0x01),
    "utoc1": b(0x10, 0x18, 0x02),
    "utoc4": b(0x10, 0x18, 0x03),
    "status": b(0x80, 0x00),
}


def descriptor(trace, name, action):
    frame = b(0x00, 0x18, 0x08) + DESC[name] + b(action, 0x00)
    trace.command(frame, b(0x09) + frame[1:])


def open_read(trace, name):
    descriptor(trace, name, 0x01)


def close(trace, name):
    descriptor(trace, name, 0x00)


# --- the queries -------------------------------------------------------------

def disc_present(trace, present):
    trace.comment("s3.14 short status: 0x40 disc present, 0x80 no disc")
    open_read(trace, "status")
    request = b(0x00, 0x18, 0x09, 0x80, 0x01, 0x02, 0x30, 0x88, 0x00, 0x00, 0x30, 0x88, 0x04,
                0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00)
    status = bytearray(9)
    status[4] = 0x40 if present else 0x80
    reply = (b(0x09, 0x18, 0x09, 0x80, 0x01, 0x02, 0x30, 0x88, 0x00, 0x00, 0x30, 0x88, 0x04,
               0x00, 0x10, 0x00, 0x00, 0x09, 0x00, 0x00) + u16(len(status)) + bytes(status))
    trace.command(request, reply)
    close(trace, "status")


def disc_flags(trace, flags):
    trace.comment("s3.7.1 disc flags: 0x10 writable, 0x40 write protected")
    request = b(0x00, 0x18, 0x06, 0x01, 0x10, 0x10, 0x00, 0xFF, 0x00, 0x00, 0x01, 0x00, 0x0B)
    reply = b(0x09, 0x18, 0x06, 0x01, 0x10, 0x10, 0x00, 0x10, 0x00, 0x00, 0x01, 0x00, 0x0B,
              flags)
    trace.command(request, reply)


def disc_capacity(trace, recorded, total, available):
    trace.comment("s3.7.3 capacity: recorded / total / available, hh:mm:ss:ff in BCD")
    request = b(0x00, 0x18, 0x06, 0x02, 0x10, 0x10, 0x00, 0x30, 0x80, 0x03, 0x00, 0xFF, 0x00,
                0x00, 0x00, 0x00, 0x00)
    reply = b(0x09, 0x18, 0x06, 0x02, 0x10, 0x10, 0x00, 0x30, 0x80, 0x03, 0x00, 0x10, 0x00,
              0x00, 0x1D, 0x00, 0x00, 0x00, 0x1B, 0x80, 0x03, 0x00, 0x17, 0x80, 0x00)
    for value in (recorded, total, available):
        reply += b(0x00, 0x05) + hms(*value)
    trace.command(request, reply)


def track_count(trace, count):
    trace.comment("s3.7.2 track count")
    request = b(0x00, 0x18, 0x06, 0x02, 0x10, 0x10, 0x01, 0x30, 0x00, 0x10, 0x00, 0xFF, 0x00,
                0x00, 0x00, 0x00, 0x00)
    reply = b(0x09, 0x18, 0x06, 0x02, 0x10, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x10, 0x00, 0x02, 0x00, count)
    trace.command(request, reply)


def track_attribute(trace, index, p1, p2, payload):
    request = (b(0x00, 0x18, 0x06, 0x02, 0x20, 0x10, 0x01) + u16(index) + u16(p1) + u16(p2) +
               b(0xFF, 0x00, 0x00, 0x00, 0x00, 0x00))
    reply = (b(0x09, 0x18, 0x06, 0x02, 0x20, 0x10, 0x01) + u16(index) + u16(p1) + u16(p2) +
             b(0x10, 0x00, 0x00, 0x00, 0x00, 0x00) + u16(len(payload)) + payload)
    trace.command(request, reply)


def track_info(trace, index, track):
    trace.comment("s3.7.4 track %u: length, encoding, protection" % (index + 1))
    track_attribute(trace, index, 0x3000, 0x0100,
                    b(0x00, 0x01, 0x00, 0x06, 0x00, 0x00) + hms_short(*track["time"]))
    track_attribute(trace, index, 0x3080, 0x0700,
                    b(0x80, 0x07, 0x00, 0x04, 0x01, 0x10, track["encoding"],
                      1 if track.get("mono") else 0))
    request = (b(0x00, 0x18, 0x06, 0x01, 0x20, 0x10, 0x01) + u16(index) +
               b(0xFF, 0x00, 0x00, 0x01, 0x00, 0x08))
    reply = (b(0x09, 0x18, 0x06, 0x01, 0x20, 0x10, 0x01) + u16(index) +
             b(0x10, 0x00, 0x00, 0x01, 0x00, 0x08, 0x03 if track.get("protect") else 0x00))
    trace.command(request, reply)


def track_title(trace, index, wide, title):
    wchar = 0x03 if wide else 0x02
    request = (b(0x00, 0x18, 0x06, 0x02, 0x20, 0x18, wchar) + u16(index) +
               b(0x30, 0x00, 0x0A, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00))
    if not title:
        # s3.8.2: an untitled track answers REJECTED, and that is an empty title.
        trace.command(request, b(0x0A) + request[1:])
        return
    payload = title.encode("cp932")
    reply = (b(0x09, 0x18, 0x06, 0x02, 0x20, 0x18, wchar) + u16(index) +
             b(0x30, 0x00, 0x0A, 0x00, 0x10, 0x00) + u16(len(payload) + 8) + b(0x00, 0x00) +
             u16(len(payload) + 6) + b(0x00, 0x0A) + u16(len(payload)) + payload)
    trace.command(request, reply)


DISC_TITLE_PAGE = 180  # bytes of title per reply; the poll length byte caps at 255


def disc_title(trace, wide, title):
    wchar = 0x01 if wide else 0x00
    open_read(trace, "contents")
    open_read(trace, "disc_title")
    payload = title.encode("cp932") if title else b""
    if not payload:
        request = (b(0x00, 0x18, 0x06, 0x02, 0x20, 0x18, 0x01, 0x00, wchar) +
                   b(0x30, 0x00, 0x0A, 0x00, 0xFF, 0x00) + u16(0) + u16(0))
        trace.command(request, b(0x0A) + request[1:])
        close(trace, "disc_title")
        close(trace, "contents")
        return
    done = 0
    total = len(payload)
    while done < total:
        remaining = 0 if done == 0 else total - done
        request = (b(0x00, 0x18, 0x06, 0x02, 0x20, 0x18, 0x01, 0x00, wchar) +
                   b(0x30, 0x00, 0x0A, 0x00, 0xFF, 0x00) + u16(remaining) + u16(done))
        chunk = payload[done:done + DISC_TITLE_PAGE]
        head = (b(0x09, 0x18, 0x06, 0x02, 0x20, 0x18, 0x01, 0x00, wchar) +
                b(0x30, 0x00, 0x0A, 0x00, 0x10, 0x00))
        if done == 0:
            # s3.8.1: the first chunk size counts its own six byte header.
            reply = (head + u16(len(chunk) + 6) + b(0x00, 0x00, 0x00, 0x00, 0x00, 0x0A) +
                     u16(total) + chunk)
        else:
            reply = head + u16(len(chunk)) + b(0x00, 0x00) + chunk
        trace.command(request, reply)
        done += len(chunk)
    close(trace, "disc_title")
    close(trace, "contents")


# --- T-042: the secure download (s4) -----------------------------------------
# The frames below are the ones netmd_secure.c and netmd_upload.c emit, in the
# order they emit them, and the ciphertext is computed by netmd_crypto_check.py
# from the same standard tables - never read back from the C code. A test that
# passes here means the C agrees with research/01 s4 and with an independent DES.

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import netmd_crypto_check as crypto

SEC_HDR = b(0x18, 0x00, 0x08, 0x00, 0x46, 0xF0, 0x03, 0x01, 0x03)

# What the injected test RNG hands out, first call then second: the same two
# constants are spelled out in test_netmd_secure.c.
HOST_NONCE = bytes.fromhex("0011223344556677")
DEV_NONCE = bytes.fromhex("8899aabbccddeeff")
RAW_KEY = bytes.fromhex("0123456789abcdef")
SESSION_KEY = crypto.retail_mac(crypto.EKB_ROOT_KEY, HOST_NONCE + DEV_NONCE)
DATA_KEY = crypto.des_block(crypto.KEK, RAW_KEY, decrypt=True)
ZERO8 = bytes(8)

EKB_CHAIN = bytes.fromhex("2545064deaca14f996bdc8a406c22b81"
                          "fb60bddd0dbcab848a005e03194d3eda")
EKB_SIGNATURE = bytes.fromhex("8f2bc352e86c5ed306dcae18d2f38c7f89b5e18555a105ea")
LEAF_ID = bytes.fromhex("0100002 1cf060000".replace(" ", ""))


def poll_idle(trace):
    """One bare poll with nothing waiting: the closing poll of s4.11."""
    trace.lines.append("> c1 01 00 00 00 00 04 00")
    trace.lines.append("< " + POLL_IDLE)


def secure(trace, cmd, data=b"", reply=None, placeholder=0x00, status=0x09):
    request = b(0x00) + SEC_HDR + bytes((cmd, 0xFF)) + data
    body = data if reply is None else reply
    trace.command(request, bytes((status,)) + SEC_HDR + bytes((cmd, placeholder)) + body)


def bulk(trace, payload):
    """A bulk OUT on EP 0x02, as netmd_replay.c serialises it: endpoint, bytes."""
    trace.lines.append("> 02 " + hexs(payload))


def operating_status(trace, raw=0xC5FF):
    trace.comment("s3.14 operating status, read before every setupDownload")
    descriptor(trace, "status", 0x01)
    request = b(0x00, 0x18, 0x09, 0x80, 0x01, 0x03, 0x30, 0x88, 0x02, 0x00, 0x30, 0x88, 0x05,
                0x00, 0x30, 0x88, 0x06, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00)
    reply = (b(0x09, 0x18, 0x09, 0x80, 0x01, 0x03, 0x30, 0x88, 0x02, 0x00, 0x30, 0x88, 0x05,
               0x00, 0x30, 0x88, 0x06, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
               0x88, 0x06) + u16(2) + u16(raw))
    trace.command(request, reply)
    close(trace, "status")


def acquire(trace):
    frame = b(0x00, 0xFF, 0x01, 0x0C) + bytes([0xFF] * 12)
    trace.command(frame, b(0x09) + frame[1:])


def release(trace):
    frame = b(0x00, 0xFF, 0x01, 0x00) + bytes([0xFF] * 12)
    trace.command(frame, b(0x09) + frame[1:])


def session_begin(trace):
    trace.comment("s4.14 preventive teardown: a session left open rejects the next one")
    secure(trace, 0x21, b(0, 0, 0))
    secure(trace, 0x81)
    acquire(trace)
    trace.comment("s4.13 new tracks must not come out checked-out")
    secure(trace, 0x2B, b(0x00, 0x01))
    trace.comment("s4.3 enter, leaf id, EKB, nonces")
    secure(trace, 0x80)
    secure(trace, 0x11, b"", reply=LEAF_ID)
    ekb = (u16(0x48) + b(0x00, 0x00) + u16(0x48) + b(0x00, 0x00, 0x00, 0x02) +
           b(0x00, 0x00, 0x00, 0x09) + b(0x26, 0x42, 0x26, 0x42) + b(0x00, 0x00, 0x00, 0x00) +
           EKB_CHAIN + EKB_SIGNATURE)
    secure(trace, 0x12, ekb, reply=u16(0x48) + b(0x00, 0x00) + u16(0x48) + b(0x00, 0x00),
           placeholder=0x01)
    secure(trace, 0x20, b(0, 0, 0) + HOST_NONCE, reply=b(0, 0, 0) + DEV_NONCE)


def session_end(trace):
    trace.comment("s4.14 teardown, unconditional, then the status that proves it came back")
    secure(trace, 0x21, b(0, 0, 0))
    secure(trace, 0x81)
    release(trace)
    operating_status(trace)


def setup_download(trace):
    plain = b(0x01, 0x01, 0x01, 0x01) + crypto.CONTENT_ID + crypto.KEK
    cipher = crypto.des_cbc_encrypt(SESSION_KEY, ZERO8, plain)
    trace.comment("s4.7 setupDownload: contentID + KEK, DES-CBC under the session key")
    secure(trace, 0x22, b(0x00, 0x00) + cipher, reply=b(0x00, 0x00, 0x00))


def send_track(trace, audio, track_number):
    frames = len(audio) // 2048
    total = len(audio) + 24
    trace.comment("s4.11 sendTrack: %d frames, totalBytes %d" % (frames, total))
    request = (b(0x00) + SEC_HDR + b(0x28, 0xFF) +
               b(0x00, 0x01, 0x00, 0x10, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x06) +
               frames.to_bytes(4, "big") + total.to_bytes(4, "big"))
    interim_body = b(0x00, 0x01, 0x00, 0x10, 0x01, 0xFF, 0xFF, 0x00)
    trace.command(request, b(0x0F) + SEC_HDR + b(0x28, 0x00) + interim_body)
    header = b(0, 0, 0, 0) + len(audio).to_bytes(4, "big") + DATA_KEY + ZERO8
    bulk(trace, header)
    bulk(trace, crypto.des_cbc_encrypt(RAW_KEY, ZERO8, audio))
    blob = crypto.des_cbc_encrypt(SESSION_KEY, ZERO8,
                                  bytes(range(8)) + bytes(4) + crypto.CONTENT_ID)
    body = b(0x00, 0x01, 0x00, 0x10, 0x01) + u16(track_number) + b(0x00) + bytes(10) + blob
    reply = b(0x09) + SEC_HDR + b(0x28, 0x00) + body
    trace.lines.append("> c1 01 00 00 00 00 04 00")
    trace.lines.append("< 01 81 %02x 00" % len(reply))
    trace.lines.append("> c1 81 00 00 00 00 %02x 00" % len(reply))
    trace.lines.append("< " + hexs(reply))
    trace.comment("s4.11 the extra poll that keeps the device in step")
    poll_idle(trace)


def write_track_title(trace, track_number, title):
    payload = title.encode("cp932")
    trace.comment("s3.9 read the old title, then write the new one")
    open_read(trace, "utoc1")
    track_title(trace, track_number, False, "")
    close(trace, "utoc1")
    descriptor(trace, "utoc1", 0x03)
    request = (b(0x00, 0x18, 0x07, 0x02, 0x20, 0x18, 0x02) + u16(track_number) +
               b(0x30, 0x00, 0x0A, 0x00, 0x50, 0x00) + u16(len(payload)) + b(0x00, 0x00) +
               u16(0) + payload)
    trace.command(request, b(0x09) + request[1:len(request) - len(payload)])
    close(trace, "utoc1")


def write_disc_title(trace, title):
    payload = title.encode("cp932")
    trace.comment("s6.5 the disc title, once, at the end - it carries the groups")
    disc_title(trace, False, "")
    descriptor(trace, "disc_title", 0x03)
    request = (b(0x00, 0x18, 0x07, 0x02, 0x20, 0x18, 0x01, 0x00, 0x00) +
               b(0x30, 0x00, 0x0A, 0x00, 0x50, 0x00) + u16(len(payload)) + b(0x00, 0x00) +
               u16(0) + payload)
    trace.command(request, b(0x09) + request[1:len(request) - len(payload)])
    close(trace, "disc_title")
    trace.comment("s3.9 the read round trip that flushes the TOC cache")
    open_read(trace, "disc_title")
    close(trace, "disc_title")


def capacity_check(trace, disc):
    trace.comment("s7.2 the free time, re-read from the device before writing")
    open_read(trace, "root")
    disc_capacity(trace, disc["recorded"], disc["total"], disc["available"])
    close(trace, "root")


TEST_AUDIO = bytes((i * 7 + 3) & 0xFF for i in range(2 * 2048))


def upload_trace(title, tracks, cancel_after=None, disc_title_text=None):
    trace = Trace(title)
    capacity_check(trace, BLANK)
    session_begin(trace)
    auth = crypto.des_block(SESSION_KEY, ZERO8)
    for index, name in enumerate(tracks):
        operating_status(trace)
        setup_download(trace)
        send_track(trace, TEST_AUDIO, index)
        write_track_title(trace, index, name)
        trace.comment("s4.12 commit: DES-ECB(0^8, sessionKey)")
        secure(trace, 0x48, b(0x00, 0x10, 0x01) + u16(index) + auth,
               reply=b(0x00, 0x10, 0x01, 0x00, 0x00))
        if cancel_after is not None and index == cancel_after:
            break
    if disc_title_text is not None:
        write_disc_title(trace, disc_title_text)
    session_end(trace)
    return trace


# --- a whole netmd_read_disc -------------------------------------------------

def read_disc(trace, disc):
    disc_present(trace, disc["present"])
    if not disc["present"]:
        return
    # The batch of s3.4: root, then contents, closed once at the end.
    open_read(trace, "root")
    disc_flags(trace, disc["flags"])
    disc_capacity(trace, disc["recorded"], disc["total"], disc["available"])
    close(trace, "root")
    open_read(trace, "contents")
    track_count(trace, len(disc["tracks"]))
    close(trace, "contents")

    disc_title(trace, False, disc.get("title", ""))
    disc_title(trace, True, disc.get("title_full", ""))

    tracks = disc["tracks"]
    if not tracks:
        return
    open_read(trace, "contents")
    for index, track in enumerate(tracks):
        track_info(trace, index, track)
    close(trace, "contents")
    open_read(trace, "utoc1")
    for index, track in enumerate(tracks):
        track_title(trace, index, False, track.get("title", ""))
    close(trace, "utoc1")
    open_read(trace, "utoc4")
    for index, track in enumerate(tracks):
        track_title(trace, index, True, track.get("title_full", ""))
    close(trace, "utoc4")


SP, LP2, LP4 = 0x90, 0x92, 0x93


def minutes(m, s):
    return (0, m, s, 0)


BLANK = {
    "present": True,
    "flags": 0x10,
    "recorded": (0, 0, 0, 0),
    "total": (1, 20, 0, 0),
    "available": (1, 20, 0, 0),
    "title": "",
    "tracks": [],
}

FULL = {
    "present": True,
    "flags": 0x10,
    "recorded": (0, 45, 12, 0),
    "total": (1, 20, 0, 0),
    "available": (0, 34, 48, 0),
    # s3.10: the disc title carries the groups, and nothing else does.
    "title": "0;Nuit blanche//1-4;Face A//5-9;Face B//",
    "tracks": [
        {"title": "Ouverture", "time": minutes(3, 47), "encoding": SP},
        {"title": "Le train de 7h", "time": minutes(4, 12), "encoding": SP},
        {"title": "Kyoto", "title_full": "ＫＹＯＴＯ", "time": minutes(5, 3),
         "encoding": SP},
        {"title": "Interlude", "time": minutes(1, 30), "encoding": SP, "mono": True},
        {"title": "Face B", "time": minutes(6, 1), "encoding": LP2},
        {"title": "Nocturne", "title_full": "東京", "time": minutes(4, 44),
         "encoding": LP2},
        {"title": "ｶﾀｶﾅ", "time": minutes(3, 20), "encoding": LP2},
        {"title": "Sans titre long qui tient quand meme", "time": minutes(7, 15),
         "encoding": LP4},
        {"title": "Derniere", "time": minutes(5, 40), "encoding": LP4, "protect": True},
        {"title": "", "time": minutes(3, 40), "encoding": SP},
    ],
}

PROTECTED = {
    "present": True,
    "flags": 0x10 | 0x40,  # recordable, but the tab is closed
    "recorded": (0, 12, 30, 0),
    "total": (1, 14, 0, 0),
    "available": (1, 1, 30, 0),
    "title": "Demo",
    "tracks": [
        {"title": "Piste unique", "time": minutes(12, 30), "encoding": SP, "protect": True},
    ],
}

NO_DISC = {"present": False, "flags": 0, "recorded": (0, 0, 0, 0), "total": (0, 0, 0, 0),
           "available": (0, 0, 0, 0), "tracks": []}

SCENARIOS = [
    ("mzn505_blank.trace", "MZ-N505, blank 80 min disc", BLANK),
    ("mzn505_full.trace", "MZ-N505, 80 min disc, 10 titled tracks, 2 groups", FULL),
    ("mzn505_protected.trace", "MZ-N505, write protected disc", PROTECTED),
    ("mzn505_nodisc.trace", "MZ-N505, empty bay", NO_DISC),
]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(
            os.path.join(here, "..", "tests", "netmd"))
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)
    uploads = [
        ("mzn505_upload_sp.trace",
         upload_trace("MZ-N505, one SP track downloaded and titled", ["Test"],
                      disc_title_text="Demo")),
        ("mzn505_upload_cancel.trace",
         upload_trace("MZ-N505, two tracks, cancelled after the first", ["Test", "Second"],
                      cancel_after=0)),
        ("mzn505_upload_resume.trace",
         upload_trace("MZ-N505, resume: the first track is already committed", ["Second"],
                      disc_title_text="Demo")),
    ]
    for name, trace in uploads:
        path = os.path.join(out_dir, name)
        with open(path, "w", encoding="ascii", newline="\n") as f:
            f.write(trace.text())
        print("%s: %d lines" % (path, len(trace.lines)))
    for name, title, disc in SCENARIOS:
        trace = Trace(title)
        read_disc(trace, disc)
        path = os.path.join(out_dir, name)
        with open(path, "w", encoding="ascii", newline="\n") as f:
            f.write(trace.text())
        print("%s: %d lines" % (path, len(trace.lines)))


if __name__ == "__main__":
    main()

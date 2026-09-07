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



# --- editing (T-022, s3.5, s3.9, s3.12) --------------------------------------
# The same rule as everything above: every frame below is assembled from the hex
# of research/01 and from what the implementation is specified to send, never
# captured from another program.

def open_write(trace, name):
    descriptor(trace, name, 0x03)


def acquire(trace):
    trace.comment("s3.5 acquire: the player's own buttons are silenced")
    frame = b(0x00, 0xFF, 0x01, 0x0C) + bytes([0xFF] * 12)
    trace.command(frame, b(0x09) + frame[1:])


def release(trace):
    trace.comment("s3.5 release: on every path, or the machine stays locked")
    frame = b(0x00, 0xFF, 0x01, 0x00) + bytes([0xFF] * 12)
    trace.command(frame, b(0x09) + frame[1:])


def read_track_title(trace, index, wide, title):
    """What netmd_get_track_title_ex emits: open, ask, close (s3.4)."""
    name = "utoc4" if wide else "utoc1"
    open_read(trace, name)
    track_title(trace, index, wide, title)
    close(trace, name)


def write_title(trace, name, wchar, index, is_disc, new_title, old_len):
    """s3.9: close, openWrite, write, close, openRead, close."""
    payload = new_title.encode("cp932") if new_title else b""
    if is_disc:
        head = (b(0x00, 0x18, 0x07, 0x02, 0x20, 0x18, 0x01, 0x00, wchar) +
                b(0x30, 0x00, 0x0A, 0x00, 0x50, 0x00) + u16(len(payload)) + b(0x00, 0x00) +
                u16(old_len))
    else:
        head = (b(0x00, 0x18, 0x07, 0x02, 0x20, 0x18, wchar) + u16(index) +
                b(0x30, 0x00, 0x0A, 0x00, 0x50, 0x00) + u16(len(payload)) + b(0x00, 0x00) +
                u16(old_len))
    close(trace, name)
    open_write(trace, name)
    # The reply echoes the frame without the title bytes (s3.9's example).
    trace.command(head + payload, b(0x09) + head[1:])
    close(trace, name)
    open_read(trace, name)
    close(trace, name)


def set_track_title(trace, index, old, new, old_full=""):
    """One netmd_set_track_title, read back included (pitfalls 9 and 10)."""
    read_track_title(trace, index, False, old)
    if old == new:
        return
    write_title(trace, "utoc1", 0x02, index, False, new, len(old.encode("cp932")))
    # The full-width space is cleared, not rewritten (netmd_edit.c).
    read_track_title(trace, index, True, old_full)
    if old_full:
        write_title(trace, "utoc4", 0x03, index, False, "", len(old_full.encode("cp932")))


def set_disc_title(trace, old, new, old_full=""):
    disc_title(trace, False, old)
    if old == new:
        return
    write_title(trace, "disc_title", 0x00, 0, True, new, len(old.encode("cp932")))
    disc_title(trace, True, old_full)
    if old_full:
        write_title(trace, "disc_title", 0x01, 0, True, "", len(old_full.encode("cp932")))


def erase_track(trace, index):
    trace.comment("s3.12 eraseTrack, by decreasing index (pitfall 17)")
    frame = b(0x00, 0x18, 0x40, 0xFF, 0x01, 0x00, 0x20, 0x10, 0x01) + u16(index)
    trace.command(frame, b(0x09) + frame[1:])


def erase_disc(trace):
    trace.comment("s3.12 eraseDisc")
    frame = b(0x00, 0x18, 0x40, 0xFF, 0x00, 0x00)
    trace.command(frame, b(0x09) + frame[1:])


def move_track(trace, source, dest):
    trace.comment("s3.12 moveTrack")
    frame = (b(0x00, 0x18, 0x43, 0xFF, 0x00, 0x00, 0x20, 0x10, 0x01) + u16(source) +
             b(0x20, 0x10, 0x01) + u16(dest))
    trace.command(frame, b(0x09) + frame[1:])


# The disc every edit transcript starts from: three SP tracks, one group over
# the first two, so that a move and an erasure both renumber the ranges.
EDIT_DISC = {
    "present": True,
    "flags": 0x10,
    "recorded": (0, 12, 0, 0),
    "total": (1, 20, 0, 0),
    "available": (1, 8, 0, 0),
    "title": "0;Demo//1-2;Face A//",
    "tracks": [
        {"title": "Un", "time": minutes(4, 0), "encoding": SP},
        {"title": "Deux", "time": minutes(4, 0), "encoding": SP},
        {"title": "Trois", "time": minutes(4, 0), "encoding": SP},
    ],
}


def edit_rename_disc(trace):
    acquire(trace)
    set_disc_title(trace, "0;Demo//1-2;Face A//", "0;Concert//1-2;Face A//")
    release(trace)


def edit_rename_track(trace):
    acquire(trace)
    set_track_title(trace, 1, "Deux", "Two")
    release(trace)


def edit_move(trace):
    acquire(trace)
    move_track(trace, 0, 2)
    # s3.10: the ranges are ours to renumber, in one single disc title write.
    set_disc_title(trace, "0;Demo//1-2;Face A//", "0;Demo//1;Face A//")
    release(trace)


def edit_erase(trace):
    acquire(trace)
    erase_track(trace, 1)
    set_disc_title(trace, "0;Demo//1-2;Face A//", "0;Demo//1;Face A//")
    release(trace)


def edit_group(trace):
    # Track 3 alone becomes a group of its own; nothing else moves.
    acquire(trace)
    set_disc_title(trace, "0;Demo//1-2;Face A//", "0;Demo//1-2;Face A//3;Face B//")
    release(trace)


def edit_erase_disc(trace):
    acquire(trace)
    erase_disc(trace)
    release(trace)


def edit_same_title(trace):
    # Pitfall 10: the title is read back, found identical, and not written. The
    # transcript ends there - if the implementation wrote, the replay diverges.
    set_track_title(trace, 1, "Deux", "Deux")


def edit_oldlen_fullwidth(trace):
    # s3.9: oldLen counts Shift-JIS *bytes*. This track's current title is two
    # full-width characters, so oldLen is 4 and not 2.
    acquire(trace)
    set_track_title(trace, 0, "ＡＢ", "AB")
    release(trace)


def edit_session(trace):
    # What the device thread runs end to end: edit, then read the disc back.
    acquire(trace)
    set_track_title(trace, 1, "Deux", "Two")
    release(trace)
    read_disc(trace, EDIT_DISC_AFTER)


EDIT_DISC_AFTER = {
    "present": True,
    "flags": 0x10,
    "recorded": (0, 12, 0, 0),
    "total": (1, 20, 0, 0),
    "available": (1, 8, 0, 0),
    "title": "0;Demo//1-2;Face A//",
    "tracks": [
        {"title": "Un", "time": minutes(4, 0), "encoding": SP},
        {"title": "Two", "time": minutes(4, 0), "encoding": SP},
        {"title": "Trois", "time": minutes(4, 0), "encoding": SP},
    ],
}

EDIT_SCENARIOS = [
    ("mzn505_edit_rename_disc.trace", "MZ-N505, rename the disc (s3.9)", edit_rename_disc),
    ("mzn505_edit_rename_track.trace", "MZ-N505, rename track 2 (s3.9)", edit_rename_track),
    ("mzn505_edit_move.trace", "MZ-N505, move track 1 to position 3 (s3.12)", edit_move),
    ("mzn505_edit_erase.trace", "MZ-N505, erase track 2 (s3.12)", edit_erase),
    ("mzn505_edit_group.trace", "MZ-N505, group the last track (s3.10)", edit_group),
    ("mzn505_edit_erase_disc.trace", "MZ-N505, erase the whole disc (s3.12)", edit_erase_disc),
    ("mzn505_edit_same_title.trace", "MZ-N505, an identical title is not written (pitfall 10)",
     edit_same_title),
    ("mzn505_edit_oldlen.trace", "MZ-N505, oldLen counts Shift-JIS bytes (s3.9)",
     edit_oldlen_fullwidth),
    ("mzn505_edit_session.trace", "MZ-N505, one edit and the disc read back (T-022)",
     edit_session),
]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(
            os.path.join(here, "..", "tests", "netmd"))
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)
    scenarios = [(name, title, disc, None) for name, title, disc in SCENARIOS]
    # The edit transcripts start from a disc that has already been read: the
    # session the device thread runs is read, then edit (T-022).
    scenarios += [(name, title, EDIT_DISC, build) for name, title, build in EDIT_SCENARIOS]
    for name, title, disc, build in scenarios:
        trace = Trace(title)
        read_disc(trace, disc)
        if build:
            build(trace)
        path = os.path.join(out_dir, name)
        with open(path, "w", encoding="ascii", newline="\n") as f:
            f.write(trace.text())
        print("%s: %d lines" % (path, len(trace.lines)))


if __name__ == "__main__":
    main()

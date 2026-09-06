#!/usr/bin/env python3
# gen_tag_vectors.py - golden vectors for T-011 (tests/test_tags.c).
#
# Every file it writes is hand assembled from the specifications, minimal
# (< 20 KB) and deterministic, so a diff on tests/data/ is a real change of
# behaviour and not an encoder update. No third party library is involved:
# writing the bytes ourselves is also the only way to produce the broken
# variants the parsers must survive.
#
#   python tools/gen_tag_vectors.py            -> tests/data/
#   python tools/gen_tag_vectors.py --demo DIR -> a tagged folder tree for the
#                                                 screenshot of the demo
import argparse
import os
import struct
import sys

# --- little helpers --------------------------------------------------------

def be32(v):
    return struct.pack(">I", v)

def be24(v):
    return struct.pack(">I", v)[1:]

def le32(v):
    return struct.pack("<I", v)

def syncsafe(v):
    return bytes([(v >> 21) & 0x7F, (v >> 14) & 0x7F, (v >> 7) & 0x7F, v & 0x7F])

# --- MPEG ------------------------------------------------------------------
# MPEG 1 layer III, 44100 Hz, 128 kbit/s, joint stereo: 417 byte frames.

MPEG_FRAME_BYTES = 417
MPEG_SAMPLES = 1152
MPEG_RATE = 44100

def mpeg_header():
    # 11111111 111 11 01 0 : sync, MPEG1, layer III, no CRC
    # 1001 00 0 0           : 128 kbit/s, 44100 Hz, no padding
    # 01 00 0 0 00          : joint stereo
    return bytes([0xFF, 0xFB, 0x90, 0x44])

def xing_frame(frames):
    side_info = 32  # MPEG 1, not mono
    body = b"Xing" + be32(0x0001) + be32(frames)
    frame = mpeg_header() + b"\x00" * side_info + body
    return frame + b"\x00" * (MPEG_FRAME_BYTES - len(frame))

def mpeg_frames(count):
    frame = mpeg_header() + b"\x00" * (MPEG_FRAME_BYTES - 4)
    return frame * count

def vbri_frame(frames):
    body = b"VBRI" + struct.pack(">HHH", 1, 0, 100) + be32(frames * MPEG_FRAME_BYTES) + be32(frames)
    frame = mpeg_header() + b"\x00" * 32 + body
    return frame + b"\x00" * (MPEG_FRAME_BYTES - len(frame))

# --- ID3 -------------------------------------------------------------------

def id3v2_frame(fid, body, version=3):
    if version == 2:
        return fid.encode() + be24(len(body)) + body
    size = syncsafe(len(body)) if version == 4 else be32(len(body))
    return fid.encode() + size + b"\x00\x00" + body

def text_frame(fid, text, encoding=0, version=3):
    if encoding == 0:
        payload = text.encode("latin-1")
    elif encoding == 1:
        payload = b"\xff\xfe" + text.encode("utf-16-le")
    elif encoding == 2:
        payload = text.encode("utf-16-be")
    else:
        payload = text.encode("utf-8")
    return id3v2_frame(fid, bytes([encoding]) + payload, version)

def txxx_frame(name, value, version=3):
    body = b"\x00" + name.encode("latin-1") + b"\x00" + value.encode("latin-1")
    return id3v2_frame("TXXX", body, version)

def apic_frame(data, version=3):
    body = b"\x00" + b"image/jpeg\x00" + b"\x03" + b"cover\x00" + data
    return id3v2_frame("APIC", body, version)

def unsynchronise(data):
    out = bytearray()
    for i, byte in enumerate(data):
        out.append(byte)
        if byte == 0xFF and (i + 1 == len(data) or data[i + 1] == 0x00 or data[i + 1] >= 0xE0):
            out.append(0x00)
    return bytes(out)

def id3v2_tag(frames, version=3, unsync=False, padding=16):
    body = b"".join(frames) + b"\x00" * padding
    flags = 0x80 if unsync else 0x00
    if unsync:
        body = unsynchronise(body)
    return b"ID3" + bytes([version, 0]) + bytes([flags]) + syncsafe(len(body)) + body

def id3v1_tag(title, artist, album, year, comment, track, genre):
    def pad(s, n):
        return s.encode("latin-1")[:n].ljust(n, b"\x00")
    body = b"TAG" + pad(title, 30) + pad(artist, 30) + pad(album, 30) + pad(year, 4)
    body += pad(comment, 28) + b"\x00" + bytes([track]) + bytes([genre])
    assert len(body) == 128
    return body

# --- APEv2 -----------------------------------------------------------------

def ape_tag(items, has_header=True):
    body = b""
    for key, value in items:
        payload = value.encode("utf-8")
        body += le32(len(payload)) + le32(0) + key.encode("ascii") + b"\x00" + payload
    size = len(body) + 32
    def block(flags):
        return (b"APETAGEX" + le32(2000) + le32(size) + le32(len(items)) + le32(flags)
                + b"\x00" * 8)
    header = block(0xA0000000) if has_header else b""
    return header + body + block(0x80000000 if has_header else 0x00000000)

# --- FLAC ------------------------------------------------------------------

def flac_block(block_type, body, last=False):
    return bytes([block_type | (0x80 if last else 0)]) + be24(len(body)) + body

def flac_streaminfo(sample_rate, channels, samples):
    body = struct.pack(">HH", 4096, 4096) + be24(64) + be24(8192)
    packed = (sample_rate << 44) | ((channels - 1) << 41) | (15 << 36) | samples
    body += struct.pack(">Q", packed) + b"\x00" * 16
    assert len(body) == 34
    return body

def vorbis_comment_block(vendor, comments):
    body = le32(len(vendor)) + vendor.encode("utf-8") + le32(len(comments))
    for comment in comments:
        payload = comment.encode("utf-8")
        body += le32(len(payload)) + payload
    return body

def flac_picture(data):
    return (be32(3) + be32(len(b"image/jpeg")) + b"image/jpeg" + be32(len(b"front"))
            + b"front" + be32(64) + be32(64) + be32(24) + be32(0) + be32(len(data)) + data)

def flac_file(comments, samples=44100 * 3, picture=None):
    out = b"fLaC"
    out += flac_block(0, flac_streaminfo(44100, 2, samples))
    blocks = [(4, vorbis_comment_block("minidisk", comments))]
    if picture is not None:
        blocks.append((6, flac_picture(picture)))
    for i, (block_type, body) in enumerate(blocks):
        out += flac_block(block_type, body, last=(i == len(blocks) - 1))
    return out + b"\x00" * 64  # a token frame

# --- Ogg -------------------------------------------------------------------

def ogg_page(serial, sequence, granule, payload, header_type=0):
    segments = []
    remaining = len(payload)
    while remaining >= 255:
        segments.append(255)
        remaining -= 255
    segments.append(remaining)
    page = (b"OggS" + bytes([0, header_type]) + struct.pack("<q", granule) + le32(serial)
            + le32(sequence) + le32(0) + bytes([len(segments)]) + bytes(segments) + payload)
    return page

def ogg_vorbis_file(comments, granule=44100 * 4):
    serial = 0x4D494E49
    ident = b"\x01vorbis" + le32(0) + bytes([2]) + le32(44100) + le32(0) * 3 + bytes([0xB8, 0x01])
    comment = b"\x03vorbis" + vorbis_comment_block("minidisk", comments) + b"\x01"
    out = ogg_page(serial, 0, 0, ident, header_type=2)
    out += ogg_page(serial, 1, 0, comment)
    out += ogg_page(serial, 2, granule, b"\x00" * 64, header_type=4)
    return out

def ogg_opus_file(comments, granule=48000 * 5 + 312):
    serial = 0x4F505553
    ident = b"OpusHead" + bytes([1, 2]) + struct.pack("<H", 312) + le32(48000) + b"\x00\x00\x00"
    comment = b"OpusTags" + vorbis_comment_block("minidisk", comments)
    out = ogg_page(serial, 0, 0, ident, header_type=2)
    out += ogg_page(serial, 1, 0, comment)
    out += ogg_page(serial, 2, granule, b"\x00" * 64, header_type=4)
    return out

# --- MP4 -------------------------------------------------------------------

def atom(name, body):
    return be32(len(body) + 8) + name.encode("latin-1") + body

def ilst_text(name, text):
    data = atom("data", be32(1) + be32(0) + text.encode("utf-8"))
    return atom(name, data)

def ilst_number(name, number, total=0):
    body = struct.pack(">HHHH", 0, number, total, 0)
    return atom(name, atom("data", be32(0) + be32(0) + body))

def ilst_freeform(name, value):
    body = atom("mean", be32(0) + b"com.apple.iTunes")
    body += atom("name", be32(0) + name.encode("ascii"))
    body += atom("data", be32(1) + be32(0) + value.encode("utf-8"))
    return atom("----", body)

def mp4_file(title, artist, album, album_artist, genre, track, disc, year,
             duration_ms=180000, cover=None, moov_last=False):
    timescale = 1000
    mvhd = atom("mvhd", bytes([0, 0, 0, 0]) + be32(0) + be32(0) + be32(timescale)
                + be32(duration_ms) + b"\x00" * 80)
    stsd = atom("stsd", be32(0) + be32(1)
                + be32(36) + b"mp4a" + b"\x00" * 6 + struct.pack(">H", 1)
                + b"\x00" * 8 + struct.pack(">H", 2) + struct.pack(">H", 16)
                + b"\x00" * 4 + be32(44100 << 16))
    trak = atom("trak", atom("mdia", atom("minf", atom("stbl", stsd))))
    items = (ilst_text("\xa9nam", title) + ilst_text("\xa9ART", artist)
             + ilst_text("\xa9alb", album) + ilst_text("aART", album_artist)
             + ilst_text("\xa9gen", genre) + ilst_text("\xa9day", year)
             + ilst_number("trkn", track, 12) + ilst_number("disk", disc, 2)
             + ilst_freeform("replaygain_track_gain", "-7.25 dB"))
    if cover is not None:
        items += atom("covr", atom("data", be32(13) + be32(0) + cover))
    meta = atom("meta", be32(0) + atom("hdlr", b"\x00" * 24) + atom("ilst", items))
    moov = atom("moov", mvhd + trak + atom("udta", meta))
    ftyp = atom("ftyp", b"M4A " + be32(512) + b"M4A isom")
    mdat = atom("mdat", b"\x00" * 512)
    return ftyp + (mdat + moov if moov_last else moov + mdat)

# --- RIFF / AIFF -----------------------------------------------------------

def riff_chunk(name, body):
    pad = b"\x00" if len(body) & 1 else b""
    return name.encode("ascii") + le32(len(body)) + body + pad

def wav_file(title, artist, album, genre, track, year, seconds=2):
    rate = 44100
    channels = 2
    bits = 16
    byte_rate = rate * channels * bits // 8
    fmt = struct.pack("<HHIIHH", 1, channels, rate, byte_rate, channels * bits // 8, bits)
    info = b"INFO"
    for name, value in (("INAM", title), ("IART", artist), ("IPRD", album),
                        ("IGNR", genre), ("ITRK", track), ("ICRD", year)):
        info += riff_chunk(name, value.encode("latin-1") + b"\x00")
    data_size = byte_rate * seconds
    # The data chunk declares its real length but we only write a token of it:
    # a parser must use the declared size for the duration, not the file size.
    body = b"WAVE" + riff_chunk("fmt ", fmt) + riff_chunk("LIST", info)
    body += b"data" + le32(data_size) + b"\x00" * 256
    return b"RIFF" + le32(len(body)) + body

def aiff_rate(rate):
    exponent = 16383 + 63
    mantissa = rate
    while mantissa and not (mantissa & (1 << 63)):
        mantissa <<= 1
        exponent -= 1
    return struct.pack(">H", exponent) + struct.pack(">Q", mantissa)

def aiff_chunk(name, body):
    pad = b"\x00" if len(body) & 1 else b""
    return name.encode("ascii") + be32(len(body)) + body + pad

def aiff_file(title, author, frames=44100 * 3):
    comm = struct.pack(">H", 2) + be32(frames) + struct.pack(">H", 16) + aiff_rate(44100)
    body = b"AIFF" + aiff_chunk("COMM", comm)
    body += aiff_chunk("NAME", title.encode("latin-1"))
    body += aiff_chunk("AUTH", author.encode("latin-1"))
    body += b"SSND" + be32(512) + b"\x00" * 512
    return b"FORM" + be32(len(body)) + body

# --- the vectors -----------------------------------------------------------

COVER = bytes(range(256)) * 2  # 512 deterministic bytes standing in for a JPEG

def build_vectors():
    files = {}

    # ID3v2.3, UTF-16 with BOM, a cover, ReplayGain, and a Xing header.
    frames = [
        text_frame("TIT2", "Sur le fil", 1),
        text_frame("TPE1", "Yann Tiersen", 1),
        text_frame("TALB", "Le Fabuleux Destin", 1),
        text_frame("TPE2", "Yann Tiersen", 1),
        text_frame("TCON", "Soundtrack", 0),
        text_frame("TRCK", "7/20", 0),
        text_frame("TPOS", "1/2", 0),
        text_frame("TYER", "2001", 0),
        txxx_frame("replaygain_track_gain", "-7.25 dB"),
        apic_frame(COVER),
    ]
    files["id3v23_utf16.mp3"] = (id3v2_tag(frames, version=3)
                                 + xing_frame(3000) + mpeg_frames(4))

    # ID3v2.4, UTF-8, syncsafe frame sizes, tag level unsynchronisation.
    frames = [
        text_frame("TIT2", "Café Müller", 3, version=4),
        text_frame("TPE1", "Ensemble Éole", 3, version=4),
        text_frame("TALB", "Nuit blanche", 3, version=4),
        text_frame("TCON", "(17)", 0, version=4),
        text_frame("TRCK", "3", 0, version=4),
        text_frame("TDRC", "2019-04-01", 3, version=4),
    ]
    files["id3v24_unsync.mp3"] = (id3v2_tag(frames, version=4, unsync=True)
                                  + xing_frame(1500) + mpeg_frames(4))

    # ID3v2.2, three character frame ids.
    frames = [
        text_frame("TT2", "Petite chanson", 0, version=2),
        text_frame("TP1", "Les Anciens", 0, version=2),
        text_frame("TAL", "Demos", 0, version=2),
        text_frame("TRK", "2/9", 0, version=2),
    ]
    files["id3v22.mp3"] = id3v2_tag(frames, version=2) + xing_frame(900) + mpeg_frames(4)

    # No ID3v2 at all: v1.1 in the tail, and a constant bitrate to measure.
    files["id3v1_only.mp3"] = (mpeg_frames(20)
                               + id3v1_tag("Sonate", "Trio Nocturne", "Recital", "1998",
                                           "note", 5, 32))

    # VBRI instead of Xing.
    files["mpeg_vbri.mp3"] = vbri_frame(2000) + mpeg_frames(4)

    # An MP3 carrying an APEv2 tag instead of an ID3 one.
    files["ape_tail.mp3"] = mpeg_frames(20) + ape_tag([
        ("Title", "Marche"), ("Artist", "Fanfare du Nord"), ("Album", "Places"),
        ("Album Artist", "Fanfare du Nord"), ("Genre", "Brass"), ("Track", "4/11"),
        ("Disc", "1/1"), ("Year", "2008"), ("replaygain_track_gain", "+2.50 dB"),
    ])

    files["flac.flac"] = flac_file([
        "TITLE=Nuages", "ARTIST=Django", "ALBUM=Swing 39", "ALBUMARTIST=Quintette",
        "GENRE=Jazz", "TRACKNUMBER=5/12", "DISCNUMBER=1", "DATE=1940",
        "REPLAYGAIN_TRACK_GAIN=-3.10 dB",
    ], samples=44100 * 3 + 22050, picture=COVER)

    files["ogg_vorbis.ogg"] = ogg_vorbis_file([
        "TITLE=Vent d'ouest", "ARTIST=Marée", "ALBUM=Littoral", "GENRE=Ambient",
        "TRACKNUMBER=2", "DATE=2015",
    ])

    files["opus.opus"] = ogg_opus_file([
        "TITLE=Ligne claire", "ARTIST=Atelier 12", "ALBUM=Traits", "TRACKNUMBER=1",
        "DATE=2021",
    ])

    files["mp4.m4a"] = mp4_file("Bleu nuit", "Sylvie Aumont", "Horizons", "Sylvie Aumont",
                                "Electronic", 3, 1, "2012", duration_ms=185500, cover=COVER)
    files["mp4_moov_last.m4a"] = mp4_file("Fin de bande", "Studio 7", "Rushes", "Studio 7",
                                          "Rock", 9, 2, "1997", duration_ms=241000,
                                          moov_last=True)

    files["wav.wav"] = wav_file("Prise 3", "Quatuor Lyre", "Sessions", "Classical", "6", "2004")
    files["aiff.aif"] = aiff_file("Boucle courte", "Pierre Vidal")

    # --- the broken ones: every one of these must fail cleanly -------------
    good = files["id3v23_utf16.mp3"]
    files["broken_truncated.mp3"] = good[:40]
    # A tag that claims far more than the file holds.
    files["broken_huge_size.mp3"] = b"ID3\x03\x00\x00" + syncsafe(0x0FFFFFFF) + good[10:200]
    # A frame whose size runs past the end of the tag.
    frame = b"TIT2" + be32(0x00FFFFFF) + b"\x00\x00" + b"\x00short"
    files["broken_frame_size.mp3"] = id3v2_tag([frame], version=3) + mpeg_frames(2)
    files["broken_flac.flac"] = files["flac.flac"][:30]
    files["broken_ogg.ogg"] = files["ogg_vorbis.ogg"][:45]
    files["broken_mp4.m4a"] = files["mp4.m4a"][:60]
    files["broken_wav.wav"] = files["wav.wav"][:20]
    # An atom that says it is 8 bytes long and contains itself.
    files["broken_mp4_loop.m4a"] = (atom("ftyp", b"M4A ") + b"\x00\x00\x00\x08moov"
                                    + b"\x00\x00\x00\x00moov")
    files["broken_ape.mp3"] = mpeg_frames(4) + b"APETAGEX" + le32(2000) + le32(0xFFFFFFF0) \
                              + le32(4096) + le32(0) + b"\x00" * 8
    files["empty.mp3"] = b""
    files["tiny.mp3"] = b"ID"

    return files

# --- the demo tree ---------------------------------------------------------

DEMO = [
    ("Yann Tiersen", "Le Fabuleux Destin", "2001", "Soundtrack", [
        "La Valse d'Amelie", "Comptine d'un autre ete", "Sur le fil", "J'y suis jamais alle"]),
    ("Django Reinhardt", "Swing 39", "1940", "Jazz", [
        "Nuages", "Minor Swing", "Douce Ambiance", "Manoir de mes reves"]),
    ("Marée", "Littoral", "2015", "Ambient", [
        "Vent d'ouest", "Estran", "Basse mer", "Ressac"]),
    ("Fanfare du Nord", "Places", "2008", "Brass", [
        "Marche", "Grand-Place", "Beffroi", "Retour"]),
    ("Sylvie Aumont", "Horizons", "2012", "Electronic", [
        "Bleu nuit", "Ligne de fuite", "Aube", "Derive"]),
    # Two albums for one artist, and accented names, so the Artist -> Album
    # browser and the accent folded sort of T-012 have something to show.
    ("Django Reinhardt", "Nuages", "1946", "Jazz", [
        "Belleville", "Blues Clair", "Les Yeux Noirs", "Swing 42"]),
    ("Émilie Simon", "Végétal", "2006", "Pop", [
        "Fleur de saison", "Dame de lotus", "Never Fall in Love", "Ice Girl"]),
    ("The Bad Plus", "Never Stop", "2010", "Jazz", [
        "The Radio Tower Has a Beating Heart", "People Like You", "Beryl Loves to Dance",
        "Super America"]),
]

def safe(name):
    return "".join(c for c in name if c.isalnum() or c in " -_'").strip()

def write_demo(root):
    count = 0
    for artist, album, year, genre, titles in DEMO:
        folder = os.path.join(root, safe(artist), safe(album))
        os.makedirs(folder, exist_ok=True)
        for index, title in enumerate(titles, start=1):
            frames = [
                text_frame("TIT2", title, 1),
                text_frame("TPE1", artist, 1),
                text_frame("TALB", album, 1),
                text_frame("TPE2", artist, 1),
                text_frame("TCON", genre, 0),
                text_frame("TRCK", "%d/%d" % (index, len(titles)), 0),
                text_frame("TYER", year, 0),
            ]
            seconds = 150 + 37 * index + 11 * count
            frames_count = seconds * MPEG_RATE // MPEG_SAMPLES
            data = id3v2_tag(frames) + xing_frame(frames_count) + mpeg_frames(4)
            path = os.path.join(folder, "%02d - %s.mp3" % (index, safe(title)))
            with open(path, "wb") as handle:
                handle.write(data)
            count += 1
    return count

# --- main ------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=os.path.join("tests", "data"))
    parser.add_argument("--demo", default=None, help="write a tagged folder tree there")
    args = parser.parse_args()

    if args.demo:
        count = write_demo(args.demo)
        print("%d demo files in %s" % (count, args.demo))
        return 0

    os.makedirs(args.out, exist_ok=True)
    total = 0
    for name, data in sorted(build_vectors().items()):
        if len(data) >= 20 * 1024:
            print("ERROR: %s is %d bytes, the ticket caps a vector at 20 KB" % (name, len(data)))
            return 1
        with open(os.path.join(args.out, name), "wb") as handle:
            handle.write(data)
        total += len(data)
        print("%-24s %6d" % (name, len(data)))
    print("%d files, %d bytes" % (len(build_vectors()), total))
    return 0

if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# gen_demo_library.py - genere une petite bibliotheque de fichiers WAV pour les
# captures de livraison (T-032). Les fichiers sont minuscules : l'en-tete RIFF
# declare la taille du chunk `data`, et c'est de la que vient la duree lue par
# core/library/tags_riff.c, donc une piste de 4 minutes tient en 200 octets.
#
#   python tools/gen_demo_library.py --out %TEMP%\minidisk_demo --count 24
#
# Rien de tout cela n'entre dans l'executable : c'est un outil de capture.
import argparse
import os
import struct

RATE = 44100
CHANNELS = 2
BITS = 16
BYTE_RATE = RATE * CHANNELS * BITS // 8

# Des titres volontairement longs : le budget TOC (255 cellules de 7 caracteres)
# deborde vers la 20e piste, ce qui est exactement la capture demandee.
ALBUMS = [
    ("Radiohead", "Kid A", [
        "Everything In Its Right Place",
        "Kid A",
        "The National Anthem (Remastered 2011)",
        "How To Disappear Completely",
        "Treefingers",
        "Optimistic",
        "In Limbo",
        "Idioteque",
    ]),
    ("Boards of Canada", "Music Has The Right To Children", [
        "Wildlife Analysis",
        "An Eagle In Your Mind",
        "The Color Of The Fire",
        "Telephasic Workshop",
        "Triangles & Rhombuses",
        "Sixtyten",
        "Turquoise Hexagon Sun",
        "Roygbiv",
    ]),
    ("Autechre", "Amber (feat. Sean Booth)", [
        "Foil",
        "Montreal",
        "Silverside",
        "Slip",
        "Glitch",
        "Piezo",
        "Nine",
        "Further (Live At The Barbican 1994)",
    ]),
]

DURATIONS = [251, 284, 351, 356, 222, 315, 211, 309]


def info_chunk(title, artist, album):
    def field(tag, value):
        raw = value.encode("latin-1", "replace") + b"\0"
        if len(raw) & 1:
            raw += b"\0"
        return tag + struct.pack("<I", len(raw)) + raw

    body = b"INFO" + field(b"INAM", title) + field(b"IART", artist) + field(b"IPRD", album)
    return b"LIST" + struct.pack("<I", len(body)) + body


def write_wav(path, title, artist, album, seconds):
    fmt = struct.pack("<HHIIHH", 1, CHANNELS, RATE, BYTE_RATE, CHANNELS * BITS // 8, BITS)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    chunks += info_chunk(title, artist, album)
    # Le chunk `data` annonce sa taille et le fichier s'arrete la : le parseur
    # lit la duree dans l'en-tete et sort proprement (tags_riff.c, ligne 73).
    declared = seconds * BYTE_RATE
    chunks += b"data" + struct.pack("<I", declared)
    riff = b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks
    with open(path, "wb") as out:
        out.write(riff)


def safe(name):
    return "".join(c if c not in '\\/:*?"<>|' else "-" for c in name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=os.path.join(os.environ.get("TEMP", "."), "minidisk_demo"))
    parser.add_argument("--count", type=int, default=24)
    args = parser.parse_args()

    written = 0
    for artist, album, titles in ALBUMS:
        folder = os.path.join(args.out, safe(artist), safe(album))
        os.makedirs(folder, exist_ok=True)
        for index, title in enumerate(titles):
            if written >= args.count:
                break
            seconds = DURATIONS[index % len(DURATIONS)]
            path = os.path.join(folder, "%02d %s.wav" % (index + 1, safe(title)))
            write_wav(path, title, artist, album, seconds)
            written += 1
    print("%d fichiers dans %s" % (written, args.out))


if __name__ == "__main__":
    main()

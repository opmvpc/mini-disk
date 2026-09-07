#!/usr/bin/env python3
"""Génère les vecteurs golden audio de tests/data/audio/ (T-040).

Le signal de référence est toujours le même : une sinusoïde de 1 kHz à -6 dBFS,
44 100 Hz, phase nulle, calculée par la même formule que tests/test_codecs.c.
Les tests peuvent donc comparer le décodage à la formule sans lire de fichier
de référence.

    python tools/gen_audio_vectors.py [dossier de sortie]

WAV, AIFF/AIFF-C et FLAC sont écrits ici, sans aucune dépendance : l'encodeur
FLAC minimal en bas de ce fichier produit des trames « fixed predictor » d'ordre
2 avec résidus Rice, c'est-à-dire le vrai chemin de décodage de dr_flac, pas du
verbatim qui ne testerait rien.

MP3, Ogg Vorbis et M4A ne peuvent pas être encodés en Python pur. Le script les
demande à ffmpeg s'il est présent, sinon à VLC (`vlc.exe --sout`), sinon il
signale le vecteur manquant et sort quand même en succès : les tests sautent
proprement les vecteurs absents.
"""

import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile

SAMPLE_RATE = 44100
TONE_HZ = 1000.0
AMPLITUDE = 0.5011872336272722  # -6 dBFS
LONG_FRAMES = 22050  # 0.5 s : SNR, bit-exact, seek
SHORT_FRAMES = 4410  # 0.1 s : couverture de format et durée exacte
LOSSY_FRAMES = 44100  # 1 s : au-dela, le MP3 a 256 kb/s depasserait les 40 KB du dépôt

VLC_CANDIDATES = [
    r"C:\Program Files\VideoLAN\VLC\vlc.exe",
    r"C:\Program Files (x86)\VideoLAN\VLC\vlc.exe",
]


def sine(frames, hz=TONE_HZ):
    step = 2.0 * math.pi * hz / SAMPLE_RATE
    return [AMPLITUDE * math.sin(step * i) for i in range(frames)]


# --- WAV ---------------------------------------------------------------------


def riff(fmt_chunk, data):
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt_chunk)) + fmt_chunk
    body += b"data" + struct.pack("<I", len(data)) + data
    if len(data) & 1:
        body += b"\0"
    return b"RIFF" + struct.pack("<I", len(body)) + body


def pcm_fmt(channels, bits, float_format=False):
    tag = 3 if float_format else 1
    block_align = channels * bits // 8
    return struct.pack(
        "<HHIIHH", tag, channels, SAMPLE_RATE, SAMPLE_RATE * block_align, block_align, bits
    )


def quantize(value, bits):
    """Arrondi vers l'entier signé le plus proche, saturé : c'est exactement ce
    que le décodeur refera en sens inverse, d'où le bit-exact des tests."""
    peak = 1 << (bits - 1)
    scaled = int(round(value * peak))
    return max(-peak, min(peak - 1, scaled))


def wav_pcm(samples_per_channel, bits, unsigned8=False):
    channels = len(samples_per_channel)
    frames = len(samples_per_channel[0])
    out = bytearray()
    for frame in range(frames):
        for channel in range(channels):
            value = quantize(samples_per_channel[channel][frame], bits)
            if bits == 8 and unsigned8:
                out.append((value + 128) & 0xFF)
            else:
                out += (value & ((1 << bits) - 1)).to_bytes(bits // 8, "little")
    return riff(pcm_fmt(channels, bits), bytes(out))


def wav_float(samples_per_channel):
    channels = len(samples_per_channel)
    frames = len(samples_per_channel[0])
    out = bytearray()
    for frame in range(frames):
        for channel in range(channels):
            out += struct.pack("<f", samples_per_channel[channel][frame])
    return riff(pcm_fmt(channels, 32, float_format=True), bytes(out))


# --- AIFF / AIFF-C -----------------------------------------------------------


def extended80(value):
    """Le taux d'échantillonnage d'un chunk COMM, en flottant étendu 80 bits."""
    exponent = 16383
    mantissa = float(value)
    while mantissa >= 2.0:
        mantissa /= 2.0
        exponent += 1
    while mantissa < 1.0 and mantissa > 0.0:
        mantissa *= 2.0
        exponent -= 1
    fraction = int(round(mantissa * (1 << 63)))
    return struct.pack(">H", exponent) + struct.pack(">Q", fraction)


def aiff_chunk(name, body):
    out = name + struct.pack(">I", len(body)) + body
    if len(body) & 1:
        out += b"\0"
    return out


def aiff(samples_per_channel, bits, compression=None):
    channels = len(samples_per_channel)
    frames = len(samples_per_channel[0])
    big_endian = compression not in (b"sowt",)
    data = bytearray()
    for frame in range(frames):
        for channel in range(channels):
            value = samples_per_channel[channel][frame]
            if compression in (b"fl32", b"FL32"):
                data += struct.pack(">f", value)
            else:
                quantized = quantize(value, bits) & ((1 << bits) - 1)
                data += quantized.to_bytes(bits // 8, "big" if big_endian else "little")

    comm = struct.pack(">HIH", channels, frames, bits) + extended80(SAMPLE_RATE)
    if compression is not None:
        comm += compression + bytes([len(b"")]) + b"\0"  # pstring vide, padée
    ssnd = struct.pack(">II", 0, 0) + bytes(data)
    body = (b"AIFC" if compression is not None else b"AIFF") + aiff_chunk(b"COMM", comm)
    if compression is not None:
        body = (
            b"AIFC"
            + aiff_chunk(b"FVER", struct.pack(">I", 0xA2805140))
            + aiff_chunk(b"COMM", comm)
        )
    body += aiff_chunk(b"SSND", ssnd)
    return b"FORM" + struct.pack(">I", len(body)) + body


# --- FLAC --------------------------------------------------------------------


class BitWriter:
    def __init__(self):
        self.data = bytearray()
        self.accumulator = 0
        self.bits = 0

    def write(self, value, bits):
        for shift in range(bits - 1, -1, -1):
            self.accumulator = (self.accumulator << 1) | ((value >> shift) & 1)
            self.bits += 1
            if self.bits == 8:
                self.data.append(self.accumulator & 0xFF)
                self.accumulator = 0
                self.bits = 0

    def write_unary(self, count):
        for _ in range(count):
            self.write(0, 1)
        self.write(1, 1)

    def align(self):
        while self.bits:
            self.write(0, 1)


def crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def crc16(data):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x8005) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def utf8_number(value):
    if value < 0x80:
        return bytes([value])
    lengths = [(0x800, 2), (0x10000, 3), (0x200000, 4), (0x4000000, 5), (1 << 36, 6)]
    for limit, length in lengths:
        if value < limit:
            break
    out = bytearray()
    for i in range(length - 1, 0, -1):
        out.append(0x80 | ((value >> (6 * i)) & 0x3F))
    prefix = ((0xFF << (8 - length)) & 0xFF)
    out.insert(0, prefix | (value >> (6 * (length - 1))))
    return bytes(out)


def rice_parameter(residuals):
    if not residuals:
        return 0
    mean = sum(abs(r) for r in residuals) / len(residuals)
    parameter = 0
    while (1 << (parameter + 1)) < mean * 2 and parameter < 14:
        parameter += 1
    return parameter


def flac_subframe(writer, block, bits):
    """Fixed predictor d'ordre 2 + résidus Rice : le vrai chemin de dr_flac."""
    order = 2
    writer.write(0, 1)
    writer.write(0b001000 | order, 6)
    writer.write(0, 1)  # pas de bits gaspillés
    for i in range(order):
        writer.write(block[i] & ((1 << bits) - 1), bits)
    residuals = [block[i] - 2 * block[i - 1] + block[i - 2] for i in range(order, len(block))]
    parameter = rice_parameter(residuals)
    writer.write(0, 2)  # méthode : Rice partitionné, paramètre sur 4 bits
    writer.write(0, 4)  # ordre de partition 0 : une seule partition
    writer.write(parameter, 4)
    for residual in residuals:
        folded = (residual << 1) ^ (residual >> 63) if residual < 0 else (residual << 1)
        writer.write_unary(folded >> parameter)
        if parameter:
            writer.write(folded & ((1 << parameter) - 1), parameter)


def flac_frame(number, blocks, bits, block_size):
    writer = BitWriter()
    writer.write(0b11111111111110, 14)
    writer.write(0, 1)
    writer.write(0, 1)  # blocs de taille fixe, numérotés par trame
    writer.write(0b0111, 4)  # taille de bloc : 16 bits à la fin de l'en-tête
    writer.write(0b1001, 4)  # 44100 Hz
    writer.write(len(blocks) - 1, 4)  # canaux indépendants
    writer.write(0b100, 3)  # 16 bits par échantillon
    writer.write(0, 1)
    for byte in utf8_number(number):
        writer.write(byte, 8)
    writer.write(block_size - 1, 16)
    writer.data.append(crc8(bytes(writer.data)))
    for block in blocks:
        flac_subframe(writer, block, bits)
    writer.align()
    writer.data += struct.pack(">H", crc16(bytes(writer.data)))
    return bytes(writer.data)


def flac(samples_per_channel, block_size=4410):
    bits = 16
    channels = [[quantize(v, bits) for v in channel] for channel in samples_per_channel]
    total = len(channels[0])

    frames = bytearray()
    number = 0
    at = 0
    while at < total:
        size = min(block_size, total - at)
        frames += flac_frame(number, [c[at : at + size] for c in channels], bits, size)
        number += 1
        at += size

    info = BitWriter()
    info.write(block_size, 16)  # min block size
    info.write(block_size, 16)  # max block size
    info.write(0, 24)  # min frame size : inconnu
    info.write(0, 24)  # max frame size : inconnu
    info.write(SAMPLE_RATE, 20)
    info.write(len(channels) - 1, 3)
    info.write(bits - 1, 5)
    info.write(total, 36)
    info.data += b"\0" * 16  # MD5 nul : « non calculé », dr_flac ne le vérifie pas

    header = bytes([0x80 | 0]) + struct.pack(">I", len(info.data))[1:]  # dernier bloc, STREAMINFO
    return b"fLaC" + header + bytes(info.data) + bytes(frames)


# --- MP3 / Ogg / M4A : par un encodeur externe -------------------------------


def find_tool(name, candidates=()):
    found = shutil.which(name)
    if found:
        return found
    for path in candidates:
        if os.path.exists(path):
            return path
    return None


def encode_external(source_wav, out_path, kind):
    """Retourne un message d'échec, ou None si le fichier a été écrit."""
    ffmpeg = find_tool("ffmpeg")
    if ffmpeg:
        args = {
            "mp3": ["-codec:a", "libmp3lame", "-b:a", "256k"],
            "ogg": ["-codec:a", "libvorbis", "-q:a", "9"],
            "m4a": ["-codec:a", "aac", "-b:a", "192k"],
        }[kind]
        command = [ffmpeg, "-y", "-i", source_wav] + args + [out_path]
        if subprocess.call(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0:
            return None
        return "ffmpeg a échoué"

    vlc = find_tool("vlc", VLC_CANDIDATES)
    if vlc:
        # Débits élevés : les encodeurs de VLC sont bien plus bruyants que ceux de
        # ffmpeg sur une sinusoïde pure (voir la Livraison de T-040).
        chain = {
            "mp3": "acodec=mp3,ab=256,channels=1,samplerate=44100}:standard{access=file,mux=raw",
            "ogg": "acodec=vorb,channels=1,samplerate=44100}:standard{access=file,mux=ogg",
            "m4a": "acodec=mp4a,ab=192,channels=1,samplerate=44100}:standard{access=file,mux=mp4",
        }[kind]
        command = [
            vlc,
            "-I",
            "dummy",
            "--no-repeat",
            "--no-loop",
            "--sout-vorbis-quality=10",
            source_wav,
            "--sout=#transcode{" + chain + ",dst=" + out_path + "}",
            "vlc://quit",
        ]
        subprocess.call(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if os.path.exists(out_path) and os.path.getsize(out_path) > 512:
            return None
        return "VLC a échoué"

    return "ni ffmpeg ni VLC sur cette machine"


# --- écriture ----------------------------------------------------------------


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join("tests", "data", "audio")
    os.makedirs(out_dir, exist_ok=True)

    long_mono = [sine(LONG_FRAMES)]
    short_mono = [sine(SHORT_FRAMES)]
    short_stereo = [sine(SHORT_FRAMES), sine(SHORT_FRAMES, 2 * TONE_HZ)]

    files = {
        "sine_16.wav": wav_pcm(long_mono, 16),
        "sine_8.wav": wav_pcm(short_mono, 8, unsigned8=True),
        "sine_24.wav": wav_pcm(short_mono, 24),
        "sine_32.wav": wav_pcm(short_mono, 32),
        "sine_f32.wav": wav_float(short_mono),
        "sine_stereo_16.wav": wav_pcm(short_stereo, 16),
        "sine_16.aif": aiff(long_mono, 16),
        "sine_sowt.aifc": aiff(short_mono, 16, compression=b"sowt"),
        "sine_f32.aifc": aiff(short_mono, 32, compression=b"fl32"),
        "sine.flac": flac(long_mono),
    }
    for name, data in files.items():
        with open(os.path.join(out_dir, name), "wb") as handle:
            handle.write(data)
        print("%-22s %7d octets" % (name, len(data)))

    # Les formats compressés avec pertes passent par un encodeur externe.
    with tempfile.TemporaryDirectory() as temp:
        source = os.path.join(temp, "sine.wav")
        with open(source, "wb") as handle:
            handle.write(wav_pcm([sine(LOSSY_FRAMES)], 16))
        for kind, name in (("mp3", "sine.mp3"), ("ogg", "sine.ogg"), ("m4a", "sine.m4a")):
            target = os.path.join(out_dir, name)
            failure = encode_external(source, target, kind)
            if failure:
                print("%-22s ABSENT (%s)" % (name, failure))
            else:
                print("%-22s %7d octets" % (name, os.path.getsize(target)))


if __name__ == "__main__":
    main()

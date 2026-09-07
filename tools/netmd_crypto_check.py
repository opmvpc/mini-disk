#!/usr/bin/env python3
# netmd_crypto_check.py - the independent side of T-042's crypto tests.
#
# It implements DES, 3DES and the ISO 9797-1 algorithm 3 retail MAC in pure
# Python, from the tables of FIPS 46-3 and from nothing else (ADR-008: no
# pycryptodome, no OpenSSL, no line of netmd-js or linux-minidisc), and prints
# the hex the C tests carry as constants. Two implementations written from the
# same standard in two languages disagreeing is exactly the signal wanted: a
# single implementation checked against itself proves nothing.
#
# The ten known-answer vectors it prints first are the anchor, and they were
# checked against a third implementation nobody here wrote: Windows CNG
# (bcrypt.dll, BCRYPT_DES_ALGORITHM / 3DES_112 in ECB), which agrees on all ten
# plus the FIPS 81 appendix B example and the 3DES block. `--cng` re-runs that
# comparison on this machine. So the chain is: standard tables -> this script ->
# netmd_des.c, with an independent oracle across the middle.
#
# Usage: python tools/netmd_crypto_check.py [--cng]

import sys

IP = [58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
      62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
      57, 49, 41, 33, 25, 17, 9, 1, 59, 51, 43, 35, 27, 19, 11, 3,
      61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7]

FP = [40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
      38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
      36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
      34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9, 49, 17, 57, 25]

E = [32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9, 8, 9, 10, 11, 12, 13,
     12, 13, 14, 15, 16, 17, 16, 17, 18, 19, 20, 21, 20, 21, 22, 23, 24, 25,
     24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1]

P = [16, 7, 20, 21, 29, 12, 28, 17, 1, 15, 23, 26, 5, 18, 31, 10,
     2, 8, 24, 14, 32, 27, 3, 9, 19, 13, 30, 6, 22, 11, 4, 25]

PC1 = [57, 49, 41, 33, 25, 17, 9, 1, 58, 50, 42, 34, 26, 18,
       10, 2, 59, 51, 43, 35, 27, 19, 11, 3, 60, 52, 44, 36,
       63, 55, 47, 39, 31, 23, 15, 7, 62, 54, 46, 38, 30, 22,
       14, 6, 61, 53, 45, 37, 29, 21, 13, 5, 28, 20, 12, 4]

PC2 = [14, 17, 11, 24, 1, 5, 3, 28, 15, 6, 21, 10,
       23, 19, 12, 4, 26, 8, 16, 7, 27, 20, 13, 2,
       41, 52, 31, 37, 47, 55, 30, 40, 51, 45, 33, 48,
       44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32]

SHIFTS = [1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1]

S = [
    [14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
     0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
     4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
     15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13],
    [15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
     3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
     0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
     13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9],
    [10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
     13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
     13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
     1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12],
    [7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
     13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
     10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
     3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14],
    [2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
     14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
     4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
     11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3],
    [12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
     10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
     9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
     4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13],
    [4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
     13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
     1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
     6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12],
    [13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
     1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
     7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
     2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11],
]


def bits(data):
    """Bytes to a list of bits, FIPS numbering: bit 1 is the MSB of byte 0."""
    out = []
    for byte in data:
        for i in range(7, -1, -1):
            out.append((byte >> i) & 1)
    return out


def unbits(bit_list):
    out = bytearray()
    for i in range(0, len(bit_list), 8):
        value = 0
        for b in bit_list[i:i + 8]:
            value = (value << 1) | b
        out.append(value)
    return bytes(out)


def permute(bit_list, table):
    return [bit_list[i - 1] for i in table]


def key_schedule(key):
    k = permute(bits(key), PC1)
    c, d = k[:28], k[28:]
    out = []
    for shift in SHIFTS:
        c = c[shift:] + c[:shift]
        d = d[shift:] + d[:shift]
        out.append(permute(c + d, PC2))
    return out


def feistel(r, subkey):
    x = [a ^ b for a, b in zip(permute(r, E), subkey)]
    out = []
    for box in range(8):
        chunk = x[box * 6:box * 6 + 6]
        row = (chunk[0] << 1) | chunk[5]
        col = (chunk[1] << 3) | (chunk[2] << 2) | (chunk[3] << 1) | chunk[4]
        value = S[box][row * 16 + col]
        out += [(value >> 3) & 1, (value >> 2) & 1, (value >> 1) & 1, value & 1]
    return permute(out, P)


def des_block(key, block, decrypt=False):
    subkeys = key_schedule(key)
    if decrypt:
        subkeys = subkeys[::-1]
    b = permute(bits(block), IP)
    left, right = b[:32], b[32:]
    for subkey in subkeys:
        left, right = right, [a ^ c for a, c in zip(left, feistel(right, subkey))]
    return unbits(permute(right + left, FP))


def xor(a, b):
    return bytes(x ^ y for x, y in zip(a, b))


def des_ecb(key, data, decrypt=False):
    return b"".join(des_block(key, data[i:i + 8], decrypt)
                    for i in range(0, len(data), 8))


def des_cbc_encrypt(key, iv, data):
    out = bytearray()
    prev = iv
    for i in range(0, len(data), 8):
        prev = des_block(key, xor(data[i:i + 8], prev))
        out += prev
    return bytes(out)


def des_cbc_decrypt(key, iv, data):
    out = bytearray()
    prev = iv
    for i in range(0, len(data), 8):
        block = data[i:i + 8]
        out += xor(des_block(key, block, decrypt=True), prev)
        prev = block
    return bytes(out)


def des3_block(key16, block, decrypt=False):
    """EDE with two keys: K3 = K1 (ANSI X9.52 two-key 3DES)."""
    k1, k2 = key16[:8], key16[8:]
    if not decrypt:
        return des_block(k1, des_block(k2, des_block(k1, block), True))
    return des_block(k1, des_block(k2, des_block(k1, block, True)), True)


def des3_cbc_encrypt(key16, iv, data):
    out = bytearray()
    prev = iv
    for i in range(0, len(data), 8):
        prev = des3_block(key16, xor(data[i:i + 8], prev))
        out += prev
    return bytes(out)


def retail_mac(key16, value, iv=b"\x00" * 8):
    """ISO 9797-1 algorithm 3: DES-CBC over all but the last block, then a
    single 3DES-EDE step over the last one (research/01 s4.6)."""
    beginning, end = value[:-8], value[-8:]
    iv2 = iv
    if beginning:
        iv2 = des_cbc_encrypt(key16[:8], iv, beginning)[-8:]
    return des3_cbc_encrypt(key16, iv2, end)[:8]


def h(data):
    return "".join("%02x" % b for b in data)


def u(text):
    return bytes.fromhex(text.replace(" ", ""))


# --- the NetMD constants of research/01 s4.5-4.10 ---------------------------
EKB_ROOT_KEY = u("12 34 56 78 9a bc de f0 0f ed cb a9 87 65 43 21")
CONTENT_ID = u("01 0f 50 00 00 04 00 00 00 48 a2 8d 3e 1a 3b 0c 44 af 2f a0")
KEK = u("14 e3 83 4e e2 d3 cc a5")


def cng_ecb(alg, key, data, decrypt=False):
    """Windows CNG, the independent oracle. Only used by --cng."""
    import ctypes
    b = ctypes.WinDLL("bcrypt")
    handle = ctypes.c_void_p()
    b.BCryptOpenAlgorithmProvider(ctypes.byref(handle), ctypes.c_wchar_p(alg), None, 0)
    mode = ctypes.create_unicode_buffer("ChainingModeECB")
    b.BCryptSetProperty(handle, ctypes.c_wchar_p("ChainingMode"), mode, len(mode) * 2, 0)
    key_handle = ctypes.c_void_p()
    obj = ctypes.create_string_buffer(8192)
    b.BCryptGenerateSymmetricKey(handle, ctypes.byref(key_handle), obj, 8192, key, len(key), 0)
    out = ctypes.create_string_buffer(len(data) + 16)
    written = ctypes.c_ulong(0)
    call = b.BCryptDecrypt if decrypt else b.BCryptEncrypt
    call(key_handle, data, len(data), None, None, 0, out, len(data) + 16, ctypes.byref(written), 0)
    return out.raw[:len(data)]


KNOWN_ANSWER = [
    ("133457799BBCCDFF", "0123456789ABCDEF"),
    ("0000000000000000", "0000000000000000"),
    ("FFFFFFFFFFFFFFFF", "FFFFFFFFFFFFFFFF"),
    ("3000000000000000", "1000000000000001"),
    ("1111111111111111", "1111111111111111"),
    ("0123456789ABCDEF", "1111111111111111"),
    ("1111111111111111", "0123456789ABCDEF"),
    ("FEDCBA9876543210", "0123456789ABCDEF"),
    ("7CA110454A1A6E57", "01A1D6D039776742"),
    ("0131D9619DC1376E", "5CD54CA83DEF57DA"),
]


def cross_check():
    failures = 0
    for key, plain in KNOWN_ANSWER:
        mine = des_block(u(key), u(plain))
        theirs = cng_ecb("DES", u(key), u(plain))
        failures += mine != theirs
        print("  %s %s %s %s" % (key.lower(), plain.lower(), h(mine),
                                 "ok" if mine == theirs else "MISMATCH " + h(theirs)))
    text = b"Now is the time for all "
    mine = des_ecb(u("0123456789abcdef"), text)
    failures += mine != cng_ecb("DES", u("0123456789abcdef"), text)
    block = u("0011223344556677")
    mine3 = des3_block(EKB_ROOT_KEY, block)
    failures += mine3 != cng_ecb("3DES_112", EKB_ROOT_KEY, block)
    raw = u("0123456789abcdef")
    failures += des_block(KEK, raw, decrypt=True) != cng_ecb("DES", KEK, raw, decrypt=True)
    print("  FIPS 81 ECB, 3DES block and the ECB decrypt: %s"
          % ("ok" if failures == 0 else "MISMATCH"))
    return failures


def main():
    if "--cng" in sys.argv:
        print("# cross-check against Windows CNG (bcrypt.dll)")
        raise SystemExit(1 if cross_check() else 0)

    print("# DES known-answer vectors (all ten agree with Windows CNG, see --cng)")
    for key, plain in KNOWN_ANSWER:
        cipher = des_block(u(key), u(plain))
        back = des_block(u(key), cipher, decrypt=True)
        assert back == u(plain)
        print("  key %s  plain %s  ->  %s" % (key.lower(), plain.lower(), h(cipher)))

    print("\n# FIPS 81 appendix B/C: 'Now is the time for all '")
    key = u("0123456789abcdef")
    iv = u("1234567890abcdef")
    text = b"Now is the time for all "
    print("  ECB %s" % h(des_ecb(key, text)))
    print("  CBC %s" % h(des_cbc_encrypt(key, iv, text)))

    print("\n# 3DES EDE two-key, K1 || K2 = the EKB root key")
    print("  block 0011223344556677 -> %s"
          % h(des3_block(EKB_ROOT_KEY, u("0011223344556677"))))
    print("  cbc 16 bytes iv=0     -> %s"
          % h(des3_cbc_encrypt(EKB_ROOT_KEY, b"\x00" * 8, u("00112233445566778899aabbccddeeff"))))

    print("\n# retail MAC (ISO 9797-1 alg 3)")
    print("  root key, nonce 00..0f -> %s"
          % h(retail_mac(EKB_ROOT_KEY, u("000102030405060708090a0b0c0d0e0f"))))
    # A 24 byte value exercises the DES-CBC part over more than one block, which
    # the 16 byte NetMD case degenerates away.
    print("  root key, 24 bytes     -> %s"
          % h(retail_mac(EKB_ROOT_KEY, u("00112233445566778899aabbccddeeff0123456789abcdef"))))

    print("\n# session key: retailMAC(rootKey, hostNonce || devNonce), s4.6")
    host = u("00 11 22 33 44 55 66 77")
    dev = u("88 99 aa bb cc dd ee ff")
    session_key = retail_mac(EKB_ROOT_KEY, host + dev)
    print("  host %s dev %s -> %s" % (h(host), h(dev), h(session_key)))

    print("\n# setupDownload payload: DES-CBC(01010101 || contentID || KEK), s4.7")
    plain = b"\x01\x01\x01\x01" + CONTENT_ID + KEK
    assert len(plain) == 32
    print("  plain  %s" % h(plain))
    print("  cipher %s" % h(des_cbc_encrypt(session_key, b"\x00" * 8, plain)))

    print("\n# commit authentication: DES-ECB(0^8, sessionKey), s4.12")
    print("  %s" % h(des_block(session_key, b"\x00" * 8)))

    print("\n# data key: DES-ECB-DECRYPT(rawKey, KEK), s4.10")
    raw_key = u("01 23 45 67 89 ab cd ef")
    data_key = des_block(KEK, raw_key, decrypt=True)
    print("  raw %s -> sent %s" % (h(raw_key), h(data_key)))
    print("  the device's side: ECB-ENCRYPT(sent, KEK) = %s" % h(des_block(KEK, data_key)))

    print("\n# packet payload: DES-CBC(rawKey, iv=0) over two SP frames of a ramp")
    audio = bytes((i * 7 + 3) & 0xFF for i in range(2 * 2048))
    cipher = des_cbc_encrypt(raw_key, b"\x00" * 8, audio)
    print("  first 8  %s" % h(cipher[:8]))
    print("  last 8   %s" % h(cipher[-8:]))
    # The chaining that matters: cutting the same data into two packets with the
    # IV carried over must produce exactly the same bytes (s4.10).
    a = des_cbc_encrypt(raw_key, b"\x00" * 8, audio[:1024])
    b2 = des_cbc_encrypt(raw_key, a[-8:], audio[1024:])
    assert a + b2 == cipher
    print("  split at 1024 and chained: identical")

    print("\n# sendTrack sizes for that track, s4.11")
    frames = len(audio) // 2048
    print("  frames %d  totalBytes %d (0x%08x)" % (frames, len(audio) + 24, len(audio) + 24))


if __name__ == "__main__":
    main()

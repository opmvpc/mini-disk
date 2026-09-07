#!/usr/bin/env python3
# gen_charset_tables.py - generates two tables, both derived from Unicode data:
#
#   src/core/plan/plan_charset.h  : Unicode codepoint -> what a MiniDisc TOC can
#                                   actually hold (research/01 s3.11), the
#                                   WRITE direction (T-031).
#   src/core/netmd/netmd_charset.h: Shift-JIS (CP932) -> Unicode, the READ
#                                   direction: what comes back off the device is
#                                   raw Shift-JIS and has to be turned into the
#                                   UTF-8 the whole app speaks (T-021).
#
# The second table is built by asking Python's own CP932 codec what each double
# byte sequence decodes to - a Unicode mapping, not a line of any NetMD
# implementation (ADR-008).
#
# ADR-008: the tables are DERIVED FROM UNICODE DATA EMBEDDED BELOW, never copied
# from netmd-js or any other implementation. Three families:
#   1. Latin letters with diacritics -> their unaccented ASCII form
#      (Unicode canonical decomposition, base letter kept, marks dropped), plus
#      the handful of letters that decompose to two ASCII letters (ss, ae, oe).
#   2. Hiragana and katakana -> HALF-WIDTH katakana (U+FF61..U+FF9F), a voiced
#      kana becoming base + U+FF9E and a semi-voiced one base + U+FF9F, which is
#      exactly why those count as two characters in the TOC budget.
#   3. Typographic punctuation -> its ASCII equivalent.
# Full-width ASCII (U+FF01..U+FF5E), U+3000 and plain ASCII are handled by an
# arithmetic range in plan_toc.c and deliberately absent from the table.
#
# Everything not in the table and not in those ranges is DROPPED by the
# sanitizer (emoji, symbols, CJK ideographs: the device would show mojibake).
#
# Usage: python tools/gen_charset_tables.py [output_path]

import sys, os

# --- 1. Latin ---------------------------------------------------------------
# Unicode 15 canonical decompositions of Latin-1 Supplement + Latin Extended-A/B,
# written base-letter first. Every character below decomposes (canonically or
# compatibly) onto the ASCII letter that heads its line.
LATIN_GROUPS = {
    "A": "ÀÁÂÃÄÅĀĂĄǍȀȂȦȺ",
    "a": "àáâãäåāăąǎȁȃȧₐ",
    "AE": "ÆǢǼ",
    "ae": "æǣǽ",
    "C": "ÇĆĈĊČƇȻ",
    "c": "çćĉċčƈȼ",
    "D": "ĎĐÐƉƊ",
    "d": "ďđðɖɗ",
    "E": "ÈÉÊËĒĔĖĘĚȄȆȨƎ",
    "e": "èéêëēĕėęěȅȇȩɇ",
    "G": "ĜĞĠĢǤǦǴ",
    "g": "ĝğġģǥǧǵ",
    "H": "ĤĦȞ",
    "h": "ĥħȟ",
    "I": "ÌÍÎÏĨĪĬĮİǏȈȊ",
    "i": "ìíîïĩīĭįıǐȉȋ",
    "IJ": "Ĳ",
    "ij": "ĳ",
    "J": "Ĵ",
    "j": "ĵǰ",
    "K": "ĶǨ",
    "k": "ķǩ",
    "L": "ĹĻĽĿŁ",
    "l": "ĺļľŀł",
    "N": "ÑŃŅŇŊǸ",
    "n": "ñńņňŋǹ",
    "O": "ÒÓÔÕÖØŌŎŐǑǪǾȌȎȪȮ",
    "o": "òóôõöøōŏőǒǫǿȍȏȫȯ",
    "OE": "Œ",
    "oe": "œ",
    "R": "ŔŖŘȐȒ",
    "r": "ŕŗřȑȓ",
    "S": "ŚŜŞŠȘ",
    "s": "śŝşšșſ",
    "ss": "ßẞ",
    "T": "ŢŤŦȚ",
    "t": "ţťŧț",
    "TH": "Þ",
    "th": "þ",
    "U": "ÙÚÛÜŨŪŬŮŰŲǓǕǗǙǛȔȖ",
    "u": "ùúûüũūŭůűųǔǖǘǚǜȕȗ",
    "W": "Ŵ",
    "w": "ŵ",
    "Y": "ÝŶŸȲ",
    "y": "ýÿŷȳ",
    "Z": "ŹŻŽƵ",
    "z": "źżžƶ",
}

# --- 2. Punctuation ---------------------------------------------------------
PUNCT = {
    0x00A0: " ", 0x2000: " ", 0x2001: " ", 0x2002: " ", 0x2003: " ", 0x2004: " ",
    0x2005: " ", 0x2006: " ", 0x2007: " ", 0x2008: " ", 0x2009: " ", 0x200A: " ",
    0x202F: " ", 0x205F: " ",
    0x2010: "-", 0x2011: "-", 0x2012: "-", 0x2013: "-", 0x2014: "-", 0x2015: "-",
    0x2212: "-", 0x00AD: "-",
    0x2018: "'", 0x2019: "'", 0x201A: "'", 0x201B: "'", 0x2032: "'", 0x00B4: "'",
    0x02B9: "'", 0x02BC: "'",
    0x201C: '"', 0x201D: '"', 0x201E: '"', 0x201F: '"', 0x2033: '"', 0x00AB: '"',
    0x00BB: '"',
    0x2026: "...", 0x2022: "-", 0x00B7: ".", 0x2027: ".",
    0x00D7: "x", 0x00F7: "/", 0x00A9: "(C)", 0x00AE: "(R)", 0x2122: "(TM)",
    0x00BD: "1/2", 0x00BC: "1/4", 0x00BE: "3/4",
    0x2160: "I", 0x2161: "II", 0x2162: "III", 0x2163: "IV", 0x2164: "V",
    0x00B0: "o", 0x2044: "/", 0x2215: "/",
    0x3001: "､", 0x3002: "｡", 0x300C: "｢", 0x300D: "｣",
    0x30FB: "･", 0x30FC: "ｰ",
    0x309B: "ﾞ", 0x309C: "ﾟ", 0x3099: "ﾞ", 0x309A: "ﾟ",
}

# --- 3. Kana ----------------------------------------------------------------
# The half-width katakana block U+FF66..U+FF9D, in its own order: this IS the
# Unicode block, and the katakana each one is the half-width form of.
HALFWIDTH_BASE = [
    (0xFF66, 0x30F2), (0xFF67, 0x30A1), (0xFF68, 0x30A3), (0xFF69, 0x30A5),
    (0xFF6A, 0x30A7), (0xFF6B, 0x30A9), (0xFF6C, 0x30E3), (0xFF6D, 0x30E5),
    (0xFF6E, 0x30E7), (0xFF6F, 0x30C3),
    (0xFF71, 0x30A2), (0xFF72, 0x30A4), (0xFF73, 0x30A6), (0xFF74, 0x30A8),
    (0xFF75, 0x30AA),
    (0xFF76, 0x30AB), (0xFF77, 0x30AD), (0xFF78, 0x30AF), (0xFF79, 0x30B1),
    (0xFF7A, 0x30B3),
    (0xFF7B, 0x30B5), (0xFF7C, 0x30B7), (0xFF7D, 0x30B9), (0xFF7E, 0x30BB),
    (0xFF7F, 0x30BD),
    (0xFF80, 0x30BF), (0xFF81, 0x30C1), (0xFF82, 0x30C4), (0xFF83, 0x30C6),
    (0xFF84, 0x30C8),
    (0xFF85, 0x30CA), (0xFF86, 0x30CB), (0xFF87, 0x30CC), (0xFF88, 0x30CD),
    (0xFF89, 0x30CE),
    (0xFF8A, 0x30CF), (0xFF8B, 0x30D2), (0xFF8C, 0x30D5), (0xFF8D, 0x30D8),
    (0xFF8E, 0x30DB),
    (0xFF8F, 0x30DE), (0xFF90, 0x30DF), (0xFF91, 0x30E0), (0xFF92, 0x30E1),
    (0xFF93, 0x30E2),
    (0xFF94, 0x30E4), (0xFF95, 0x30E6), (0xFF96, 0x30E8),
    (0xFF97, 0x30E9), (0xFF98, 0x30EA), (0xFF99, 0x30EB), (0xFF9A, 0x30EC),
    (0xFF9B, 0x30ED),
    (0xFF9C, 0x30EF), (0xFF9D, 0x30F3),
]
# Voiced (dakuten) and semi-voiced (handakuten) katakana: Unicode composes them
# from the base kana + U+3099 / U+309A, so half-width they are base + FF9E/FF9F.
VOICED = "ガギグゲゴザジズゼゾ" \
         "ダヂヅデドバビブベボ" \
         "ヴヷヺ"
SEMIVOICED = "パピプペポ"
# Katakana with no half-width form of their own: the closest one the TOC can show.
KANA_FALLBACK = {0x30F0: 0xFF72, 0x30F1: 0xFF74, 0x30F5: 0xFF76, 0x30F6: 0xFF79,
                 0x30F8: 0xFF72, 0x30F9: 0xFF74, 0x30EE: 0xFF9C}


def build():
    table = {}

    for ascii_form, chars in LATIN_GROUPS.items():
        for ch in chars:
            table[ord(ch)] = ascii_form

    for cp, repl in PUNCT.items():
        table[cp] = repl

    kata_to_half = {}
    for half, kata in HALFWIDTH_BASE:
        kata_to_half[kata] = chr(half)
        table[kata] = chr(half)

    for ch in VOICED:
        # U+3099 composes the base kana; the base is the previous codepoint for
        # every row but wa/wo, which Unicode places just after their base.
        base = ord(ch) - 1
        if ord(ch) == 0x30F4:      # katakana VU = U + dakuten
            base = 0x30A6
        elif ord(ch) == 0x30F7:    # VA = WA + dakuten
            base = 0x30EF
        elif ord(ch) == 0x30FA:    # VO = WO + dakuten
            base = 0x30F2
        table[ord(ch)] = kata_to_half[base] + "ﾞ"
    for ch in SEMIVOICED:
        table[ord(ch)] = kata_to_half[ord(ch) - 2] + "ﾟ"
    for cp, half in KANA_FALLBACK.items():
        table[cp] = chr(half)

    # Hiragana U+3041..U+3096 is katakana minus 0x60 throughout the block.
    for cp in range(0x3041, 0x3097):
        kata = cp + 0x60
        if kata in table:
            table[cp] = table[kata]

    return table


def emit(table, path):
    lines = []
    lines.append("// plan_charset.h - GENERATED by tools/gen_charset_tables.py. Do not edit.")
    lines.append("//")
    lines.append("// Unicode codepoint -> what a MiniDisc TOC can hold (research/01 s3.11):")
    lines.append("// Latin diacritics folded to ASCII, kana folded to half-width katakana")
    lines.append("// (a voiced kana becomes two characters, which is what makes it cost two")
    lines.append("// cells' worth of budget). Sorted by codepoint: plan_toc.c binary searches it.")
    lines.append("// Anything absent, outside ASCII and outside the full-width ASCII range, is")
    lines.append("// dropped by plan_toc_sanitize.")
    lines.append("#ifndef PLAN_CHARSET_H")
    lines.append("#define PLAN_CHARSET_H")
    lines.append("")
    lines.append("typedef struct PlanCharsetEntry {")
    lines.append("    u32 cp;      // the codepoint being replaced")
    lines.append("    u8 size;     // bytes of UTF-8 below")
    lines.append("    u8 utf8[7];  // its half-width replacement, UTF-8")
    lines.append("} PlanCharsetEntry;")
    lines.append("")
    lines.append("static const PlanCharsetEntry plan_charset_map[] = {")
    for cp in sorted(table):
        repl = table[cp].encode("utf-8")
        assert len(repl) <= 7, (hex(cp), repl)
        body = ", ".join("0x%02X" % b for b in repl)
        pad = ", ".join(["0x00"] * (7 - len(repl)))
        if pad:
            body = body + ", " + pad
        lines.append("    {0x%04X, %d, {%s}},  // U+%04X" % (cp, len(repl), body, cp))
    lines.append("};")
    lines.append("")
    lines.append("#define PLAN_CHARSET_COUNT (sizeof(plan_charset_map) / sizeof(plan_charset_map[0]))")
    lines.append("")
    lines.append("#endif  // PLAN_CHARSET_H")
    text = "\n".join(lines) + "\n"
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write(text)
    return len(table)


# --- the read direction: Shift-JIS -> Unicode --------------------------------
# Only the lead bytes below are tabulated. 0x81..0x84 are JIS rows 1 to 8
# (punctuation, full-width ASCII, hiragana, katakana, Greek, Cyrillic) and
# 0x87 is the NEC row of units and roman numerals: everything a MiniDisc title
# can hold that is not a kanji. Kanji (lead 0x88..0xFC, ~6500 codepoints) would
# add ~26 KB to an executable whose whole budget is 360 KB, for titles no Latin
# user can read anyway - netmd_charset.c turns them into '?'. Single bytes are
# arithmetic and deliberately absent: 0x00..0x7E is ASCII and 0xA1..0xDF is the
# half-width katakana block at U+FF61 + (b - 0xA1).
SJIS_LEAD_BYTES = [0x81, 0x82, 0x83, 0x84, 0x87]


def build_sjis():
    table = {}
    for lead in SJIS_LEAD_BYTES:
        for trail in range(0x40, 0x100):
            if trail == 0x7F:
                continue
            raw = bytes([lead, trail])
            try:
                text = raw.decode("cp932")
            except UnicodeDecodeError:
                continue
            if len(text) != 1:
                continue
            table[(lead << 8) | trail] = ord(text)
    return table


def emit_sjis(table, path):
    lines = []
    lines.append("// netmd_charset.h - GENERATED by tools/gen_charset_tables.py. Do not edit.")
    lines.append("//")
    lines.append("// Shift-JIS (CP932) double byte sequence -> Unicode codepoint: the read")
    lines.append("// direction of research/01 s3.11, which is what a title coming back off the")
    lines.append("// device needs before anything else can look at it. Derived from the Unicode")
    lines.append("// mapping of CP932, never copied from another NetMD implementation (ADR-008).")
    lines.append("//")
    lines.append("// Single bytes are not here because they are arithmetic: 0x00..0x7E is ASCII")
    lines.append("// and 0xA1..0xDF is half-width katakana at U+FF61 + (b - 0xA1). Kanji are not")
    lines.append("// here either - see the generator for why. Sorted: netmd_charset.c bsearches it.")
    lines.append("#ifndef NETMD_CHARSET_H")
    lines.append("#define NETMD_CHARSET_H")
    lines.append("")
    lines.append("typedef struct NetmdCharsetEntry {")
    lines.append("    u16 sjis;  // the double byte sequence, big-endian as it travels")
    lines.append("    u16 cp;    // the codepoint it stands for (all of them fit in the BMP)")
    lines.append("} NetmdCharsetEntry;")
    lines.append("")
    lines.append("static const NetmdCharsetEntry netmd_charset_map[] = {")
    for sjis in sorted(table):
        cp = table[sjis]
        assert cp <= 0xFFFF, (hex(sjis), hex(cp))
        lines.append("    {0x%04X, 0x%04X}," % (sjis, cp))
    lines.append("};")
    lines.append("")
    lines.append("#define NETMD_CHARSET_COUNT "
                 "(sizeof(netmd_charset_map) / sizeof(netmd_charset_map[0]))")
    lines.append("")
    lines.append("#endif  // NETMD_CHARSET_H")
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return len(table)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default = os.path.join(here, "..", "src", "core", "plan", "plan_charset.h")
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(default)
    count = emit(build(), path)
    print("%s: %d entries" % (path, count))

    sjis_default = os.path.join(here, "..", "src", "core", "netmd", "netmd_charset.h")
    sjis_path = sys.argv[2] if len(sys.argv) > 2 else os.path.normpath(sjis_default)
    sjis_count = emit_sjis(build_sjis(), sjis_path)
    print("%s: %d entries" % (sjis_path, sjis_count))


if __name__ == "__main__":
    main()

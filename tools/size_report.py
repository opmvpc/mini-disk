# size_report.py - agrege build\minidisk.map par module (T-070).
#
# Pourquoi la carte et pas dumpbin /symbols sur les objets : notre code est un
# unity build (un seul main.obj), donc l'attribution "par unite de traduction"
# n'existe pas cote objets, et sous /LTCG dumpbin ne voit que des objets
# intermediaires anonymes. La carte du linker, elle, liste chaque symbole public
# avec son RVA final : la taille d'un symbole est l'ecart jusqu'au suivant dans
# la meme section (methode de P-004). Les statiques inlines ou fusionnes par
# /OPT:ICF sont donc comptes dans le symbole public qui les precede - c'est une
# borne, pas une mesure exacte, mais elle est stable d'un build a l'autre et
# c'est ce qu'on compare.
#
# Usage: python tools/size_report.py build/minidisk.map [build/minidisk.exe]
#        python tools/size_report.py --diff avant.json apres.json

import json
import os
import re
import sys

# Prefixe de symbole -> module. Le premier qui matche gagne, donc l'ordre compte
# (netmd_des_ avant netmd_, plan_charset avant plan_).
MODULES = [
    ("third_party: minimp3", ("mp3d_", "mp3dec_", "L3_", "L12_", "L1_", "g_pow43",
                              "g_scf", "g_deq", "g_win", "hdr_")),
    ("third_party: dr_flac", ("drflac", "drflac_")),
    ("third_party: dr_wav", ("drwav", "drwav_", "g_drwav")),
    ("third_party: stb_vorbis", ("stb_vorbis", "vorbis_", "tp_")),
    ("codecs", ("codec_",)),
    ("dsp", ("dsp_",)),
    ("pipeline", ("pipeline_",)),
    ("netmd", ("netmd_", "des_")),
    ("plan", ("plan_",)),
    ("library", ("lib_",)),
    ("tags", ("tags_", "tag_")),
    ("cache", ("cache_",)),
    ("renderer", ("r_", "gl_", "glsl_", "icon_")),
    ("ui", ("ui_",)),
    ("app", ("app_", "view_", "prefs_", "transfer_", "strings_", "str_table")),
    ("platform win32", ("os_", "win32_", "entry_point", "minidisk_main")),
    ("base", ("arena_", "str8", "str_", "mem_", "hash_", "jobs_", "job_",
              "f32_", "f64_", "u64_", "i64_", "math_", "v2_", "v3_", "v4_",
              "rect_", "rng_", "atomic_")),
]

IMPORT_RE = re.compile(r"^__imp_")
# `?tabs@?1??L3_huffman@@9@9` : un static local a une fonction. On le rattache a
# la fonction qui le contient, sinon tous les gros tableaux des decodeurs
# atterrissent dans "autres".
LOCAL_STATIC_RE = re.compile(r"^\?[^?]+@\?\d\?\?([A-Za-z_][A-Za-z0-9_]*)@@")
# `??_C@_0...` : un litteral chaine ou un source de shader.
LITERAL_RE = re.compile(r"^\?\?_C@")
# stb_vorbis est ecrit sans prefixe de module : la liste est explicite.
STB_NAMES = frozenset("""
start_decoder inverse_mdct decode_residue do_floor init_blocksize get_bits get32
compute_sorted_huffman compute_codewords compute_bitreverse compute_accelerated_huffman
codebook_decode codebook_decode_start codebook_decode_step codebook_decode_scalar
codebook_decode_scalar_raw codebook_decode_deinterleave_repeat inverse_db_table
imdct_step3_inner_s_loop imdct_step3_inner_s_loop_ld654 imdct_step3_inner_r_loop
imdct_step3_iter0_loop is_whole_packet_present start_page start_page_no_capturepattern
start_first_decoder setup_malloc setup_temp_malloc setup_temp_free
neighbors point_compare uint32_compare lookup1_values ilog float32_unpack
draw_line flush_packet next_segment get8 get8_packet get8_packet_raw
maybe_start_packet vorbis_finish_frame vorbis_pump_first_frame residue_decode
""".split())


def classify(name):
    m = LOCAL_STATIC_RE.match(name)
    if m:
        name = m.group(1)
    elif LITERAL_RE.match(name):
        return "chaines litterales / shaders (.rdata)"
    n = name
    while n[:1] in ("_", "?"):
        n = n[1:]
    if n in STB_NAMES:
        return "third_party: stb_vorbis"
    for module, prefixes in MODULES:
        for p in prefixes:
            if n.startswith(p):
                return module
    if IMPORT_RE.match(name):
        return "imports (kernel32/user32)"
    return "autres / compilateur"


def parse_map(path):
    """-> (sections, symbols) ; symbols = [(sec, offset, rva, name)]."""
    sections = []
    symbols = []
    in_publics = False
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            s = line.rstrip("\n")
            if "Publics by Value" in s:
                in_publics = True
                continue
            if not in_publics:
                m = re.match(r"\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+"
                             r"([0-9a-fA-F]+)H\s+(\S+)\s+(\S+)", s)
                if m:
                    sections.append((int(m.group(1), 16), int(m.group(2), 16),
                                     int(m.group(3), 16), m.group(4), m.group(5)))
                continue
            m = re.match(r"\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+"
                         r"([0-9a-fA-F]{16})", s)
            if not m:
                continue
            sec = int(m.group(1), 16)
            off = int(m.group(2), 16)
            name = m.group(3)
            rva = int(m.group(4), 16)
            if sec == 0:
                continue  # <absolute>
            symbols.append((sec, off, rva, name))
    return sections, symbols


# .bss n'occupe pas un octet du fichier (VirtualSize sans RawData) : compter ses
# symboles ferait passer les 1,5 Mo de tampons statiques pour de la taille d'exe.
NO_FILE_BYTES = (".bss",)


def sizes(sections, symbols):
    """Taille par symbole = ecart jusqu'au suivant, borne par la fin du bloc.

    Le bloc est la ligne de la table des sections qui contient le symbole
    (.text$mn, .rdata, .xdata...), pas la section PE entiere : sinon le dernier
    symbole de .text absorbe tout .rdata."""
    blocks = sorted(sections, key=lambda t: (t[0], t[1]))

    def block_of(sec, off):
        for b_sec, start, length, name, _cls in blocks:
            if b_sec == sec and start <= off < start + length:
                return name, start + length
        return None, None

    ordered = sorted(symbols, key=lambda t: (t[0], t[1]))
    out = []
    for i, (sec, off, _rva, name) in enumerate(ordered):
        block_name, block_end = block_of(sec, off)
        if block_name is None or block_name in NO_FILE_BYTES:
            continue
        size = block_end - off
        if i + 1 < len(ordered) and ordered[i + 1][0] == sec:
            size = min(size, ordered[i + 1][1] - off)
        out.append((name, block_name, off, max(0, size)))
    return out


def report(map_path, exe_path):
    sections, symbols = parse_map(map_path)
    entries = sizes(sections, symbols)

    per_module = {}
    for name, _sec, _off, size in entries:
        module = classify(name)
        per_module[module] = per_module.get(module, 0) + size

    total = sum(per_module.values())
    exe_size = os.path.getsize(exe_path) if exe_path and os.path.exists(exe_path) else 0

    print()
    print("=== sections ===")
    for _sec, _start, length, name, cls in sections:
        flag = "  (pas d'octets fichier)" if name in NO_FILE_BYTES else ""
        print("  %-14s %-6s %9d o%s" % (name, cls, length, flag))
    print()
    print("=== taille par module (somme des symboles publics) ===")
    print("  %-28s %10s  %6s" % ("module", "octets", "%"))
    for module, size in sorted(per_module.items(), key=lambda kv: -kv[1]):
        pct = (100.0 * size / total) if total else 0.0
        print("  %-28s %10d  %5.1f%%" % (module, size, pct))
    print("  %-28s %10d" % ("TOTAL symboles", total))
    if exe_size:
        print("  %-28s %10d" % ("exe sur disque", exe_size))
    print()
    print("=== top 40 symboles ===")
    print("  %-52s %9s  %s" % ("symbole", "octets", "module"))
    for name, _sec, _off, size in sorted(entries, key=lambda t: -t[3])[:40]:
        print("  %-52s %9d  %s" % (name[:52], size, classify(name)))
    print()

    dump = {
        "exe": exe_size,
        "modules": per_module,
        "top": [{"name": n, "size": s} for n, _c, _o, s in
                sorted(entries, key=lambda t: -t[3])[:80]],
    }
    out_path = os.path.splitext(map_path)[0] + "_size.json"
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump(dump, handle, indent=1, sort_keys=True)
    print("  -> %s" % out_path)
    return 0


def diff(before_path, after_path):
    with open(before_path, encoding="utf-8") as handle:
        before = json.load(handle)
    with open(after_path, encoding="utf-8") as handle:
        after = json.load(handle)
    keys = sorted(set(before["modules"]) | set(after["modules"]))
    print("  %-28s %10s %10s %10s" % ("module", "avant", "apres", "delta"))
    for key in keys:
        b = before["modules"].get(key, 0)
        a = after["modules"].get(key, 0)
        print("  %-28s %10d %10d %+10d" % (key, b, a, a - b))
    print("  %-28s %10d %10d %+10d" % ("exe", before["exe"], after["exe"],
                                       after["exe"] - before["exe"]))
    return 0


def main(argv):
    if len(argv) >= 4 and argv[1] == "--diff":
        return diff(argv[2], argv[3])
    if len(argv) < 2:
        print("usage: size_report.py <map> [exe] | --diff <a.json> <b.json>")
        return 1
    return report(argv[1], argv[2] if len(argv) > 2 else None)


if __name__ == "__main__":
    sys.exit(main(sys.argv))

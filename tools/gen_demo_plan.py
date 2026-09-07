#!/usr/bin/env python3
# gen_demo_plan.py - genere un plan de demonstration au format texte `.mdplan.txt`
# (celui de plan_file.c, une ligne par piste, champs separes par des tabulations)
# pour les captures de livraison de T-032.
#
#   python tools/gen_demo_library.py --out %TEMP%\minidisk_t032
#   python tools/gen_demo_plan.py --library %TEMP%\minidisk_t032 --out build\demo.mdplan.txt
#   build\minidisk.exe --scan %TEMP%\minidisk_t032 --plan build\demo.mdplan.txt
#
# Les chemins pointent sur la bibliotheque generee : au chargement, `plan_resolve`
# les retrouve et les pistes ne sont pas marquees manquantes (sauf --missing, qui
# en casse une expres pour la capture de l'indicateur).
import argparse
import os

MODES = ["SP", "SP", "SP", "SP-MONO", "LP2", "LP2", "LP4", "SP"]


def collect(library):
    files = []
    for root, _dirs, names in os.walk(library):
        for name in sorted(names):
            if name.lower().endswith((".wav", ".mp3", ".flac", ".m4a", ".ogg", ".aif")):
                files.append(os.path.join(root, name))
    files.sort()
    return files


def title_of(path):
    name = os.path.splitext(os.path.basename(path))[0]
    return name[3:] if name[:2].isdigit() else name


def album_of(path):
    return os.path.basename(os.path.dirname(path))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--count", type=int, default=20)
    parser.add_argument("--duration-ms", type=int, default=251000)
    parser.add_argument("--groups", action="store_true", help="un groupe par album")
    parser.add_argument("--missing", action="store_true", help="une piste au chemin casse")
    parser.add_argument("--title", default="Compil voiture")
    args = parser.parse_args()

    files = collect(args.library)
    if not files:
        raise SystemExit("aucun fichier audio dans %s" % args.library)
    # Plus de pistes que de fichiers : on boucle, ce qui est exactement ce que
    # fait un utilisateur qui met deux fois le meme morceau (Ctrl force le
    # doublon, MI-06).
    while len(files) < args.count:
        files = files + files

    lines = ["# minidisk plan 1", "plan\t%s" % args.title]
    lines.append("disc\t%s\t80\tSP" % args.title)

    albums = []
    for path in files[: args.count]:
        album = album_of(path)
        if album not in albums:
            albums.append(album)
    if args.groups:
        for index, album in enumerate(albums):
            lines.append("group\t%d\t%s" % (index, album))

    for index, path in enumerate(files[: args.count]):
        mode = MODES[index % len(MODES)]
        group = str(albums.index(album_of(path))) if args.groups else "-"
        title = ""  # vide : le titre vient de la bibliotheque, comme a l'usage
        if index == 3:
            title = "%s (feat. Someone) [2011 Remaster]" % title_of(path)
        entry_path = path
        if args.missing and index == 5:
            entry_path = path + ".introuvable"
        lines.append(
            "track\t%s\t%d\t%s\t-\t0\t0\t0\t0\t%s\t%s"
            % (mode, args.duration_ms, group, title, entry_path)
        )

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as out:
        out.write("\n".join(lines) + "\n")
    print("%d pistes dans %s" % (min(args.count, len(files)), args.out))


if __name__ == "__main__":
    main()

# -*- coding: utf-8 -*-
"""i18n_audit.py - aucune chaine visible par l'utilisateur hors de strings.h (T-072).

Regle : dans src/app/, tout `str8_lit("...")` dont la partie *affichee* contient
une lettre ASCII doit venir de la table de strings. Ce que le script accepte :

  - la partie apres "###" : c'est une cle de box, jamais dessinee ;
  - une chaine sans aucune lettre ASCII (glyphes UTF-8, "%u / %u", "###foo") ;
  - un jeton technique, c'est-a-dire une chaine sans le moindre espace : une cle
    de preferences ("library.thumbnails"), une extension ("*.mdplan"), une URL,
    un drapeau de ligne de commande, un identifiant de box ;
  - les quelques chaines listees dans ALLOW ci-dessous, avec leur raison.

Lance par `build.bat check`. Code de retour 1 des la premiere infraction.

    python tools/i18n_audit.py [racine]
"""
import io
import os
import re
import sys

# Les chaines a espaces qui ne sont pas de l'interface : arguments de ligne de
# commande, en-tete du fichier de preferences, deux abandons ecrits sur le
# debugger avant que la moindre fenetre existe (donc avant toute langue).
ALLOW = {
    "--scan ": "argument de ligne de commande",
    "--query ": "argument de ligne de commande",
    "--plan ": "argument de ligne de commande",
    "--netmd-trace ": "argument de ligne de commande",
    "# minidisk preferences": "commentaire ecrit dans le fichier de preferences",
    "minidisk: OpenGL 3.3 core is required, aborting\\n":
        "os_debug_print avant l'existence de la fenetre",
    "minidisk: no usable system font, aborting\\n":
        "os_debug_print avant l'existence de la fenetre",
}

LITERAL = re.compile(r'str8_lit\(\s*"((?:[^"\\]|\\.)*)"\s*\)')
ESCAPE = re.compile(r'\\x[0-9A-Fa-f]{2}|\\u[0-9A-Fa-f]{4}|\\.')
SPECIFIER = re.compile(r'%[0-9]*(?:ll|l|z)?[diuxcfsS%]')
LETTER = re.compile(r'[A-Za-z]')


def visible_part(literal):
    """Ce que l'utilisateur lit : "Label###id" ne montre que "Label"."""
    return literal.split("###", 1)[0]


def is_ok(literal):
    if literal in ALLOW:
        return True
    text = visible_part(literal)
    if text == "":
        return True
    text = ESCAPE.sub(" ", text)
    text = SPECIFIER.sub(" ", text)
    if not LETTER.search(text):
        return True
    # Un jeton technique n'a pas d'espace : une phrase en a toujours un.
    return " " not in literal and "\\t" not in literal


def audit(root):
    app_dir = os.path.join(root, "src", "app")
    failures = []
    scanned = 0
    for name in sorted(os.listdir(app_dir)):
        if not name.endswith((".c", ".h")) or name == "strings.h":
            continue
        path = os.path.join(app_dir, name)
        scanned += 1
        with io.open(path, encoding="utf-8") as handle:
            for number, line in enumerate(handle, 1):
                for match in LITERAL.finditer(line):
                    literal = match.group(1)
                    if not is_ok(literal):
                        failures.append((name, number, literal))
    return scanned, failures


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(
        os.path.abspath(__file__)))
    scanned, failures = audit(root)
    for name, number, literal in failures:
        sys.stdout.write(
            '  ERREUR: src/app/%s(%d): chaine visible hors de strings.h : "%s"\n'
            % (name, number, literal))
    if failures:
        sys.stdout.write("  %d chaine(s) a migrer vers Str_* (docs/CONVENTIONS.md)\n"
                         % len(failures))
        return 1
    sys.stdout.write("  i18n: %d fichier(s) de src/app/, aucune chaine visible en dur\n"
                     % scanned)
    return 0


if __name__ == "__main__":
    sys.exit(main())

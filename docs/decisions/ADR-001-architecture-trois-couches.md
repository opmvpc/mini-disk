# ADR-001 — Architecture en trois couches, core sans dépendance plateforme

Statut : **accepté** (2026-09-06)

## Contexte
Le core (bibliothèque, plan, pipeline, NetMD, ATRAC3) doit être réutilisé tel quel sur Linux et macOS.
L'UI est un renderer OpenGL maison. Le couplage est l'ennemi numéro un du portage.

## Décision
- `base/` : types, arènes, String8, hash, math/SIMD, jobs, log. Aucune dépendance OS hors `platform.h`.
- `core/` : logique métier pure. Interdit d'inclure `windows.h`, `gl*`, `malloc`, ou quoi que ce soit de `ui/`.
  Expose des structs plates + fonctions pures + files de commandes. Testable en CLI.
- `platform/` : `platform.h` (contrat unique, ~90 fonctions, cf. research/03 §10) + `win32/` maintenant,
  `linux/`, `macos/` plus tard. Contient fenêtre, contexte GL, fichiers, threads, USB, audio, DirectWrite/MF.
- `ui/` : moteur UI + renderer GL + widgets. Dépend de `base/` et lit le core en lecture seule via snapshots.
- `app/` : glue et boucle principale.
- `build.bat check` vérifie mécaniquement les interdictions par grep (cf. research/03 §10).

## Alternatives rejetées
- Monolithe "tout dans main.c" : rapide au début, portage impossible ensuite.
- Interface plateforme en vtable de pointeurs de fonction : inutile, on compile une seule plateforme par binaire.

## Conséquences
- Le protocole NetMD s'écrit contre `UsbTransport` (ADR-011), rejouable sans device.
- Le portage Linux est estimé à ~2 000 lignes de `platform/linux` uniquement.

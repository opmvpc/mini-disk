# T-010 — Scan de bibliothèque multi-thread et modèle de données SoA

Phase 2 · Statut : **todo** · Dépend de : T-008, ADR-001, ADR-010

## Objectif
Le module `core/library` : parcourir des dossiers, collecter les fichiers audio, produire un index SoA
en mémoire. Aucune lecture de tags ici (T-011) : ce ticket pose le modèle et le scan.

## Livrables
- `platform.h` : `os_dir_iter_begin/next/end` (nom UTF-8, taille, mtime, is_dir), `os_file_stat`,
  `os_file_open/read_at/close` (lecture aléatoire, sans CRT), `os_path_*` (join, extension, parent,
  normalisation `\`/`/`), `os_known_folder(Music)`.
- `src/core/library/lib_model.h` : `Library` SoA : tables parallèles (`path_id`, `size`, `mtime`,
  `duration_ms`, `sample_rate`, `channels`, `codec`, `title_id`, `artist_id`, `album_id`, `album_artist_id`,
  `genre_id`, `track_no`, `disc_no`, `year`, `replaygain_track_db`, `cover_hash`, `flags`), `StringTable`
  internée (hash → offset, UTF-8, dédoublonnage), `TrackId` = u32 stable, tombstones pour les suppressions.
- `src/core/library/lib_scan.c` : scan récursif par jobs (T-008) — un job par sous-dossier, résultats
  fusionnés dans une file lock-free vers le thread principal ; filtre sur extensions
  (`mp3 flac wav aif aiff ogg m4a aac alac wma opus`) ; détection des changements par (size, mtime) pour
  un rescan incrémental ; annulation coopérative ; progression (fichiers vus / dossiers restants).
- `src/core/library/lib_events.h` : file d'événements core → UI (`Lib_ScanProgress`, `Lib_ScanDone`,
  `Lib_TracksAdded`, `Lib_TracksRemoved`) consommée par `app.c` sans jamais bloquer.
- Démo : bouton "Ajouter un dossier" (dialogue `os_dialog_pick_folder` via `IFileDialog`, COM en C,
  `ole32`/`shell32` dynamiques) + affichage de la progression et de la liste des chemins.
- Tests : `tests/test_library.c` — StringTable (interning, collisions), SoA add/remove/tombstones,
  scan d'une arborescence factice générée dans `%TEMP%` (500 fichiers, 30 dossiers), rescan incrémental
  (1 fichier modifié → 1 seul rescan), annulation.
- Banc : scan de 50 000 fichiers factices (0 octet) < 2 s à froid, < 300 ms à chaud.

## Critères d'acceptation
- 100 000 pistes en mémoire < 40 MB (hors chaînes) ; aucune allocation par piste hors arènes.
- Le scan n'empêche jamais l'UI de rendre à 60 fps ; annulation < 100 ms.
- `core/` toujours sans `windows.h` (`build.bat check`). Exe < 160 KB. Tests, check, analyze verts.

# T-010 — Scan de bibliothèque multi-thread et modèle de données SoA

Phase 2 · Statut : **fait** · Dépend de : T-008, ADR-001, ADR-010

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

## Livraison (2026-09-06)

### Ce qui est livré
- **`platform.h` (système de fichiers)** : `OsDirIter` (poignée + tampon `WIN32_FIND_DATAW` dans la
  struct, le nom UTF-8 est une tranche de l'itérateur → **zéro allocation par entrée**),
  `os_dir_iter_begin/next/end`, `os_file_stat`, `os_dir_create`, `os_file_delete`, `os_dir_delete`,
  `os_file_open/read_at/close` (lecture aléatoire par `OVERLAPPED`, plusieurs threads sur le même
  fichier), `os_path_is_separator/join/parent/filename/extension/normalize`, `os_known_folder`
  (Music, LocalAppData, Temp — `shell32`/`ole32` chargés à la main), `os_command_line`.
  Implémentation : `src/platform/win32/win32_file.c`.
- **`os_dialog_pick_folder`** (`win32_dialog.c`) : `IFileDialog` avec `FOS_PICKFOLDERS`, COM en C,
  vtables déclarées à la main comme `win32_font_dwrite.c` (P-006), `ole32`/`shell32` en
  `LoadLibraryW` → **la table d'imports reste kernel32 + user32**.
- **`core/library/lib_model.h/.c`** : SoA de 19 colonnes (`LIB_BYTES_PER_TRACK` = **74 octets**),
  un seul bloc qui double et se découpe par alignement décroissant, `TrackId` stable, tombstones
  chaînées dans `path_id` (la liste libre ne coûte pas une colonne), index `hash(chemin) → id`
  en adressage ouvert avec réinsertion du run à la suppression, `StringTable` internée
  (préfixe u16 + UTF-8, arène de texte dédiée pour rester contiguë).
- **`core/library/lib_scan.c`** : un job par sous-dossier (`jobs_push` depuis les jobs eux-mêmes),
  allocateur bump **sans verrou** (256 MB réservés, committés par tranches de 4 MB) pour les chemins
  et les lots, pile de Treiber d'**offsets** (jamais réutilisés dans un scan → pas d'ABA) comme file
  de fusion vers le thread principal, lots de 64 entrées, filtre d'extensions, rescan incrémental par
  `(taille, mtime)`, balayage des disparus par génération, annulation coopérative (un `store`).
- **`core/library/lib_events.h`** : anneau de 64 événements, producteurs qui **abandonnent** plutôt
  que d'attendre. `TracksAdded/Removed` portent un compte et pas une plage : les tombstones rendent
  les ids non contigus.
- **Démo** : bouton « Ajouter un dossier », « Annuler le scan » pendant le scan, progression
  (fichiers vus, dossiers faits / connus) dans le sous-titre du panneau, liste des chemins
  (fichier + dossier relatif à la racine ajoutée). Sans dossier ajouté, la démo générée de T-007
  est intacte.
- **Tests** (`tests/test_library.c`, 5 cas / 76 `EXPECT`) : table de chaînes (interning, 4 000
  entrées, croissance des slots, dédoublonnage), SoA add/remove/tombstone réutilisée/croissance,
  helpers de chemins et filtre d'extensions, scan d'un arbre réel de **500 fichiers / 30 dossiers**
  dans `%TEMP%` (dont un sur dix non audio), rescan incrémental (0 ajout, 0 modif), puis
  1 modifié + 1 supprimé + 1 ajouté → **1 / 1 / 1** et la tombstone réutilisée, annulation.
- **Banc** (`bench_library_scan`) : 50 000 fichiers dans 500 dossiers.

### Mesures (i7-8550U, 4 c / 8 t, Windows 11, 7 workers)
| Mesure | Valeur | Cible |
|--------|--------|-------|
| Exe release | **123 904 o** (+16 896 o vs T-008) | < 160 KB (CI 250 KB) |
| Imports | kernel32 + user32 | ces deux-là |
| Scan 50 000 fichiers, à froid | **168,5 ms** | < 2 s |
| Scan 50 000 fichiers, à chaud (incrémental) | **63,5 ms** | < 300 ms |
| Mémoire 100 000 pistes (hors chaînes) | **7,4 MB** de colonnes (14,8 MB au pire pendant un doublement) + 1 MB d'index | < 40 MB |
| Chaînes internées | 3 466 KB pour 50 000 chemins | — |
| Allocation par piste hors arène | **aucune** | aucune |
| Annulation | < 100 ms (vérifiée par test) | < 100 ms |
| CPU au repos, 12 s, après un scan | **15,6 ms** (3 mesures : 78,1 / 62,5 / 15,6 — quantifiées au tick de 15,625 ms) | 0 % |
| Tests | 5 cas / 76 checks ajoutés ; **79 cas / 1 477 checks, 0 échec** au total | verts |
| `build.bat check` | vert | vert |
| `build.bat analyze` | vert (`/analyze` + clang-tidy) | vert |

Capture : `build/demo.png` — 144 pistes scannées, chemins listés, bouton et progression.

### Écarts et décisions
1. **`os_dialog_pick_folder(arena, title)` sans fenêtre parente** : la boîte est modale sur
   `GetActiveWindow()`. Passer `OsWindow` obligeait à un cast entier → pointeur que `clang-tidy`
   (`performance-no-int-to-ptr`) refuse ; `OsFile` et `OsDirIter` portent donc un `void *` et non un `u64`.
2. **Ajout non demandé : `os_command_line` et le drapeau `--scan <dossier>`**, qui prend le même
   chemin que le bouton sans dialogue. C'est ce qui rend la capture et un scan reproductibles depuis
   un script ; sans lui il faudrait piloter une boîte de dialogue Win32 pour vérifier quoi que ce soit.
3. **Durées affichées** : estimées depuis la taille à 128 kbit/s tant que T-011 n'a pas lu les tags.
   Le champ `duration_ms` du SoA existe et reste à 0.
4. **Le scan réserve 256 MB de virtuel** et n'en commit que ce qu'il touche (≈ 6 MB pour 50 000
   fichiers). L'épuisement du bloc est une erreur de domaine : `exhausted` est levé, le scan s'annule.
5. **`C:\Users\admin\Music` est vide** sur cette machine : la capture porte sur un arbre généré
   (6 artistes × 3 albums × 8 pistes = 144 fichiers) dans `%TEMP%\minidisk_demo`.
6. **Piège** : supprimer des fichiers pendant qu'une recherche `FindFirstFileW` est ouverte sur le
   même dossier fait **sauter des entrées** à `FindNextFileW` — le nettoyage des arbres factices
   (tests et banc) liste d'abord, supprime ensuite. Le scanner lui-même ne fait que lire.
7. **Boîte de dialogue vérifiée en vrai** : clic sur « Ajouter un dossier », navigation vers
   `C:\Windows\Media`, sélection → **80 fichiers .wav scannés**, sous-dossier `dm` compris ;
   `Show()` rend `S_OK` à la sélection et `0x800704C7` (annulé) sur Échap, sans rien changer.
   `C:\Users\admin\Music` étant vide, la capture livrée porte sur l'arbre généré.
8. **`clang-tidy`** a dicté deux choix : poignées `void *` plutôt que `u64`
   (`performance-no-int-to-ptr`) et pas de `(void *)` intermédiaire sur `GetProcAddress`
   (`bugprone-casting-through-void`). Aucune règle n'a été désactivée.
9. Aucun fichier de T-009 (`r_core.c`, `r_gl.c`, `r_atlas.c`, `tests/test_render.c`) n'a été touché.

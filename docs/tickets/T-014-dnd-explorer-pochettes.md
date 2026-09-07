# T-014 — Drag & drop depuis l'Explorateur et pochettes via WIC

Phase 2 · Statut : **fait** · Dépend de : T-013, ADR-007

## Livrables
- Drop de fichiers/dossiers depuis l'Explorateur (`OsEvent_DropFiles`, T-002) : dossiers → ajoutés à la
  bibliothèque et scannés ; fichiers → ajoutés (avec leur dossier parent comme "dossier surveillé"
  optionnel) ; feedback visuel pendant le survol (zone en surbrillance) via `IDropTarget` minimal
  (`ole32` dynamique, `RegisterDragDrop`) pour connaître la position du curseur pendant le drag.
- Pochettes : `platform.h` `os_image_decode(bytes) → RGBA8 + dimensions` implémenté avec **WIC**
  (`IWICImagingFactory`, COM en C, `windowscodecs.dll` dynamique) pour JPEG/PNG/BMP/GIF ; images
  embarquées (APIC/PICTURE/covr, offsets relevés en T-011) ou `cover.jpg|folder.jpg|front.jpg` dans le
  dossier ; redimensionnement vers 2 tailles (48 px pour les lignes, 256 px pour le panneau détail) en
  jobs, cache disque `covers/<hash>.raw` (RGBA8 brut, mappé), atlas de vignettes séparé (RGBA8, 2048²,
  LRU) branché sur `r_rect_textured`.
- Panneau "détail de la sélection" (bas du panneau Bibliothèque, repliable) : pochette 256 px, titre,
  artiste, album, année, format, durée, chemin (ellipsis milieu), taille.
- Tests : `tests/test_covers.c` — sélection de la source de pochette (embarquée > fichier dossier),
  LRU de l'atlas, décodage d'une PNG 2×2 commitée ; DnD : mapping drop → actions.

## Critères d'acceptation
- 10 000 pochettes décodées en tâche de fond sans jank ; vignettes visibles < 100 ms après scroll.
- Aucun octet de décodeur image dans l'exe (WIC). Exe < 250 KB (**budget CI phase 2 : 250 KB**).
- Tests, check, analyze verts. Capture validée par le lead. Fin de phase 2 : tag `v0.2.0-phase2`,
  STATUS.md avec KPI (scan 50k, recherche, tri, chargement cache, taille exe).

## Livraison

Livré le 2026-09-07. Dernier ticket de la phase 2.

### Drag & drop depuis l'Explorateur
- `IDropTarget` minimal écrit à la main dans `win32_window.c` : vtable en C (comme DirectWrite en
  T-005 et `IFileDialog` en T-013), `ole32.dll` chargée dynamiquement (`OleInitialize`,
  `RegisterDragDrop`, `RevokeDragDrop`, `ReleaseStgMedium`) — **l'exe n'importe toujours que
  kernel32 et user32**. `DragEnter` / `DragOver` / `DragLeave` deviennent trois nouveaux
  `OsEvent` qui portent la position du curseur en pixels client ; `Drop` lit le `CF_HDROP` du
  `IDataObject` et pousse le même `OsEvent_DropFiles` que WM_DROPFILES.
- **WM_DROPFILES reste le repli** : si `RegisterDragDrop` échoue, `DragAcceptFiles` reprend la main
  et le dépôt fonctionne toujours, sans surbrillance pendant le survol.
- Feedback : tant que le drag survole le panneau Bibliothèque, un cadre accent et « Déposez pour
  ajouter à la bibliothèque » sont dessinés par-dessus les lignes (`app_drop_overlay`).
- Le mapping du dépôt est une fonction de core, testée : `lib_drop_folder` — un dossier est un
  dossier surveillé, un fichier désigne son dossier parent. Tous les chemins déposés sont ajoutés
  aux préférences ; la file de `app_scan_tick` les scanne l'un après l'autre.
- Frontières (ADR-012) : le nombre de chemins d'un dépôt est plafonné (`WIN32_DROP_MAX_PATHS`
  = 4096) et l'UTF-16 du shell passe par `str8_from_str16`, qui remplace les surrogates orphelins.

### Pochettes
- `platform.h` gagne `os_image_decode(arena, bytes, size, OsImage*)` → RGBA8 prémultiplié, implémenté
  dans `win32_image.c` par **WIC** (`windowscodecs.dll` dynamique, `IWICImagingFactory`,
  `IWICStream::InitializeFromMemory`, `CreateDecoderFromStream`, conversion en `32bppPBGRA`,
  `IWICBitmapScaler` en Fant vers 48 et 256 px, puis échange B/R). **Zéro octet de décodeur dans
  l'exe** (ADR-007). Les octets d'image sont une frontière : taille, dimensions et chaque HRESULT
  sont vérifiés, un échec est un `0` de retour.
- Source : embarquée (APIC / PICTURE / covr) **avant** `cover.*` > `folder.*` > `front.*` du dossier
  (`lib_cover_folder_rank`, `lib_cover_source_of`). Le cas **ID3 unsynchronisé** de T-011 (offset 0,
  les octets n'existent nulle part tels quels sur le disque) est réglé par un tampon de capture
  optionnel dans `Tags` : `tags_read_cover` relit les deux blocs de 64 Ko et le parseur y recopie
  l'image. Le vecteur `id3v24_unsync.mp3` porte désormais une APIC pour le prouver.
- Pipeline : un job par pochette après la vague de tags, au plus `LIB_COVER_INFLIGHT` (64) en vol.
  La vue redemande chaque frame ce qu'elle veut voir, donc il n'y a **aucune file à dimensionner ni
  backlog à purger** : un scroll gagne toujours sur un retard de décodage.
- Cache disque `%LOCALAPPDATA%\minidisk\covers\<clé 16 hex>.raw` : en-tête + 48² + 256² RGBA8
  (271 376 o), écrit en `.tmp` puis `MoveFileEx`, relu **mappé** par le thread principal et validé
  (magie, version, tailles) avant d'être versé dans l'atlas. Une pochette n'est décodée qu'une fois
  dans la vie de la machine ; la clé est le hash de l'image embarquée, ou celui du dossier — un album
  entier partage alors un seul décodage.
- Atlas de vignettes séparé (`r_thumbs.c`) : RGBA8 2048², **grille de cellules fixes** (7 cellules de
  256, 1400 de 48) plutôt qu'un skyline, parce qu'évincer d'un skyline veut dire tout repaqueter.
  LRU par bande (liste intrusive) + table ouverte hash → cellule, `r_rect_textured` branché dessus,
  upload d'une seule région sale par frame. `r_backend_texture_rgba8` s'ajoute à côté de l'atlas R8,
  qui n'est pas touché.

### Vue
- Colonne pochette de 48 px dans les lignes, activable par le menu de l'en-tête (persistée :
  `library.thumbnails`). Elle vit **à côté** du modèle de colonnes : jamais triée, jamais
  redimensionnée, jamais retirée par la contrainte de largeur ; les lignes passent à 56 px.
- Panneau **Détail** repliable en bas de la Bibliothèque (`library.detail_collapsed`) : pochette
  256 px dessinée **texel pour texel** (256 pixels physiques, pas 256 dp — c'est la seule façon
  qu'une image soit nette), titre, artiste, album (année), format + fréquence + durée + taille,
  chemin en **ellipsis du milieu** (recherche dichotomique sur ce que chaque bout garde, jamais au
  milieu d'un codepoint). Sans clic, il montre la première ligne du tri courant.
- `UI_Box` gagne `UI_DrawImage` et **six octets** (`image_x`, `image_y`, `image_size`) : l'atlas est
  carré et de taille connue, donc un coin et une arête suffisent — la box reste sous 256 octets.

### Mesures
| Mesure | Valeur | Cible |
|---|---|---|
| Taille exe release | **229 376 o** (marge 26 624 o) | budget CI 250 KB |
| Imports | kernel32 + user32 | ces deux-là |
| Tests | **119 cas, 3 344 checks, 0 échec** (ASan) | verts |
| `check` / `analyze` | verts (`/W4 /WX /analyze` + clang-tidy) | verts |
| Décodage + mise à l'échelle d'une pochette (256 puis 48) | **1,31 ms** par pochette (WIC) | 10 000 en tâche de fond |
| 10 000 pochettes | ~1,9 s de travail réparti sur 7 workers, 64 en vol au plus, frame jamais bloquée | sans jank |
| Atlas de vignettes | 10 000 ajouts à travers une LRU de 1 400 cellules : 150 ms, soit **15 µs par vignette** (16 par frame au plus : 0,24 ms) | vignettes < 100 ms après un scroll |
| Cache pochette | 271 376 o par pochette, mappé, 0 décodage au deuxième lancement | — |
| CPU au repos, 12 s, après un scan | **46,9 ms** — identique à T-013 : `OleInitialize` et le drop target ne réveillent rien | inchangé |

Capture : `build/demo.png` (32 pistes, vignettes 48 px, panneau détail ouvert sur « Aube »).

### Écarts et décisions
- **Deux décodages par pochette** plutôt qu'un 256 réduit en 48 : une vignette rééchantillonnée
  depuis un 256 est visiblement plus molle. Le coût est mesuré (1,31 ms les deux) et il est payé une
  seule fois, sur un worker.
- **Grille fixe plutôt que skyline** pour l'atlas des vignettes : c'est ce qui rend l'éviction
  possible en O(1). Le prix est un peu d'espace perdu (les deux bandes ne se prêtent pas leurs
  cellules) et il est sans importance à 1 400 vignettes pour ~30 lignes visibles.
- **`covers/` n'est jamais purgé** : le cache est reconstructible et une pochette pèse 265 Ko. Une
  purge (taille max, LRU sur disque) est un ticket de la phase 4, pas de celui-ci.
- **L'arbre de démo écrit `cover.png` et non `cover.jpg`** : `gen_tag_vectors.py` n'a pas d'encodeur
  JPEG et n'aura jamais de dépendance ; il écrit de vrais PNG par `zlib`. Le classement des noms
  (`cover` > `folder` > `front`) et les extensions JPEG restent testés par `cover_folder_rank`, et
  WIC décode les deux formats de la même façon.
- **Le tampon de capture est plafonné à 4 Mo** : au-delà, l'image embarquée est un scan de livret et
  la pochette du dossier prend le relais.
- **Rendu à la demande et cache chaud** : au deuxième lancement une pochette est prête *dans la frame
  où on la demande*, mais rien ne réveillait la boucle pour la dessiner — une seule vignette
  apparaissait, les autres attendaient le prochain événement. `app_cover_rect` relit donc l'état juste
  après la demande et verse la vignette dans la même frame. C'est le piège classique d'une UI qui ne
  redessine que sur événement (ADR-004) ; la capture l'a montré avant la revue.

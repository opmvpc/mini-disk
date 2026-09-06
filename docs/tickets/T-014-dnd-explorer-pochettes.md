# T-014 — Drag & drop depuis l'Explorateur et pochettes via WIC

Phase 2 · Statut : **todo** · Dépend de : T-013, ADR-007

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

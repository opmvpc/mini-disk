# STATUS — où on en est

Dernière mise à jour : 2026-09-06 (soir)

## Phase actuelle : 2 · Bibliothèque — T-009 (optim draw calls) et T-010 (scan) en cours, en parallèle

### Fait (phase 0 terminée)
- 4 rapports de recherche livrés (`research/01..04`, ~10 700 lignes) + tokens de design (`02b`).
- Device identifié : **Sony MZ-N505** (PID 0084, Type-R). Driver WinUSB à installer via Zadig (P-001).
- ADR-001..012 acceptés (`decisions/README.md`) : architecture 3 couches, no-CRT, GL 3.3, UI Fleury,
  renderer SDF, texte DirectWrite, codecs + DSP, WinUSB rejouable, ATRAC3 clean-room, persistance binaire,
  décisions produit D1-D10, qualité (tests / analyse statique / zéro défensif).
- CI GitHub Actions (`.github/workflows/ci.yml`) : check → test → release-build (budget taille,
  `--selftest`) → release sur tag `v*`. **Rouge tant que T-001 n'a pas livré `build.bat`.**
- Backlog phases 1-8 (`tickets/BACKLOG.md`), T-001 détaillé.
- Repo GitHub : https://github.com/opmvpc/mini-disk

### En cours
- T-009 : renderer, 94 → < 10 draw calls par frame réelle (prompt `prompts/I-009`).
- T-010 : scan multi-thread, modèle SoA, StringTable, événements core → UI (prompt `prompts/I-010`).

### Fait en phase 1 (suite)
- T-008 livré : `platform.h` threads / sémaphores / SRW / atomiques (`win32_thread.c`), `base_jobs`
  (pool de N-1 workers, ring MPMC de Vyukov sans lock ni allocation, `jobs_dispatch` parallel-for,
  `jobs_wait` où le thread principal exécute des jobs, workers endormis sur sémaphore), overlay
  debug **F11** (fps + min/avg/max sur 120 frames avec graphe en barres, boxes, draw calls,
  vertices, atlas et remplissage, mémoire par arène, jobs et workers, DPI, taille fenêtre, réveils
  de `os_events_pump` et messages WndProc par identifiant). **910 ns par job vide à 7 workers**,
  4,35x de speedup sur un parallel-for borné calcul, 70 cas / **1 378 checks**, exe 107 008 o,
  imports kernel32+user32. **P-005 résolu** : 0 réveil et 0 message sur 12 s de repos, le CPU
  résiduel est sur un thread du pilote GL. Capture : `build/demo.png`.
- T-007 livré : `ui_theme` (tokens de research/02b, couleurs de mode SP/mono/LP2/LP4), `ui_widgets`
  (bouton, bouton icône, label, séparateur, champ texte UTF-8 avec sélection/presse-papiers/undo,
  **liste virtualisée 100 000 lignes à 110 boxes par frame**, splitter, tooltip 500 ms, menu contextuel
  au clavier), démo trois panneaux « Bibliothèque | Plan | Disque », 65 cas / **1 345 checks**,
  exe 97 792 o, imports kernel32+user32, 31 ms de CPU sur 12 s au repos. Capture : `build/demo.png`.
- T-006 livré : `ui_core` (clés hachées, piles de style, layout sémantique en 2 parcours, signaux, animations, 3 couches), démo 3 colonnes, **layout de 12 020 boxes en 338 µs** (28 ns/box, meilleur de 200 passes), 1 231 checks, exe 66 048 o, imports kernel32+user32, 15,6 ms de CPU sur 12 s au repos.
- T-005 livré et reviewé : DirectWrite → atlas R8, fallback système (japonais, katakana half-width), caches glyphes/mesure, ellipsis, chiffres tabulaires, 63 ns/glyphe à chaud, 1 143 checks, exe 54 272 o. P-006 (vtables COM en C).
- T-005 livré : DirectWrite chargé dynamiquement (vtables COM à la main), fallback système par
  `IDWriteFontFallback` (japonais et katakana demi-chasse), cache de glyphes (police, glyphe, quart
  de pixel) et cache de mesure, ellipsis, chiffres tabulaires, gamma du texte dans le shader,
  **1 143 checks**, exe 54 272 o, imports kernel32+user32, 46,9 ms de CPU sur 12 s au repos. P-006.
- T-004 livré et reviewé : file de commandes, batches (texture, clip, 16k quads), VBO persistant triple-buffered + fences, atlas skyline, rastériseur à couverture exacte, 8 icônes, **8 draw calls pour 2 200 quads**, 1 080 checks, exe 45 568 o.

### Fait en phase 1
- T-003 livré : contexte WGL 3.3 core, loader X-macro (48 fonctions), renderer SDF (1 shader, 1 VBO), démo 5 rects, 0,5 % CPU au repos mesuré en delta exact (P-005, ouvert), 491 checks, exe 33 792 o (cible du ticket < 30 KB non tenue, écart détaillé dans la Livraison de T-003).
- T-002 livré et reviewé : fenêtre complète, `OsEvent` ring, DPI v2, dark title bar, drop files, **0,0 % CPU au repos** (mesuré), 440 checks, exe 23 040 o (P-004).
- T-001 livré et reviewé : `base/`, `build.bat` (7 cibles), 125 checks, exe release **7 168 octets**, imports kernel32+user32. Voir P-003 (TLS sans CRT).

### Prochain pas
1. Review de T-008, tag `v0.1.0-phase1`, puis phase 2 (bibliothèque : T-010..T-017 à détailler).
2. P-007 : plus de tension immédiate (marge de 24 Ko sous le budget de 128 Ko) ; le levier `/O1`
   (−16,9 Ko mesuré) reste disponible quand la phase 2 grossira.
3. P-005 : clos. À re-mesurer sur la GeForce 930MX si l'occasion se présente.

### Action utilisateur requise
- Zadig → WinUSB sur "Net MD Walkman" avant la phase 3.

## KPI — fin de phase 1 (i7-8550U, 4 cœurs / 8 threads, Intel UHD 620, Windows 11)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **107 008 o**, marge 24 064 o | < 128 KB | 2026-09-06 |
| Imports | kernel32 + user32 | ces deux-là | 2026-09-06 |
| Fps / temps de frame (démo, overlay ouvert) | 60 fps vsync, frame typique 2,3 ms (min 1,6 / max 100 au réveil) | 60 fps | 2026-09-06 |
| Layout | 12 020 boxes en **493 µs** (41 ns/box, meilleur de 200 passes) | < 1 ms | 2026-09-06 |
| Liste virtualisée | 110 boxes pour 100 000 lignes | < 200 boxes | 2026-09-06 |
| Draw calls | **94** pour la frame de la démo (overlay ouvert, 1 499 quads) ; 8 sur le banc renderer de T-004 | < 10 | 2026-09-06 |
| Texte | 80 ns par glyphe à chaud | — | 2026-09-06 |
| Jobs | 910 ns par job vide (7 workers), speedup 4,35x sur un parallel-for | < 1 µs, > 3x | 2026-09-06 |
| CPU au repos, 12 s | 31 à 78 ms, **0 ms sur nos threads** (0 réveil, 0 message) ; le reste est un thread du pilote GL | 0 % | 2026-09-06 |
| Tests | 70 cas, **1 378 checks**, 0 échec (ASan) | verts | 2026-09-06 |
| Budget CI (`SIZE_BUDGET_KB`) | 128 KB (phase 2 : 250 KB ; `/O1` = −16,9 Ko reste un levier, cf. P-007) | — | 2026-09-06 |

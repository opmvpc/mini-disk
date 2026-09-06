# Backlog

Ordonné par priorité. Statuts : `todo` / `doing` / `review` / `done`. Les tickets de la phase courante
ont un fichier `T-NNN-*.md` ; les suivants sont des lignes à détailler au moment venu.

## Phase 1 — Fondations (cible : démo UI 60 fps, exe < 100 KB)

| Ticket | Titre | Statut |
|--------|-------|--------|
| [T-001](T-001-base-et-build.md) | `base/` (types, arena, string8, math, stubs CRT), `build.bat` debug/release/test/check/analyze, runner de tests, fenêtre noire < 8 KB, CI verte | done |
| [T-002](T-002-fenetre-win32.md) | `platform/win32` fenêtre : WndProc, `OsEvent`, boucle `MsgWaitForMultipleObjectsEx`, DPI v2, dark title bar, 0 % CPU au repos mesuré | doing |
| [T-003](T-003-wgl-gl33-quad-sdf.md) | Contexte WGL GL 3.3 core + loader + `r_gl` : quad SDF arrondi anti-aliasé | todo |
| T-004 | `r_core` : batches, VBO dynamique, scissor, atlas R8 + packing skyline | todo |
| T-005 | `win32_font_dwrite` + `ui_text` : cache de glyphes, mesure, "Hello 世界" | todo |
| T-006 | `ui_core` : UI_Box, clés, piles, layout 5 passes, signaux, animations | todo |
| T-007 | `ui_theme` (tokens 02b) + widgets : bouton, label, champ texte, liste virtualisée 100k lignes, splitter | todo |
| T-008 | `base_jobs` (thread pool) + overlay debug F11 (fps, draw calls, mémoire arènes) | todo |

## Phase 2 — Bibliothèque
T-010 scan récursif multi-thread · T-011 tags ID3/Vorbis/MP4/APE · T-012 index SoA + string table ·
T-013 recherche incrémentale · T-014 cache binaire mappé · T-015 vue bibliothèque (colonnes, tri, sélection) ·
T-016 drag & drop Explorer · T-017 pochettes via WIC.

## Phase 3 — Device (lecture)
T-020 WinUSB + énumération + hotplug · T-021 écran "driver manquant" · T-022 protocole de base (identification,
capacité, pistes, titres, groupes) · T-023 transport de rejeu + premières transcriptions MZ-N505 · T-024 vue Disque.

## Phase 4 — Plan & capacité
T-030 modèle `.mdplan` + undo/redo · T-031 jauge en clusters (validée sur device) · T-032 budget TOC + sanitize
titres · T-033 groupes depuis albums · T-034 vue Plan (réordonner, modes, DnD interne).

## Phase 5 — Pipeline & SP
T-040 minimp3/dr_flac/dr_wav/stb_vorbis · T-041 Media Foundation AAC/ALAC/WMA · T-042 resampler sinc ·
T-043 R128 + true-peak + dither · T-044 session sécurisée + upload SP · T-045 titrage post-upload ·
T-046 vue Transfert (progression, ETA, annulation, reprise) · T-047 cache de transcodage.

## Phase 6 — ATRAC3 & LP
T-050 QMF + MDCT + bitstream writer · T-051 frame silencieuse valide (ffmpeg décode) · T-052 quantif + Huffman ·
T-053 bit allocation · T-054 psy + gain control · T-055 LP4 joint stereo · T-056 upload LP2/LP4 · T-057 bancs perf.

## Phase 7 — Polish
T-060 raccourcis complets · T-061 i18n FR/EN · T-062 thème clair · T-063 toasts/confirmations/simulation TOC ·
T-064 prefs · T-065 barre de titre custom · T-066 IDropTarget.

## Phase 8 — Portabilité
T-070 `platform/linux` · T-071 `platform/macos`.

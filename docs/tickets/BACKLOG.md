# Backlog

Ordonné par priorité. Statuts : `todo` / `doing` / `review` / `done`. Les tickets de la phase courante
ont un fichier `T-NNN-*.md` ; les suivants sont des lignes à détailler au moment venu.

## Phase 1 — Fondations (cible : démo UI 60 fps, exe < 100 KB)

| Ticket | Titre | Statut |
|--------|-------|--------|
| [T-001](T-001-base-et-build.md) | `base/` (types, arena, string8, math, stubs CRT), `build.bat` debug/release/test/check/analyze, runner de tests, fenêtre noire < 8 KB, CI verte | done |
| [T-002](T-002-fenetre-win32.md) | `platform/win32` fenêtre : WndProc, `OsEvent`, boucle `MsgWaitForMultipleObjectsEx`, DPI v2, dark title bar, 0 % CPU au repos mesuré | done |
| [T-003](T-003-wgl-gl33-quad-sdf.md) | Contexte WGL GL 3.3 core + loader + `r_gl` : quad SDF arrondi anti-aliasé | done |
| [T-004](T-004-r-core-batches-atlas.md) | `r_core` : batches, VBO dynamique, scissor, atlas R8 + packing skyline | done |
| [T-005](T-005-texte-directwrite.md) | `win32_font_dwrite` + `ui_text` : cache de glyphes, mesure, "Hello 世界" | done |
| [T-006](T-006-ui-core.md) | `ui_core` : UI_Box, clés, piles, layout 5 passes, signaux, animations | done |
| [T-007](T-007-theme-et-widgets.md) | `ui_theme` (tokens 02b) + widgets : bouton, label, champ texte, liste virtualisée 100k lignes, splitter | done |
| [T-008](T-008-jobs-et-overlay-debug.md) | `base_jobs` (thread pool) + overlay debug F11 (fps, draw calls, mémoire arènes) | done |

## Phase 2 — Bibliothèque (cible : 100k pistes, recherche < 5 ms, exe < 250 KB)

| Ticket | Titre | Statut |
|--------|-------|--------|
| [T-009](T-009-draw-calls-tri-par-texture.md) | Optimisation renderer : 94 → < 10 draw calls (tri par clip/texture, banc frame réaliste) | done |
| T-015 | Fix : l'overlay F11 est positionné en unités mélangées (hors écran à DPI ≠ 1) ; unités logiques partout dans `ui_debug_overlay.c` | done |
| [T-010](T-010-scan-bibliotheque.md) | Scan multi-thread, modèle SoA + StringTable, dialogue dossier, événements core → UI | done |
| [T-011](T-011-tags-audio.md) | Tags ID3v1/v2, Vorbis, MP4, APE, WAV/AIFF + durées exactes, fuzz ASan | done |
| [T-012](T-012-index-recherche-cache.md) | Index triés, navigateur par colonnes, recherche incrémentale, cache `.mdlib` mappé | done |
| [T-013](T-013-vue-bibliotheque.md) | Vue Bibliothèque réelle : colonnes, tri, navigateur, recherche, états, prefs | done |
| [T-014](T-014-dnd-explorer-pochettes.md) | Drag & drop Explorateur (IDropTarget), pochettes WIC, panneau détail, tag v0.2.0 | done |

## Phase 3 — Device (lecture et édition) — prérequis : Zadig → WinUSB sur le MZ-N505

| Ticket | Titre | Statut |
|--------|-------|--------|
| [T-020](T-020-winusb-enumeration-hotplug.md) | WinUSB dynamique, énumération, control/bulk, hotplug, transport de rejeu, thread device, écran "driver manquant" | todo |
| [T-021](T-021-protocole-netmd-lecture.md) | Protocole NetMD lecture : trames/poll, identification, capacité, pistes, titres, groupes, charsets, transcriptions MZ-N505 | todo |
| [T-022](T-022-edition-disque.md) | Édition : renommer, déplacer, effacer, groupes, simulation + sauvegarde TOC, budget TOC, tag v0.3.0 | todo |

## Phase 4 — Plan & capacité (le cœur produit : D1, D2, D3)

| Ticket | Titre | Statut |
|--------|-------|--------|
| [T-030](T-030-modele-plan-undo.md) | Modèle `.mdplan` multi-disques, commandes réversibles, undo/redo, autosave, résolution des pistes | fait |
| [T-031](T-031-capacite-clusters-budget-toc.md) | Capacité en clusters, budget TOC 255×7, sanitize/raccourcissement des titres, titrage auto, first-fit | doing |
| [T-032](T-032-vue-plan-jauge.md) | Vue Plan : DnD interne, modes, groupes, jauge de capacité au pixel, barre TOC, tag v0.4.0 | todo |

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

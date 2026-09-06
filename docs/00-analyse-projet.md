# Analyse du projet — minidisk

> Statut : **v1.0** (2026-09-06). Les rapports research/01..04 sont intégrés via les ADR-001..012 ; les marqueurs `[R-xx]` renvoient aux rapports pour le détail.
> (sections marquées `[R-xx]` = à compléter/valider avec le rapport correspondant).

## 1. Vision

Un logiciel Windows natif, moderne, ultra-léger, pour gérer une bibliothèque musicale locale et **graver des
MiniDiscs** sur un enregistreur Sony NetMD branché en USB. L'équivalent de Web MiniDisc Pro, mais :

- un seul `.exe` portable (< 1 MB visé, sans installation, sans runtime, sans DLL fournie) ;
- UI OpenGL écrite from scratch, 60 fps quand ça bouge, 0 % CPU au repos ;
- un **core en C pur, indépendant de la plateforme** (Windows d'abord, Linux/macOS ensuite) ;
- pensé "workflow" : sélectionner → vérifier que ça tient → transcoder/optimiser → graver → titrer, sans friction.

Philosophie : Casey Muratori / RAD. Data-oriented, arènes mémoire, pas d'abstraction gratuite, pas de bloat.
On mesure tout (taille exe, temps de démarrage, temps de scan bibliothèque, débit d'upload).

## 2. Ce que le matériel impose

| Fait | Valeur | Source |
|------|--------|--------|
| Device branché | `USB\VID_054C&PID_0084` "Net MD Walkman", classe FF/00/00 | `Get-PnpDevice` |
| Modèle | **Sony MZ-N505**, SoC CXD2677 Type-R, firmware R1.3/1.4 | research/01 §1 |
| Driver | Absent (ProblemCode 28) → WinUSB via Zadig requis | problems/P-001 |
| Génération | NetMD (pas Hi-MD) → upload SP (PCM → ATRAC1 sur le device), LP2/LP4 (ATRAC3 encodé côté hôte) | `[R-01]` |
| Capacité 80 min | 80 min SP / 160 min LP2 / 320 min LP4, 254 pistes max, budget de caractères TOC | `[R-01]` |

## 3. Ce que la machine de dev impose

- Windows 11 Pro 26200, i7-8550U (4c/8t, AVX2), 15 GB RAM, GPU Intel UHD 620 + GeForce 930MX (OpenGL 4.6 dispo sur les deux).
- MSVC 14.44 (VS Build Tools 2022) + clang-cl + Windows SDK 10.0.26100. Pas de gcc/tcc. Git présent.
- **Baseline mesurée** : fenêtre Win32 sans CRT (`/NODEFAULTLIB /ENTRY:entry_point /MERGE:.rdata=.text /ALIGN:16`) =
  **1 536 octets**, se lance et se ferme proprement (scratch `smoke/min.c`). Tout ce que pèsera l'exe final sera donc
  du code à nous ; objectif < 1 MB très réaliste, < 500 KB probable hors police embarquée.

## 4. Architecture cible : trois couches, zéro couplage

```
+----------------------------------------------------------------+
|  app/ (glue)         main loop, wiring core <-> ui <-> platform |
+--------------+-------------------------------+-----------------+
|  ui/         |  core/                        |  platform/      |
|  widgets,    |  library (scan, tags, index)  |  platform.h     |
|  layout,     |  plan (burn list, capacity)   |  win32/  (v1)   |
|  renderer GL |  pipeline (decode>dsp>encode) |  linux/  (v2)   |
|  text, theme |  netmd (protocole, session)   |  macos/  (v2)   |
|              |  codecs (mp3/flac/wav/ogg/..) |                 |
|              |  atrac3 (encodeur LP2/LP4)    |  fournit: fs,   |
|              |  persist (cache, plans, prefs)|  threads, usb,  |
|              |                               |  audio out, gl, |
|  depend de:  |  depend de: base/, platform.h |  clock, dialogs |
|  base/, core |  (interface), RIEN d'autre    |                 |
+--------------+-------------------------------+-----------------+
|  base/   types, arena, string8, hash, math, simd, log, assert  |
+----------------------------------------------------------------+
```

Règles :
- `core/` ne connaît ni Win32, ni OpenGL, ni l'UI. Il expose des **données** (structs plates) et des **commandes**
  (fonctions pures ou file de jobs). Testable en ligne de commande sans fenêtre.
- `platform.h` est l'unique contrat vers l'OS : `os_file_*`, `os_thread_*`, `os_usb_*`, `os_audio_*`, `os_time_*`,
  `os_dialog_*`. Une implémentation par OS. Le renderer GL est dans `ui/` mais la création du contexte est dans `platform/`.
- `ui/` lit l'état du core (lecture seule, snapshot par frame) et pousse des commandes. Jamais de logique métier dans l'UI.
- Le protocole NetMD est écrit contre une interface `UsbTransport` (control/bulk transfer) → testable avec un faux
  transport qui rejoue des transcriptions de vraies sessions.

## 5. Modules du core (v1)

| Module | Responsabilité | Notes perf |
|--------|----------------|------------|
| `library` | scan récursif, lecture tags (ID3v1/v2, Vorbis, MP4, APE), index SoA, recherche incrémentale, tri, cache binaire mappable | 100k pistes cibles ; scan multi-thread ; string table interned |
| `plan` | la "burn list" : ordre, mode par piste (SP/LP2/LP4/mono), jauge de capacité exacte, budget titres, groupes, split multi-disques | calcul O(n) à chaque édition, instantané |
| `pipeline` | decode → resample 44.1k → normalisation (R128) → trim/fade/gap → dither 16-bit → PCM BE (SP) ou ATRAC3 (LP) | job system, SIMD, buffers réutilisés, ≥ 50x temps réel |
| `codecs` | MP3 (minimp3), FLAC (dr_flac), WAV/AIFF (dr_wav/maison), OGG (stb_vorbis), AAC/ALAC/WMA via Media Foundation (0 octet embarqué) | `[R-03]` valider Opus |
| `atrac3` | encodeur ATRAC3 LP2/LP4 en C (port d'atracdenc ou clean-room contre ffmpeg) | `[R-04]` gros risque, à cadrer |
| `netmd` | énumération, identification, lecture disque (titre, pistes, temps), session sécurisée, upload chiffré DES, titrage, move/erase, groupes | thread device dédié, `[R-01]` |
| `persist` | cache bibliothèque, plans sauvegardés, préférences | fichiers binaires versionnés, pas de SQLite |

## 6. UI (v1) — `[R-02]` à consolider

Hypothèse de départ : layout 3 panneaux **Bibliothèque | Plan de disque | Disque/Device**, jauge de capacité live
en bas du plan, transfert avec progression par piste + ETA. Tout au clavier possible. Thème sombre par défaut.
Rendu : 1 shader, 1 VBO, atlas glyphes+icônes, rounded rects SDF en fragment shader, listes virtualisées, rendu à la
demande. Texte : `[R-03]` choisir entre stb_truetype + police embarquée subsetée et rasterisation via DirectWrite
dans notre atlas (0 octet de police, fallback Unicode/katakana gratuit).

## 7. Plan par phases

| Phase | Livrable | Critère de sortie |
|-------|----------|-------------------|
| 0 · Recherche & analyse | ce doc + 4 rapports + ADR fondateurs + backlog | ADR-001..00N acceptés, backlog priorisé |
| 1 · Fondations | `base/`, `platform/win32` (fenêtre, GL, input, DPI, threads, fs), `build.bat`, renderer + texte + 5 widgets | démo UI 60 fps, exe < 100 KB |
| 2 · Bibliothèque | scan, tags, index, recherche, liste virtualisée, cache | 50k pistes scannées et affichées, recherche < 5 ms |
| 3 · Device (lecture) | WinUSB, énumération, lecture disque/titres/temps, hotplug, écran "driver manquant" | le disque inséré s'affiche correctement |
| 4 · Plan & capacité | burn list, modes, jauge exacte, budget titres, groupes | jauge validée contre le device réel |
| 5 · Pipeline & SP | décodeurs, resampler, R128, dither, upload SP PCM chiffré, titrage | une piste MP3 → disque en SP, titre OK |
| 6 · ATRAC3 & LP | encodeur ATRAC3 C, upload LP2/LP4 | round-trip ffmpeg OK, piste LP2 lisible sur le walkman |
| 7 · Polish | undo/redo, DnD, raccourcis, i18n FR/EN, toasts, thème clair, prefs | checklist QoL de R-02 cochée |
| 8 · Portabilité | `platform/linux` (X11/Wayland+EGL, libusb ou usbfs), macOS ensuite | core compile sans modif |

## 8. Risques identifiés (à raffiner avec les rapports)

1. **Encodeur ATRAC3** : seul encodeur open = atracdenc (C++). Port en C = effort et risque qualité. Mitigation :
   livrer SP d'abord (phase 5), LP en phase 6 ; évaluer licence et qualité `[R-04]`.
2. **Protocole NetMD sécurisé** (EKB, DES, session) : fragile, mal documenté, comportements par modèle. Mitigation :
   transport rejouable + transcriptions capturées de netmd-js pour comparer byte à byte.
3. **Driver WinUSB** : friction utilisateur (Zadig). Mitigation : détection + écran guidé (P-001).
4. **Texte/Unicode dans un renderer maison** : katakana half-width, accents, ellipsis, DPI. Mitigation : `[R-03]`.
5. **Media Foundation en C (COM)** pour AAC/ALAC : verbeux mais faisable ; sinon fallback "non supporté" propre.
6. **Ambition** : beaucoup de modules. Mitigation : phases strictement séquentielles avec démo fonctionnelle à chaque sortie.

## 9. Méthode de travail

- Moi (Fable) = lead/architecte : analyse, ADR, tickets, super-prompts, review des livraisons, mise à jour de STATUS.md.
- Opus = implémenteurs : un ticket par agent, prompt archivé dans `docs/prompts/I-NNN-*.md`, livraison = code + note
  dans le ticket + entrée `problems/` si un obstacle a été rencontré.
- Chaque phase se termine par : build release, mesure taille exe, mise à jour STATUS.md, commit.

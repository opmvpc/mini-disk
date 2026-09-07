# STATUS — où on en est

Dernière mise à jour : 2026-09-07

## Phase actuelle : 4 · Plan & capacité — T-030 fait, T-031 en cours (phase 3 en attente de Zadig)

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
- T-031 : capacité en clusters, budget TOC, sanitize/raccourcissement des titres, titrage auto, first-fit (prompt `prompts/I-031`).
- Phase 3 (device) : bloquée tant que Zadig → WinUSB n'est pas installé sur le MZ-N505 (P-001).
  reste l'action utilisateur préalable.

### Fait en phase 4
- T-030 livré : le **plan de disque est un document** (ADR-011 D1). `src/core/plan/` — `Plan` = jusqu'à
  8 `PlanDisc`, chacun ≤ **254 entrées en SoA** (B-28) avec mode SP/LP2/LP4 + mono, override de titre,
  groupe, gain, rognage et fondus, longueur 60/74/80 et mode par défaut ; les plages de groupes sont
  **dérivées** de la colonne `group_id`, donc aucune commande n'a de plage à recoller. **Toute**
  modification passe par `plan_apply` : dix commandes réversibles (`Add, Remove, Move, SetMode,
  SetTitle, SetDiscTitle, Group, Ungroup, SetDiscLength, SplitDisc`), un seul enregistrement plat de
  80 octets qui porte son propre inverse, pile bornée à **256** dans la struct, **coalescence** des
  frappes dans un titre (800 ms) et événements `PlanChanged` — B-25 est vrai par construction, pas par
  discipline. Un `TrackId` n'est cru que tant qu'il désigne encore le chemin stocké : sinon il est
  re-résolu par chemin, sinon marqué **manquant et conservé** (jamais supprimé en silence). Format
  binaire `.mdplan` versionné, écrit atomiquement et relu **mappé**, avec validation intégrale de la
  frontière (offsets, tailles, chaque `StringId` contre un **bitmap des débuts de chaîne** — un id au
  milieu d'une chaîne est refusé) ; la table d'interne n'est pas stockée mais **rejouée**, ce qui
  reproduit exactement les mêmes offsets. Export/import texte `.mdplan.txt` (une ligne par piste).
  **Autosave** toutes les 5 s dans `%LOCALAPPDATA%\minidisk\plans\`, écriture immédiate à la
  fermeture, **récupération au démarrage** ; la boucle ne se réveille pour lui que si le plan est sale.
  Panneau Plan branché sur le vrai modèle (Entrée ajoute, Suppr retire, Ctrl+Z/Y, compteur et durée en
  en-tête, piste manquante en rouge) — la vue complète reste T-032. **9 cas de test** dont 1 000
  opérations aléatoires annulées et rétablies pas à pas avec comparaison d'empreinte
  (**128 cas / 5 219 checks**), **exe 244 224 o**, imports toujours kernel32 + user32.
  **P-009** ouvert : les 5 ms save+load ne sont pas tenues en temps mur (7,45 ms) parce que 7,1 ms sont
  la barrière de durabilité de l'OS ; notre part fait 0,5 ms.

### Fait en phase 2
- T-014 livré (dernier de la phase) : **drag & drop depuis l'Explorateur** par un `IDropTarget`
  minimal écrit à la main (`ole32` chargée dynamiquement, `RegisterDragDrop`), qui donne la position
  du curseur pendant le survol — le panneau Bibliothèque se met en surbrillance et dit « Déposez pour
  ajouter à la bibliothèque » — avec **WM_DROPFILES en repli** ; un dossier déposé est un dossier
  surveillé, un fichier désigne son dossier parent (`lib_drop_folder`, testé). **Pochettes par WIC** :
  `os_image_decode` (COM en C, `windowscodecs.dll` dynamique, conversion en `32bppPBGRA`, mise à
  l'échelle Fant vers 48 et 256 px) — **aucun octet de décodeur dans l'exe**, imports toujours
  kernel32 + user32. Source embarquée (APIC / PICTURE / covr, y compris **tag ID3 unsynchronisé**
  grâce à un tampon de capture dans `Tags`) avant `cover.*` > `folder.*` > `front.*` du dossier ;
  décodage en jobs (64 en vol au plus, la vue redemande chaque frame, donc aucune file à purger),
  cache disque `covers/<clé>.raw` (48² + 256² RGBA8, écriture atomique, relu **mappé**), **atlas de
  vignettes RGBA8 2048² séparé** en grille de cellules fixes avec **LRU** par bande, branché sur
  `r_rect_textured`. Vue : colonne pochette 48 px activable par le menu de l'en-tête et **panneau
  Détail repliable** (pochette 256 px texel pour texel, titre, artiste, album, année, format, durée,
  taille, chemin en ellipsis du milieu). **1,31 ms de décodage + mise à l'échelle par pochette**,
  15 µs par vignette versée dans l'atlas, 8 cas de test ajoutés (**119 cas / 3 344 checks**),
  **exe 229 376 o**, **46,9 ms de CPU sur 12 s au repos (inchangé)**. Capture : `build/demo.png`.
- T-013 livré : vue Bibliothèque réelle (la démo factice de 100 000 pistes est supprimée) — huit colonnes
  `#` / Titre / Artiste / Album / Durée / Format (badge codec + kHz) / Année / Ajouté, en-têtes triables
  avec indicateur, largeurs redimensionnables à la poignée et **persistées**, largeurs calculées une fois
  par frame et partagées par l'en-tête et les lignes (rien n'est jamais rogné ; sous 1024 dp les colonnes
  partent dans l'ordre Ajouté → Année → Format → Album → Durée, research/02 §8.7), menu de visibilité au
  clic droit sur l'en-tête, navigateur **Artiste | Album** repliable, **Ctrl+F** focalise la recherche et
  **Échap** l'efface, compteur « N / total », liste **sans aucune copie** (les lignes lisent les `TrackId`
  de `LibSearch` puis le SoA), sélection multiple, Ctrl+A, Entrée = ajout au plan, menu contextuel, états
  **vide** (gros bouton + dépôt de dossier), **scan** (barre de progression de 2 dp) et **aucun résultat** ;
  `app_state.{h,c}` sépare l'état de la boucle, `prefs.{h,c}` écrit `minidisk.prefs` (texte, écriture
  atomique, mode portable) et `strings.h` porte les 48 chaînes FR/EN (ADR-011 D10). **P-008 résolu** : la
  box conteneur des trois panneaux n'avait pas de clé, son `rect` était nul, et les splitters n'avaient
  donc **jamais** borné — les trois panneaux tiennent maintenant jusqu'à **1024 × 640 logique**.
  **Clic de tri sur 100 000 pistes : 36,45 ms**, recherche 4,06 ms, 7 cas de test ajoutés
  (111 cas / 1 875 checks), **exe 207 872 o**, imports kernel32+user32, **46,9 à 78,1 ms de CPU sur 12 s au
  repos**. Captures : `build/demo.png`, `build/demo_empty.png`.
- T-012 livré : index triés par colonne (titre, artiste, album, durée, date d'ajout) en fusion stable
  sur des paires `(clé u64, id)`, clés normalisées (casse, accents repliés par deux tables plates de
  384 o couvrant latin-1 et latin ext-A, articles optionnels), navigateur **Artiste → Album** avec
  comptes, recherche incrémentale (masque 64 bits par piste, sous-chaîne SSE2 sur un blob normalisé,
  tokens ET, raffinement qui ne reteste ni les tokens déjà prouvés ni les tokens d'une lettre,
  **zéro allocation pendant la recherche**), cache `library.mdlib` mappé (SoA écrit tel quel, string
  table + slots d'internement, écriture atomique `.tmp` + `MoveFileEx`, validation exhaustive de
  l'en-tête, des offsets, de chaque `StringId` et de la free-list, version → rejet propre), TrackId
  stables entre sessions, démarrage « cache puis rescan de fond qui ne pousse que les diffs », tri au
  clic sur l'en-tête. **Tri 100k en 22,8 ms, recherche « the » sur 100k en 4,09 ms, raffinement
  « the b » en 0,978 ms, cache 100k chargé en 26,9 ms** (fichier 12,04 Mo), 8 cas de test ajoutés
  (104 cas / 1 780 checks), **exe 187 392 o**, imports kernel32+user32. Capture : `build/demo.png`.
- T-011 livré : lecture des tags et des en-têtes sans décoder l'audio, **2 lectures de 64 Ko par
  fichier** (tête + queue) : ID3v2.2/2.3/2.4 (unsync tag et frame, ISO-8859-1 / UTF-16 BOM /
  UTF-16BE / UTF-8, TXXX ReplayGain, APIC → offset + hash), ID3v1/v1.1, en-tête MPEG avec
  Xing/Info/VBRI, FLAC (STREAMINFO / VORBIS_COMMENT / PICTURE), OGG Vorbis et Opus (durée par la
  granule de la dernière page), MP4 (`moov/udta/meta/ilst`, `mvhd`, `stsd`, `moov` en fin de fichier
  lu dans la queue), APEv2 en queue, WAV (`fmt `/`LIST INFO`) et AIFF (`COMM`/`NAME`/`AUTH`),
  dispatcher par signature et replis nom de fichier / dossiers parents. Tags lus dans une deuxième
  vague de jobs sur les seules pistes nouvelles ou modifiées : **un rescan sans changement n'ouvre
  aucun fichier**. **2,13 µs par fichier** au parsing, 50 000 fichiers tagués en **2,38 s à froid**
  (73 ms à chaud), 24 vecteurs golden de 34,8 Ko générés par `tools/gen_tag_vectors.py`, **fuzz de
  220 000 mutations sous ASan sans crash**, 16 cas de test ajoutés, exe 167 424 o, imports
  kernel32+user32. Capture : `build/demo.png` (titres, artistes, albums et durées réels).
- T-010 livré : `platform.h` système de fichiers (itération de dossier sans allocation, `os_file_stat`,
  lecture aléatoire, helpers de chemins, dossiers connus), sélecteur de dossier `IFileDialog` (COM en C,
  `ole32`/`shell32` dynamiques), `core/library` : SoA de 19 colonnes (**74 o/piste**, 7,4 MB pour
  100 000 pistes), `StringTable` internée, `TrackId` stable + tombstones, scan **un job par
  sous-dossier** avec allocateur bump sans verrou et pile de Treiber vers le thread principal, rescan
  incrémental par (taille, mtime), annulation coopérative, file d'événements core → UI, démo
  « Ajouter un dossier » + progression + chemins. **50 000 fichiers scannés en 168 ms à froid,
  63 ms à chaud**, 5 cas / 76 checks ajoutés, exe 123 904 o, imports kernel32+user32, 15,6 ms de CPU
  sur 12 s au repos. Capture : `build/demo.png`.

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
| Budget CI (`SIZE_BUDGET_KB`) | 350 KB (phase 4) ; historique : 128 KB fin de phase 1, 250 KB phase 2 (phase 2 : 250 KB ; `/O1` = −16,9 Ko reste un levier, cf. P-007) | — | 2026-09-06 |

## KPI — phase 2 terminée (même machine)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **229 376 o**, marge 26 624 o | budget CI 250 KB | 2026-09-07 |
| Scan bibliothèque | 50 000 fichiers / 500 dossiers : **168 ms à froid**, **63 ms à chaud** (7 workers) | < 2 s / < 300 ms | 2026-09-06 |
| Mémoire bibliothèque | **74 o/piste** → 7,4 MB pour 100 000 pistes (hors chaînes), 0 allocation par piste | < 40 MB | 2026-09-06 |
| Annulation du scan | < 100 ms (test) | < 100 ms | 2026-09-06 |
| Lecture des tags | **2,13 µs/fichier** (parsing), 2 lectures de 64 Ko par fichier | < 3 s / 10 000 | 2026-09-06 |
| Scan + tags, 50 000 fichiers | **2,38 s à froid**, **73 ms à chaud** (0 fichier ouvert au rescan) | < 3 s / 10 000 | 2026-09-06 |
| Fuzz des parseurs | 22 vecteurs x 10 000 mutations, **0 crash, 0 rapport ASan** | 0 | 2026-09-06 |
| Tests | **119 cas, 3 344 checks**, 0 échec (ASan) | verts | 2026-09-07 |
| Tri de la bibliothèque | 100 000 pistes par artiste : **22,8 ms** (fusion stable sur clés normalisées) | < 30 ms | 2026-09-07 |
| Recherche incrémentale | « the » sur 100 000 pistes : **4,09 ms** ; raffinée « the b » : **0,978 ms**, 0 allocation | < 5 ms / < 1 ms | 2026-09-07 |
| Cache `library.mdlib` | 100 000 pistes, fichier de 12,04 Mo : **chargé en 26,9 ms**, écrit en 53 ms | < 50 ms | 2026-09-07 |
| Clic de tri (en-tête), 100 000 pistes | **36,45 ms** : ordre construit + 100 000 ids réémis dans la liste | < 50 ms perçu | 2026-09-07 |
| Trois panneaux visibles | jusqu'à **1024 × 640 logique** (biblio 597 px, plan 357, disque 300 à 125 %) | 1024 x 640 | 2026-09-07 |
| Préférences | fichier de 789 o, aller-retour sérialisation + parsing en 13 µs, écriture atomique | — | 2026-09-07 |
| Pochettes | **1,31 ms** par pochette (décodage WIC + mise à l'échelle vers 256 et 48), 10 000 pochettes = ~1,9 s réparties sur 7 workers, 64 en vol au plus | 10 000 sans jank | 2026-09-07 |

## KPI — phase 4, T-030 (même machine)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **244 224 o**, marge 114 176 o | budget CI 350 KB | 2026-09-07 |
| Imports | kernel32 + user32 | ces deux-là | 2026-09-07 |
| Tests | **128 cas, 5 219 checks**, 0 échec (ASan) | verts | 2026-09-07 |
| Plan `.mdplan`, 254 pistes | **save 6,96 ms** (dont 7,09 ms de plancher OS) + **load 0,49 ms** = 7,45 ms | < 5 ms — **non tenu**, cf. P-009 | 2026-09-07 |
| Sérialisation du plan (notre code) | **≈ 0,5 ms** pour un disque plein de 28 040 o | < 5 ms | 2026-09-07 |
| Undo/redo | pile bornée à 256 commandes, 80 o chacune, 0 allocation ; 1 000 opérations aléatoires annulées et rétablies pas à pas | exact | 2026-09-07 |
| Mémoire du plan | **~76 Ko** pour 8 disques × 254 pistes, tout compris (pile d'undo incluse) | — | 2026-09-07 |
| Atlas de vignettes | 2048² RGBA8, 1 400 cellules de 48 px + 7 de 256 px, **15 µs** par vignette versée (16 par frame au plus : 0,24 ms), LRU par bande | vignettes < 100 ms après un scroll | 2026-09-07 |
| Cache pochettes | 271 376 o par pochette, relu mappé, **0 décodage au deuxième lancement** | — | 2026-09-07 |
| CPU au repos, 12 s, après un scan | **46,9 et 78,1 ms** sur trois mesures (46,9 ms après T-014 : le drop target OLE ne réveille rien), soit 0,4 à 0,65 % d'un cœur — même résidu de thread pilote GL qu'en T-008 (P-005) | 0 % | 2026-09-07 |

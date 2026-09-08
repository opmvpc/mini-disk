# Changelog

## Unreleased

### Phase 7 — Polish
- **Polish visuel (T-075)** : les boutons ne sont plus collés les uns aux autres et ne sortent plus
  de leur panneau. Un bouton fait `control_h` (28 dp) et sa rangée `row_control` (36 dp), les
  rangées se replient sur une seconde ligne quand la place manque (`UI_Flow` dans `ui_core`, mesuré
  dans les deux passes de layout existantes), le transport du disque passe en cinq icônes, le
  panneau Disque défile au lieu de couler sous la barre de statut, les messages du pré-vol tiennent
  sur deux ou trois lignes au lieu d'être coupés, les bulles basculent au-dessus de leur bouton près
  du bas de la fenêtre et les compteurs de diagnostic quittent la barre de statut pour l'overlay
  F11. Panneaux Disque et Plan élargis (360 et 400 dp minimum) : à 1100 × 700, la fenêtre minimale,
  plus rien n'est coupé. La liste de pistes de la bibliothèque garde ses 160 dp quoi qu'il arrive :
  le panneau de détail cède d'abord, puis le navigateur. Le survol d'une poignée de séparation se
  voit sur 2 dp et non sur toute sa largeur.
- **Régime de taille (T-070)** : nouvelle cible `build.bat map` (`/MAP` + `tools/size_report.py`) qui
  ventile l'exe par module et par symbole, et compare deux mesures (`--diff`). L'exe release passe de
  **636 928 à 622 080 octets** : `#pragma optimize("s") + inline_depth(1)` sur les unités froides que
  plus aucun banc ne traverse (protocole NetMD, dialogues, USB, Media Foundation, commandes de plan,
  Media Foundation codec, orchestration du transfert), `/INCLUDE:codec_open` retiré du lien — il ne
  valait plus rien depuis T-043 — et `--selftest` qui ouvre désormais un vrai WAV par `codec_open`
  pour prouver que `/OPT:REF` laisse les décodeurs dans l'image. `SIZE_BUDGET_KB` : 700 → 630.
  La cible de 500 KB **n'est pas atteinte** : les leviers qui la donneraient (`/O2 /Os` global,
  `DR_FLAC_NO_CRC`) coûtent de la perf mesurée ou une fonction ; les chiffres de chaque arbitrage
  sont dans la Livraison du ticket.

### Phase 5 — Audio & gravure (T-040..T-043), terminée — tag `v0.5.0-phase5`
De la bibliothèque au disque, pour de vrai, dans un exe de 636 928 octets :
- **Décodeurs** (T-040) : MP3, FLAC, WAV/AIFF, Ogg Vorbis sans CRT, plus un repli Media Foundation
  pour l'AAC ; sortie planaire f32 en blocs de 4096 frames, seek exact, fichiers cassés refusés
  proprement (fuzz de 2 200 mutations d'en-tête).
- **DSP et pipeline** (T-041) : `dsp_math` sans libm, resampler polyphase sinc/Kaiser SSE2
  (SNR > 100 dB), loudness EBU R128 / BS.1770-4 avec true-peak, édition (downmix, trim, fondus,
  gap, gain), dither TPDF et noise-shaping, cadrage SP big-endian 2048 o et écriture WAV. Le
  pipeline fait deux passes par piste — mesurer, puis rendre — un job par piste.
- **Session sécurisée et upload SP** (T-042) : DES, 3DES et retail-MAC écrits depuis la FIPS 46-3,
  tables construites au premier usage ; toute la séquence de research/01 §4 (EKB, nonces, clé de
  session, `setupDownload`, paquets DES-CBC chaînés, `commitTrack`) avec démontage garanti sur
  toutes les sorties ; orchestration d'un plan piste par piste, titre puis commit, titre de disque
  écrit une seule fois à la fin, annulation et reprise. **Première piste réellement gravée sur le
  MZ-N505.**
- **Vue Transfert et cache de transcodage** (T-043) : la gravure devient une étape visible.
  Pré-vol (D4) qui liste ce qui sera écrit avec les titres finaux, la capacité et le budget TOC
  avant/après et tous les avertissements ; transfert avec état par piste, deux barres, ETA honnête
  (moyenne glissante 30 s qui ne remonte jamais), pause entre deux pistes, annulation qui énonce ce
  qu'elle laisse sur le disque, reprise, bandeau « ne pas éjecter », fermeture refusée pendant
  l'envoi, journal d'opérations. Cache disque des pistes transcodées
  (`<cache>\transcode\<clé>.pcm`, clé sur le fichier source **et** tous les paramètres du pipeline,
  en-tête validé, écriture atomique) avec purge LRU par taille partagée avec le cache de pochettes.
  Variante « progression de gravure » de la jauge.

### Phase 3 — NetMD, lecture et édition (T-020..T-022), terminée
Le vrai appareil, sans pilote propriétaire :
- **WinUSB, énumération et hotplug** (T-020) : `winusb`/`setupapi`/`cfgmgr32` chargées
  dynamiquement, états `Ready` / `NoDriver` / `InUse` avec le code CM, politiques de pipe,
  notifications de branchement débruitées, thread device dédié avec files de commandes et
  d'événements, transport rejouable, écran guidé « pilote manquant » (P-001).
- **Protocole NetMD, lecture** (T-021) : requêtes/réponses `%b %w %d %x`, statut AV/C, jeu de
  caractères NetMD, TOC complet (titres, groupes, durées, capacité, protections) lu en une
  commande, transcriptions `--netmd-trace` rejouables. Disque réel « 202001 » lu et affiché.
- **Édition du disque** (T-022) : renommage, déplacement, effacement, groupes, effacement du
  disque — chacun **simulé** avant toute écriture (ADR-011 D4), sauvegarde texte du TOC avant
  chaque écriture, `oldLen` relu sur l'appareil, titre identique jamais réécrit, bandeau
  « ne pas éjecter » et fermeture refusée tant que le TOC est en RAM.

### Phase 4 — Plan & capacité (T-030..T-032), terminée — tag v0.4.0-phase4
Le cœur produit, dans un exe de 308 224 octets :
- **Plan = document** (T-030) : multi-disques (8 × 254 entrées en SoA), commandes réversibles avec
  undo/redo (256, coalescence de la saisie, undo par geste), `.mdplan` binaire + `.mdplan.txt`,
  autosave, résolution des pistes par identifiant puis par chemin.
- **Capacité en clusters** (T-031) : SP 2 s, mono/LP2 4 s, LP4 8 s par cluster (à valider sur
  l'appareil), états par piste, « ce qui rentrerait encore » dans les quatre modes, répartition
  multi-disques first-fit ou par albums ; budget TOC 255 × 7 avec syntaxe de groupes, sanitize vers le
  jeu NetMD par tables générées, raccourcissement ordonné avec aperçu exact, titrage automatique.
  Recalcul de 254 pistes en 30 µs.
- **Vue Plan** (T-032) : liste virtualisée réordonnable (DnD interne, clavier complet), badges de
  mode, titres MD tels qu'ils seront écrits, groupes, onglets multi-disques, jauge 56 dp au pixel de
  la recherche §9 et variante compacte, barre TOC avec raccourcissement automatique, remplir l'espace
  restant, nouveau disque, ouvrir / enregistrer. 144 cas / 5 731 checks.

### Phase 2 — Bibliothèque (T-009..T-015), terminée
Une bibliothèque qui tient 100 000 pistes dans un exe de 229 376 octets, toujours sans CRT et sans
autres imports que kernel32 et user32 :
- **Scan** (T-010) : un job par sous-dossier, allocateur bump sans verrou, pile de Treiber vers le
  thread principal, tombstones et `TrackId` stables — 50 000 fichiers en **168 ms à froid**.
- **Tags** (T-011) : ID3v1/v2.2/2.3/2.4 (unsync), Vorbis, MP4, APEv2, WAV, AIFF, en-têtes MPEG
  Xing/VBRI, écrits à la main, **deux lectures de 64 Ko par fichier**, 2,13 µs de parsing chacun,
  lus dans une seconde vague de jobs sur les seules pistes nouvelles ou modifiées.
- **Index, recherche, cache** (T-012) : tris stables par colonne sur clés normalisées, navigateur
  Artiste → Album, recherche incrémentale SSE2 sans allocation (4,09 ms sur 100 000, 0,978 ms en
  raffinement), cache binaire mappé `library.mdlib` chargé en 26,9 ms.
- **Vue bibliothèque** (T-013) : huit colonnes triables, redimensionnables et persistées, navigateur
  repliable, liste virtualisée sans aucune copie, états vide / scan / aucun résultat, préférences en
  texte écrites atomiquement, 48 chaînes FR/EN.
- **Drag & drop et pochettes** (T-014) : `IDropTarget` maison sur `ole32` dynamique (surbrillance du
  panneau pendant le survol, WM_DROPFILES en repli), `os_image_decode` par **WIC** (aucun décodeur
  dans l'exe), source embarquée > `cover|folder|front` du dossier, décodage en jobs (1,31 ms par
  pochette), cache disque `covers/<clé>.raw` mappé, **atlas de vignettes RGBA8 2048² à LRU**, colonne
  de vignettes 48 px et panneau détail repliable avec pochette 256 px.
- **DPI et overlay** (T-015), **tri des draw calls par texture** (T-009).
- Qualité : **119 cas de test / 3 344 checks** sous ASan (dont un fuzz de 220 000 mutations sur les
  parseurs de tags), `check` et `analyze` verts, 19 bancs de mesure. **46,9 ms de CPU sur 12 s au
  repos**, inchangé depuis la phase 1.

### Phase 1 — Fondations (T-001..T-008), terminée
Une application Windows autonome de 107 008 octets, sans CRT, qui n'importe que kernel32 et user32 :
- `base/` : arènes sur mémoire virtuelle réservée, scratch arenas par thread, `String8` et formatage
  maison, hash, math/SIMD, **job system** (pool de N-1 workers, file MPMC sans lock, parallel-for).
- `platform/` : fenêtre Win32 complète (DPI par moniteur v2, drop files, presse-papiers, curseurs),
  boucle d'événements à la demande, contexte OpenGL 3.3 core chargé à la main, texte DirectWrite,
  threads / sémaphores / SRW / atomiques.
- `ui/` : renderer SDF (1 shader, 1 VBO persistant, atlas skyline R8, < 10 draw calls), moteur UI
  immédiat à cœur retenu (clés hachées, piles de style, layout sémantique, 3 couches, animations),
  jeu de widgets (bouton, champ texte UTF-8, liste virtualisée 100 000 lignes, splitter, tooltip,
  menu contextuel), thème sombre en tokens, **overlay de debug F11**.
- Qualité : 70 cas de test / 1 378 checks sous ASan, `build.bat check` et `analyze`
  (cl /W4 /WX /analyze + clang-tidy) verts, 7 bancs de mesure avec seuils.
- Repos : 0 réveil et 0 message sur 12 s ; le CPU résiduel mesuré vient d'un thread du pilote GL
  (P-005, résolu par les compteurs de l'overlay).

- T-008 : `base_jobs` (pool N-1, ring MPMC de Vyukov, `jobs_dispatch`/`jobs_wait` avec entraide du
  thread principal, workers endormis sur sémaphore), primitives threads/sémaphores/SRW/atomiques
  dans `platform.h`, overlay debug F11 (fps, temps de frame, boxes, draw calls, atlas, arènes, jobs,
  DPI, réveils et messages WndProc). 910 ns par job vide à 7 workers, 4,35x sur un parallel-for.
- T-005 : texte DirectWrite → atlas R8, fallback Unicode système, caches de glyphes et de mesure,
  ellipsis, chiffres tabulaires, 4 styles de police reconstruits au changement de DPI.
- Phase 0 : recherche, analyse, ADR-001..012, CI.

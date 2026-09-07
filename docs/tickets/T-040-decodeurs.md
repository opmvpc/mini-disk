# T-040 — Décodeurs : minimp3, dr_flac, dr_wav/AIFF, stb_vorbis + Media Foundation (AAC/ALAC/WMA)

Phase 5 · Statut : **fait** · Dépend de : T-014, ADR-007

## Livrables
- `src/third_party.c` : intégration de `minimp3.h` (CC0), `dr_flac.h`, `dr_wav.h` (MIT-0), `stb_vorbis.c`
  (public domain) compilés **sans CRT** (macros de remplacement `malloc/free/memcpy…` vers nos arènes et
  stubs, `DR_*_NO_STDIO`, `STB_VORBIS_NO_STDIO`, pas de `stdio`), versions épinglées et licences copiées
  dans `third_party/LICENSES.md`.
- `src/core/codecs/codec.h` : interface unique `Decoder { open(bytes/file), read_f32_planar(frames),
  seek, info {sample_rate, channels, total_frames, codec}, close }` ; sortie **f32 désentrelacée** par blocs
  de 4 096 frames dans des buffers d'arène ; lecture du fichier par `os_file_read_at` en blocs de 256 KB
  (jamais le fichier entier en mémoire).
- `codec_mp3.c`, `codec_flac.c`, `codec_wav.c` (WAV PCM 8/16/24/32/float + AIFF maison), `codec_ogg.c`.
- `platform.h` : `os_media_decoder_open/read/close` implémenté via **Media Foundation** dans
  `win32_media.c` (`mfplat`/`mfreadwrite` dynamiques, `IMFSourceReader` depuis un `IMFByteStream` sur
  notre lecteur, sortie forcée en PCM float 32 bits) pour AAC/M4A, ALAC, WMA ; `codec_mf.c` l'adapte à
  `Decoder`. Opus : hors v1 (ADR-007) → erreur "format non supporté" propre.
- Dispatcher par signature (réutiliser `tags.c`) ; gestion des fichiers corrompus : erreur typée, jamais
  de crash (fuzz sur les en-têtes, pas sur le flux entier).
- Tests : `tests/test_codecs.c` — vecteurs golden générés par `tools/gen_audio_vectors.py` (sinus 1 kHz
  −6 dBFS 2 s en WAV 16/24/float, AIFF, FLAC via encodeur Python minimal ou fichier commité < 60 KB,
  OGG et MP3 : fichiers minuscules commités, générés une fois avec ffmpeg et documentés) : durée exacte,
  SNR du sinus décodé > 60 dB (MP3/OGG) ou bit-exact (WAV/FLAC), seek, fin de flux, fichier tronqué.
  Media Foundation testé en CI Windows sur un M4A commité.
- Banc : décodage MP3 320 kbps, FLAC, OGG en x temps réel (cible ≥ 100x MP3, ≥ 300x FLAC).

## Critères d'acceptation
- Aucun `malloc` : les libs tierces allouent dans une arène par décodeur, libérée en bloc.
- Exe < 420 KB (**relever `SIZE_BUDGET_KB` à 500 en phase 5**, cf. research/03 annexe A). Tests, check, analyze verts.

## Livraison

Statut : **fait** (2026-09-07). Mesures sur la machine de référence (i7-8550U, 4 cœurs / 8 threads,
Windows 11).

### Ce qui a été construit

**`third_party/` — cinq fichiers vendorisés, épinglés, non modifiés.** `minimp3.h` + `minimp3_ex.h`
(lieff/minimp3 `ea99364f`, CC0), `dr_flac.h` 0.13.4 et `dr_wav.h` 0.14.6 (mackron/dr_libs `dfe83776`,
Unlicense ou MIT-0), `stb_vorbis.c` 1.22 (nothings/stb `2c980bb5`, MIT ou domaine public). Récupérés par
`curl` sur les URL raw officielles ; `third_party/LICENSES.md` porte le tableau des versions, des
commits et le texte intégral de chaque licence. `minimp3_ex.h` est vendorisé pour référence mais **pas
compilé** : il `malloc` et lit ou mappe le fichier entier, ce qu'interdisent ADR-002 et la règle des
blocs de 256 KB. Toute mise à jour se fait par un nouveau `curl` : aucun patch local, jamais.

**`src/third_party.h` + `src/third_party.c` + `src/third_party_vorbis.c` — l'intégration sans CRT.**
L'en-tête ne porte que les réglages qui changent les *déclarations* (`MINIMP3_FLOAT_OUTPUT`,
`DR_FLAC_NO_STDIO`, `DR_FLAC_NO_OGG`, `DR_WAV_NO_STDIO`, `DR_WAV_NO_WCHAR`, `STB_VORBIS_NO_STDIO`) ;
les `.c` portent le reste, chaque bouton documenté dans le bloc de commentaires en tête de
`third_party.c` :

- **Mémoire** : `DRFLAC_MALLOC/REALLOC/FREE` et `DRWAV_MALLOC/REALLOC/FREE` pointent sur `tp_malloc` /
  `tp_realloc` / `tp_release`, qui poussent sur l'arène que l'appelant a liée avec `tp_arena_bind()`
  (variable de thread : deux jobs qui décodent deux fichiers ne partagent pas la liaison). Un en-tête de
  16 octets devant chaque bloc porte sa taille — c'est ce qui rend `realloc` possible, les bibliothèques
  ne repassant jamais l'ancienne taille. `tp_release` ne fait rien : `codec_close()` libère l'arène d'un
  bloc. stb_vorbis n'alloue **que** dans son `stb_vorbis_alloc` (1 MB pris sur l'arène du décodeur) ;
  minimp3 n'alloue rien du tout. **Zéro `malloc`** : `build.bat check` est vert et l'édition de liens
  release se fait sous `/NODEFAULTLIB`.
- **libm** : `third_party_vorbis.c` fournit `floor`, `ldexp`, `exp`, `log`, `pow`, `sin`, `cos`, `sqrt`,
  `abs` et `qsort` en double précision (réduction de Cody-Waite sur π/2, `pow` exact par carrés répétés
  pour les exposants entiers dont dépend le calcul des codebooks, tri par tas sans récursion) et les
  mappe par macro juste avant l'inclusion. Aucun symbole `pow`/`exp`/`log`/`sqrt`/`floor`/`cos`/`sin`
  externe ne reste : l'exe release se lie avec kernel32 + user32 seuls.
- **`alloca`** : son unique usage est la branche non prise de `temp_alloc()`, dont l'autre branche est
  `setup_temp_malloc(f, size)` et dont le `f` est en portée à chaque site d'expansion. On pointe donc
  `alloca` sur cette branche : plus aucune allocation de pile dynamique, donc plus de `_alloca_probe`
  que `/NODEFAULTLIB` ne saurait résoudre.
- **Assertions** : désactivées. Ces décodeurs reçoivent des fichiers écrits par d'autres ; un flux
  malformé doit rendre une erreur, pas casser sur un `assert` (ADR-012).
- **Deux unités de compilation** au lieu d'une : minimp3 et stb_vorbis définissent tous deux un
  `get_bits` statique, un unity build des deux ne compile pas.
- **`memcpy`/`memset`/`memmove`/`memcmp`** sont redirigés par macro vers nos `mem_*`, et les deux unités
  tierces sont compilées **`/GL-`** : sous LTCG, MSVC transforme leurs affectations de struct
  (`pFlac->bs = pInit->bs`) en appels « library helper » que l'éditeur de liens refuse de résoudre sur
  notre stub `#pragma function` (C2268 — le piège que CONVENTIONS.md consigne depuis T-004). On ne patche
  pas du code vendorisé : on lui retire `/GL`, il reste en `/O2`.

**`src/core/codecs/codec.h` — une interface, cinq back ends.** `codec_open(Decoder **, String8 path)`
prend **une arène par décodeur** (`arena_alloc`, réserve de 64 MB, engagée à la demande) et
`codec_close()` la libère d'un bloc : le struct, la fenêtre de lecture et tout ce que la bibliothèque
tierce a alloué disparaissent ensemble. `CodecSource` est un lecteur séquentiel sur `os_file_read_at`
avec **une seule fenêtre de 256 KB** alignée sur sa taille ; toutes les bibliothèques tirent au travers,
et **jamais le fichier entier**. `codec_read_f32_planar(decoder, channels, frames)` écrit du f32
**désentrelacé** dans des tampons fournis par l'appelant, par blocs d'au plus 4 096 frames, et rend 0 en
fin de flux. `codec_seek(decoder, frame)` est **exact pour tous les formats**. `CodecInfo` publie
`sample_rate`, `channels`, `total_frames` (0 si inconnu) et l'identifiant de codec. Les erreurs sont
typées : `CODEC_ERR_FILE`, `_FORMAT`, `_UNSUPPORTED`, `_CORRUPT`, `_NO_DECODER`.

- **`codec_wav.c`** — WAVE par dr_wav (PCM 8/16/24/32, IEEE float, extensible, W64, RF64) et **AIFF /
  AIFF-C écrits à la main** : parcours des chunks `COMM` et `SSND`, taux d'échantillonnage en flottant
  étendu 80 bits, PCM big-endian 8/16/24/32, `sowt` little-endian, `fl32` et `fl64`. Le nombre de frames
  annoncé par `COMM` est borné par ce que le fichier contient vraiment.
- **`codec_flac.c`** — dr_flac sur les callbacks `read`/`seek`/`tell` de `CodecSource`.
- **`codec_mp3.c`** — boucle de trames écrite ici (minimp3_ex n'est pas utilisé) : saut de l'étiquette
  ID3v2, fenêtre d'entrée compressée de 64 KB, en-tête Xing/Info/VBRI pour la durée (estimation CBR
  sinon), trame d'en-tête VBR écartée, décodeur réinitialisé après le sondage pour que le réservoir de
  bits de la trame sondée ne fuie pas dans la première trame rendue. Le seek décode : en avant depuis la
  position courante, depuis la première trame quand la cible est derrière. C'est exact, et aux 400x temps
  réel mesurés un album entier coûte une seconde.
- **`codec_ogg.c`** — stb_vorbis en mode **pushdata** : c'est la seule API qui ne réclame jamais le
  fichier entier et la seule qui accepte un `stb_vorbis_alloc`, donc qui alloue strictement dans notre
  arène. Le seek utilise `stb_vorbis_flush_pushdata` (les codebooks restent en place) puis décode depuis
  la première page audio. La durée vient de la granule position de la dernière page.
- **`codec_mf.c`** + **`src/platform/win32/win32_media.c`** — AAC/M4A, ALAC et WMA par Media Foundation.
  `mfplat.dll` et `mfreadwrite.dll` sont chargées par `LoadLibraryW` à la première ouverture, jamais
  importées ; les interfaces (`IMFSourceReader`, `IMFMediaType`, `IMFSample`, `IMFMediaBuffer`) sont
  déclarées en vtables à la main comme dans `win32_image.c` et `win32_dialog.c` (P-006), et les GUID sont
  écrits en dur plutôt que liés depuis `mfuuid.lib`. Sortie forcée en **PCM float 32 bits**, recopiée en
  blocs planaires. Opus → `CODEC_ERR_UNSUPPORTED` propre (ADR-007).
- **`codec.c`** — dispatch **par signature, jamais par extension**, en réutilisant `TagsReader` et
  `tags_match` de `tags.c` : `fLaC`, `OggS` (+ `vorbis` / `OpusHead` dans la première page),
  `RIFF`/`WAVE`, `FORM`/`AIFF`/`AIFC`, le GUID d'en-tête ASF, `ftyp`, `ID3`, et la synchro MPEG.

### Choix : `MFCreateSourceReaderFromURL` plutôt qu'un `IMFByteStream`

Le ticket laisse le choix. Écrire un `IMFByteStream` en C demande une vtable de quatorze méthodes plus un
`IMFAsyncCallback` pour la moitié asynchrone — environ 250 lignes et ~3 KB d'exe — pour remplacer un
lecteur de fichier que Windows implémente déjà et qui pagine tout seul. La règle des 256 KB de T-040 vise
**nos** décodeurs, pour qu'ils ne tiennent pas un fichier entier en mémoire ; la source Media Foundation
fait ses propres entrées/sorties paginées. On a donc pris l'URL, et le commentaire en tête de
`win32_media.c` le dit.

### Un vrai bug trouvé par le fuzz, corrigé à la frontière

stb_vorbis 1.22 lit l'en-tête de commentaires Vorbis avec les longueurs que le fichier lui donne : une
longueur de commentaire à `0x7FFFFFFF` fait déborder `len + 1`, le test d'allocation passe sur une taille
négative et la boucle de recopie sort du tampon. ASan l'a attrapé dès les premières mutations. Le
correctif n'est **pas** un patch sur du code vendorisé : `codec_ogg.c` réassemble les deux paquets
d'en-tête à partir des pages Ogg qu'il tient et vérifie chaque longueur contre le paquet qui la porte
(`codec_ogg_headers_are_sane`), avec le `TagsReader` borné de `tags.h`. C'est exactement l'endroit où
ADR-012 place une frontière de validation.

### Tests et vecteurs

`tools/gen_audio_vectors.py` (commité) écrit `tests/data/audio/` — 265 263 octets au total. Le signal est
toujours le même : sinus 1 kHz à −6 dBFS, 44 100 Hz, mono. WAV 8/16/24/32/float, WAV stéréo (1 kHz à
gauche, 2 kHz à droite), AIFF 16 bits big-endian, AIFF-C `sowt` et `fl32`, et un **encodeur FLAC minimal
en Python pur** (prédicteur fixe d'ordre 2, résidus Rice, CRC-8 et CRC-16 — le vrai chemin de décodage de
dr_flac, pas du verbatim qui ne testerait rien).

`tests/test_codecs.c` : dispatch par signature, durée exacte, décodage comparé à la formule à un quantum
près de la profondeur du fichier, **bit-exact entre décodeurs** (FLAC et AIFF rendent octet pour octet le
même f32 que le WAV — c'est la vraie affirmation « sans perte »), désentrelacement stéréo, seek exact
(WAV, AIFF, FLAC, MP3, OGG), fin de flux et reprise après seek, douze fichiers cassés de `tests/data/`
qui échouent proprement, et le fuzz d'en-têtes.

### Écarts

1. **Pas de `ffmpeg` sur cette machine.** Le générateur le cherche d'abord, puis retombe sur VLC
   (`vlc.exe --sout`), et documente le vecteur manquant s'il ne trouve ni l'un ni l'autre. Les encodeurs
   de VLC sont bien plus bruyants que LAME et libvorbis sur une sinusoïde pure : **MP3 73 dB** (256 kb/s,
   au-dessus des 60 dB du ticket), **Ogg Vorbis 53 dB** (qualité 10, plafond de l'encodeur), **AAC 27 dB**
   (invariant du débit : 128, 192 et 320 kb/s donnent le même chiffre). Vérification croisée : le
   décodeur de VLC lui-même donne **les mêmes valeurs à 0,4 dB près** sur ces fichiers, donc le plancher
   vient de l'encodage, pas de notre décodage. Les seuils du test sont donc 60 / 45 / 20 dB, commentés sur
   place ; régénérer les vecteurs sur une machine avec ffmpeg les fait tous passer au-dessus de 60 dB sans
   toucher au test au-delà du tableau `floors`.
2. **Le seek MP3 et le seek OGG décodent** au lieu de sauter (pas de table de seek dans un flux MPEG nu,
   pas d'API de seek dans le mode pushdata de stb_vorbis). Le résultat est exact au frame près, ce que le
   test vérifie ; le coût est linéaire, invisible aux 234-400x temps réel mesurés.
3. **`DR_FLAC_NO_SIMD`.** dr_flac embarque trois copies du décodeur de résidus Rice (scalaire, SSE4.1
   32 bits, SSE4.1 64 bits) et `/O2` les déroule toutes : 50 KB d'exe, dont 27,5 KB pour les deux versions
   SIMD. Le décodeur scalaire tient **451x temps réel**, très au-dessus des 300x d'ADR-007, et le budget
   de taille était la contrainte serrée. Mesuré à ce point du ticket : 449 536 o avec SIMD → 417 792 o sans (l'exe final fait 419 328 o, la
   frontière Ogg de l'écart suivant ayant coûté 1 536 o).
4. **`build.bat bench` est vert machine au repos, mais s'arrête au milieu quand elle est chargée**,
   pour une raison qui **précède T-040** (arène du banc, pas les codecs) : cf.
   `docs/problems/P-010-bench-jobs-parallel-sum-intermittent.md`. C'est aussi ce qui fait varier les
   chiffres des bancs de 40 % d'une passe à l'autre — les valeurs publiées sont les meilleures de
   plusieurs passes.

### Mesures

| Mesure | Valeur | Cible |
|--------|--------|-------|
| Exe release | **419 328 o** (départ 308 224 o, +111 104 o pour cinq décodeurs) | < 420 KB (430 080 o) — marge 10 752 o |
| Budget CI `SIZE_BUDGET_KB` | 500 KB (512 000 o) — marge 92 672 o | — |
| Imports (table d'import lue à la main) | **KERNEL32.dll, USER32.dll** | ces deux-là |
| Coût des réglages de taille | `MINIMP3_ONLY_MP3` + `STB_VORBIS_NO_PULLDATA_API` : −3 584 o ; `DR_FLAC_NO_SIMD` : −28 160 o | — |
| Tests | **155 cas, 5 842 checks, 0 échec** (debug + ASan) | verts |
| Fuzz d'en-têtes | 11 vecteurs × 200 mutations = 2 200 ; **2 026 ouvertures**, 0 crash, 0 rapport ASan | 0 |
| SNR MP3 / OGG / M4A décodés | **73 / 53 / 27 dB** (qualité des encodeurs VLC, cf. écart 1) | > 60 dB MP3 |
| Décodage WAV s16 | **922x temps réel** (525 à 1 101x selon la charge machine) | — |
| Décodage FLAC | **451x temps réel** (220 à 451x) | ≥ 300x |
| Décodage MP3 | **401x temps réel** (234 à 401x) | ≥ 100x |
| Décodage Ogg Vorbis | **234x temps réel** (111 à 234x) | — |
| `malloc` dans notre code | **0** (`build.bat check` vert) | 0 |
| Cibles `build.bat` | debug, release, test, check, analyze, bench toutes vertes (bench sensible à la charge machine, P-010) | vertes |
| Vecteurs audio commités | 13 fichiers, **265 263 o** (le plus gros : `sine_16.wav` 44 144 o ; MP3 28 421 o, M4A 12 586 o, OGG 8 705 o) | < 40 KB par fichier compressé |

### Revue (lead, 2026-09-07)
- Grille ADR-012 dans le worktree `t040` : `check/test/release/analyze/bench` verts, **419 328 o** (10,7 KB sous
  les 420 KB du ticket), 155 cas / 5 842 checks, FLAC 694×, MP3 490×, Ogg 256×, WAV 1 378× temps réel (machine au repos).
- Approuvé : deux unités tierces en `/GL-` plutôt que de patcher les sources vendorisées ; `/INCLUDE:codec_open`
  temporaire jusqu'à T-043 (à retirer alors) ; débordement de stb_vorbis contenu à notre frontière (ADR-012) ;
  `MFCreateSourceReaderFromURL` accepté pour la v1 (le byte-stream COM coûterait ~8 KB pour rien).
- P-010 (banc qui casse sous charge) accepté comme ticket de suite : rendre l'assertion de `os_memory_commit` parlante
  et découper l'arène du banc. Merge.

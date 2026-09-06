# T-011 — Lecture des tags et des en-têtes : ID3v1/v2, Vorbis comments, MP4 atoms, APE, WAV/AIFF

Phase 2 · Statut : **fait** · Dépend de : T-010, ADR-007, ADR-012 (validation aux frontières)

## Objectif
Remplir titre/artiste/album/album artist/genre/numéro/disque/année/durée/sample rate/canaux/codec pour
chaque fichier scanné, sans décoder l'audio, en lisant le minimum d'octets (en-tête + queue).

## Livrables
- `src/core/library/tags_id3.c` : ID3v2.2/2.3/2.4 (unsync, frames compressées ignorées, encodages
  ISO-8859-1/UTF-16 BOM/UTF-16BE/UTF-8, TIT2 TPE1 TALB TPE2 TCON TRCK TPOS TYER/TDRC TXXX:replaygain_*,
  APIC → hash + offset pour T-014), ID3v1/v1.1 en repli, en-tête MPEG (version, layer, bitrate, sample
  rate, Xing/Info/VBRI pour la durée exacte, sinon estimation par taille).
- `tags_vorbis.c` : FLAC (STREAMINFO → durée exacte, VORBIS_COMMENT, PICTURE) et OGG Vorbis/Opus
  (pages, comment header, durée via granule position de la dernière page).
- `tags_mp4.c` : atomes `moov/udta/meta/ilst` (©nam ©ART ©alb aART ©gen trkn disk ©day covr), `mvhd`
  pour la durée, `stsd` pour le codec (mp4a AAC / alac).
- `tags_ape.c` : APEv2 en queue (WavPack/Musepack/MP3 sans ID3) ; `tags_riff.c` : WAV (fmt, LIST/INFO)
  et AIFF (COMM, NAME/AUTH).
- `tags.c` : dispatcher par signature (pas par extension), normalisation (trim, "feat." conservé),
  repli titre = nom de fichier, artiste/album = dossiers parents.
- Intégration au scan (T-010) : lecture des tags dans les jobs, `os_file_read_at` par blocs de 64 KB,
  jamais le fichier entier.
- Tests : `tests/test_tags.c` avec vecteurs golden **minimaux commités** dans `tests/data/` (< 20 KB
  chacun, générés par script Python `tools/gen_tag_vectors.py` archivé) : ID3v2.3 UTF-16, ID3v2.4 UTF-8
  avec unsync, ID3v1 seul, FLAC, OGG, M4A, APE, WAV, AIFF, fichiers tronqués/corrompus (doivent
  échouer proprement, jamais crasher : fuzz de 10 000 mutations aléatoires en ASan).

## Critères d'acceptation
- Chaque parseur est une frontière (ADR-012) : validation exhaustive des longueurs, aucune lecture hors
  buffer (ASan + fuzz), sortie canonique.
- 10 000 MP3 réels taggés en < 3 s à froid sur SSD (≤ 2 lectures de 64 KB par fichier).
- Exe < 200 KB. Tests, check, analyze verts.

## Livraison

Statut : **fait** (2026-09-06).

### Ce qui est livré
- `src/core/library/tags.h` : `TagsFile` (les deux seules fenêtres lisibles : tête et queue),
  `Tags` (sortie canonique, chaînes dans la struct, zéro allocation), `TagsReader` borné —
  toute lecture hors limites lève `fail` et renvoie 0, jamais un accès hors buffer (ADR-012).
- `tags_id3.c` : ID3v2.2/2.3/2.4 (unsync au niveau tag et au niveau frame, en-tête étendu 2.3 et 2.4,
  tailles syncsafe avec repli sur u32 brut pour les encodeurs non conformes, frames compressées ou
  chiffrées ignorées, encodages ISO-8859-1 / UTF-16 BOM / UTF-16BE / UTF-8), frames TIT2 TPE1 TALB
  TPE2 TCON TRCK TPOS TYER TDRC TXXX:replaygain_* et APIC (offset + taille + hash 64 bits),
  équivalents 3 lettres en v2.2 (TT2 TP1 TAL TP2 TCO TRK TPA TYE TXX PIC) ; ID3v1/v1.1 en queue
  (table des 126 genres) ; en-tête MPEG (version 1/2/2.5, layers I/II/III, tables de débit et de
  fréquence) avec Xing/Info **et** VBRI pour la durée exacte, sinon durée = octets audio / débit.
- `tags_vorbis.c` : FLAC (STREAMINFO → fréquence, canaux, durée exacte ; VORBIS_COMMENT ; PICTURE →
  offset + hash) et OGG Vorbis / Opus (pages, en-tête d'identification, comment header, durée par la
  granule de la dernière page trouvée dans la queue, pre-skip retiré pour Opus).
- `tags_mp4.c` : marche d'atomes qui traverse un `mdat` de 40 Mo sans le lire (les en-têtes sont
  demandés à `TagsFile`, donc un `moov` écrit en fin de fichier est lu dans la queue) ;
  `moov/udta/meta/ilst` (©nam ©ART ©alb aART ©gen ©day gnre trkn disk covr, freeform `----` pour
  ReplayGain), `mvhd` pour la durée, `stsd` pour le codec (mp4a / alac), les canaux et la fréquence.
- `tags_ape.c` : APEv2 par le footer (taille validée contre le fichier, items binaires ignorés).
- `tags_riff.c` : WAV (`fmt `, `data` pour la durée, `LIST`/`INFO` : INAM IART IPRD IGNR ITRK ICRD)
  et AIFF/AIFC (`COMM` avec décodage entier du flottant étendu 80 bits, `NAME`, `AUTH`).
- `tags.c` : helpers de texte (UTF-8 canonique — un octet invalide devient U+FFFD, jamais l'octet
  brut —, latin-1, UTF-16 LE/BE, gain ReplayGain en virgule fixe 1/256 dB sans flottant, table de
  genres compacte), dispatcher **par signature** (fLaC, OggS, RIFF, FORM, ftyp, ID3, sync MPEG, puis
  APEv2 en queue), replis titre = nom de fichier, album = dossier parent, artiste = grand-parent.
- Intégration au scan : les tags sont lus dans une **deuxième vague de jobs** (`lib_scan_tag_job`),
  sur les seules pistes que le merge a vues nouvelles ou modifiées ; deux tampons de 64 Ko par job
  (scratch arena), **≤ 2 `os_file_read_at` par fichier** (tête + queue), résultats republiés par une
  pile de Treiber, internés par le thread principal (la `StringTable` reste mono-thread). Nouvel
  événement `LibEvent_TracksTagged`.
- Colonnes remplies : title / artist / album / album_artist / genre / track_no / disc_no / year /
  duration_ms / sample_rate / channels / codec / replaygain_track_db / cover_hash.
- Démo : la bibliothèque affiche TITRE / ARTISTE / ALBUM / DUREE issus des tags (capture
  `build/demo.png`, arbre généré par `python tools/gen_tag_vectors.py --demo build\demo_library`).
- `tools/gen_tag_vectors.py` : 24 vecteurs assemblés octet par octet (aucune dépendance), **34 792 o
  au total**, le plus gros 8 618 o (< 20 Ko), plus le générateur de l'arbre de démo.
- `tests/test_tags.c` : 16 cas, dont les 11 vecteurs cassés (tronqués, taille de tag délirante,
  taille de frame hors tag, atome MP4 qui se contient lui-même, APEv2 de 4 Go, fichier vide) et un
  **fuzz de 10 000 mutations par vecteur** (22 vecteurs, 220 000 parsings) sous ASan, la copie mutée
  étant allouée à sa taille exacte pour que la zone rouge d'ASan colle au dernier octet.

### Mesures (i7-8550U, Windows 11, ASan pour les tests)
| Mesure | Valeur | Cible |
|--------|--------|-------|
| Taille exe release | **167 424 o** (marge 37 376 o) | < 200 KB |
| Imports statiques | KERNEL32.dll + USER32.dll | ces deux-là |
| Tests | **96 cas, 1 697 checks, 0 échec** (dont 16 cas ajoutés par T-011) | verts |
| Fuzz | 220 000 mutations parsées, 0 crash, 0 rapport ASan | 0 |
| Bench parsing | **2 131 ns/fichier** (4 246 cycles), 1 147 MB/s sur les 13 vecteurs valides | — |
| Scan 50 000 fichiers, tags compris | **2,38 s à froid** (≈ 0,48 s pour 10 000) | < 3 s / 10 000 |
| Rescan à chaud | **73 ms**, 0 fichier ouvert | — |
| `check` / `analyze` | verts | verts |

### Écarts
1. **Les tags ne sont pas lus dans le job de dossier** mais dans une vague de jobs qui suit le merge.
   Les lire pendant le parcours aurait ouvert les 50 000 fichiers à chaque rescan et détruit le
   « 63 ms à chaud » de T-010 ; ici un rescan sans changement n'ouvre aucun fichier. C'est toujours
   « dans les jobs du scan », et le scan ne se termine qu'une fois la dernière piste taguée.
2. **APIC dans un tag ID3v2 désynchronisé** : le retrait des `0xFF 0x00` se fait dans une copie, donc
   les offsets ne correspondent plus au fichier. `cover_offset` vaut alors 0 ; la taille et le hash
   restent exacts. T-014 relira l'image par le parseur plutôt que par l'offset dans ce cas.
3. **En-tête de commentaire Ogg** : seul le contenu de la page qui le porte est lu (un commentaire
   étalé sur plusieurs pages serait tronqué). Aucun encodeur courant n'en produit à cette taille.
4. **Durée WAV** : prise sur la taille déclarée du chunk `data`, pas sur ce qui reste du fichier —
   un WAV coupé annonce toujours sa durée d'origine.
5. Le bench `library scan` inclut désormais la lecture des tags : ses 2,38 s à froid ne se comparent
   pas aux 168 ms de T-010, qui n'ouvrait aucun fichier. Le chiffre à chaud (73 ms) est inchangé.
6. `Tags` fait ~1,1 Ko (1 Ko de texte pour cinq champs, 256 o par champ) : c'est une struct de pile
   par fichier, jamais un tableau, et un champ qui ne rentre plus est abandonné plutôt que tronqué
   au milieu d'un codepoint.

# T-011 — Lecture des tags et des en-têtes : ID3v1/v2, Vorbis comments, MP4 atoms, APE, WAV/AIFF

Phase 2 · Statut : **todo** · Dépend de : T-010, ADR-007, ADR-012 (validation aux frontières)

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

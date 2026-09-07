# T-040 — Décodeurs : minimp3, dr_flac, dr_wav/AIFF, stb_vorbis + Media Foundation (AAC/ALAC/WMA)

Phase 5 · Statut : **todo** · Dépend de : T-014, ADR-007

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

# R-04 — Recherche encodeur ATRAC3 (Opus, 2026-09-06)

Rôle : ingénieur DSP/codec. Question : porter l'encodeur ATRAC3 d'atracdenc (C++) en C, ou écrire un encodeur
depuis le format en s'appuyant sur le décodeur ffmpeg (libavcodec/atrac3.c) comme référence bitstream.

Sources à lire : code atracdenc (atrac3*.cpp, qmf, mdct, bitalloc, main), ffmpeg atrac3.c/atrac3data.h/atrac.c,
netmd-js/Web MiniDisc (framing envoyé au device), minidisc.wiki, multimedia.cx, brevets Sony.

Sections : essentiels du format (1024 samples/frame, tailles LP2/LP4/LP105, QMF 3 bandes, MDCT, gain control,
tonal components, tables scalefactor/wordlen/Huffman, layout bit-exact d'une frame, joint stereo LP4) ;
architecture d'atracdenc (fichiers, LOC, pipeline, deps, licence et compatibilité MIT/zlib) ; qualité vs Sony,
patches asivery, encodeur ffmpeg ? DLL Sony ? ; encodage ATRAC3 sur le device possible ? ; container/framing
exact pour l'upload (WAVE_FORMAT_SONY_ATRAC3 0x0270, block align, padding, silent frame, PCM SP big-endian 2048) ;
recommandation & plan de port par jalons, tests round-trip ffmpeg, perf ≥ 50x temps réel, API C ; tables à extraire.
Format : Markdown français, 1200-2500 lignes, chemins de fichiers et URLs.

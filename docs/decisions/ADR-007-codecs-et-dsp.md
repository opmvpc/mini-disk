# ADR-007 — Codecs de décodage et pipeline DSP

Statut : **accepté** (2026-09-06) — source : research/03 §6

## Décision — décodage
| Format | Décodeur | Licence | Taille approx. |
|--------|----------|---------|----------------|
| MP3 | minimp3 | CC0 | 35 KB |
| FLAC | dr_flac | MIT-0 / public domain | 45 KB |
| WAV / AIFF | dr_wav (+ parseur AIFF maison) | MIT-0 | 25 KB |
| OGG Vorbis | stb_vorbis | MIT / public domain | 60 KB |
| AAC / M4A / ALAC / WMA | **Media Foundation** (`IMFSourceReader`, COM en C) | Windows inbox | ~300 lignes, 0 KB |
| Opus | **pas en v1** (pas de single-header crédible, +250 KB) | — | — |
| Pochettes | WIC (Windows Imaging Component) | inbox | 0 KB |

Tags écrits à la main : ID3v1/v2.3/v2.4, Vorbis comments, MP4 atoms (`ilst`), APE. Pas de TagLib.

## Décision — pipeline (ordre canonique, à ne pas changer)
1. Décodage → f32 désentrelacé.
2. Rééchantillonnage vers 44 100 Hz : sinc polyphase fenêtré Kaiser, 64 taps, table générée au runtime.
3. Downmix mono optionnel (pour SP mono / LP mono).
4. Mesure de loudness EBU R128 (K-weighting, gating) validée contre `ffmpeg -af ebur128` ; gain vers une
   cible (défaut −14 LUFS, réglable), avec plafond true-peak −1 dBTP (on réduit le gain, on ne clippe pas).
5. Trim silences début/fin (seuil réglable), fondu optionnel, gestion du gap inter-pistes.
6. Dither TPDF vers s16.
7. Sortie : PCM s16 **big-endian** entrelacé (SP, frames de 2048 octets) ou ATRAC3 (LP, ADR-009).

Exécution par job system (N-1 threads), buffers d'arène réutilisés, SSE2 baseline, dispatch AVX2 plus tard.
Cible ≥ 50x temps réel par piste hors ATRAC3.

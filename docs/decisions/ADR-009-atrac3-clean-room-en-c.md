# ADR-009 — Encodeur ATRAC3 : réécriture clean-room en C, ffmpeg comme oracle

Statut : **accepté** (2026-09-06) — source : research/04

## Contexte
LP2 (132 kbps, 384 o/frame) et LP4 (66 kbps, 192 o/frame, joint stereo obligatoire) exigent des frames
ATRAC3 encodées côté hôte : **aucun chemin USB ne fait encoder de l'ATRAC3 au device** (seul le PCM →
ATRAC1/SP est encodé à bord). Le seul encodeur open source, atracdenc, est **LGPL-2.1 et C++17** : un port
ligne à ligne serait une œuvre dérivée, incompatible avec un exe unique statique sous licence MIT/zlib.

## Décision
- Réimplémentation **from scratch en C99** de l'encodeur ATRAC3 (~2 150 lignes estimées), en utilisant le
  décodeur ffmpeg (`libavcodec/atrac3.c`) comme spécification de bitstream et comme oracle de test
  (round-trip encode → decode ffmpeg → PSNR/ODG). atracdenc sert uniquement de documentation
  d'heuristiques (bit allocation, psy) et de second oracle.
- Tables : celles qui sont des faits du format (scalefactors, wordlen, Huffman canoniques, split des BFU)
  sont régénérées ; la table ATH héritée de Musepack et la courbe de loudness d'atracdenc ne sont **pas**
  copiées (formule analytique de Terhardt à la place).
- MDCT maison ou kissfft (BSD-3, compatible).
- Livré en **phase 6**, après SP. Filet de secours si la phase 6 dépasse 4 semaines : exe externe
  atracdenc optionnel appelé par l'app (jamais embarqué).
- Perf cible ≥ 50x temps réel scalaire ; point critique = cache `<bfu, wordlen>` de l'allocation de bits.

## Pièges documentés (research/04)
- Facteur 2^16 entre `ff_atrac_sf_table` et la ScaleTable d'atracdenc.
- LP4 : second canal écrit à l'envers depuis la fin du bloc, bit reservoir inter-canaux, préambule JS.
- Croisement des bandes QMF et inversion spectrale des bandes impaires.
- `nBlockAlign` ∈ {192, 304, 384} ; netmd-js compte des "frames" de moitié (192/152/96).

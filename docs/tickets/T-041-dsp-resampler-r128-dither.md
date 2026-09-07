# T-041 — DSP : rééchantillonnage sinc, loudness EBU R128, true-peak, trim/fade/gap, dither TPDF, sortie PCM BE

Phase 5 · Statut : **todo** · Dépend de : T-040, ADR-007 (ordre canonique du pipeline), research/03 §6.4-6.6

## Livrables
- `src/core/dsp/dsp_resample.c` : sinc fenêtré Kaiser (β ≈ 9, 64 taps par phase, 512 phases, table
  générée au runtime dans une arène), n'importe quel taux → 44 100 Hz, interpolation linéaire entre
  phases, SSE2 pour le produit scalaire, latence compensée, traitement par blocs sans allocation.
- `dsp_loudness.c` : EBU R128 / ITU-R BS.1770-4 : filtre K (pré-shelf + high-pass, coefficients à
  44,1 kHz), blocs de 400 ms à 75 % de recouvrement, gating absolu −70 LUFS et relatif −10 LU,
  loudness intégrée ; **true-peak** par suréchantillonnage ×4 (polyphase court) ; gain vers une cible
  (défaut −14 LUFS, réglable) plafonné pour rester ≤ −1 dBTP (on réduit le gain, on ne limite pas).
- `dsp_edit.c` : downmix mono (L+R)/2, trim de silence tête/queue (seuil −60 dBFS, hystérésis 50 ms),
  fondu entrant/sortant (courbe cosinus), gap inter-pistes (silence ajouté ou coupé), gain fixe par piste.
- `dsp_dither.c` : TPDF 16 bits (LCG rapide), optionnel noise-shaping simple, écriture **PCM s16
  big-endian entrelacé** par frames de 2 048 octets (format SP NetMD, research/04 §6) ou s16 LE pour
  un aperçu WAV.
- `src/core/pipeline/pipeline.c` : la chaîne complète par piste selon l'ordre d'ADR-007, exécutée
  en jobs (une piste = un job, blocs de 4 096 frames), progression, annulation, sortie vers un fichier
  de cache (T-047) ; **aperçu** : export WAV 44,1 kHz du résultat pour écoute (le lecteur audio interne
  est phase 7).
- Tests : `tests/test_dsp.c` — resampler : réponse impulsionnelle symétrique, sinus 1 kHz 48 k → 44,1 k
  SNR > 90 dB, aliasing d'un sinus 20 kHz < −90 dB ; R128 : vecteurs de conformité EBU Tech 3341
  (sinus 1 kHz à −23 dBFS → −23 LUFS ± 0,1, cas de gating 3341-3/4, true-peak Tech 3342 ± 0,2 dB)
  générés par script ; dither : moyenne nulle, variance attendue ; trim/fade/gap : longueurs exactes ;
  BE : octets attendus.
- Bancs : resampler ≥ 200x temps réel, R128 ≥ 500x, pipeline complet d'un MP3 de 4 min < 0,5 s.

## Critères d'acceptation
- Conformité EBU sur les vecteurs ; sortie SP bit-exacte sur un WAV 44,1 k sans traitement (bypass).
- Aucune allocation par bloc. Exe < 470 KB. Tests, check, analyze verts.

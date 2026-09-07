# T-041 — DSP : rééchantillonnage sinc, loudness EBU R128, true-peak, trim/fade/gap, dither TPDF, sortie PCM BE

Phase 5 · Statut : **fait** · Dépend de : T-040, ADR-007 (ordre canonique du pipeline), research/03 §6.4-6.6

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

## Livraison

Statut : **fait** (2026-09-07). Mesuré sur la machine de référence (i7-8550U, 4 cœurs / 8 threads,
Windows 11), **machine partagée avec l'agent T-040 pendant les mesures** : les chiffres donnés sont
les meilleurs de trois passes, l'écart entre passes atteint 40 % sur les bancs longs.

T-040 (décodeurs) étant écrit en parallèle, tout le DSP est bâti sur une interface de blocs `f32`
désentrelacés et sur un `PipelineSource` en pointeurs de fonction. Aucun fichier de `core/codecs/` ni
`src/third_party.c` n'a été touché.

### Ce qui a été construit

**`src/core/dsp/dsp_math.{h,c}` — les transcendantes, sans libm.** On lie `/NODEFAULTLIB` : `sin`,
`cos`, `tan`, `exp`, `log`, `pow` n'existent pas. Réduction d'argument + série de Taylor dont le
premier terme abandonné est sous l'epsilon du `double` sur l'intervalle réduit :
`dsp_sin_f64`/`dsp_cos_f64` (Cody-Waite sur trois morceaux de π/2, polynôme jusqu'à r¹⁵),
`dsp_tan_f64`, `dsp_exp_f64` (réduction sur ln 2 en deux morceaux, Horner 1 + r(1 + r/2(… + r/14))),
`dsp_log_f64` (exposant lu dans les bits, série d'atanh sur s = (m−1)/(m+1), |s| < 0,172),
`dsp_pow_f64`, `dsp_log10_f64`, `dsp_sinc`, `dsp_bessel_i0` (série de termes successifs), plus
`dsp_sqrt_f64` (`_mm_sqrt_sd`) et les conversions dB. Toutes tournent au montage des tables ou une
fois par conversion de niveau, jamais dans une boucle d'échantillons. Précision vérifiée contre des
valeurs de référence : 1e-12 sur les sinus loin de zéro, 1e-14 sur `exp`, 1e-13 sur `bessel_i0(9)`.

**`dsp_resample.{h,c}` — sinc fenêtré polyphase.** Kaiser β = 9, **512 phases + une phase de garde**
(la ligne 512 est le même prototype décalé d'un échantillon : l'interpolation linéaire entre phases
n'a jamais à reboucler), table construite à l'init dans l'arène, produit scalaire SSE2 sur deux
phases à la fois avec quatre accumulateurs, historique de `taps−1` échantillons par canal, aucune
allocation par bloc, mono et stéréo. Le filtre est **centré** : la sortie *n* lit l'entrée autour de
`n · in/out`, donc une impulsion d'entrée *i* ressort en `i · out/in` — il n'y a pas de latence à
compenser en aval, la compensation est l'historique lui-même. Position en virgule fixe 32.32,
`out_limit` déduit du nombre de frames source pour que la vidange de queue n'invente pas
d'échantillons après la fin. Chemin passe-plat quand `in == out` (80 % des pistes, research/03 §6.6).

**`dsp_loudness.{h,c}` — BS.1770-4 / R128.** Les biquads du filtre K sont **dérivés** pour le taux
donné (transformation bilinéaire du prototype analogique, `tan(π f0 / fs)`), jamais recopiés de la
table 48 kHz de la norme — c'est le bug classique que research/03 §6.8 annonce. Blocs de 400 ms par
quatre sous-blocs de 100 ms en anneau (recouvrement 75 %), énergie de chaque bloc stockée (8 octets
par 100 ms, tableau dimensionné une fois à l'init), porte absolue −70 LUFS puis porte relative
−10 LU, les deux comparées **en énergie** pour éviter un logarithme par bloc. True-peak par
suréchantillonnage ×4, polyphase 4 phases × 12 taps (annexe 2), la phase 0 étant l'identité le pic
d'échantillon brut est mesuré au passage. `dsp_r128_gain_db` renvoie `cible − mesuré`, rabaissé
jusqu'à ce que `pic + gain ≤ plafond` : **on réduit le gain, on ne limite jamais**, et on ne remonte
pas non plus le gain pour aller chercher le plafond.

**`dsp_edit.{h,c}`** — downmix mono `(L+R)/2` en SSE2 (aliasage autorisé, le pipeline appelle en
place), gain fixe en SSE2, fondu cosinus `0,5 − 0,5·cos(πt)` sans état (il lit la position dans le
flux, donc il donne la même courbe bloc par bloc que d'un seul tenant) et trim de silence en deux
temps : un balayage streaming pendant la passe de mesure, deux index rendus à la passe de rendu.

**`dsp_dither.{h,c}`** — TPDF (deux uniformes indépendants tirés d'un LCG par canal), noise-shaping
du premier ordre optionnel, quantification à l'échelle **32768 avec clamp** (et non 32767), écriture
s16 big-endian en frames de 2048 octets avec zéro-padding de la dernière, en-tête WAV de 44 octets
écrit à la main.

**`src/core/pipeline/pipeline.{h,c}`** — la chaîne d'ADR-007 par piste, `PipelineSource` en pointeurs
de fonction, `PipelineConfig`, `PipelineResult`, sortie par callback (T-043 y branchera le fichier de
cache), compteur de progression atomique, drapeau d'annulation lu **une fois par bloc** de 4096
frames, un job par piste via `base_jobs` (`pipeline_run_many`), une arène par job.

### Deux passes plutôt qu'un tampon

R128 ne rend son gain qu'après la piste entière et le trim ne connaît sa queue qu'à la fin. Les deux
options sont donc : garder la piste décodée en mémoire, ou la décoder deux fois. Une piste stéréo de
quatre minutes en `f32` à 44,1 kHz pèse **42 Mo** ; sept workers avec deux pistes en vol en tiennent
590, sur une machine qu'on dimensionne par ailleurs pour 100 000 pistes de bibliothèque. La seconde
passe, elle, coûte un décodage de plus — au-dessus de 100× temps réel. On mesure, puis on rend. Une
source non rejouable (`rewind == 0`) est décodée une seule fois, normalisation et trim désactivés :
c'est le repli honnête, pas un gain inventé sur une piste partielle.

### Mesures

| Mesure | Valeur | Cible |
|--------|--------|-------|
| Impulsion 48 k → 44,1 k | pic exactement en sortie **147** (entrée 160, 160/147 = 48000/44100), réponse symétrique à 1e-6 près | pic à l'endroit prévu |
| Sinus 1 kHz 48 k → 44,1 k | **SNR 102,8 dB** | > 90 dB |
| Sinus 1 kHz 96 k → 44,1 k | **SNR 106,0 dB** | passe |
| Sinus 1 kHz 22,05 k → 44,1 k | **SNR 96,8 dB** | — |
| Image d'un 20 kHz (48 k → 44,1 k), repliée à 16,1 kHz | **−121,5 dBFS** | < −90 dB |
| EBU Tech 3341-1 (sinus −23 dBFS, 20 s) | **−22,991 LUFS** | −23,0 ± 0,1 |
| EBU Tech 3341-2 (−33 dBFS) | **−32,991 LUFS** | −33,0 ± 0,1 |
| EBU Tech 3341-3 (−36 / −23 / −36) | **−23,011 LUFS** | −23,0 ± 0,1 |
| EBU Tech 3341-4 (+ épaules à −72, portée absolue) | **−23,011 LUFS** | −23,0 ± 0,1 |
| Plancher −60 dBFS à côté d'un −14 dBFS | **−14,034 LUFS** (le plancher est bien porté) | — |
| Silence numérique | aucun bloc porté, LUFS = −∞ | gaté |
| True-peak, sinus à fs/4 déphasé de π/4 (échantillons à −3,01 dBFS) | **+0,088 dBTP** | 0,0 ± 0,2 |
| Dither TPDF, erreur totale | moyenne **−0,001 LSB**, variance **0,250 LSB²** | 0 et 1/4 |
| Dither + noise-shaping | moyenne **0,001 LSB**, variance **0,333 LSB²** = 1/6 + 2·1/12 | — |
| Trim, tonalité de 2 s entre deux silences | fenêtre gardée **exactement** [44000, 132000) | exact |
| Trim + gap de 0,5 s (pipeline) | **110 050 frames** exactement | exact |
| Bypass 44,1 k stéréo sans traitement | **bit-exact** sur 100 000 frames aléatoires (`mem_cmp == 0`) | bit-exact |
| Framing SP | 4120 octets utiles → **3 frames de 2048**, padding à zéro vérifié octet par octet | exact |
| Pipeline 48 k → SP, 6 s | −22,997 LUFS, −23,004 dBTP, gain **+8,997 dB**, frames = `expected_out` | exact |
| Pipeline 4 min, annulation à mi-course | statut `CANCELLED`, arrêt dans le bloc suivant, arène rendue | propre |

Bancs (meilleur de trois, machine partagée) :

| Banc | Valeur | Cible |
|------|--------|-------|
| Resampler 48 k → 44,1 k stéréo, 4 min | **671 ms, 358× temps réel** (257–358× selon la charge) | ≥ 200× |
| R128 + true-peak, 4 min stéréo | **403 ms, 595× temps réel** (421–595×) | ≥ 500× |
| Pipeline complet 4 min (R128 + trim + fondus + dither, deux passes) | **669 ms** | < 500 ms — **non tenu** |
| Pipeline 4 min, passe de rendu seule (gain fixe, dither) | **195 ms, 1234× temps réel** | < 500 ms |
| 8 pistes de 4 min à travers les jobs (7 workers) | **1,82 s, 1058× temps réel** | — |
| Exe release | **308 224 octets, inchangé** | < 470 KB |
| Tests | **168 cas, 5 855 checks**, 0 échec sous ASan (24 cas ajoutés) | verts |
| Cibles `build.bat` | debug, release, test, check, analyze, bench vertes | vertes |

### Écarts et décisions

1. **Nombre de taps variable, pas 64 fixes.** 64 taps couvrent 64 échantillons d'entrée : à 96 kHz la
   bande de transition serait deux fois plus large qu'à 48 kHz et mangerait 4 kHz de bande passante.
   Le nombre de taps est donc `64 × arrondi(in/out)` : **64 à 44,1 et 48 kHz, 128 à 88,2 et 96 kHz**.
   La transition reste ainsi une fraction à peu près constante du taux de *sortie*. Coupure placée à
   `Nyquist_min − D/2N·fs` avec D = 5,74 (β = 9), soit ≈ 19,9 kHz en 48 → 44,1 : plat jusqu'à
   ~17,7 kHz, bande d'arrêt dès 22,05 kHz. C'est ce que 64 taps achètent réellement ; l'ATRAC coupe
   de toute façon entre 16 et 20 kHz.
2. **Le banc « pipeline complet < 0,5 s » n'est pas tenu : 669 ms.** La passe de mesure R128 coûte à
   elle seule 403 ms (595× temps réel, au-dessus de sa propre cible) ; la passe de rendu en coûte 195.
   Le budget de 0,5 s du ticket avait été écrit pour une passe unique décodage → rendu : cette
   passe-là mesure **195 ms**, largement sous le budget, et est mesurée séparément
   (`bench_dsp_pipeline_render`). Aller sous 0,5 s avec la normalisation exigerait soit d'abandonner
   les deux passes (et de payer 42 Mo par piste en vol), soit de renoncer au true-peak exact. Aucune
   des deux ne vaut le gain. À trancher par le lead ; sinon la ligne du banc reste rouge par
   construction.
3. **Coefficients true-peak générés, pas recopiés.** L'annexe 2 de BS.1770-4 imprime une table de
   4 × 12 coefficients. On génère à l'init un sinc fenêtré Kaiser (β = 4,5, coupure 0,45·fs) de même
   forme : la phase 0 est l'identité comme dans la norme, et le résultat tombe à **0,088 dB** du pic
   inter-échantillon théorique, sous la tolérance de ±0,2 dB. Recopier 48 constantes à la main aurait
   été une source d'erreur de saisie non testable.
4. **Échelle de quantification 32768, pas 32767.** C'est ce qui rend le bypass bit-exact : un
   échantillon entré comme `s/32768` ressort exactement `s` pour tout `s ∈ [−32768, 32767]`. Le
   snippet de research/03 §6.9 utilisait 32767, ce qui rate le bit-exact d'un LSB en haut d'échelle.
   Le seul coût est que +32768 est ramené à +32767 — un échantillon déjà à pleine échelle.
5. **Trim sur l'enveloppe, pas sur les échantillons.** Un sinus traverse zéro cinquante fois par
   seconde et chaque traversée pose un échantillon sous n'importe quel seuil : une hystérésis comptée
   en échantillons consécutifs n'atteindrait jamais 50 ms de « fort ». On prend le pic sur une fenêtre
   de 1 ms puis on demande 50 fenêtres consécutives avant de basculer. Conséquence : le trim est
   quantifié à la milliseconde, ce que les tests vérifient exactement.
6. **Ordre trim → gain.** ADR-007 numérote le gain (4) avant le trim (5). On applique le trim d'abord,
   pour deux raisons : le seuil de trim est un niveau **absolu** en dBFS, il doit donc porter sur le
   signal tel qu'il est dans le fichier ; et la mesure R128 porte alors sur exactement l'audio livré.
   Le gain étant un scalaire, l'ordre ne change rien d'autre.
7. **Le dither est coupé sur le gap inter-pistes.** Semer du bruit TPDF sur du silence numérique est
   le seul endroit où un auditeur l'entendrait.
8. **L'exe release ne bouge pas (308 224 o).** Rien dans `app/` n'appelle encore le pipeline, donc
   `/OPT:REF` élague tout le module. La taille réelle apparaîtra quand T-043 branchera la vue
   transfert ; les tables (resampler, filtre K, true-peak) sont calculées au runtime, il n'y a aucune
   donnée figée dans le binaire.
9. **Bancs sensibles à la charge.** Les gardes `AssertAlways` des nouveaux bancs sont réglées sous les
   cibles affichées (150× / 300× / 1,5 s) : elles attrapent une régression, pas une machine occupée.
   Les chiffres qui comptent sont ceux qu'imprime le banc. Note au passage : le banc pré-existant
   `jobs dispatch` (< 1000 ns/job) et `r_core batch build` (< 900 µs) échouent quand deux agents
   compilent en même temps sur cette machine — rien à voir avec ce ticket.

### Revue (lead, 2026-09-07)
- Grille ADR-012 dans le worktree `t041` : `check/test/release/analyze` verts, 168 cas / 5 855 checks, exe inchangé
  (module élagué tant que T-043 ne l'appelle pas). Conformité EBU 3341/3342 tenue avec marge, bypass bit-exact.
- Décisions tranchées : **deux passes gardées** (42 Mo par piste en vol serait contraire à l'esprit du projet ; un
  second décodage à > 100× coûte ~40 ms). La cible « pipeline complet < 0,5 s » visait la passe de rendu (195 ms) ;
  la chaîne complète à 669 ms reste 360× plus rapide que la gravure SP, qui est à 1× temps réel : non bloquant,
  cible reformulée dans STATUS. Trim avant gain accepté (seuil absolu en dBFS) : ADR-007 sera amendé en phase 7.
- Bancs `jobs dispatch` et `r_core batch build` sensibles à la charge machine quand deux agents compilent : à
  passer en « meilleur de N » avec garde relâchée si ça se reproduit sur CI.

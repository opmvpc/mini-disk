# R-04 — Encodeur ATRAC3 pour NetMD LP2 / LP4 en C pur

> Recherche menée le 2026-09-06 pour le projet `mini-disk` (application C pure, licence cible MIT/zlib).
> Question posée : **porter l'encodeur ATRAC3 d'`atracdenc` (C++) vers C**, ou **écrire un encodeur depuis
> zéro en se servant du décodeur FFmpeg (`libavcodec/atrac3.c`) comme référence bitstream** ?
>
> Sources primaires lues intégralement ou partiellement pendant cette recherche :
> `dcherednik/atracdenc` (`src/atrac/at3/*`, `src/atrac3denc.{h,cpp}`, `src/qmf/*`, `src/atrac/atrac_scale.cpp`,
> `src/atrac/atrac_psy_common.cpp`, `src/CMakeLists.txt`, `src/main.cpp`, `LICENSE`),
> `FFmpeg/libavcodec/atrac3.c`, `atrac3data.h`, `atrac.c`,
> `cybercase/netmd-js` (`src/netmd-interface.ts`),
> `asivery/webminidisc` (`src/utils.ts`, `src/services/audio/*`).

---

## Table des matières

1. [Résumé exécutif et recommandation](#1-résumé-exécutif-et-recommandation)
2. [Essentiels du format ATRAC3](#2-essentiels-du-format-atrac3)
   - 2.1 [Paramètres globaux](#21-paramètres-globaux)
   - 2.2 [Banc de filtres QMF (3 étages, 4 bandes)](#22-banc-de-filtres-qmf-3-étages-4-bandes)
   - 2.3 [MDCT 512 points par bande](#23-mdct-512-points-par-bande)
   - 2.4 [Gain control (compensation de gain)](#24-gain-control-compensation-de-gain)
   - 2.5 [BFU, scalefactors, word length](#25-bfu-scalefactors-et-word-length)
   - 2.6 [Quantification et codage des mantisses (CLC / VLC)](#26-quantification-et-codage-des-mantisses-clc--vlc)
   - 2.7 [Composantes tonales](#27-composantes-tonales)
   - 2.8 [Layout bit-exact d'une frame](#28-layout-bit-exact-dune-frame)
   - 2.9 [Joint stereo (LP4) — matrixing, pondération, bit reservoir](#29-joint-stereo-lp4--matrixing-pondération-bit-reservoir)
   - 2.10 [Tableau des modes et octets par frame](#210-tableau-des-modes-et-octets-par-frame)
3. [Architecture d'atracdenc](#3-architecture-datracdenc)
4. [Qualité, encodeurs concurrents, licences](#4-qualité-encodeurs-concurrents-licences)
5. [Encodage ATRAC3 sur le device : possible ?](#5-encodage-atrac3-sur-le-device--possible-)
6. [Framing exact pour l'upload NetMD](#6-framing-exact-pour-lupload-netmd)
7. [Recommandation, plan par jalons, tests, API C](#7-recommandation-plan-par-jalons-tests-api-c)
8. [Tables constantes à extraire (fichier + symbole)](#8-tables-constantes-à-extraire-fichier--symbole)
9. [Liste des URLs](#9-liste-des-urls)

---

## 1. Résumé exécutif et recommandation

### 1.1 Verdict

**Recommandation : réécriture en C « from scratch », guidée ligne à ligne par le décodeur FFmpeg pour le
bitstream, et par `atracdenc` uniquement comme *documentation de référence* (algorithmes, ordre des champs,
astuces psychoacoustiques) — sans copier de code.**

Il ne s'agit pas d'un choix esthétique mais d'une contrainte **juridique** :

| Projet | Licence | Compatible avec un binaire MIT/zlib statique ? |
|---|---|---|
| `dcherednik/atracdenc` | **LGPL v2.1 or later** (en-tête de *chaque* fichier `src/atrac/at3/*`, `src/atrac3denc.cpp`, `src/qmf/qmf.h`, + fichier `LICENSE` de 502 lignes = texte LGPL 2.1) | **Non** en pratique pour un binaire statique C mono-fichier : la LGPL impose la possibilité de relier une version modifiée de la bibliothèque (§6 : fourniture des `.o`, ou linkage dynamique). Un « port » ligne à ligne en C reste une **œuvre dérivée** et doit rester LGPL. |
| `FFmpeg/libavcodec/atrac3.c` | **LGPL v2.1+** également (décodeur) | Idem — mais on n'en copie **rien** : on l'utilise comme *spécification lisible* et comme **oracle de test**. |
| Tables de constantes numériques (`subband_tab`, `clc_length_tab`, `atrac3_hufftabs`, …) | Faits/données imposés par le format | Reproductibles : ce sont les constantes du format ATRAC3, non protégeables en tant que telles, et de toute façon régénérables par formule ou par observation du décodeur. Prudence : **ne pas copier la mise en forme du code source**, réécrire les tables. |

Autrement dit : **si le projet `mini-disk` doit rester MIT/zlib et livrer un binaire statique**, porter
`atracdenc` en C oblige à publier tout le produit sous LGPL (ou à isoler l'encodeur dans une `.so`/`.dll`
LGPL séparée et relogeable). Si en revanche l'équipe accepte **une DLL/SO séparée sous LGPL** (fichier
`libatrac3enc.so` chargée dynamiquement, sources publiées), alors le portage devient l'option la plus rapide.

Les deux voies sont donc décrites plus bas, avec un plan de jalons commun : **les 80 % de l'effort
(banc QMF, MDCT, allocation de bits, bitstream) sont identiques dans les deux cas.**

### 1.2 Comparaison des deux stratégies

| Critère | (A) Port C++→C d'atracdenc | (B) Écriture from-scratch (réf. FFmpeg) |
|---|---|---|
| Effort de mise au point initiale | ~2 à 3 semaines-homme | ~5 à 8 semaines-homme |
| Risque « ça ne décode pas » | Faible (code déjà validé par des milliers d'uploads Web MiniDisc) | Moyen — mitigé par un oracle FFmpeg dès le jour 1 |
| Qualité audio atteinte | Celle d'atracdenc (voir §4) : correcte en LP2, médiocre en LP4 | Peut la dépasser (on maîtrise la boucle d'allocation) mais nécessite du tuning d'écoute |
| Licence du résultat | **LGPL 2.1 imposée** | MIT/zlib possible |
| C++→C : difficultés | `std::vector`, templates (`TQmf<N>`, `TMDCT<512>`), lambdas, `std::function` dans le gain processor, exceptions, RAII | — |
| Dépendances à porter | kissfft (BSD-3, bundlable), pas de libsndfile ni libgha pour ATRAC3 | kissfft ou une FFT/MDCT maison (~200 LOC) |
| Nombre de LOC C à écrire (estimation) | ~2 800 | ~2 500 |
| Bit-exactitude vs Sony | Non requise (le device ne vérifie rien) | Non requise |

### 1.3 Chiffres de cadrage (mesurés sur le dépôt amont, `master`, 2026-09)

```
src/atrac/at3/atrac3.h              280 lignes   (tables + paramètres + settings)
src/atrac/at3/atrac3.cpp             56 lignes   (définitions constexpr + choix du mode)
src/atrac/at3/atrac3_bitstream.h     71 lignes
src/atrac/at3/atrac3_bitstream.cpp  850 lignes   ★ cœur : alloc de bits + écriture bitstream
src/atrac/at3/atrac3_qmf.h           44 lignes   (assemblage 3 QMF)
src/atrac3denc.h                    135 lignes
src/atrac3denc.cpp                  869 lignes   ★ pipeline : MDCT, gain, tonal, matrixing
src/qmf/qmf.h                       ~90 lignes   (template TQmf<nIn>)
src/qmf/qmf.cpp                     ~80 lignes   (TapHalf[24] + fenêtre 48 taps)
src/atrac/atrac_scale.cpp           199 lignes   (quantification, scalefactors)
src/atrac/atrac_psy_common.cpp      201 lignes   (ATH type Musepack, spread)
src/transient_detector.cpp          484 lignes   (détection de transitoires → gain control)
src/gain_processor.h                122 lignes   (modulation/démodulation de gain)
src/main.cpp                        757 lignes   (CLI — non nécessaire)
LICENSE                             502 lignes   = GNU LGPL 2.1
```

**Surface strictement nécessaire à un encodeur ATRAC3 (hors CLI, hors ATRAC1, hors ATRAC3+) :
environ 2 700 à 3 000 lignes C++**, dont ~1 700 réellement algorithmiques.

### 1.4 Rappel des cibles NetMD

| Mode | Débit | Frame audio (stéréo) | Frame « fil » NetMD | Joint stereo | Durée / frame |
|---|---|---|---|---|---|
| **SP** | 292 kbps (ATRAC1) | — (encodé **par le device**) | 2048 o de PCM s16**BE** | non | 11,61 ms |
| **LP2** | 132,3 kbps | **384 o** | 192 o | **non** | 23,22 ms |
| LP105 | 104,7 kbps | 304 o | 152 o | non | 23,22 ms |
| **LP4** | 66,15 kbps | **192 o** | 96 o | **oui** | 23,22 ms |

> La « frame fil » NetMD (`FrameSize` dans `netmd-js`) vaut exactement **la moitié** de la frame audio
> ATRAC3 : c'est la taille d'une *sound unit* mono. Comme la sortie de l'encodeur est toujours un multiple
> de la frame audio (2 × sound unit), l'alignement est automatiquement satisfait.

---

## 2. Essentiels du format ATRAC3

ATRAC3 (Adaptive TRansform Acoustic Coding 3), alias **RealAudio 8 / cook-like**, alias
`WAVE_FORMAT_SONY_SCX` = **`0x0270`**. Codec par transformée, 44,1 kHz, stéréo, à débit **constant**
(pas de bit reservoir inter-frames ; seulement, en joint stereo, un reservoir *inter-canaux* à l'intérieur
d'une même frame — voir §2.9).

### 2.1 Paramètres globaux

| Paramètre | Valeur | Source |
|---|---|---|
| Fréquence d'échantillonnage | 44 100 Hz (imposé par NetMD ; le format tolère d'autres valeurs en OMA) | `atrac3.c:atrac3_decode_init` refuse tout ce qui n'est pas `SAMPLES_PER_FRAME * channels` |
| Échantillons par frame et par canal | **1024** (`SAMPLES_PER_FRAME`) | `TAtrac3Data::NumSamples = 1024` |
| Durée d'une frame | 1024 / 44100 = **23,2199 ms** | — |
| Nombre de coefficients spectraux / canal | **1024** (`NumSpecs`) | `TAtrac3Data::NumSpecs` |
| Bandes QMF | **4** (`NumQMF`) de 256 coefficients chacune | `TAtrac3Data::NumQMF = 4` |
| Taille MDCT | **512** points → 256 coefficients par bande | `TAtrac3Data::MDCTSz = 512`, FFmpeg `av_tx_init(..., 256, AV_TX_FULL_IMDCT)` |
| BFU max (Block Floating Units) | **32** (`MaxBfus`) | `TAtrac3Data::MaxBfus = 32` |
| Délai d'encodage annoncé | `0x88E` = 2190 échantillons | `atrac3.c` : `delay != 0x88E` → erreur |
| « version » extradata | 4 | idem |
| Identifiant de sound unit | **`0x28`** sur 6 bits (`101000`) | `atrac3.c` : `if (get_bits(gb, 6) != 0x28)` |
| Identifiant de sound unit 2 en joint stereo | **`3`** sur 2 bits | `atrac3.c` : `if (get_bits(gb, 2) != 3)` |

Le pipeline complet d'encodage est :

```
PCM 16 bits stéréo 44.1 kHz
   │
   ├─ (LP4 uniquement) matrixing M/S dans le domaine temporel, par sous-bande
   │
   ▼
[QMF 3 étages]  1024 éch. → 4 × 256 éch. (bandes 0..3, band 3 et 2 croisées)
   │
   ▼
[détection de transitoires]  → courbes de gain control par bande (0..7 points)
   │
   ▼
[modulation de gain]  (division par la courbe, pour aplatir l'attaque avant MDCT)
   │
   ▼
[fenêtrage 512 + MDCT 512]  → 256 coefficients / bande ; bandes impaires inversées
   │
   ▼
[extraction des composantes tonales]  (retirées du spectre résiduel)
   │
   ▼
[scaling par BFU]  → (ScaleFactorIndex 0..63, valeurs normalisées ∈ [-1,1])
   │
   ▼
[allocation de bits]  (recherche binaire sur un « shift » global, ATH + spread)
   │
   ▼
[quantification + CLC/VLC]  → mantisses
   │
   ▼
[écriture bitstream]  → 2 sound units → 1 frame de FrameSz octets
```

### 2.2 Banc de filtres QMF (3 étages, 4 bandes)

ATRAC3 découpe 0–22,05 kHz en **4 bandes égales de 5,5125 kHz** via un arbre de QMF 2 bandes à 48 taps.

Structure (source : `src/atrac/at3/atrac3_qmf.h`) :

```c
/* 1024 échantillons en entrée */
Qmf1 : 1024 -> lo(512), hi(512)
Qmf2 : lo(512) -> subs[0] (256, 0–5.5 kHz), subs[1] (256, 5.5–11 kHz)
Qmf3 : hi(512) -> subs[3] (256, 16.5–22 kHz), subs[2] (256, 11–16.5 kHz)   /* ← inversion ! */
```

> **Piège n° 1** : la sortie *lower* du troisième QMF va dans `subs[3]` et la sortie *upper* dans `subs[2]`.
> C'est l'effet du repliement spectral du deuxième étage : la bande haute sort **inversée en fréquence**.
> Toute implémentation qui oublie ce croisement produit un fichier qui « décode » mais dont les bandes 2 et 3
> sont interverties (son métallique caractéristique).

Filtre prototype (demi-réponse, 24 coefficients ; `src/qmf/qmf.cpp:TapHalf`,
identique à `FFmpeg/libavcodec/atrac.c:qmf_48tap_half`) :

```c
static const float qmf_48tap_half[24] = {
    -0.00001461907f, -0.00009205479f, -0.000056157569f,  0.00030117269f,
     0.0002422519f,  -0.00085293897f, -0.0005205574f,    0.0020340169f,
     0.00078333891f, -0.0042153862f,  -0.00075614988f,   0.0078402944f,
    -0.000061169922f,-0.01344162f,     0.0024626821f,    0.021736089f,
    -0.007801671f,   -0.034090221f,    0.01880949f,      0.054326009f,
    -0.043596379f,   -0.099384367f,    0.13207909f,      0.46424159f
};
/* fenêtre 48 taps, symétrique, gain 2.0 : */
for (i = 0; i < 24; i++)
    qmf_window[i] = qmf_window[47 - i] = qmf_48tap_half[i] * 2.0f;
```

Analyse (transcription C directe de `TQmf<nIn>::Analysis`) :

```c
/* état persistant : float hist[nIn + 46]; initialisé à 0 */
void qmf_analysis(qmf_t *q, const float *in, size_t n, float *lower, float *upper)
{
    memmove(q->hist, q->hist + n, 46 * sizeof(float));   /* garder 46 échantillons */
    memcpy (q->hist + 46, in, n * sizeof(float));

    for (size_t j = 0; j < n; j += 2) {
        float lo = 0.0f, hi = 0.0f;
        for (size_t i = 0; i < 24; i++) {
            lo += qmf_window[2*i    ] * q->hist[48 - 1 + j - 2*i    ];
            hi += qmf_window[2*i + 1] * q->hist[48 - 1 + j - 2*i - 1];
        }
        float t = hi;
        upper[j/2] = lo - hi;      /* attention : ordre des affectations */
        lower[j/2] = lo + t;
    }
}
```

Latence : chaque étage introduit 46 échantillons de mémoire ; la latence totale du banc à 3 étages,
combinée au recouvrement MDCT, explique le `delay = 0x88E` (2190) annoncé dans l'extradata.

**Coût CPU** : 24 MAC × 2 × (nIn/2) par étage. Pour 1024 échantillons : 1024·24 + 2 × 512·24 ≈ 49 k MAC
par canal et par frame. C'est le poste le plus lourd du codec en scalaire naïf ; il se vectorise très bien
(convolution polyphasée).

### 2.3 MDCT 512 points par bande

Chaque bande de 256 nouveaux échantillons est concaténée aux 256 échantillons précédents pour former
un bloc de 512, fenêtré puis transformé par une MDCT 512 → 256 coefficients (recouvrement 50 %).

Extrait de `src/atrac3denc.cpp:TAtrac3MDCT::Mdct` :

```c
for (band = 0; band < 4; ++band) {
    float *src = bands[band];           /* [0..255] = frame précédente, [256..511] = frame courante */
    float  tmp[512];
    memcpy(tmp, src, 256 * sizeof(float));
    if (gain_modulator[band]) gain_modulator[band](tmp, src + 256);   /* cf. §2.4 */

    for (i = 0; i < 256; i++) {
        src[i]       = EncodeWindow[i]       * src[256 + i];   /* mémorise pour la frame suivante */
        tmp[256 + i] = EncodeWindow[255 - i] * src[256 + i];
    }
    mdct512(tmp, &specs[band * 256]);
    if (band & 1) reverse_256(&specs[band * 256]);             /* ← bandes impaires inversées */
}
```

**Fenêtre d'analyse** (`TAtrac3Data::EncodeWindow`, générée à l'exécution) :

```c
for (i = 0; i < 256; i++)
    EncodeWindow[i] = sinf(((i + 0.5f) / 256.0f - 0.5f) * (float)M_PI) + 1.0f;
```

soit `w[i] = 1 + sin(π·((i+0.5)/256 − 0.5))`, une fenêtre montante de 0 à 2 (non normalisée : le facteur
0,5 est commenté dans le source amont, le gain est absorbé par la table de scalefactors).

**Fenêtre de synthèse** (bi-orthogonale, pour le décodeur / les tests de round-trip interne) :

```c
for (i = 0; i < 256; i++) {
    double a = EncodeWindow[i], b = EncodeWindow[255 - i];
    DecodeWindow[i] = 2.0 * a / (a*a + b*b);
}
```

Côté FFmpeg, la synthèse utilise `mdct_window` construit par `init_imdct_window()` dans
`libavcodec/atrac3.c` avec un facteur d'échelle global `-1/sqrt(32768)` (le décodeur travaille en
« unités int16 »). **C'est là qu'est le facteur 2¹⁶ qui sépare les deux tables de scalefactors** (§2.5).

**Inversion des bandes impaires** : confirmée des deux côtés.
`atracdenc` : `if (band & 1) SwapArray(curSpec, 256);` — `FFmpeg` : `imlt(..., band & 1)` qui fait
`FFSWAP(input[i], input[255-i])` pour `i < 128` avant l'IMDCT.

**Implémentation MDCT recommandée en C** : MDCT-IV 512 → 256 via une FFT complexe N/4 = 128 points
(pré-rotation, FFT, post-rotation). ~150 LOC + une FFT radix-2/4 de 128 points, ou réutilisation de
kissfft (BSD-3-Clause, compatible MIT). Coût : ≈ 4 × (128·log₂128 complexes) par frame et par canal,
soit un ordre de grandeur **sous** le coût du QMF.

### 2.4 Gain control (compensation de gain)

Objectif : éviter le *pre-echo*. Chaque bande QMF peut porter une **courbe de gain** décrite par
0 à 7 « points de gain », appliquée par le **décodeur** sur la moitié de recouvrement.

Format d'un point (par bande, dans l'ordre bande 0 → bande `bands_coded`) :

| Champ | Bits | Domaine | Sémantique |
|---|---|---|---|
| `num_points` | 3 | 0..7 | nombre de points de gain de la bande |
| `lev_code[j]` | 4 | 0..15 | index de niveau ; `gain = 2^(4 − lev_code)` |
| `loc_code[j]` | 5 | 0..31 | position ; l'unité est `1 << loc_scale = 8` échantillons |

Contrainte de validité imposée par le décodeur FFmpeg
(`atrac3.c:decode_gain_control`) : **`loc_code[j] > loc_code[j-1]` strictement croissant**, sinon
`AVERROR_INVALIDDATA`. Un encodeur qui émet deux points à la même position produit un fichier
que FFmpeg refuse (et que certains décodeurs Sony rendent silencieux).

Tables générées (`ff_atrac_init_gain_compensation(&ctx, id2exp_offset = 4, loc_scale = 3)`) :

```c
for (i = 0; i < 16; i++)  gain_tab1[i]      = powf(2.0f, 4 - i);              /* 16 .. 2^-11 */
for (i = -15; i < 16; i++) gain_tab2[i + 15] = powf(2.0f, -1.0f / 8 * i);      /* 31 entrées   */
```

Côté `atracdenc` (identiques, symboles `TAtrac3Data::GainLevel[16]` et
`TAtrac3Data::GainInterpolation[31]`, avec `ExponentOffset = 4`, `LocScale = 3`, `LocSz = 8`).

**Côté encodeur**, la chaîne est :
1. `TTransientDetector` (`src/transient_detector.cpp`, 484 lignes) détecte une attaque dans la bande ;
2. `TAtrac3Encoder::CreateSubbandInfo()` (≈ 280 lignes, `atrac3denc.cpp:299`) construit la courbe :
   choix du nombre de points, des niveaux et des positions, avec un score de *mismatch* sur le début
   de la trame (`CalcCurveEarlyMismatchScore`) ;
3. `TGainProcessor::Modulate()` (`src/gain_processor.h`) **divise** le signal par la courbe avant MDCT ;
4. le décodeur **remultiplie** après IMDCT (`ff_atrac_gain_compensation`).

> **Simplification légitime pour un premier jet** : `atracdenc` expose l'option `--nogaincontrol`.
> Un encodeur qui écrit systématiquement `num_points = 0` pour les 4 bandes produit un flux **parfaitement
> valide** ; on perd seulement la protection anti-pre-echo sur les transitoires (castagnettes, glockenspiel).
> **C'est le bon compromis pour le jalon 1** : ~500 lignes de code (détection + construction de courbes)
> économisées, à réintroduire au jalon 5.

**Nombre de bandes codées** : le champ `bands_coded` (2 bits) indique `nb_bandes − 1`. Écrire `3`
(4 bandes) est le comportement d'`atracdenc` ; un encodeur peut coder moins de bandes pour économiser
(les bandes non codées sont mises à zéro par le décodeur, ce qui équivaut à un filtre passe-bas à
5,5 / 11 / 16,5 kHz).

### 2.5 BFU, scalefactors et word length

Le spectre de 1024 raies est découpé en **32 BFU** (*Block Floating Units*) de largeur croissante.

`BlockSizeTab[33]` (= `subband_tab[33]` chez FFmpeg) donne les bornes :

```c
static const uint16_t bfu_bounds[33] = {
      0,   8,  16,  24,  32,  40,  48,  56,
     64,  80,  96, 112, 128, 144, 160, 176,
    192, 224, 256, 288, 320, 352, 384, 416,
    448, 480, 512, 576, 640, 704, 768, 896,
   1024
};
```

Largeurs correspondantes (`SpecsPerBlock[33]` chez atracdenc) :

```
BFU  0..7   :   8 raies
BFU  8..15  :  16 raies
BFU 16..25  :  32 raies
BFU 26..29  :  64 raies
BFU 30..31  : 128 raies
```

Correspondance BFU ↔ bande QMF (`BlocksPerBand[5] = {0, 18, 26, 30, 32}`) :

| Bande QMF | BFU | Raies | Fréquences |
|---|---|---|---|
| 0 | 0 – 17 | 0 – 255 | 0 – 5,51 kHz |
| 1 | 18 – 25 | 256 – 511 | 5,51 – 11,03 kHz |
| 2 | 26 – 29 | 512 – 767 | 11,03 – 16,54 kHz |
| 3 | 30 – 31 | 768 – 1023 | 16,54 – 22,05 kHz |

Fréquence de coupure supérieure de chaque BFU à 44,1 kHz (`bfu_bounds[bfu+1] * 44100 / 2048`, kHz) —
utile pour construire la courbe ATH :

```
BFU  0.. 7 : 0.172  0.345  0.517  0.689  0.861  1.034  1.206  1.378
BFU  8..15 : 1.723  2.067  2.412  2.756  3.101  3.445  3.790  4.134
BFU 16..23 : 4.823  5.513  6.202  6.891  7.580  8.269  8.958  9.647
BFU 24..31 :10.336 11.025 12.403 13.781 15.159 16.538 19.294 22.050
```

**Scalefactor** : 6 bits par BFU codé, index 0..63.

| Implémentation | Formule | Plage |
|---|---|---|
| FFmpeg (`atrac.c:atrac_generate_tables`) | `ff_atrac_sf_table[i] = pow(2.0, (i - 15) / 3.0)` | 2⁻⁵ … 2¹⁶ |
| atracdenc (`atrac3.h`, ctor `TAtrac3Data`) | `ScaleTable[i] = pow(2.0, i/3.0 - 21.0)` | 2⁻²¹ … 2⁰ |

Les deux diffèrent d'un facteur constant **2¹⁶ = 65536**, qui compense simplement le fait qu'atracdenc
travaille avec un PCM normalisé dans `[-1, 1]` et FFmpeg avec l'échelle int16 (et son
`scale = 1/32768` dans `av_tx_init`, plus le `-1/sqrt(32768)` de la fenêtre IMDCT). **Ce facteur est le
piège n° 2** : se tromper dessus donne un fichier qui décode… 96 dB trop bas ou saturé.

Pas de quantification : **3 dB par pas** exactement (2^(1/3) en amplitude).

**Word length** (`subband_vlc_index` chez FFmpeg, `precisionPerBlock` chez atracdenc) : 3 bits par BFU,
valeur 0..7. `0` = BFU non codée (mise à zéro par le décodeur).

Quantificateur associé :

```c
/* encodeur : mantisse = round(valeur_normalisée * MaxQuant[wl]) */
static const float max_quant[8] = { 0.0f, 1.5f, 2.5f, 3.5f, 4.5f, 7.5f, 15.5f, 31.5f };
/* décodeur : valeur = mantisse * sf_table[sfi] * inv_max_quant[wl] */
static const float inv_max_quant[8] = {
    0.0f, 1.0f/1.5f, 1.0f/2.5f, 1.0f/3.5f, 1.0f/4.5f, 1.0f/7.5f, 1.0f/15.5f, 1.0f/31.5f
};
```

Donc, en CLC (§2.6), le nombre de bits par mantisse suit `clc_length_tab[8] = {0, 4, 3, 3, 4, 4, 5, 6}` :

| wl | max_quant | Plage mantisse | Bits CLC | Remarque |
|---|---|---|---|---|
| 0 | — | — | 0 | BFU non codée |
| 1 | 1,5 | −2 … +1 | 4 bits **pour 2 mantisses** | mode « paires », 2 bits/mantisse |
| 2 | 2,5 | −2 … +2 | 3 | signé |
| 3 | 3,5 | −3 … +3 | 3 | signé |
| 4 | 4,5 | −4 … +4 | 4 | signé |
| 5 | 7,5 | −7 … +7 | 4 | signé |
| 6 | 15,5 | −15 … +15 | 5 | signé |
| 7 | 31,5 | −31 … +31 | 6 | signé |

> **Piège n° 3** : pour `wl == 1`, le nombre de codes est **divisé par deux** (`if (selector == 1)
> num_codes /= 2;` dans `read_quant_spectral_coeffs`) : deux mantisses sont empaquetées dans un seul
> code de 4 bits (CLC) ou un seul symbole Huffman (VLC). Cette exception se propage partout dans le
> calcul de coût en bits.

### 2.6 Quantification et codage des mantisses (CLC / VLC)

Chaque *sound unit* choisit **un seul mode de codage** pour toutes ses BFU spectrales :
`coding_mode` (1 bit) — **0 = VLC (Huffman)**, **1 = CLC (longueur constante)**.

#### 2.6.1 CLC — Constant Length Coding

```c
uint32_t clc_encode(int wl, const int *mant, int n, bitwriter_t *bs)
{
    int nbits = clc_length_tab[wl];
    if (wl > 1) {
        for (int i = 0; i < n; i++) bw_put_signed(bs, mant[i], nbits);
        return nbits * n;
    } else {                       /* wl == 1 : paires empaquetées, nbits == 4 */
        for (int i = 0; i < n/2; i++) {
            uint32_t code = (mantissa_to_clc_idx(mant[2*i]) << 2)
                          |  mantissa_to_clc_idx(mant[2*i+1]);
            bw_put(bs, code, 4);
        }
        return nbits * n / 2;
    }
}
```

Table d'aller-retour pour `wl == 1` :

```c
/* décodeur (FFmpeg atrac3data.h) : */
static const int8_t mantissa_clc_tab[4] = { 0, 1, -2, -1 };
/* encodeur (inverse ; atracdenc TAtrac3Data::MantissaToCLcIdx) :
   index = mantissa_clc_rtab[mantissa + 2], mantissa ∈ {-2,-1,0,1}          */
static const uint8_t mantissa_clc_rtab[4] = { 2, 3, 0, 1 };
```

#### 2.6.2 VLC — Huffman

7 tables, sélectionnées par `wl − 1` (`wl ≥ 1`). Symboles :

- **`wl == 1`** : le symbole encode **une paire** (a, b) avec a, b ∈ {−1, 0, +1} → 9 symboles.
  ```c
  /* décodeur : */
  static const int8_t mantissa_vlc_tab[18] = {
      0,0,  0,1,  0,-1,  1,0,  -1,0,  1,1,  1,-1,  -1,1,  -1,-1
  };
  /* encodeur (atracdenc MantissasToVlcIndex) : */
  static const uint8_t mantissas_vlc_rtab[9] = { 8, 4, 7, 2, 0, 1, 6, 3, 5 };
  idx = mantissas_vlc_rtab[3*(a+1) + (b+1)];
  ```
- **`wl ≥ 2`** : un symbole par mantisse, indexé en **zigzag** :
  ```c
  /* atracdenc VLCEnc : */
  uint32_t s = (m < 0) ? ((-m) << 1) | 1 : (m << 1);
  if (s) s -= 1;      /* → 0, +1, -1, +2, -2, +3, -3, ... */
  ```

Chez FFmpeg les tables sont stockées en `(symbole+31, longueur)` et initialisées avec
`ff_vlc_init_from_lengths(..., offset = -31)`, donc le symbole décodé **est directement la mantisse
signée** pour `wl ≥ 2`, et un index 0..8 pour `wl == 1`.

**Vérification croisée effectuée pendant cette recherche** : les tables `HuffTable5/6/7` d'atracdenc
portent le commentaire `//TODO: is it right table???` sur leurs deux dernières entrées.
La comparaison avec `atrac3_hufftabs` de FFmpeg montre que **ces entrées sont correctes** : elles
correspondent aux valeurs extrêmes (±7 pour la table 5, ±15 pour la table 6, ±31 pour la table 7) qui,
dans l'ordre zigzag de l'encodeur, arrivent en fin de table alors que dans l'ordre canonique FFmpeg elles
apparaissent juste après les valeurs de longueur minimale. Détail de la table 5 :

| index zigzag | mantisse | code atracdenc | longueur | ordre FFmpeg |
|---|---|---|---|---|
| 0 | 0 | 0x00 | 2 | `{31,2}` |
| 1 / 2 | +1 / −1 | 0x02 / 0x03 | 3 | `{32,3} {30,3}` |
| 3..6 | +2 −2 +3 −3 | 0x08..0x0B | 4 | `{33,4} {29,4} {34,4} {28,4}` |
| **13 / 14** | **+7 / −7** | **0x0C / 0x0D** | **4** | `{38,4} {24,4}` |
| 7 / 8 | +4 / −4 | 0x1C / 0x1D | 5 | `{35,5} {27,5}` |
| 9..12 | +5 −5 +6 −6 | 0x3C..0x3F | 6 | `{36,6} {26,6} {37,6} {25,6}` |

→ Les 7 tables Huffman d'atracdenc sont **cohérentes avec le décodeur FFmpeg**, longueurs et codes
compris. On peut donc les régénérer sans risque à partir de `atrac3_hufftabs` + `huff_tab_sizes`
(ce qui est aussi la voie la plus propre juridiquement).

Tailles : `huff_tab_sizes[7] = { 9, 5, 7, 9, 15, 31, 63 }`.

Note sur la **table 4** : atracdenc la fait pointer sur la table 1 (`HuffTables[3] = {HuffTable1, 9}`).
Vérification faite pendant cette recherche : ce **n'est pas un bug**. FFmpeg déclare pour la table 4
les longueurs `{1,3,3,4,4,5,5,5,5}` (symboles `0,+1,−1,+2,−2,+3,−3,+4,−4`), strictement identiques à
celles de la table 1 ; la construction canonique donne alors les codes
`0x0, 0x4, 0x5, 0xC, 0xD, 0x1C, 0x1D, 0x1E, 0x1F` — exactement `HuffTable1`. Les deux tables sont donc
interchangeables. Notre implémentation peut soit les partager, soit les dupliquer par clarté.

#### 2.6.3 Choix CLC vs VLC

`atracdenc` calcule les deux coûts pour chaque BFU et retient globalement le moins cher :

```c
/* atrac3_bitstream.cpp : CalcSpecsBitsConsumption */
const bool mode = (clcSpecBits <= vlcSpecBits);   /* 1 = CLC, 0 = VLC */
return { mode, bitsUsed + (mode ? clcSpecBits : vlcSpecBits) };
```

C'est correct et peu coûteux (les mantisses sont quantifiées une seule fois, les deux coûts sont
accumulés en parallèle). À reprendre tel quel.

#### 2.6.4 Quantification avec compensation d'erreur d'énergie

`src/atrac/atrac_scale.cpp:QuantMantisas()` implémente un arrondi « energy-aware » :
après l'arrondi naïf `m = round(x * mul)`, la fonction compare l'énergie d'origine `e1 = Σx²` à l'énergie
reconstruite `e2 = Σ(m/mul)²`, puis bascule dans l'autre sens les mantisses dont la partie fractionnaire
est proche de 0,5 (|delta| < 0,25) jusqu'à rapprocher `e2` de `e1`. Le facteur retourné `e1/e2` sert
ensuite dans `ConsiderEnergyErr()` pour donner +1 bit aux BFU basses (index < `BOOST_NAQ_END = 10`) dont
l'énergie dérive de plus de ±20 %.

Ce raffinement est **optionnel** (il est conditionné par `bfu > LOSY_NAQ_START = 18`) mais apporte un
gain audible sur les basses fréquences. À implémenter au jalon 4, pas au jalon 1.

#### 2.6.5 Allocation de bits

L'algorithme d'atracdenc (`CalcBitsAllocation` + `TAlloc` + `TBitAllocHandler`) :

1. `Spread = AnalizeScaleFactorSpread(scaledBlocks)` — mesure de « tonalité » globale ∈ [0,1] ;
2. recherche binaire sur un **`shift` global** ∈ [−8, +20] (`ba.Start(targetBits, -8.0f, 20.0f)`) ;
3. pour chaque BFU :
   ```c
   if (energie_corrigée < ATH[bfu] * loudness) {
       wl[bfu] = 0;                       /* sous le seuil d'audition → non codée */
   } else {
       float x = 6.0f;                    /* diviseur dépendant de la bande */
       if      (i < 3)  x = 2.8f;
       else if (i < 10) x = 2.6f;
       else if (i < 15) x = 3.3f;
       else if (i <= 20) x = 3.6f;
       else if (i <= 28) x = 4.2f;
       int tmp = spread * (sfi_corrigé / x) + (1 - spread) * fixed_alloc[i] - shift;
       wl[bfu] = clamp01_7(tmp);          /* tmp == 0 → 1 ; tmp < 0 → 0 ; tmp > 7 → 7 */
   }
   ```
4. les BFU portant une composante tonale voient leur `wl` décrémenté de 1 (si > 2) ;
5. `CheckBfus()` élague la dernière BFU si son `wl` est 0, et relance ;
6. la boucle s'arrête quand le total (spectre + tonal + en-têtes) tient dans `targetBits`.

Table d'allocation fixe de repli (`atrac3_bitstream.cpp:FixedBitAllocTable`, 32 entrées) :

```c
static const uint8_t fixed_bit_alloc[32] = {
    6,6,5,4,4,4,3,3,3,3,3,3,3,3,3,3,3,3,
    2,2,2,2,2,1,1,1,
    1,1,1,0,
    0,0
};
```

Courbe ATH : `CalcATH(1024, 44100)` dans `atrac_psy_common.cpp`, basée sur la **formule ATH de Musepack**
(`ATHformula_Frank`, table de 128 valeurs en millibels de 10 Hz à ~20 kHz), puis réduite par BFU via un
`min` sur les raies de la BFU et convertie en énergie linéaire (`pow(10, 0.1*x)`).

> **Attention licence n° 2** : cette table ATH est explicitement « Borrowed from Musepack ». Pour un
> projet MIT/zlib, **il ne faut pas la recopier** : reconstruire une courbe ATH à partir de la formule
> analytique publique de Terhardt / ISO 226, par exemple
> `ath_db(f) = 3.64·f^-0.8 − 6.5·exp(−0.6·(f−3.3)²) + 10⁻³·f⁴` (f en kHz), qui est la formule
> standard citée dans la littérature (Painter & Spanias, *Perceptual Coding of Digital Audio*, 2000) et
> qui donne des résultats équivalents à ±2 dB.

### 2.7 Composantes tonales

Les *tonal components* permettent de coder finement quelques raies isolées à forte énergie qui seraient
noyées dans la quantification grossière de leur BFU. Elles sont **retirées du spectre** avant la
quantification des BFU, puis **réinjectées** par le décodeur (`add_tonal_components`).

Structure de l'en-tête tonal (dans l'ordre du bitstream) :

| Champ | Bits | Sémantique |
|---|---|---|
| `nb_components` | 5 | nombre de **groupes** (0..31). Si 0 → fin de la section tonale. |
| `coding_mode_selector` | 2 | 0 = tout VLC, 1 = tout CLC, **2 = illégal**, 3 = un bit par composante |

Puis, pour chacun des `nb_components` groupes :

| Champ | Bits | Sémantique |
|---|---|---|
| `band_flags[b]` | 1 × (`bands_coded` + 1) | drapeau de présence par bande QMF |
| `coded_values_per_component` | 3 | nombre de raies par composante **moins 1** (donc 1..8) |
| `quant_step_index` | 3 | word length du groupe ; **doit être ≥ 2** (FFmpeg rejette ≤ 1) |
| (`coding_mode`) | 1 | seulement si `coding_mode_selector == 3` |

puis, pour chaque bloc de 64 raies `b` dans `0..(bands_coded+1)*4 − 1` dont `band_flags[b >> 2]` est vrai :

| Champ | Bits | Sémantique |
|---|---|---|
| `coded_components` | 3 | nombre de composantes dans ce bloc de 64 raies (0..7) |
| ↳ `sf_index` | 6 | scalefactor de la composante |
| ↳ `pos` | 6 | position **relative** dans le bloc de 64 → position absolue `b*64 + pos` |
| ↳ mantisses | var. | `coded_values` mantisses en CLC ou VLC avec `quant_step_index` |

Le **groupement** (`GroupTonalComponents`) réunit les composantes qui partagent le même
`(quant_step_index, nb_raies)` : c'est la clé `quant * 8 + len` sur 64 buckets, puis un sous-découpage
tel qu'un sous-groupe ne couvre pas plus de 64 raies d'écart et pas plus de 8 composantes.

**Détection côté encodeur** (`atrac3denc.cpp:ExtractTonalComponents`, version amont actuelle) :

```c
for (bfu = 8; bfu < 29; ++bfu) {
    if (flatness[bfu] >= 0.01f) continue;         /* BFU trop "bruitée" → pas de tonal */
    /* cherche la fenêtre [start, start+len), len ≤ 5, qui maximise Σ|spec| */
    ...
    for (n = 0; n < best_len; ++n) {
        res.push_back({ pos, specs[pos], bfu });
        specs[pos] = 0.0f;                        /* retirée du spectre résiduel */
    }
}
```

Bornes : BFU < 8 trop courtes pour être rentables, BFU ≥ 29 difficiles à régler. `flatness` provient du
`TSpectralUpsampler` / de la mesure de platitude spectrale par BFU.

> **Simplification légitime pour le jalon 1** : émettre `nb_components = 0` (5 bits à zéro). Le flux reste
> valide. `atracdenc` a d'ailleurs l'option `--notonal`. Le coût qualité est modeste en LP2, plus net
> en LP4 sur les sources très tonales (piano, cordes).

### 2.8 Layout bit-exact d'une frame

Une **frame ATRAC3 stéréo** = 2 *sound units* (SU) empaquetées dans `FrameSz` octets, MSB-first.

#### 2.8.1 Sound unit, mode normal (LP2, LP105 — `coding_mode == SINGLE`)

```
┌ bit 0
│ [ 6] id = 0x28                      (101000)
│ [ 2] bands_coded  (= nb_bandes - 1, 0..3)
│
│ ── gain control, pour band = 0 .. bands_coded ──
│   [ 3] num_points  (0..7)
│   pour j = 0 .. num_points-1 :
│     [ 4] lev_code[j]
│     [ 5] loc_code[j]   (strictement croissant)
│
│ ── composantes tonales ──
│   [ 5] nb_components  (0..31)   ; si 0 → section terminée
│   [ 2] coding_mode_selector     ; 0=VLC 1=CLC 2=ILLÉGAL 3=par composante
│   pour g = 0 .. nb_components-1 :
│     [ 1] × (bands_coded+1)  band_flags
│     [ 3] coded_values_per_component - 1
│     [ 3] quant_step_index  (≥ 2)
│     [ 1] coding_mode        ; si selector == 3 uniquement
│     pour chaque bloc de 64 raies b (0..(bands_coded+1)*4-1) avec band_flags[b>>2] :
│       [ 3] coded_components
│       pour c = 0 .. coded_components-1 :
│         [ 6] sf_index
│         [ 6] pos relative
│         [  ] mantisses (CLC ou VLC selon quant_step_index)
│
│ ── spectre ──
│   [ 5] num_subbands   (= nb_BFU_codées - 1, 0..31)
│   [ 1] coding_mode    ; 0 = VLC, 1 = CLC
│   pour i = 0 .. num_subbands :
│     [ 3] wordlen[i]   (0..7)
│   pour i = 0 .. num_subbands, si wordlen[i] != 0 :
│     [ 6] sf_index[i]
│   pour i = 0 .. num_subbands, si wordlen[i] != 0 :
│     [  ] mantisses de la BFU i  (bfu_bounds[i+1]-bfu_bounds[i] valeurs)
│
│ ── bourrage jusqu'à FrameSz/2 octets (zéros) ──
└
```

Ordre confirmé côté décodeur par `atrac3.c:decode_channel_sound_unit()` :
`id → bands_coded → decode_gain_control → decode_tonal_components → decode_spectrum`,
et côté encodeur par `atrac3_bitstream.cpp:WriteSoundUnit()` + `EncodeSpecs()`.

> **Piège n° 4** : dans `EncodeSpecs`, les composantes tonales sont écrites **avant** le champ
> `num_subbands`, mais **après** le gain control. La section tonale est donc « entre » les deux. Une
> implémentation qui écrit la section tonale à la fin produit un flux inexploitable.

#### 2.8.2 Coût fixe minimal d'une sound unit

```
6 (id) + 2 (bands) + 4×3 (gain, 4 bandes sans point) + 5 (tonal=0)
  + 5 (num_subbands) + 1 (coding_mode) + 3 (wordlen[0]) = 34 bits
```

`atracdenc` initialise le budget à `bitsToAlloc = -6` puis retranche la taille de l'en-tête + gain
(`bitsUsedByGainInfoAndHeader`), les 6 bits couvrant `num_subbands` (5) + `coding_mode` (1) écrits
plus tard.

#### 2.8.3 Frame silencieuse (« silent frame »)

Utile pour le bourrage de fin de piste, l'amorçage (*priming*) et les tests. Séquence de bits pour une
sound unit silencieuse, 4 bandes déclarées, sans tonal, une seule BFU non codée :

```
101000        id = 0x28                (6 bits)
11            bands_coded = 3          (2 bits)
000 000 000 000   num_points = 0 × 4   (12 bits)
00000         nb_components = 0        (5 bits)
00000         num_subbands = 0         (5 bits)
1             coding_mode = CLC        (1 bit)
000           wordlen[0] = 0           (3 bits)
--------------------------------------------------
34 bits  →  octets : 0xA3 0x00 0x00 0x00 0x40 0x00 ... (zéros jusqu'à FrameSz/2)
```

Détail du calcul des 5 premiers octets :
`101000 11 000 000 000 000 00000 00000 1 000` →
`10100011 00000000 00000000 00000000 01000000` = `A3 00 00 00 40`, puis zéros.

Une frame LP2 silencieuse = ce motif deux fois (192 octets chacun) = 384 octets.
Une frame LP4 silencieuse = SU1 (motif ci-dessus, 96 octets) suivi de SU2 **inversé octet par octet**,
avec le préambule joint stereo (§2.9).

> **Alternative plus simple et tout aussi valide** : mettre `bands_coded = 0` (2 bits `00`) et n'écrire
> qu'un seul champ `num_points`. On obtient `101000 00 000 00000 00000 1 000` = 25 bits →
> `10100000 00000000 00010000` = `A0 00 10`, puis zéros. Les 3 bandes hautes sont alors implicitement
> muettes. Les deux variantes sont acceptées par FFmpeg **et** par les décodeurs Sony.

### 2.9 Joint stereo (LP4) — matrixing, pondération, bit reservoir

Le mode joint stereo est **obligatoire** pour LP4 (66 kbps) et pour 93 kbps ; il est **interdit**
(inutilisé) pour LP2, LP105 et au-dessus. Voir `TAtrac3Data::ContainerParams` : le champ `Js` est `true`
uniquement pour 66 150 et 93 713 bps.

#### 2.9.1 Disposition dans la frame

```
octet 0                                                   octet FrameSz-1
├──────────── SU1 (canal M), lu en avant ────────────┤├── SU2 (canal S), lu à l'ENVERS ──┤
                        ↑
             frontière variable (bit reservoir inter-canaux)
```

- SU1 est écrit **en avant** depuis l'octet 0 ;
- SU2 est écrit **en arrière** depuis le dernier octet (`OutBuffer.insert(end, rbegin, rend)` chez
  atracdenc ; `FFSWAP` sur tout le bloc chez FFmpeg avant lecture) ;
- **la frontière n'est pas signalée** : chaque décodeur s'arrête quand il a lu ce dont il a besoin ;
- le décodeur FFmpeg saute des octets de synchro `0xF8` en tête du buffer inversé
  (`for (i = 4; *ptr1 == 0xF8; i++, ptr1++)`) — c'est le bourrage utilisé par les encodeurs Sony.
  `atracdenc` bourre à `0x00`, ce qui fonctionne aussi (la boucle ne saute alors rien).

#### 2.9.2 Répartition des octets entre M et S

`atracdenc` calcule un décalage `msBytesShift` (`CalcMSBytesShift`) proportionnel au rapport d'énergie :

```c
float ratio = m_energy / (m_energy + s_energy) - 0.5f;   /* +0.5 = M seul, -0.5 = S seul */
int   shift = clamp(round(FrameSz * ratio), -maxAllowedShift, +maxAllowedShift);
bits_su1 += 8 * (FrameSz/2 + shift);
bits_su2 += 8 * (FrameSz/2 - shift);
```

avec `maxAllowedShift = FrameSz/2 − ceil(bits_entêtes / 8)`.
C'est un vrai **bit reservoir intra-frame** : sur du contenu quasi-mono (le cas courant), presque tous
les octets vont au canal M, ce qui explique pourquoi LP4 tient à peu près la route sur ce type de source.

#### 2.9.3 Préambule joint stereo de SU2

Écrit **au début** du flux SU2 (donc, après inversion, à la **fin** du buffer physique) :

| Champ | Bits | Valeur écrite par atracdenc | Sémantique décodeur |
|---|---|---|---|
| — | 1 | `0` | `weighting_delay[4]` (flag d'échange G/D de la pondération) |
| — | 3 | `7` | `weighting_delay[5]` (index de pondération ; **7 = pas de pondération**) |
| `matrix_coeff_index_next[0..3]` | 4 × 2 | `3` chacun | sélecteur de matrice par bande |
| id SU2 | 2 | `3` | `if (get_bits(gb,2) != 3) → erreur` |

Code amont (`atrac3_bitstream.cpp`) :

```c
void WriteJsParams(TBitStream* bs) {
    bs->Write(0, 1);
    bs->Write(7, 3);
    for (int i = 0; i < 4; i++) bs->Write(3, 2);
}
/* puis, dans WriteSoundUnit : */
if (Params.Js && channel == 1) { WriteJsParams(bitStream); bitStream->Write(3, 2); }
else                            { bitStream->Write(0x28, 6); }
```

#### 2.9.4 Matrice M/S

Côté **encodeur** (`atrac3denc.cpp:Matrixing`), dans le **domaine temporel, après QMF**, sous-bande par
sous-bande :

```c
for (subband = 0; subband < 4; subband++)
  for (sample = 0; sample < 256; sample++) {
      float l = L[subband][sample], r = R[subband][sample];
      M[subband][sample] = (l + r) / 2.0f;
      S[subband][sample] = (l - r) / 2.0f;
  }
```

Côté **décodeur** (`atrac3.c:reverse_matrixing`), avec `s2 == 3` (le cas écrit par atracdenc) :

```c
case 2: case 3:
    su1[n] = c1 + c2;      /* L = M + S */
    su2[n] = c1 - c2;      /* R = M - S */
```

→ round-trip exact. Les autres sélecteurs existent (`0` = M/S ×2, `1` = variante), et une
**interpolation sur les 8 premiers échantillons** de chaque bande est appliquée quand le sélecteur change
entre deux frames (`INTERPOLATE(mc1, mc2, n)`), ce qui impose de **garder le même sélecteur** pour éviter
tout artefact — c'est le choix d'atracdenc (toujours `3`).

Table des coefficients (`atrac3data.h`) :

```c
static const float matrix_coeffs[8] = { 0.0, 2.0, 2.0, 2.0, 0.0, 0.0, 1.0, 1.0 };
/* utilisée par paires : mc[s*2], mc[s*2+1] */
```

#### 2.9.5 Pondération de canal

`channel_weighting()` n'est active que si `p3[1] != 7 || p3[3] != 7`. Comme atracdenc écrit toujours 7,
la pondération est **désactivée**. Table de calcul (à implémenter seulement si on veut l'exploiter) :

```c
static void get_channel_weights(int index, int flag, float ch[2]) {
    if (index == 7) { ch[0] = ch[1] = 1.0f; }
    else {
        ch[0] = (index & 7) / 7.0f;
        ch[1] = sqrtf(2 - ch[0]*ch[0]);
        if (flag) swap(ch[0], ch[1]);
    }
}
```

> **Piège n° 5** : le champ de pondération est **retardé de 2 frames** côté décodeur
> (`weighting_delay[]` est un buffer glissant de 6 valeurs, décalé de 2 à chaque frame, et
> `channel_weighting` utilise `p3[1]` et `p3[3]` c'est-à-dire les valeurs des frames **précédentes**).
> Même chose pour `matrix_coeff_index_prev/now/next` : ce que l'on écrit dans une frame n'est appliqué
> qu'à la frame **suivante**. C'est pourquoi il faut écrire `3` **dès la première frame** et ne jamais
> changer.

### 2.10 Tableau des modes et octets par frame

Table maîtresse (`TAtrac3Data::ContainerParams[8]` ; le champ `FrameSz` est la taille de la frame
**stéréo** = `nBlockAlign` du WAV) :

| # | Bitrate (bps) | FrameSz (o) | Joint stereo | Nom usuel | Usage NetMD |
|---|---|---|---|---|---|
| 0 | 66 150 | **192** | **oui** | LP4 | ✔ `Wireformat.lp4 = 0xA8` |
| 1 | 93 713 | 272 | oui | — | ✘ |
| 2 | 104 738 | **304** | non | LP105 | ✔ `Wireformat.l105kbps = 0x90` |
| 3 | 132 300 | **384** | non | LP2 | ✔ `Wireformat.lp2 = 0x94` |
| 4 | 146 081 | 424 | non | — | ✘ |
| 5 | 176 400 | 512 | non | — | ✘ (ATRAC3 « 176k », OMA) |
| 6 | 264 600 | 768 | non | — | ✘ |
| 7 | 352 800 | 1024 | non | — | ✘ |

Sélection : `GetContainerParamsForBitrate()` fait un `std::lower_bound` — c'est-à-dire **le premier mode
dont le bitrate est ≥ à la valeur demandée**. La CLI d'atracdenc passe `bitrate * 1024` :

| Argument `--bitrate` | bps calculés | Mode retenu | FrameSz |
|---|---|---|---|
| `64` | 65 536 | 66 150 | 192 (LP4) |
| `102` | 104 448 | 104 738 | 304 (LP105) |
| `128` | 131 072 | 132 300 | 384 (LP2) |

C'est exactement ce que fait Web MiniDisc Pro (`atracdenc-export.ts`) :
`132 → "128"`, `105 → "102"`, `66 → "64"`.
Cette « fudge » vers la puissance de 2 inférieure est mentionnée dans la communauté MiniDisc :
passer directement `132` produirait `lower_bound(132)` → mode 0 (66 150) puisque 132 < 66 150 est faux…
en réalité `132 < 66150` est vrai, donc on obtiendrait LP4 : **d'où l'obligation de multiplier par 1024**.

Débit réel : `FrameSz × 8 / 0.0232199 s` :
- 192 o → 66 150 bps ✔
- 304 o → 104 738 bps ✔
- 384 o → 132 300 bps ✔

**Vérification de cohérence à coder dans les tests** : `octets_totaux = ceil(n_samples / 1024) × FrameSz`.

---

## 3. Architecture d'atracdenc

### 3.1 Arborescence utile (branche `master`, septembre 2026)

```
atracdenc/
├── LICENSE                              GNU LGPL 2.1 (502 lignes)
├── CMakeLists.txt
└── src/
    ├── CMakeLists.txt                   ★ deps : kissfft (bundlé), libgha (submodule), libsndfile
    ├── main.cpp                    757   CLI (getopt) — non requis
    ├── config.h, env.{h,cpp}            arrondi flottant, helpers plateforme
    ├── util.h                           ToInt(), GetFirstSetBit(), SwapArray(), Div8Ceil()
    ├── pcmengin.{h,cpp}                 moteur d'itération PCM (lambda par frame)
    ├── delay_buffer.h                   TDelayBuffer<T, NCh, NSamples> — buffers de recouvrement
    ├── compressed_io.h                  interface ICompressedOutput::WriteFrame(vector<char>)
    ├── oma.{h,cpp} / at3.cpp       378   conteneurs OMA (EA3, 96 o d'en-tête) et WAV/at3
    ├── aea.{h,cpp}, rm.{h,cpp}, raw.*   autres conteneurs
    ├── wav.{h,cpp}                      entrée WAV (via libsndfile ou Media Foundation)
    ├── transient_detector.{h,cpp}  484  ★ détection d'attaques par bande
    ├── transient_spectral_upsampler.*   platitude spectrale / upsampling pour le tonal
    ├── gain_processor.h            122  ★ TGainProcessor<T>::Modulate/Demodulate (std::function)
    ├── atrac3denc.{h,cpp}       135+869 ★★ pipeline ATRAC3 complet
    ├── qmf/qmf.{h,cpp}             ~170 ★ TQmf<nIn> (template) + TapHalf[24]
    ├── lib/
    │   ├── bitstream/bitstream.{h,cpp}  ★ TBitStream (Write(value, nbits), GetBytes())
    │   ├── mdct/mdct.h                  ★ NMDCT::TMDCT<512> / TMIDCT<512> (sur kissfft)
    │   ├── fft/kissfft_impl/            kissfft (BSD-3-Clause)
    │   └── libgha/                      submodule — GHA, utilisé par ATRAC3+ seulement
    └── atrac/
        ├── atrac_scale.{h,cpp}      199 ★ TScaler<T>::Scale(), QuantMantisas()
        ├── atrac_psy_common.{h,cpp} 201 ★ CalcATH(), AnalizeScaleFactorSpread(), CreateLoudnessCurve()
        ├── atrac_enc_cache.{h,cpp}      cache de quantification <bfu, wordlen>
        ├── at1/                          ATRAC1 (SP) — hors périmètre
        ├── at3/
        │   ├── atrac3.h             280 ★★ TOUTES les tables + TContainerParams
        │   ├── atrac3.cpp            56  définitions + GetContainerParamsForBitrate()
        │   ├── atrac3_bitstream.h    71
        │   ├── atrac3_bitstream.cpp 850 ★★ allocation de bits + écriture bitstream
        │   └── atrac3_qmf.h          44  ★ assemblage des 3 QMF
        ├── at3p/                          ATRAC3+ — hors périmètre
        └── atrac3plus_pqf/                ATRAC3+ — hors périmètre
```

### 3.2 Chaîne d'appel (encodage ATRAC3)

```
main.cpp
 └─ TAtrac3Encoder(TCompressedOutputPtr oma, TAtrac3EncoderSettings)
     └─ GetLambda()   → lambda appelée par TPCMEngine pour chaque bloc de 1024 échantillons
         ├─ AnalysisFilterBank[ch].Analysis(pcm, subs)        (atrac3_qmf.h → qmf.h)
         ├─ [LP4] Matrixing()                                  (M/S temporel par sous-bande)
         ├─ CreateSubbandInfo(upInput, ch, &subbandInfo)       (transient detector → gain points)
         ├─ MakeGainModulatorArray(subbandInfo)                (gain_processor.h)
         ├─ Mdct(specs, bands, maxLevels, gainModulators)      (lib/mdct/mdct.h)
         ├─ ExtractTonalComponents(specs, flatnessPerBfu)
         ├─ MapTonalComponents(tonal, &sce.TonalBlocks)        (Scaler.Scale)
         ├─ Scaler.Scale(...) pour chaque BFU → sce.ScaledBlocks
         └─ bitStreamWriter->WriteSoundUnit(sce[], loudness)
              ├─ WriteJsParams (LP4, canal 1)
              ├─ en-tête + gain info                          → bitsToAlloc
              ├─ CalcMSBytesShift (LP4)                        → reservoir inter-canaux
              ├─ Encoder.Do(&ctx, bitstream)  [TConfigure, TAlloc]
              │    ├─ AnalizeScaleFactorSpread
              │    ├─ boucle de recherche binaire sur le shift
              │    │    ├─ CalcBitsAllocation (ATH + spread + fixed table)
              │    │    ├─ CalcSpecsBitsConsumption (cache <bfu,wl>, CLC vs VLC)
              │    │    ├─ ConsiderEnergyErr
              │    │    └─ EncodeTonalComponents(..., nullptr)  ← coût seulement
              │    └─ Dump → EncodeSpecs(...)
              │         ├─ EncodeTonalComponents(..., bitstream)  ← écriture réelle
              │         ├─ num_subbands, coding_mode
              │         ├─ wordlens, sf_indexes
              │         └─ CLCEnc / VLCEnc par BFU
              └─ Container->WriteFrame(OutBuffer)
```

### 3.3 Dépendances et build

Extrait de `src/CMakeLists.txt` :

- **kissfft** : bundlé dans `src/lib/fft/kissfft_impl/` (`kiss_fft.c`, `tools/kiss_fftr.c`).
  Licence **BSD-3-Clause** → **compatible MIT/zlib**. C'est la seule dépendance DSP réelle pour ATRAC3.
- **libgha** : `add_subdirectory(lib/libgha)`, submodule git. Utilisé pour l'analyse harmonique
  d'**ATRAC3+** uniquement. **Non nécessaire** pour ATRAC3.
- **libsndfile** (LGPL) ou **Media Foundation** (Windows/MSVC) : uniquement pour lire le PCM d'entrée.
  **Non nécessaire** si notre application fournit déjà le PCM en mémoire.
- **googletest** : tests unitaires (`*_ut.cpp`).
- C++17, CMake ≥ 3.1.

**Conclusion pour un port** : hors CLI et I/O, il n'y a **aucune dépendance externe bloquante**.
Une MDCT maison de ~200 LOC remplace kissfft si l'on veut zéro dépendance.

### 3.4 Difficultés du port C++ → C

| Construction C++ | Où | Transposition C |
|---|---|---|
| `template <size_t nIn> class TQmf` | `qmf/qmf.h` | `struct qmf_t { float hist[MAX]; }` + `size_t n` en paramètre |
| `NMDCT::TMDCT<512>` retournant `const vector<float>&` | `lib/mdct/mdct.h`, `atrac3denc.cpp` | `void mdct512(const float in[512], float out[256])` avec buffers pré-alloués |
| `std::function<void(float*, float*)>` (gain modulator) | `gain_processor.h` | pointeur de fonction + `void *ctx`, ou branchement direct sur la structure `gain_curve_t` |
| `std::vector<uint32_t>` d'allocation | `atrac3_bitstream.cpp` | `uint32_t wl[32]` (taille bornée par `MaxBfus`) |
| `std::vector<TTonalBlock>` | idem | `tonal_block_t tonal[64]; int n_tonal;` (borné : FFmpeg refuse > 64) |
| `IBitStreamPartEncoder` (polymorphisme, `EStatus::Repeat`) | `atrac3_bitstream.cpp` | boucle `do { ... } while (repeat)` explicite |
| `TEncCache` (cache <bfu, wordlen> → mantisses) | `atrac_enc_cache.cpp` | tableau statique `spec_unit_t cache[32*8]` + bitmap de validité |
| RAII / exceptions | partout | codes de retour `int` |
| `std::lower_bound` sur `ContainerParams` | `atrac3.cpp` | boucle linéaire sur 8 entrées |
| `lrint`, `std::isfinite` | | `lrintf`, `isfinite` (C99) |

Aucune de ces transpositions n'est délicate. Le vrai travail est de **reconstruire la logique de la
boucle d'allocation** proprement en C (elle est répartie sur 3 classes et un handler d'allocation dans
le C++ amont).

---

## 4. Qualité, encodeurs concurrents, licences

### 4.1 Encodeurs ATRAC3 existants

| Encodeur | Nature | Licence | Qualité LP2 | Qualité LP4 | Disponible en C ? |
|---|---|---|---|---|---|
| **Sony SonicStage / `atrac3.acm` / `Atrac3Enc.dll`** | binaire propriétaire Win32 | propriétaire | référence | référence | non (x86 32 bits uniquement) |
| **atracdenc** | C++ open source | **LGPL 2.1** | ~correcte | médiocre | portable |
| **ATRAC3-RE (asivery)** | réimplémentation/RE de l'encodeur Sony, compilée en WASM (`at3re-harness.js`) | à vérifier | proche de Sony | proche de Sony | WASM, harness C |
| **Serveur d'encodage minidisc.wiki** | service HTTP `POST /transcode?type=LP2\|LP4\|LP105` | service | meilleure | meilleure | non (réseau) |
| **FFmpeg** | **décodeur seulement** | LGPL | — | — | pas d'encodeur ATRAC3 |

**FFmpeg ne contient aucun encodeur ATRAC3.** Il n'y a que `atrac3.c` (décodeur ATRAC3),
`atrac3plusdec.c` (décodeur ATRAC3+), `atrac1.c` et `atrac9dec.c`. Aucune ligne `ff_atrac3_encoder`.
Cette option est donc à écarter définitivement.

### 4.2 Où atracdenc perd en qualité

Constats issus de la lecture du code et des retours communautaires :

1. **Aucun bug de table Huffman** : la vérification croisée avec FFmpeg (§2.6.2) montre que les 7 tables
   d'atracdenc, y compris la table 4 aliasée sur la table 1 et les entrées marquées « TODO », sont
   correctes. Les faiblesses sont donc **algorithmiques**, pas structurelles.
2. **Gain control approximatif** : la construction des courbes (`CreateSubbandInfo`, ~280 lignes) a été
   réécrite plusieurs fois ; elle reste plus grossière que celle de Sony (pre-echo audible sur
   percussions en LP2).
3. **Composantes tonales rudimentaires** : une seule composante par BFU, longueur ≤ 5, seuil de platitude
   fixe à 0,01, bornée à BFU 8..28. Sony en extrait davantage et sur toute la bande.
4. **Allocation de bits heuristique** : mélange linéaire `spread * (sfi/x) + (1-spread) * fixed[i] - shift`
   avec des constantes empiriques (2,8 / 2,6 / 3,3 / 3,6 / 4,2 / 6). Pas de modèle de masquage
   inter-bandes (pas d'étalement de masque type ISO/MPEG psychoacoustic model 1 ou 2).
5. **Aucun *lookahead* réel au-delà d'une frame** pour la répartition du budget (le débit est constant
   par frame, il n'y a pas de bit reservoir inter-frames dans ATRAC3 — c'est une contrainte du format,
   pas un défaut d'atracdenc).
6. **LP4** : le format lui-même est très contraint (192 octets pour 23 ms stéréo, soit ~1,5 bit par raie
   utile) ; même Sony y est médiocre. atracdenc y est nettement en dessous.

### 4.3 Les « patches asivery »

`asivery` (auteur de Web MiniDisc Pro / ElectronWMD / netmd-exploits) maintient un **fork d'atracdenc**
dont les changements sont ensuite remontés/miroités. Points relevés :

- **Build Emscripten/WASM** : `atracdenc.js` + `Module.callMain(['-e','atrac3','-i',...,'--bitrate',...])`,
  I/O via `Module.FS`. C'est ce que charge `atracdenc-worker.ts`.
- **Fudge des bitrates** vers la puissance de 2 inférieure (`128`, `102`, `64`) : nécessaire à cause de
  `lower_bound` sur `bitrate * 1024` (§2.10). Sans cela « l'audio est mal encodé et rien d'autre ne peut
  le décoder ».
- **ATRAC3-RE** : projet distinct d'asivery — un *harness* C (`at3re-harness.js` en WASM) exposant
  `initialize(bitrate)`, `calculate_atrac_buf_size(n)`, `encode(in, out, in_len, out_cap)`, `finish(out, n)`.
  C'est aujourd'hui l'encodeur par défaut de Web MiniDisc Pro pour l'ATRAC3 local, précisément parce
  qu'il est meilleur qu'atracdenc. **API très proche de celle que nous voulons** (§7.4) — à étudier
  comme modèle d'interface, et à évaluer juridiquement avant tout emprunt (réimplémentation d'un
  encodeur Sony : le statut de licence doit être vérifié explicitement auprès de l'auteur).
- **Encodeur distant** : `RemoteAtracExportService` (`remote-atrac-export.ts`) poste le fichier sur
  `https://<serveur>/transcode?type=LP2|LP4|LP105|PLUS<kbps>` et récupère un WAV `0x0270` dont l'en-tête
  est ensuite retiré. Le serveur officiel est financé par dons via minidisc.wiki. C'est le chemin
  « meilleure qualité » aujourd'hui, mais il impose une dépendance réseau.

### 4.4 « Silent frame », padding et fin de piste

Deux comportements coexistent dans l'écosystème :

- **netmd-js** : bourrage à **zéro** jusqu'au multiple de la taille de frame fil
  (`MDTrack.getTotalSize()` arrondit ; l'itérateur de paquets complète avec des zéros).
  Ce n'est pas une frame ATRAC3 valide, mais comme la longueur totale est déjà un multiple de
  `FrameSz` quand l'encodeur fait son travail, ce cas ne se produit pas en pratique.
- **Bonne pratique recommandée** : que l'**encodeur** garantisse l'alignement en complétant le **PCM**
  d'entrée à un multiple de 1024 échantillons avec des zéros, puis en émettant une ou deux frames
  silencieuses (§2.8.3) pour vider le recouvrement MDCT (le dernier bloc MDCT ne contient que la moitié
  du signal). **Deux frames de rinçage** suffisent (1 pour le recouvrement MDCT, 1 pour le QMF).

---

## 5. Encodage ATRAC3 sur le device : possible ?

**Réponse courte : non, pas via USB/NetMD.**

### 5.1 Ce que le device encode réellement

Le protocole NetMD ne connaît que quatre `Wireformat`
(`netmd-js/src/netmd-interface.ts`, lignes 47-52) :

```ts
export enum Wireformat {
    pcm      = 0,      // 2048 octets/frame  — PCM 16 bits BIG ENDIAN, 44.1 kHz stéréo
    l105kbps = 0x90,   //  152 octets/frame  — ATRAC3 déjà encodé
    lp2      = 0x94,   //  192 octets/frame  — ATRAC3 déjà encodé
    lp4      = 0xa8,   //   96 octets/frame  — ATRAC3 déjà encodé
}
```

- **`Wireformat.pcm`** : l'hôte envoie du **PCM brut**, le DSP du baladeur encode **ATRAC1 (SP)** à la
  volée pendant l'écriture. C'est le seul cas d'encodage embarqué exposé par le protocole.
  Correspondance : `discforwire[Wireformat.pcm] = DiscFormat.spStereo (6)`.
- **`lp2` / `l105kbps` / `lp4`** : l'hôte doit fournir des **frames ATRAC3 déjà encodées**. Le device se
  contente de les chiffrer/déchiffrer (DES/3DES via l'EKB) et de les écrire sur le disque.
  Correspondances : `lp2 → DiscFormat.lp2 (2)`, `l105kbps → DiscFormat.lp2 (2)`, `lp4 → DiscFormat.lp4 (0)`.

Il n'existe **aucune commande** « envoie-moi du PCM, je te grave en LP2 ». Ce serait d'ailleurs
contradictoire avec le débit USB 1.1 et avec le modèle DRM de Sony (le contenu LP est chiffré côté hôte).

### 5.2 Les seules voies « encodage par le matériel » en LP

1. **Enregistrement analogique ou optique en temps réel** : le baladeur, mis en mode d'enregistrement
   LP2/LP4 depuis son entrée ligne/optique, **encode bien de l'ATRAC3 dans son DSP**. Web MiniDisc Pro
   expose des aides pour cela (`src/components/line-in-helpers.tsx`, `record-dialog.tsx`) : on pilote le
   device par USB pendant qu'on lui envoie l'audio par le jack/TOSLINK.
   → Qualité : celle de l'encodeur Sony, donc **la meilleure possible**.
   → Inconvénient rédhibitoire pour notre usage : **temps réel** (1× la durée), matériel additionnel,
   pas de titrage automatique fiable, aucune garantie d'alignement.
2. **Exploits en mode usine** (`asivery/netmd-exploits`) : permettent notamment le *download* (extraction)
   de pistes SP et diverses manipulations de l'UTOC. Ils n'ajoutent **pas** de capacité d'encodage
   ATRAC3 côté device.

### 5.3 Conséquence pour `mini-disk`

**Un encodeur ATRAC3 côté hôte est indispensable** dès qu'on veut LP2 ou LP4. Il n'y a pas de
contournement. Pour SP, en revanche, **aucun encodeur n'est nécessaire** : on envoie du PCM 16 bits
big-endian et le device fait le travail (cf. §6.4).

---

## 6. Framing exact pour l'upload NetMD

### 6.1 En-tête WAV `WAVE_FORMAT_SONY_ATRAC3` (0x0270)

Format produit par atracdenc en sortie `.at3` / par le serveur d'encodage, et attendu en entrée par
Web MiniDisc Pro (`utils.ts:getATRACWAVEncoding`) :

```
offset  taille  contenu
------  ------  -----------------------------------------------------------
  0       4     "RIFF"
  4       4     tailleFichier - 8                                    (LE32)
  8       4     "WAVE"
 12       4     "fmt "
 16       4     tailleChunkFmt = 32 (0x20)                           (LE32)
 20       2     wFormatTag        = 0x0270  (WAVE_FORMAT_SONY_SCX)   (LE16)
 22       2     nChannels         = 2                                (LE16)
 24       4     nSamplesPerSec    = 44100                            (LE32)
 28       4     nAvgBytesPerSec   = 16537 (LP2) / 13091 (LP105) / 8268 (LP4)  (LE32)
 32       2     nBlockAlign       = 384 (LP2) / 304 (LP105) / 192 (LP4)       (LE16)
 34       2     wBitsPerSample    = 0                                (LE16)
 36       2     cbSize            = 14                               (LE16)
 38      14     extradata (voir ci-dessous)
 52       4     "data"
 56       4     tailleDonnées                                        (LE32)
 60      ...    frames ATRAC3
```

**Extradata (14 octets, tout en little-endian)** — décodé par `atrac3.c:atrac3_decode_init` :

| offset | taille | valeur | sens |
|---|---|---|---|
| 0 | 2 | `1` | inconnu, toujours 1 |
| 2 | 2 | `0x0800` = 2048 | « samples per channel » — champ ignoré/sauté (`edata_ptr += 4`) |
| 4 | 2 | ? | (partie du saut de 4 octets) |
| 6 | 2 | **`0` ou `1`** | **coding mode : 0 = normal (LP2/LP105), 1 = joint stereo (LP4)** |
| 8 | 2 | idem | duplicata du coding mode |
| 10 | 2 | `1` | `frame_factor`, toujours 1 |
| 12 | 2 | `0` | inconnu, toujours 0 |

Contrôle effectué par FFmpeg :

```c
if (block_align !=  96 * channels * frame_factor &&
    block_align != 152 * channels * frame_factor &&
    block_align != 192 * channels * frame_factor)   → AVERROR_INVALIDDATA
```

soit, en stéréo (`channels = 2`, `frame_factor = 1`) : **192, 304 ou 384**. Aucune autre valeur n'est
acceptée par le décodeur FFmpeg pour la variante WAV.

`nAvgBytesPerSec` = `nBlockAlign / 0.0232199` = `nBlockAlign * 44100 / 1024` :
384 → 16 537,5 → **16537** ; 304 → 13 092,2 → **13091** (valeur usuelle) ; 192 → 8 268,75 → **8268**.
(Ce champ est purement informatif, aucun décodeur ne le vérifie.)

**Variante `0xFFFE` (WAVE_FORMAT_EXTENSIBLE)** : acceptée aussi par Web MiniDisc Pro
(`if ((wavType !== 0x270 && wavType !== 0xfffe) || channels !== 0x02) return null;`).

### 6.2 Repérage de la section `data` (strip de l'en-tête)

Web MiniDisc Pro ne suppose **pas** un en-tête de taille fixe ; il parcourt les chunks
(`utils.ts:getATRACWAVEncoding`) :

```ts
let headerLength = 12;                       // après "RIFF" + size + "WAVE"
while (headerLength < fileData.byteLength) {
    const chunkType = readAscii(fileData, headerLength, 4);
    const chunkSize = readUInt32LE(fileData, headerLength + 4);
    if (chunkType === 'data') { headerLength += 8; break; }
    headerLength += chunkSize + 8;
}
```

puis, pour un encodage distant : `return source.slice(headerLength);` — **seules les frames brutes sont
envoyées au device**.

Le débit est ensuite déduit de `nBlockAlign / 2` (« octets par frame fil ») :

```ts
const bytesPerFrame = readUInt16LE(fileData, 32) / 2;
switch (bytesPerFrame) {
    case 192: return { codec: 'AT3', bitrate: 132 };   // LP2
    case 152: return { codec: 'AT3', bitrate: 105 };   // LP105
    case  96: return { codec: 'AT3', bitrate:  66 };   // LP4
}
```

### 6.3 Conteneur OMA / EA3 (chemin utilisé par atracdenc en WASM)

`atracdenc-worker.ts` écrit `outAt3File.aea`, laisse atracdenc produire un **OMA**, puis coupe les
**96 premiers octets** :

```ts
// Read file and trim header (96 bytes)
const tmp = new Uint8Array(size - 96);
Module.FS.read(stream, tmp, 0, tmp.length, /* offset */ 96);
```

Structure OMA reconnue par `utils.ts:getATRACOMAEncoding` :

```
[ tag "ea3" ID3-like optionnel : longueur syncsafe sur 4×7 bits, +10, +10 si flag 0x10 ]
"EA3\x01"  (4 o)
 ...
 offset  5 : taille du tag EA3 = 96
 offset  6-7 : type de chiffrement — 0xFFFF ou 0xFF80 = non chiffré
 offset 32   : codecType — 0 = ATRAC3, 1 = ATRAC3+, 3/4/5 = MP3/LPCM/WMA
 offset 33-35: codecInfo (24 bits)
                 bits 13-15 : index de fréquence  (table {320,441,480,882,960,0} × 100)
                 bit  17    : joint stereo
                 bits 0-9   : frameSize / 8
```

Correspondance ATRAC3 (`codecType == 0`) :

| frameSize | jointStereo | mode |
|---|---|---|
| 384 | 0 | LP2 (132 kbps) |
| 304 | 0 | LP105 |
| 192 | 1 | LP4 (66 kbps) |

**Recommandation pour `mini-disk`** : notre encodeur produira directement les **frames brutes** en
mémoire. Le conteneur WAV `0x0270` ne sera généré que pour :
- l'export vers un fichier (compatibilité SonicStage / VLC / FFmpeg) ;
- les **tests de round-trip** avec `ffmpeg -i out.at3 -f s16le -` (§7.3).

### 6.4 Ce que l'on envoie réellement au device

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ MDTrack(title, Wireformat.lp2, data, chunkSize = 0x400, fullWidthTitle, iter) │
└──────────────────────────────────────────────────────────────────────────────┘
        │
        │  data = frames ATRAC3 brutes (multiple de 384 pour LP2)
        │
        ├─ getFrameSize()  = FrameSize[format]  = 192 (LP2) / 152 (LP105) / 96 (LP4) / 2048 (PCM)
        ├─ getTotalSize()  = arrondi supérieur de data.byteLength au multiple de frameSize
        ├─ getFrameCount() = totalSize / frameSize
        │
        ▼
 sendTrack : query '1800 080046 f0030103 28 ff 000100 1001 ffff 00 %b %b %d %d'
             avec : wireformat, discformat, frames, totalBytes = pktSize + 24
```

Points de vigilance :

1. **`totalBytes = pktSize + 24`** — les 24 octets supplémentaires sont l'en-tête du premier paquet
   (clé + IV). Un seul en-tête pour tout le transfert.
2. **`chunkSize = 0x400` (1024 octets)** : taille des paquets DES-CBC envoyés au device. Sans rapport
   avec la taille de frame ATRAC3.
3. **Chiffrement** : `Crypto.mode.ECB` + `Pkcs7` pour la dérivation de clé, `NoPadding` pour les données
   (`padding: Crypto.pad.NoPadding`), avec un `contentID` fixe de 20 octets et une KEK dérivée de l'EKB.
4. **Padding** (`netmd-interface.ts`) :
   ```ts
   const frameSize = this.getFrameSize();
   if (uint8DataArray.length % frameSize !== 0) {
       let padding = frameSize - (uint8DataArray.length % frameSize);
       uint8DataArray = concatUint8Arrays(uint8DataArray, new Uint8Array(Array(padding).fill(0)));
   }
   ```
   → **zéros**. À éviter en produisant nous-mêmes un nombre entier de frames.
5. **`discformat`** est déduit du wireformat via `discforwire` (§5.1) : LP105 est stocké comme du LP2
   sur le disque (même `DiscFormat`), seul le débit change.

### 6.5 Le cas SP (PCM big-endian)

```ts
// audio-export.ts
async encodePCM(parameters) {
    const ffmpegCommand = await this.createFfmpegParams(parameters, 's16be');
    // → "-ac 2 -ar 44100 -f s16be"
    ...
    return data.buffer;      // AUCUN en-tête : flux brut
}
```

- **Format** : PCM signé 16 bits **big-endian**, 2 canaux entrelacés (L R L R…), 44 100 Hz.
- **Frame fil** : 2048 octets = 512 trames stéréo = **11,61 ms** — exactement la durée d'un
  *sound group* ATRAC1.
- **Pas d'en-tête WAV**, pas de padding autre que l'alignement sur 2048 octets.
- Le device encode en ATRAC1 SP (292 kbps) pendant le transfert.

> **Piège n° 6** : `s16be`. Sur une machine little-endian, il faut **échanger les octets** de chaque
> échantillon. Une inversion oubliée produit un bruit blanc saturé caractéristique — c'est l'erreur la
> plus fréquente sur les réimplémentations de NetMD.

Résumé des formats de charge utile :

| Mode | Contenu envoyé | Taille unitaire | En-tête |
|---|---|---|---|
| SP | PCM s16**BE** stéréo 44,1 kHz | 2048 o | aucun |
| LP2 | frames ATRAC3 brutes | 192 o (½ frame) | aucun |
| LP105 | frames ATRAC3 brutes | 152 o (½ frame) | aucun |
| LP4 | frames ATRAC3 brutes (joint stereo) | 96 o (½ frame) | aucun |

---

## 7. Recommandation, plan par jalons, tests, API C

### 7.1 Décision recommandée

**Écrire `libmdat3` : un encodeur ATRAC3 en C99 pur, sans dépendance, sous licence MIT/zlib**, en
utilisant :
- `libavcodec/atrac3.c` comme **spécification du bitstream** (lecture, pas copie) ;
- `atracdenc` comme **documentation des heuristiques** (allocation, gain, tonal) ;
- **FFmpeg comme oracle de test** dès la première frame produite.

Justification :
1. la contrainte de licence MIT/zlib est **structurante** et disqualifie le portage direct ;
2. l'écart d'effort (≈ 3 semaines) est acceptable au regard du bénéfice (autonomie, absence de
   dépendance C++/CMake, binaire unique) ;
3. le format est **petit** : 1024 échantillons, 32 BFU, 7 tables Huffman, 8 modes. La partie « dure »
   (QMF, MDCT) est du DSP standard, entièrement décrit par des formules publiques ;
4. l'oracle FFmpeg rend le débogage déterministe : à chaque étape, on décode ce qu'on vient d'écrire.

**Plan B assumé** : si la qualité obtenue reste insuffisante après le jalon 5, ajouter un *backend*
optionnel « encodeur externe » (binaire atracdenc, ou serveur `POST /transcode`) derrière la **même
API C** (§7.4), sans toucher au reste de l'application.

### 7.2 Jalons

#### Jalon 0 — Outillage et oracle (2 jours)

- Script de round-trip : `pcm → notre encodeur → .at3 (WAV 0x0270) → ffmpeg → pcm' → métriques`.
- Métriques : RMS d'erreur, SNR segmental, PEAQ approximé (ou au minimum corrélation par bande de tiers
  d'octave), plus une écoute A/B.
- Corpus de test : 10 extraits de 15 s (voix seule, castagnettes, piano, applaudissements, orchestre,
  électro, silence, sinus 1 kHz, sweep 20 Hz–20 kHz, bruit rose).
- Référence : les mêmes extraits encodés par `atracdenc --bitrate 128` et, si possible, par le serveur
  minidisc.wiki.

**Critère de sortie** : le pipeline tourne, l'oracle FFmpeg décode un `.at3` produit par atracdenc.

#### Jalon 1 — Frames valides, qualité minimale (1 semaine)

Objectif : produire un `.at3` **que FFmpeg décode sans erreur**, LP2 mono-canal dupliqué.

- QMF 3 étages (§2.2) — vérifié par un test de reconstruction (analyse + synthèse) à −90 dB.
- MDCT 512 + fenêtre (§2.3) — vérifiée par round-trip MDCT/IMDCT.
- Scaling par BFU + quantification naïve (`round`).
- Allocation de bits **fixe** (`fixed_bit_alloc[]` du §2.6.5), sans ATH ni spread.
- CLC uniquement (`coding_mode = 1`).
- `num_points = 0` pour toutes les bandes, `nb_components = 0`.
- Bitwriter MSB-first.

**Critère de sortie** : `ffmpeg -i out.at3 -f s16le out.pcm` sans warning ; SNR global > 10 dB ;
audio reconnaissable.

#### Jalon 2 — Allocation de bits et VLC (1 semaine)

- Recherche binaire sur le shift global (§2.6.5), budget = `FrameSz/2 * 8 − entêtes`.
- Courbe ATH reconstruite par formule analytique (pas la table Musepack).
- Mesure de `spread` (dispersion des scalefactors).
- Tables Huffman complètes (7 tables, **table 4 corrigée**), choix CLC/VLC par sound unit.
- Élagage de la dernière BFU (`CheckBfus`).

**Critère de sortie** : LP2 « écoutable », SNR segmental comparable à atracdenc ±2 dB, taille de fichier
exacte (`ceil(n/1024) * 384`).

#### Jalon 3 — Joint stereo et LP4 (1 semaine)

- Matrixing M/S temporel par sous-bande (§2.9.4).
- Préambule JS, écriture inversée de SU2, calcul de `msBytesShift`.
- Sélecteur de matrice figé à `3`, pondération figée à `7`.

**Critère de sortie** : LP4 décodé correctement par FFmpeg **et** par un baladeur réel ; contrôle que
`L ≈ M+S` et `R ≈ M−S` à la reconstruction.

#### Jalon 4 — Qualité : tonal + quantification énergétique (1 semaine)

- Extraction des composantes tonales (§2.7), groupement, écriture.
- `QuantMantisas` energy-aware (§2.6.4) et boost des BFU basses.
- Courbe de *loudness* et pondération de l'ATH par le niveau global.

**Critère de sortie** : gain mesurable sur les sources tonales (piano, cordes) en LP4.

#### Jalon 5 — Gain control (1 à 2 semaines)

- Détection de transitoires par bande QMF (énergie court terme / long terme, seuil adaptatif).
- Construction des courbes de gain (nombre de points, niveaux, positions strictement croissantes).
- Modulation avant MDCT.

**Critère de sortie** : disparition du pre-echo sur castagnettes et applaudissements.

#### Jalon 6 — Performance et intégration (3 jours)

- Objectif : **≥ 50× temps réel** sur un cœur moderne (soit ≥ 2150 frames/s, soit ≤ 465 µs par frame
  stéréo). Budget par frame et par canal :
  | Étage | Coût brut | Optimisation |
  |---|---|---|
  | QMF (3 étages) | ~49 k MAC | boucle polyphasée, `restrict`, SIMD optionnelle |
  | MDCT ×4 | ~4 × 128·7 papillons | FFT radix-4 128 points, tables pré-calculées |
  | Scaling + quantification | ~1024 mult + 1024 `lrintf` | `lrintf` scalaire suffit |
  | Boucle d'allocation | ~8 itérations × 32 BFU | **cache <bfu, wordlen>** obligatoire |
  | Bitwriter | ~3 000 bits | accumulateur 64 bits |
  Sans SIMD, un encodeur C bien écrit tourne autour de **150–300× temps réel** sur un cœur x86-64 récent :
  la cible de 50× est largement atteignable, **à condition** de mettre en place le cache de
  quantification (sans lui, la boucle d'allocation quantifie 8 fois les 1024 raies).
- Sortie : frames brutes en mémoire + écriture optionnelle du WAV `0x0270`.
- Padding de fin : compléter le PCM à un multiple de 1024, ajouter 2 frames de rinçage.

**Critère de sortie** : `bench` reproductible, ≥ 50× temps réel, `valgrind`/ASan propres.

### 7.3 Stratégie de test

#### 7.3.1 Tests unitaires (par étage)

| Test | Méthode | Critère |
|---|---|---|
| QMF analyse/synthèse | bruit blanc → analyse → synthèse → comparaison | erreur RMS < −85 dBFS après compensation du délai |
| QMF croisement des bandes | sinus à 18 kHz | énergie concentrée dans `subs[3]`, pas `subs[2]` |
| MDCT/IMDCT | signal aléatoire, 3 frames, recouvrement | reconstruction parfaite (TDAC) à −120 dB |
| Fenêtre | somme des carrés des deux moitiés | condition de Princen-Bradley vérifiée |
| Tables Huffman | encodage puis décodage de tous les symboles | bijection, longueurs conformes à `huff_tab_sizes` |
| Bitwriter | écriture de motifs connus | comparaison octet à octet avec un vecteur de référence |
| Scalefactors | `sf_table[i] * inv_max_quant[wl] * mantisse` | reconstruction de la valeur à ±1 LSB |
| Silent frame | encodage de 4096 zéros | frames identiques à `A3 00 00 00 40 00 …`, décodage FFmpeg silencieux |

#### 7.3.2 Tests d'intégration — round-trip FFmpeg

```sh
# 1. encoder
./mdat3enc --mode lp2 --in test.wav --out test.at3

# 2. décoder avec l'oracle
ffmpeg -v error -i test.at3 -f s16le -acodec pcm_s16le -ar 44100 -ac 2 out.pcm

# 3. comparer
./mdat3cmp test.wav out.pcm --delay 2190 --report snr,segsnr,band
```

Assertions automatiques :
- FFmpeg ne produit **aucun** message d'erreur (`Sound Unit id != 0x28`, `JS mono Sound Unit id != 3`,
  `Unknown frame/channel/frame_factor configuration`, `AVERROR_INVALIDDATA` sur le gain ou le tonal) ;
- `taille(test.at3) − 60 == ceil(n_samples / 1024) * FrameSz` ;
- SNR segmental ≥ seuil dépendant du jalon ;
- absence de NaN/Inf dans la sortie.

#### 7.3.3 Tests de non-régression bitstream

Constituer un corpus de **frames de référence** produites par atracdenc et par le serveur
minidisc.wiki, et écrire un **analyseur de bitstream** (≈ 200 LOC, réutilisant nos tables) qui affiche
la structure d'une frame :

```
frame 0 / SU1 : id=0x28 bands=3 gain=[0,0,0,0] tonal=0
                num_subbands=21 mode=CLC
                wl = 7 7 6 6 5 5 5 4 4 4 3 3 3 3 2 2 2 1 1 1 0 0
                sfi= 41 40 40 39 38 37 ...
                bits utilisés : 1523 / 1530
```

Ce diagnostic est **indispensable** : sans lui, un décalage d'un bit se traduit par « ça sonne mal »
sans indice sur la cause.

#### 7.3.4 Tests sur matériel

- Upload LP2 et LP4 sur au moins deux baladeurs de générations différentes (par ex. un MZ-N510 et un
  MZ-RH1), lecture complète, contrôle de la durée affichée et de l'absence de clic en fin de piste.
- Contrôle du `DiscFormat` remonté par `getTrackEncoding()` : `Encoding.lp2 = 0x92`, `lp4 = 0x93`.

#### 7.3.5 Fuzzing inverse

Passer nos frames dans FFmpeg compilé avec ASan/UBSan : toute frame qui fait planter ou déclenche
`AVERROR_INVALIDDATA` est un bug de notre encodeur.

### 7.4 Esquisse d'API C

```c
/* ============================================================================
 * mdat3.h — Encodeur ATRAC3 (LP2 / LP105 / LP4) pour NetMD
 * Licence : MIT
 * ==========================================================================*/
#ifndef MDAT3_H
#define MDAT3_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Modes
 * ---------------------------------------------------------------------- */
typedef enum {
    MDAT3_MODE_LP2   = 0,   /* 132.3 kbps, 384 o/frame, stéréo L/R        */
    MDAT3_MODE_LP105 = 1,   /* 104.7 kbps, 304 o/frame, stéréo L/R        */
    MDAT3_MODE_LP4   = 2    /*  66.2 kbps, 192 o/frame, joint stereo M/S  */
} mdat3_mode_t;

/* Nombre d'échantillons PCM (par canal) consommés par frame. Toujours 1024. */
#define MDAT3_FRAME_SAMPLES  1024

/* Taille en octets d'une frame encodée (frame stéréo complète). */
size_t mdat3_frame_size(mdat3_mode_t mode);           /* 384 / 304 / 192 */

/* Débit nominal en bits/s (66150, 104738, 132300). */
uint32_t mdat3_bitrate(mdat3_mode_t mode);

/* -------------------------------------------------------------------------
 * Options
 * ---------------------------------------------------------------------- */
typedef struct {
    int  enable_gain_control;   /* 1 = anti pre-echo (défaut 1)             */
    int  enable_tonal;          /* 1 = composantes tonales (défaut 1)       */
    int  force_bfu_count;       /* 0 = automatique ; 1..32 = forcé (debug)  */
    int  flush_frames;          /* frames de rinçage en fin de flux, déf. 2 */
} mdat3_options_t;

void mdat3_options_default(mdat3_options_t *opt);

/* -------------------------------------------------------------------------
 * Codes d'erreur
 * ---------------------------------------------------------------------- */
typedef enum {
    MDAT3_OK              =  0,
    MDAT3_E_INVAL         = -1,   /* argument invalide                     */
    MDAT3_E_NOMEM         = -2,
    MDAT3_E_BUFFER_SMALL  = -3,   /* buffer de sortie trop petit           */
    MDAT3_E_STATE         = -4    /* appel après mdat3_finish()            */
} mdat3_err_t;

const char *mdat3_strerror(int err);

/* -------------------------------------------------------------------------
 * Encodeur
 * ---------------------------------------------------------------------- */
typedef struct mdat3_enc mdat3_enc_t;

/* Crée un encodeur. `opt` peut être NULL (options par défaut).
 * L'entrée est toujours : 44100 Hz, 2 canaux, entrelacé.                  */
mdat3_enc_t *mdat3_enc_create(mdat3_mode_t mode, const mdat3_options_t *opt);
void         mdat3_enc_destroy(mdat3_enc_t *enc);

/* Encode EXACTEMENT MDAT3_FRAME_SAMPLES trames stéréo.
 *   pcm  : 2 * 1024 int16_t entrelacés, ordre natif de la machine
 *   out  : au moins mdat3_frame_size(mode) octets
 * Retourne le nombre d'octets écrits (== mdat3_frame_size) ou < 0.        */
int mdat3_enc_frame_s16(mdat3_enc_t *enc,
                        const int16_t *pcm,
                        uint8_t *out, size_t out_cap);

/* Variante flottante : 2 * 1024 float entrelacés, normalisés dans [-1, 1]. */
int mdat3_enc_frame_f32(mdat3_enc_t *enc,
                        const float *pcm,
                        uint8_t *out, size_t out_cap);

/* Vide les buffers internes (recouvrement MDCT + mémoire QMF).
 * Écrit `flush_frames` frames dans `out`.
 * Retourne le nombre total d'octets écrits ou < 0.                        */
int mdat3_enc_finish(mdat3_enc_t *enc, uint8_t *out, size_t out_cap);

/* -------------------------------------------------------------------------
 * Aide « one-shot » : encode un buffer PCM complet.
 * `*out` est alloué par la fonction (malloc), à libérer par l'appelant.
 * Le PCM est complété à un multiple de 1024 avec des zéros.
 * ---------------------------------------------------------------------- */
int mdat3_encode_buffer_s16(mdat3_mode_t mode, const mdat3_options_t *opt,
                            const int16_t *pcm, size_t n_frames_stereo,
                            uint8_t **out, size_t *out_len);

/* -------------------------------------------------------------------------
 * Conteneur WAV WAVE_FORMAT_SONY_ATRAC3 (0x0270) — pour l'export fichier
 * et les tests de round-trip. Le device NetMD reçoit les frames BRUTES.
 * ---------------------------------------------------------------------- */
#define MDAT3_WAV_HEADER_SIZE 60

/* Écrit l'en-tête de 60 octets dans `hdr`. `data_bytes` = taille des frames. */
int mdat3_write_wav_header(mdat3_mode_t mode, uint32_t data_bytes,
                           uint8_t hdr[MDAT3_WAV_HEADER_SIZE]);

/* -------------------------------------------------------------------------
 * Frame silencieuse (bourrage, amorçage, tests)
 * ---------------------------------------------------------------------- */
int mdat3_silent_frame(mdat3_mode_t mode, uint8_t *out, size_t out_cap);

/* -------------------------------------------------------------------------
 * Introspection / debug : décrit la structure d'une frame encodée.
 * Écrit un texte lisible dans `buf`. Retourne la longueur ou < 0.
 * ---------------------------------------------------------------------- */
int mdat3_dump_frame(mdat3_mode_t mode, const uint8_t *frame, size_t len,
                     char *buf, size_t buf_cap);

#ifdef __cplusplus
}
#endif
#endif /* MDAT3_H */
```

**Notes de conception**

1. **Pas d'allocation dans le chemin chaud** : tout l'état (historiques QMF, recouvrements MDCT,
   caches de quantification) vit dans `mdat3_enc_t`, alloué une fois. Empreinte estimée :
   - historiques QMF : (1024+46) + 2 × (512+46) floats × 2 canaux ≈ 17 ko
   - recouvrements MDCT : 4 bandes × 256 floats × 2 canaux = 8 ko
   - spectres, blocs mis à l'échelle, caches : ~40 ko
   - **total ≈ 70 ko par encodeur** → parfaitement compatible avec un usage embarqué.
2. **Granularité fixe de 1024 échantillons** : c'est la contrainte du format ; laisser l'appelant gérer
   son propre buffer d'accumulation évite une file interne.
3. **`mdat3_dump_frame`** est exposé publiquement : c'est l'outil de diagnostic n° 1 (§7.3.3), et il sert
   aussi à valider les fichiers `.at3` produits par des tiers.
4. **Pas de gestion de conteneur OMA** : inutile pour NetMD, et le WAV `0x0270` suffit pour
   l'interopérabilité.
5. **Backend externe optionnel** : ajouter, derrière `mdat3_enc_create`, une variante
   `mdat3_enc_create_external(const char *cmdline)` qui délègue à un binaire atracdenc ou à un service
   HTTP. La signature du reste de l'API ne change pas.

### 7.5 Ordre d'implémentation conseillé (fichiers)

```
src/atrac3/
├── mdat3.h              API publique (ci-dessus)
├── mdat3_tables.c/.h    toutes les tables constantes (§8)
├── mdat3_bitwriter.c/.h ~80 LOC   — écriture MSB-first, accumulateur 64 bits
├── mdat3_qmf.c/.h       ~120 LOC  — banc 3 étages
├── mdat3_mdct.c/.h      ~220 LOC  — MDCT 512 via FFT complexe 128
├── mdat3_psy.c/.h       ~150 LOC  — ATH analytique, loudness, spread, flatness
├── mdat3_scale.c/.h     ~180 LOC  — scaling BFU, quantification energy-aware
├── mdat3_alloc.c/.h     ~260 LOC  — boucle d'allocation, coût CLC/VLC, cache
├── mdat3_tonal.c/.h     ~230 LOC  — extraction, groupement, écriture
├── mdat3_gain.c/.h      ~300 LOC  — transitoires + courbes de gain (jalon 5)
├── mdat3_frame.c        ~320 LOC  — assemblage sound unit, joint stereo, reservoir
├── mdat3_wav.c          ~90  LOC  — en-tête 0x0270
└── mdat3_dump.c         ~200 LOC  — analyseur de bitstream
                        ─────────
                        ≈ 2 150 LOC + tables
```

---

## 8. Tables constantes à extraire (fichier + symbole)

Toutes ces tables sont **imposées par le format**. Celles marquées « formule » doivent être **régénérées
par calcul** plutôt que recopiées (propreté juridique et lisibilité).

### 8.1 Structure du spectre

| Table | Taille | Source de référence | Mode d'obtention |
|---|---|---|---|
| Bornes de BFU | 33 × uint16 | `FFmpeg/libavcodec/atrac3data.h : subband_tab[33]` ⟷ `atracdenc/src/atrac/at3/atrac3.h : TAtrac3Data::BlockSizeTab[33]` | littérale (donnée du format) |
| Largeur de BFU | 33 × uint8 | `atrac3.h : TAtrac3Data::SpecsPerBlock[33]` | **dérivée** : `bounds[i+1] − bounds[i]` |
| Première BFU de chaque bande QMF | 5 × uint32 | `atrac3.h : TAtrac3Data::BlocksPerBand[5] = {0,18,26,30,32}` | littérale |
| Allocation fixe de repli | 32 × uint8 | `atracdenc/src/atrac/at3/atrac3_bitstream.cpp : FixedBitAllocTable[32]` | littérale (heuristique — peut être retunée) |

### 8.2 Quantification

| Table | Taille | Source | Mode |
|---|---|---|---|
| `max_quant[8]` (encodeur) | 8 × float | `atrac3.h : TAtrac3Data::MaxQuant[8] = {0, 1.5, 2.5, 3.5, 4.5, 7.5, 15.5, 31.5}` | littérale |
| `inv_max_quant[8]` (décodeur) | 8 × float | `atrac3data.h : inv_max_quant[8]` | **dérivée** : `1/max_quant[i]` |
| `clc_length_tab[8]` | 8 × uint8 | `atrac3data.h : clc_length_tab` ⟷ `atrac3.h : ClcLengthTab` : `{0,4,3,3,4,4,5,6}` | littérale |
| `mantissa_clc_tab[4]` (décodeur) | 4 × int8 | `atrac3data.h : mantissa_clc_tab = {0, 1, -2, -1}` | littérale |
| `mantissa_clc_rtab[4]` (encodeur) | 4 × uint8 | `atrac3.h : MantissaToCLcIdx → {2, 3, 0, 1}` | **dérivée** (inverse de la précédente) |
| `mantissa_vlc_tab[18]` (décodeur) | 18 × int8 | `atrac3data.h : mantissa_vlc_tab` | littérale |
| `mantissas_vlc_rtab[9]` (encodeur) | 9 × uint8 | `atrac3.h : MantissasToVlcIndex → {8,4,7,2,0,1,6,3,5}` | **dérivée** (inverse) |
| `sf_table[64]` | 64 × float | `FFmpeg/libavcodec/atrac.c : ff_atrac_sf_table` | **formule** : `powf(2.0f, (i - 15) / 3.0f)` ; version normalisée `powf(2.0f, i/3.0f - 21.0f)` |

### 8.3 Tables Huffman (7 tables)

| Élément | Source | Mode |
|---|---|---|
| Codes + longueurs, 7 tables | `FFmpeg/libavcodec/atrac3data.h : atrac3_hufftabs[][2]` (paires `{symbole+31, longueur}`) | littérale — **source de vérité** |
| Tailles | `atrac3data.h : huff_tab_sizes[7] = {9, 5, 7, 9, 15, 31, 63}` | littérale |
| Codes canoniques | — | **dérivés** : construction canonique à partir des longueurs, dans l'ordre de la table |
| Correspondance zigzag encodeur | `atracdenc/src/atrac/at3/atrac3.h : HuffTable1..7` | **dérivée** : `s = (m<0) ? ((-m)<<1)|1 : (m<<1); if (s) s--;` |

> **Rappel** : la table 4 est identique à la table 1 (longueurs `{1,3,3,4,4,5,5,5,5}` pour les symboles
> `0,+1,−1,+2,−2,+3,−3,+4,−4`) — l'aliasing d'atracdenc est correct, vérifié par construction canonique.

### 8.4 Filtres et fenêtres

| Table | Taille | Source | Mode |
|---|---|---|---|
| Prototype QMF demi | 24 × float | `FFmpeg/libavcodec/atrac.c : qmf_48tap_half[24]` ⟷ `atracdenc/src/qmf/qmf.cpp : TapHalf[24]` | littérale (identiques) |
| Fenêtre QMF | 48 × float | — | **dérivée** : `w[i] = w[47-i] = half[i] * 2.0f` |
| Fenêtre MDCT d'analyse | 256 × float | `atracdenc/src/atrac/at3/atrac3.h`, ctor | **formule** : `1 + sinf(((i+0.5f)/256 - 0.5f) * M_PI)` |
| Fenêtre MDCT de synthèse | 256 × float | idem | **formule** : `2a / (a² + b²)`, `b = w[255-i]` |
| Table de rotation MDCT | 128 × complexe | — | **formule** : `exp(-i·2π(n + 1/8)/512)` |

### 8.5 Gain control

| Table | Taille | Source | Mode |
|---|---|---|---|
| `gain_tab1[16]` (niveaux) | 16 × float | `FFmpeg/libavcodec/atrac.c : ff_atrac_init_gain_compensation` ⟷ `atrac3.h : GainLevel[16]` | **formule** : `powf(2.0f, 4 - i)` |
| `gain_tab2[31]` (interpolation) | 31 × float | idem ⟷ `atrac3.h : GainInterpolation[31]` | **formule** : `powf(2.0f, -i/8.0f)` pour `i ∈ [-15, 15]` |
| Constantes | — | `ExponentOffset = 4`, `LocScale = 3`, `LocSz = 8`, `MaxGainPointsNum = 8` | littérales |

### 8.6 Joint stereo

| Table | Taille | Source | Mode |
|---|---|---|---|
| `matrix_coeffs[8]` | 8 × float | `atrac3data.h : {0,2,2,2,0,0,1,1}` | littérale |
| Poids de canal | — | `atrac3.c : get_channel_weights()` | **formule** : `ch0 = (i&7)/7`, `ch1 = sqrt(2 − ch0²)` |
| Préambule JS écrit | — | `atracdenc/src/atrac/at3/atrac3_bitstream.cpp : WriteJsParams` | littéral : `0(1b), 7(3b), 3×4(2b chacun), 3(2b)` |

### 8.7 Paramètres de conteneur

| Table | Taille | Source | Mode |
|---|---|---|---|
| `ContainerParams[8]` | 8 × {bitrate, frameSz, js} | `atracdenc/src/atrac/at3/atrac3.h : TAtrac3Data::ContainerParams` | littérale (§2.10) |
| `Wireformat` / `FrameSize` NetMD | 4 entrées | `netmd-js/src/netmd-interface.ts` (lignes 47-52 et 105-110) | littérale (§6.4) |
| `DiscFormat` | 4 entrées | idem, lignes 40-45 : `lp4=0, lp2=2, spMono=4, spStereo=6` | littérale |
| `Encoding` | 3 entrées | idem, lignes 54-58 : `sp=0x90, lp2=0x92, lp4=0x93` | littérale |

### 8.8 Psychoacoustique (à NE PAS copier)

| Table | Source amont | Remplacement recommandé |
|---|---|---|
| Table ATH 128 valeurs (millibels) | `atracdenc/src/atrac/atrac_psy_common.cpp : ATHformula_Frank` — **« Borrowed from Musepack »** | **formule analytique** : `ath_db(f) = 3.64·f^-0.8 − 6.5·exp(−0.6·(f−3.3)²) + 1e-3·f⁴` (f en kHz), clampée à [−20, +80] dB |
| Courbe de loudness | `atrac_psy_common.cpp : CreateLoudnessCurve` | pondération A ou ISO 226 phon 60, à recalculer |
| Constantes d'allocation `2.8 / 2.6 / 3.3 / 3.6 / 4.2 / 6` | `atrac3_bitstream.cpp : CalcBitsAllocation` | à retuner par écoute — ce sont des heuristiques, pas des constantes du format |

---

## 9. Liste des URLs

### atracdenc (LGPL 2.1)

- Dépôt : https://github.com/dcherednik/atracdenc
- `src/atrac/at3/atrac3.h` : https://github.com/dcherednik/atracdenc/blob/master/src/atrac/at3/atrac3.h
- `src/atrac/at3/atrac3.cpp` : https://github.com/dcherednik/atracdenc/blob/master/src/atrac/at3/atrac3.cpp
- `src/atrac/at3/atrac3_bitstream.cpp` : https://github.com/dcherednik/atracdenc/blob/master/src/atrac/at3/atrac3_bitstream.cpp
- `src/atrac/at3/atrac3_bitstream.h` : https://github.com/dcherednik/atracdenc/blob/master/src/atrac/at3/atrac3_bitstream.h
- `src/atrac/at3/atrac3_qmf.h` : https://github.com/dcherednik/atracdenc/blob/master/src/atrac/at3/atrac3_qmf.h
- `src/atrac3denc.cpp` : https://github.com/dcherednik/atracdenc/blob/master/src/atrac3denc.cpp
- `src/atrac3denc.h` : https://github.com/dcherednik/atracdenc/blob/master/src/atrac3denc.h
- `src/qmf/qmf.h` : https://github.com/dcherednik/atracdenc/blob/master/src/qmf/qmf.h
- `src/qmf/qmf.cpp` : https://github.com/dcherednik/atracdenc/blob/master/src/qmf/qmf.cpp
- `src/atrac/atrac_scale.cpp` : https://github.com/dcherednik/atracdenc/blob/master/src/atrac/atrac_scale.cpp
- `src/atrac/atrac_psy_common.cpp` : https://github.com/dcherednik/atracdenc/blob/master/src/atrac/atrac_psy_common.cpp
- `src/gain_processor.h` : https://github.com/dcherednik/atracdenc/blob/master/src/gain_processor.h
- `src/transient_detector.cpp` : https://github.com/dcherednik/atracdenc/blob/master/src/transient_detector.cpp
- `src/lib/bitstream/` : https://github.com/dcherednik/atracdenc/tree/master/src/lib/bitstream
- `src/lib/mdct/mdct.h` : https://github.com/dcherednik/atracdenc/blob/master/src/lib/mdct/mdct.h
- `src/CMakeLists.txt` : https://github.com/dcherednik/atracdenc/blob/master/src/CMakeLists.txt
- `src/main.cpp` : https://github.com/dcherednik/atracdenc/blob/master/src/main.cpp
- `LICENSE` (LGPL 2.1) : https://github.com/dcherednik/atracdenc/blob/master/LICENSE
- `README.md` : https://github.com/dcherednik/atracdenc/blob/master/README.md

### FFmpeg (LGPL 2.1) — référence bitstream et oracle de test

- `libavcodec/atrac3.c` : https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/atrac3.c
- `libavcodec/atrac3data.h` : https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/atrac3data.h
- `libavcodec/atrac.c` : https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/atrac.c
- `libavcodec/atrac.h` : https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/atrac.h

### NetMD — framing et upload

- `cybercase/netmd-js` : https://github.com/cybercase/netmd-js
- `netmd-js/src/netmd-interface.ts` : https://github.com/cybercase/netmd-js/blob/master/src/netmd-interface.ts
- `netmd-js/src/netmd-commands.ts` : https://github.com/cybercase/netmd-js/blob/master/src/netmd-commands.ts
- `netmd-js/src/encrypt-generator.ts` : https://github.com/cybercase/netmd-js/blob/master/src/encrypt-generator.ts
- `netmd-js/src/netmd-ekb.ts` : https://github.com/cybercase/netmd-js/blob/master/src/netmd-ekb.ts

### Web MiniDisc / Web MiniDisc Pro

- `cybercase/webminidisc` : https://github.com/cybercase/webminidisc
- `asivery/webminidisc` (Pro) : https://github.com/asivery/webminidisc
- `src/utils.ts` (parsing WAV 0x0270 / OMA) : https://github.com/asivery/webminidisc/blob/master/src/utils.ts
- `src/services/audio/audio-export.ts` (PCM s16be) : https://github.com/asivery/webminidisc/blob/master/src/services/audio/audio-export.ts
- `src/services/audio/atracdenc-export.ts` : https://github.com/asivery/webminidisc/blob/master/src/services/audio/atracdenc-export.ts
- `src/services/audio/atracdenc-worker.ts` (trim 96 octets OMA) : https://github.com/asivery/webminidisc/blob/master/src/services/audio/atracdenc-worker.ts
- `src/services/audio/atrac3re-export.ts` : https://github.com/asivery/webminidisc/blob/master/src/services/audio/atrac3re-export.ts
- `src/services/audio/atrac3re-worker.ts` (API C du harness) : https://github.com/asivery/webminidisc/blob/master/src/services/audio/atrac3re-worker.ts
- `src/services/audio/remote-atrac-export.ts` (serveur `/transcode`) : https://github.com/asivery/webminidisc/blob/master/src/services/audio/remote-atrac-export.ts
- `src/create-empty-wave.ts` : https://github.com/asivery/webminidisc/blob/master/src/create-empty-wave.ts
- `asivery/netmd-exploits` : https://github.com/asivery/netmd-exploits
- `DaveFlashNL/WebMDPro` (fork) : https://github.com/DaveFlashNL/WebMDPro

### Documentation communautaire

- MiniDisc Wiki : https://www.minidisc.wiki/
- MiniDisc Wiki — mise à jour automne 2024 (serveur d'encodage ATRAC3) : https://www.minidisc.wiki/about/updates/2024-11-01
- MultimediaWiki — ATRAC3 : https://wiki.multimedia.cx/index.php/ATRAC3
- MultimediaWiki — ATRAC1 : https://wiki.multimedia.cx/index.php/ATRAC1
- « The Semi-Ultimate ATRAC Encoding Comparison » : https://nytpu.com/gemlog/2023-06-27
- Wikipedia — ATRAC : https://en.wikipedia.org/wiki/ATRAC
- Wikipedia — MiniDisc : https://en.wikipedia.org/wiki/MiniDisc
- `gavinbenda/linux-minidisc` (protocole NetMD historique) : https://github.com/gavinbenda/linux-minidisc

### Divers

- kissfft (BSD-3-Clause) : https://github.com/mborgerding/kissfft
- Musepack (origine de la table ATH d'atracdenc) : https://www.musepack.net/
- Brevet Sony EP 1262980 A2 (enregistrement/reproduction) : https://data.epo.org/gpi/EP1262980A2

---

*Fin du document — R-04, `docs/research/04-atrac3-encoder.md`.*

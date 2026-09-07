# T-045 — Le surcoût de piste mesuré entre dans le modèle de capacité

Phase 5 · Statut : **fait** · Dépend de : T-031, T-043 (mesures appareil), ADR-011 D2

## Le problème

T-043 a mesuré, sur le vrai MZ-N505, ce que chaque piste SP coûte **au temps libre que l'appareil
annonce lui-même** : `ceil(durée / 2 s) × 2 s` **plus un cluster de 2 s**. Sept mesures, écart moyen
**+2 007 ms**, écart-type **80 ms**. Notre modèle (`plan_capacity.{h,c}`, T-031) ne comptait que les
clusters arrondis : la jauge sous-estimait donc d'un cluster par piste — 40 s de mensonge sur un
disque de 20 pistes, exactement ce qu'ADR-011 D2 interdit.

---

## Livraison

### Ce qui a changé

**1. La constante, avec sa mesure (`src/core/plan/plan_capacity.h`)**

```c
#define PLAN_TRACK_OVERHEAD_CLUSTERS 1u
```

Le commentaire porte la mesure (7 uploads SP sur le MZ-N505, 2026-09-07, +2 007 ms ± 80 par piste,
voir T-043) et dit ce qu'elle ne dit pas : elle est **mesurée en SP seulement**, et elle est appliquée
à **tous les modes** comme **un cluster de disque** de `PLAN_CLUSTER_SP_MS`, parce que ce qu'elle paie
est un cluster de lien/TOC **du disque**, pas de l'audio. Une piste LP4 coûte donc un cluster de
8 000 ms d'audio plus un cluster de 2 000 ms de disque.

Le pavé `MD_MODE_TABLE` ne dit plus « à valider sur l'appareil » : il dit **validé pour le SP le
2026-09-07**, le pas de 2 000 ms est confirmé à la milliseconde, et les tailles de cluster LP2 / LP4 /
mono **restent non mesurées** — l'appareil n'encode que du SP en v1, donc rien de ce que nous écrivons
ne passe par ces lignes.

**2. L'arithmétique (`plan_capacity.c`)**

- `plan_audio_clusters_for` (nouveau, interne) : l'arrondi d'avant, jamais zéro.
- `plan_clusters_for` = `plan_audio_clusters_for` + `PLAN_TRACK_OVERHEAD_CLUSTERS`, donc jamais sous
  `1 + surcoût`.
- `plan_padding_ms` **inchangé de sens** : les millisecondes d'audio que l'arrondi gaspille dans le
  dernier cluster **audio**. Le cluster de lien n'y est pas — ce n'est pas du silence, c'est un
  cluster du disque. `PlanCapacity.entry_padding_ms` suit la même règle : la jauge **hachure ce
  seul morceau**, le surcoût fait partie de la largeur du segment.
- `PlanCapacity.billed_ms` **inclut** le surcoût : c'est ce que le disque est facturé.
  `PlanCapacity.padding_ms` est la somme des `entry_padding_ms` ; `billed_ms - audio_ms` vaut donc
  `padding_ms + entry_count × 2 000 ms`, et un test le vérifie.

**3. Les appelants, relus un par un**

| Fichier | Ce qu'il voulait dire | Suite |
|---------|----------------------|-------|
| `src/app/plan_view.c` (`plan_gauge_layout`) | la hachure = part de la piste que l'arrondi gaspille | **inchangé** : la formule `largeur × padding_ms / (clusters × cluster_ms)` normalise déjà les deux termes en clusters, donc le cluster de lien élargit le segment sans jamais le hachurer — ce qu'on veut |
| `src/app/plan_view.c` (`plan_fill_count`) | ce qui rentre encore | **inchangé**, il appelle `plan_clusters_for` |
| `src/app/view_plan.c` (bulle par piste) | « facturé X (+Y) » | `plan_billed_ms_of(clusters, entries, mode)` (nouveau, en ligne) au lieu de `clusters × cluster_ms` — sinon le cluster de lien d'un segment LP4 serait compté 8 000 ms au lieu de 2 000. La chaîne dit maintenant **« +Y dont +1 cluster de lien »** (FR) / *« including +1 link cluster »* (EN) |
| `src/app/transfer.c` (pré-vol) | ce que la gravure va prendre | **inchangé**, il appelle `plan_clusters_for` : les chiffres avant/après du pré-vol sont désormais ceux de l'appareil |
| `src/app/view_device.c` | ce que le disque inséré porte déjà | `billed_ms` passe par `plan_billed_ms_of` et `padding_ms` devient la somme des `entry_padding_ms` (il faisait `billed - audio`, ce qui compterait le lien comme du silence) |
| `src/core/netmd/netmd_upload.c` (`netmd_upload_check_capacity`) | assez de place, d'après l'appareil | `clusters += 1` avec le renvoi à la mesure : le garde-fou refusait un plan un cluster par piste trop tard |

### La mesure (recopiée de T-043)

`--device-burn`, MZ-N505, disque « 202001 », une session d'upload par piste, temps libre lu sur
l'appareil avant et après chacune.

| Piste | Octets SP | Libre avant | Libre après | Coût réel | `plan_clusters_for` × 2 s (avant) | Écart |
|-------|-----------|-------------|-------------|-----------|-----------------------------------|-------|
| MINIDISK TEST 5s | 882 688 | 3 383 134 ms | 3 375 105 ms | **8 029 ms** | 6 000 ms | **+2 029 ms** |
| MINIDISK TEST 25s | 4 411 392 | 3 375 105 ms | 3 347 003 ms | **28 102 ms** | 26 000 ms | **+2 102 ms** |
| MINIDISK TEST 61s | 10 762 240 | 3 347 003 ms | 3 283 113 ms | **63 890 ms** | 62 000 ms | **+1 890 ms** |
| *(run précédent, même piste de 5 s)* | 882 688 | 3 391 164 ms | 3 383 134 ms | **8 030 ms** | 6 000 ms | **+2 030 ms** |
| **Les trois en une session** | 16 056 320 | 3 283 113 ms | 3 183 091 ms | **100 022 ms** | 94 000 ms | **+6 022 ms** (3 × 2 007) |

Avec `PLAN_TRACK_OVERHEAD_CLUSTERS 1`, le modèle dit 8 000 / 28 000 / 64 000 ms et 100 000 ms pour les
trois : chacun **dans le pas de 2 000 ms** de la mesure, qui est toute la résolution qu'elle a.

### Les attentes recalées

Aucune assertion n'a été affaiblie : chaque cas garde son intention, et les commentaires disent les
nouveaux nombres.

**`tests/test_capacity.c`**
- *(nouveau cas)* `capacity_track_overhead_matches_the_device` : épingle la mesure — 5 s, 25 s et 61 s
  en SP coûtent **4, 14 et 32 clusters** (8 000 / 28 000 / 64 000 ms), chacun encadré à ±2 000 ms du
  coût réel mesuré, et 100 000 ms pour les trois ensemble. Sans le cluster de lien on aurait 6 000 /
  26 000 / 62 000 — un cluster court à chaque fois, ce que le cas dit aussi.
- Arrondi : `plan_clusters_for(3000, LP4)` 1 → **2** ; `(1, SP)` 1 → **2** ; `(2000, SP)` 1 → **2** ;
  `(2001, SP)` 2 → **3**. `plan_padding_ms(3000, LP4)` reste **5 000** (le lien n'est pas du silence),
  `plan_padding_ms(2000, SP)` reste **0**.
- 40 × 2:00,001 en SP : 61 → **62** clusters chacun, 2 440 → **2 480**, débordement 40 → **80**,
  `billed_ms` 40 × 122 000 → **40 × 124 000**, `padding_ms` **inchangé** à 40 × 1 999 (+ l'égalité
  `billed - audio == padding + 40 × 2 000`). La piste à cheval sur la fin recule de deux places :
  `first_overflow` 39 → **38**, `fit[37]` Fits, `fit[38]` Partial, `fit[39]` **Overflow**.
- 79 × 1:00,7 en SP : 31 → **32** clusters chacun, 2 449 → **2 528** (84:16 de disque pour 79:55,3
  d'audio), débordement 49 → **128**.
- SP / LP2 / LP4 / mono à 5:00 : 150/75/38/75 = 338 → **151/76/39/76 = 342**, libre 2 062 → **2 058**.
- Split, 30 × 5:00 : 150 → **151** clusters, donc **15** par disque de 80 min et non 16 —
  `counts` 16/14 → **15/15**, `clusters` 2 400/2 100 → **2 265/2 265**, la frontière passe de
  `disc_of[15]` à **`disc_of[14]` = 0 / `disc_of[15]` = 1**.
- Split, premier-ajusté et non suivant : 16 × 4:50 = **2 336** clusters (145 → 146 chacun), 64 libres ;
  la piste de 10:00 (**301**) va sur le disque 2 et celle de 0:30 (**16**) revient sur le disque 1 —
  le cas tient tel quel, seuls les nombres du commentaire changent.
- Split, albums gardés ensemble : 900 → **906** clusters par album, deux albums par disque —
  conclusion inchangée. En premier-ajusté, `counts[0]` 16 → **15** (album C coupé après trois pistes).

**`tests/test_view_plan.c`**
- `view_plan_gauge_hatch_and_alternation` : 4 000 ms → largeur 2 → **3** px, 4 001 ms → 3 → **4** px ;
  la hachure reste **1 px** (1 999 ms de padding sur 4 clusters), donc `hatch_x` = `x + 2` → **`x + 3`**
  sur le second segment et `x + 2` → **`x + 3`** sur le premier (qui ne hachure toujours rien).
- `view_plan_gauge_overflow_zone` : 3:00 en SP = 90 → **91** clusters ; 27 pistes = 2 430 → **2 457**,
  `first_overflow` reste **26** et le début de la zone rouge passe de `26 × 90` à **`26 × 91` = 2 366**
  clusters sur 2 400.
- `view_plan_fill_remaining_stops_where_it_must` : **scénario refait** pour que l'intention tienne
  encore (deux entrent, la troisième non ; en LP4 les quatre entrent). 26 × 3:00 donnerait 2 366 et
  34 clusters libres, où plus rien n'entre. Désormais **26 × 2:56** (176 000 ms) = 88 + 1 = **89**
  clusters chacun, **2 314** de 2 400, donc **86 libres**. En SP : 31 + 31 = 62 ≤ 86, la troisième
  demanderait 151 → **2**. En LP2 : 16 + 16 = 32, la troisième 76 → 32 + 76 = 108 > 86 → **2**. En
  LP4 : 9 + 9 + 39 + 5 = 62 ≤ 86 → **4**. Le reste du cas (rien n'entre sur un disque déjà plein) est
  inchangé.
- `view_plan_gauge_segments_tile_the_bar`, `..._merges_thin_segments`, `..._hit_test_and_scale` :
  **inchangés** — ils n'assertent que des invariants géométriques (pavage, ordre, hachure incluse dans
  son segment, échelle), tous encore vrais.

**`tests/test_transfer.c`**
- `transfer_sim_lists_what_will_be_written` : 180 s et 240 s de SP, `clusters` 90/120 → **91/121**,
  `clusters_needed` 210 → **212**, `free_ms_after` = 4 800 000 − **212** × 2 000 = **4 376 000 ms**.
- `transfer_sim_missing_track_and_overflow`, `transfer_sim_append_versus_erase`,
  `transfer_sim_protected_disc_refuses` : **inchangés** — les marges y sont larges (31 contre 300
  clusters libres, 601 contre 300, 601 contre 2 220), les conclusions ne bougent pas.

### Résultats

- Six cibles `build.bat` vertes : `debug`, `release`, `test`, `check`, `analyze`, `bench`.
- Tests : **249 cas / 6 848 checks**, 0 échec sous ASan (+1 cas, +34 checks).
- Exe release **636 928 o**, **inchangé à l'octet** (la constante est un `+1` en ligne, la nouvelle
  chaîne de bulle remplace l'ancienne). Imports `KERNEL32.dll` + `USER32.dll`.
- Bench : recalcul complet d'un disque de 254 pistes **39,8 µs**, cible **< 50 µs tenue** (clusters +
  états 5,8 µs, budget TOC 34,1 µs, premier-ajusté multi-disques 17,8 µs).

### Écarts et décisions

1. **Le surcoût est appliqué à tous les modes alors qu'il n'est mesuré qu'en SP.** C'est le choix
   conservateur *et* le choix physique : ce qui est payé est un cluster du disque par piste, pas de
   l'audio. Ne l'appliquer qu'au SP ferait mentir la jauge en LP2/LP4 dans le sens interdit
   (sous-estimation). Rien n'est écrit en LP2/LP4 en v1 de toute façon.
2. **`padding_ms` cesse d'être `billed_ms - audio_ms`.** Les deux étaient égaux tant que tout ce qui
   était facturé au-delà de l'audio était du silence d'arrondi. Ce n'est plus vrai : le cluster de
   lien est facturé et n'est pas du silence. `padding_ms` est maintenant la somme explicite des
   `entry_padding_ms`, et un test épingle la relation entre les trois.
3. **La deuxième série de mesures sur disque vierge n'a pas été refaite.** T-043 la demandait pour
   séparer surcoût fixe et fragmentation ; la revue du lead a tranché que les 7 mesures (σ 80 ms)
   suffisent. Si une série sur disque vierge donnait un autre chiffre, c'est cette constante-là, à un
   seul endroit, qui bougerait.
4. **P-014 reste ouvert.** Voir ci-dessous.

### P-014 — laissé ouvert

Le refus « réservée par SonicStage » vit dans `netmd_edit_simulate`
(`src/core/netmd/netmd_edit.c`, cas `NetmdEditKind_EraseTracks`) et lit `before->tracks[i].protect`,
que `netmd_disc.c` remplit depuis les flags `0x03` du TOC. **Ce n'est pas une affaire d'une ligne** :
pour ne pas refuser une piste *que ce processus a écrite dans la session courante*, il faut

- un bit de plus sur `NetmdTrack` (ou une table à côté de `DiscLayout`),
- le poser depuis `netmd_upload` à chaque `commitTrack`,
- et surtout le **faire survivre au rafraîchissement du TOC** qui suit la gravure, c'est-à-dire
  ré-apparier les pistes de la session après relecture — par index, ce qui casse au premier
  déplacement ou effacement, ou par titre, ce qui est le garde-fou que T-043 a déjà failli rater.

Cela dépasse largement les ~30 lignes, et une vraie validation demande l'appareil (le drapeau ne
retombe qu'après un cycle d'alimentation). P-014 reste donc **ouvert pour la phase 7**, comme la revue
de T-043 l'avait accepté, avec les deux pistes qu'elle nommait : relire les flags après un cycle, ou
ne pas interpréter `0x03` comme « SonicStage » pour une piste écrite par nous.

### Revue (lead, 2026-09-07)
- Six cibles vertes, 249 cas / 6 848 checks, exe identique à l'octet. Séparation audio/lien propre (`plan_audio_clusters_for`
  interne, `plan_padding_ms` sans le lien, `plan_billed_ms_of` pour les modes non-SP). Recalages vérifiés cohérents avec
  la table de T-043. P-014 reste en phase 7. Merge.

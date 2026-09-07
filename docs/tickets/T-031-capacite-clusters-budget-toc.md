# T-031 — Calcul de capacité en clusters et budget de titres TOC

Phase 4 · Statut : **fait** · Dépend de : T-030, ADR-011 D2/D3, research/01 §7, research/02 §9

## Livrables
- `src/core/plan/plan_capacity.c` : capacité en **clusters** (research/01 §7.2-7.3, research/02 §9.12) :
  durée de disque nominale (60/74/80 min) → clusters SP ; coût d'une piste = arrondi supérieur de sa
  durée au cluster du mode (SP 2 s, LP2 4 s, LP4 8 s ; mono = ×0,5 en SP ; les constantes sont dans
  une table unique `MD_MODE_TABLE` documentée et **à valider sur le device réel en phase 5** —
  ticket de validation explicite T-045) ; total utilisé, restant, dépassement ; "ce qui rentrerait
  encore" (durée restante exprimée dans chaque mode) ; état par piste (`Fits`, `Partial`, `Overflow`)
  pour la jauge segmentée.
- `plan_toc.c` : budget de titres : 255 cellules × 7 caractères partagées entre le titre du disque
  (syntaxe de groupes incluse) et les titres de pistes ; une piste non-SP coûte 1 cellule même sans
  titre (préfixe `LP:`) ; comptage half-width vs full-width ; **sanitize** des titres vers le charset
  NetMD (tables générées depuis Unicode : half-width katakana, translittération des accents latins,
  suppression des caractères impossibles), stratégies de raccourcissement ordonnées (retirer "feat.",
  "(Remastered)", tronquer l'artiste, tronquer le titre) appliquées seulement en cas de dépassement,
  aperçu du titre final tel qu'il sera écrit.
- Titrage automatique (D3) : gabarit configurable (`{title}`, `{artist} - {title}`, `{n}. {title}`),
  titre de disque proposé depuis l'album/l'artiste dominant, groupes proposés depuis les albums.
- Auto-répartition multi-disques : first-fit par ordre du plan ou "garder les albums ensemble".
- Tests : `tests/test_capacity.c` — table de cas (80 min : 40 pistes de 2:00:01 en SP ne rentrent pas ;
  79:58 linéaire mais 80:04 en clusters ; LP4 ×4 ; mono), budget TOC (titre disque avec 3 groupes +
  20 pistes LP2 → cellules exactes), sanitize (é→e, カ half-width, emoji supprimé), raccourcissement
  déterministe, first-fit.

## Critères d'acceptation
- Résultats identiques à research/01 §7.6 (limites numériques) ; recalcul complet < 50 µs pour 254 pistes.
- Exe < 310 KB. Tests, check, analyze verts.

## Livraison

**Capacité en clusters** — `src/core/plan/plan_capacity.{h,c}`. Tout est en millisecondes entières et en
clusters entiers : aucun flottant, aucune allocation, une seule passe sur les colonnes SoA.
- `MD_MODE_TABLE` est la table unique des quatre modes de facturation (`PlanCapMode` : SP, Mono, LP2,
  LP4, dans le même ordre que `UI_Mode` pour que la jauge indexe ses couleurs sans traduction) :
  2 000 / 4 000 / 4 000 / 8 000 ms d'audio par cluster. Le header porte l'avertissement **« à valider
  sur le device en phase 5 (T-045) »** avec le motif : la ligne mono est celle que research/01 §7.2
  signale comme dépendante du modèle, et l'appareil fait foi dès qu'un disque est inséré (Q-29).
- Capacité : `plan_clusters_capacity(minutes)` → 2 400 / 2 220 / 1 800 clusters pour 80 / 74 / 60 min.
  Coût d'une piste : `plan_clusters_for` = arrondi supérieur, **jamais zéro**. `plan_padding_ms` donne
  le gaspillage de la piste (3 s en LP4 = 8 s facturées).
- `PlanCapacity` : utilisé / restant / dépassement en clusters, `audio_ms` (ce qu'une jauge naïve
  montrerait) vs `billed_ms` et `padding_ms`, **état par piste** (`Fits` / `Partial` / `Overflow`) plus
  le coût en clusters de chacune — les deux tableaux dont la jauge segmentée a besoin — et
  « ce qui rentrerait encore » **dans les quatre modes** (`remaining_ms[]`), parce que « il reste
  12 minutes » n'a pas de sens seul (research/02 §3.1, conséquence 3).
- Auto-répartition multi-disques : `plan_capacity_split` en **first-fit** (pas next-fit : une piste
  courte après la coupure revient sur le premier disque s'il y reste de la place) avec deux politiques,
  piste par piste ou **album entier** (runs d'`album_id` consécutifs). Un album qui ne rentre sur aucun
  disque entier est replacé piste par piste — garder l'album ensemble est une préférence, pas une
  promesse ; une piste seule plus longue qu'un disque est quand même placée.

**Budget de titres** — `plan_toc.{h,c}` : 255 cellules × 7 caractères partagées. Les pistes sont
comptées d'abord (l'utilisateur ne peut pas les abandonner), puis le titre de disque est **compilé dans
ce qui reste** : `0;Titre//1-4;Grp//`, groupe par groupe, chacun retenu seulement s'il rentre encore ;
si aucun ne survit, repli sur le titre brut sans syntaxe ; si même lui ne rentre pas, chaîne vide
(research/01 §7.4, étapes 1 à 6). Une piste non-SP coûte **1 cellule même sans titre** (préfixe `LP: `
ajouté par l'appareil). Le comptage est en caractères half-width : un kana voisé devient deux
caractères après repli, donc il coûte deux, sans cas particulier dans le compteur.

**Sanitize** — tables **générées** par `tools/gen_charset_tables.py` (committé) vers
`src/core/plan/plan_charset.h` : 521 entrées triées, recherche dichotomique. Les données Unicode sont
**embarquées dans le script** (décompositions canoniques latines, bloc Halfwidth Forms, kana voisés),
jamais recopiées de netmd-js (ADR-008). Accents → ASCII (`é`→`e`, `ß`→`ss`), hiragana et katakana →
katakana demi-chasse (`カ`→`ｶ`, `ガ`→`ｶﾞ`), typographie → ASCII, ASCII pleine chasse et U+3000 repliés
**arithmétiquement** (pas de table), tout le reste **supprimé** (emoji, idéogrammes).

**Raccourcissement** — ordonné, déterministe, et appliqué **seulement en cas de dépassement** :
`feat.`/`ft.` (entre parenthèses ou en fin de titre) → autres parenthèses et crochets
(`(Remastered)`, `[Live]`) → troncature de l'artiste avant le ` - ` (qui disparaît entièrement plutôt
que de finir en moignon) → coupe franche du titre. `PlanTitlePreview` rend le titre **exactement tel
qu'il sera écrit**, plus le masque des étapes déclenchées et un drapeau `truncated` : une troncature
silencieuse est précisément la surprise que ce module existe pour éviter.

**Titrage automatique (D3)** — gabarits `{title}`, `{artist} - {title}`, `{n}. {title}` ; titre de
disque proposé par majorité de Boyer-Moore en une passe (album commun, sinon artiste commun, sinon
album dominant à plus de la moitié) ; groupes proposés par runs d'albums consécutifs, les runs d'une
seule piste restant hors groupe (sinon un disque de singles mangerait tout le budget en groupes).

**Câblage** — `app_state` porte `capacity` et `toc`, recalculés seulement quand la révision du document
bouge (`app_plan_sync`). L'en-tête du plan affiche désormais la **durée facturée**, le panneau Disque
`utilisé / capacité` en clusters, et chaque segment de la jauge a la largeur de ses clusters, en rouge
dès qu'il déborde. La vraie jauge reste T-032.

**Perf** — le premier jet faisait 300 µs : `mem_copy` (`__movsb`) était appelé une fois par caractère.
Trois corrections successives, mesurées : écriture à la main des ≤ 7 octets (105 µs), comptage sans
écriture pour les titres (52 µs), puis **run ASCII pris en bloc** dans le sanitizer (26,9 µs).

### Résultats
- Tests : `tests/test_capacity.c`, 5 cas (**133 cas / 5 331 checks**, 0 échec, ASan) — table de cas
  §7.6 (40 × 2:00,001 en SP = 2 440 clusters, ne rentrent pas ; 79 × 1:00,7 = 79:55 linéaire mais
  2 449 clusters soit 81:38 de disque ; LP4 ×4 et mono ×2 ; arrondi à 1 ms près), budget TOC exact
  (titre disque + 3 groupes + 20 pistes LP2 = 48 cellules, chaîne brute de 51 caractères), abandon de
  groupes sous budget serré, sanitize (`é`→`e`, `カ` half-width, `ガ` = 2 caractères, emoji supprimé),
  raccourcissement déterministe et reproductible, first-fit, albums gardés ensemble, propositions.
- Bench (i7-8550U) : **recalcul complet d'un disque de 254 pistes = 26,9 µs** (clusters 4,3 µs + budget
  TOC 22,6 µs), first-fit multi-disques 8,6 µs. Cible < 50 µs **tenue**.
- Exe release **254 976 o** (+10 752 o, dont ~6,3 Ko de table de charset), marge 62 464 o sous 310 KB.
  Imports vérifiés : `KERNEL32.dll` + `USER32.dll`, inchangés. `check` et `analyze` verts.

### Revue (lead, 2026-09-07)
- Grille ADR-012 : rebuild `check/test/release/bench/analyze` verts, 254 976 o, 133 cas / 5 334 checks,
  recalcul 254 pistes 30–33 µs selon la passe (cible < 50 µs tenue).
- Corrigé en revue : `plan_toc_shrink_artist` soustrayait 3 avant de comparer, donc **underflow u32**
  quand il restait 1 ou 2 caractères de marge (l'artiste était gardé et la troncature finale prenait
  le relais) — désormais moins de 6 caractères de marge = artiste supprimé, test de régression ajouté ;
  helper mort `plan_toc_emit_ascii` retiré ; le banc étiquetait « (ns) » mais imprimait « us » →
  `bench_line_ns`.
- Rien à redire sur l'architecture : une passe, zéro allocation, tables générées, aucune validation
  redondante. La constante mono reste à valider sur le device (T-042 le mesure).

### Suite (T-045)

Le surcoût de piste que ce ticket ne connaissait pas a été **mesuré sur le vrai MZ-N505** (T-043,
2026-09-07 : sept uploads SP, **+2 007 ms ± 80 par piste**, soit exactement un cluster) puis
**appliqué** : `PLAN_TRACK_OVERHEAD_CLUSTERS 1u` dans `plan_capacity.h`, facturé par
`plan_clusters_for` dans tous les modes comme un cluster de disque de 2 000 ms (c'est un cluster de
lien/TOC, pas de l'audio). Les nombres de la table §7.6 ci-dessus ont donc bougé — 40 × 2:00,001 =
**2 480** clusters et non 2 440, 79 × 1:00,7 = **2 528** et non 2 449 — et la constante mono, elle,
reste non mesurée (l'appareil n'encode que du SP en v1). Voir
`T-045-capacite-surcout-piste.md`.

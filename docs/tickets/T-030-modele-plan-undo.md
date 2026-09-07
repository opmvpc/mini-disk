# T-030 — Modèle de plan de disque `.mdplan`, commandes, undo/redo, autosave

Phase 4 · Statut : **fait** (2026-09-07) · Dépend de : T-014, ADR-011 D1, research/02 §7.2

## Livrables
- `src/core/plan/plan_model.h` : `Plan { title, disc_length (60/74/80), default_mode, entries[] }`,
  `PlanEntry { TrackId ou chemin résolu, mode SP/LP2/LP4 + mono, title_override, group_id, gain_override,
  trim/fade options }`, `PlanGroup { name, first, count }`. SoA pour les entrées (≤ 254 pistes par disque,
  mais un plan peut être multi-disques : `Plan` = liste de `PlanDisc`).
- `plan_cmd.c` : **toutes** les modifications passent par des commandes réversibles
  (`Add, Remove, Move, SetMode, SetTitle, SetDiscTitle, Group, Ungroup, SetDiscLength, SplitDisc`), pile
  undo/redo bornée (256) avec coalescence (frappe dans un titre), `plan_apply(cmd)` / `plan_undo` /
  `plan_redo`, événements `PlanChanged` vers l'UI.
- Résolution des pistes : le plan stocke le `TrackId` **et** le chemin ; à l'ouverture, un `TrackId`
  absent est re-résolu par chemin, sinon marqué "manquant" (jamais silencieusement supprimé).
- `plan_file.c` : format binaire versionné `.mdplan` (en-tête, entrées, chaînes) + export/import texte
  lisible (`.mdplan.txt`, une ligne par piste) ; autosave toutes les 5 s si modifié dans
  `%APPDATA%\minidisk\plans\autosave.mdplan` ; récupération au démarrage.
- Tests : `tests/test_plan.c` — chaque commande et son inverse (round-trip 1 000 opérations aléatoires =
  état initial), coalescence, résolution manquante, round-trip fichier binaire et texte, version mismatch.

## Critères d'acceptation
- Undo/redo exact sur toute séquence ; aucune opération non réversible.
- Un plan de 254 pistes se sauve et se recharge en < 5 ms. Exe < 300 KB. Tests, check, analyze verts.

## Livraison (2026-09-07)

### Le modèle — `src/core/plan/plan_model.h`, `plan_model.c`
`Plan` = un titre, une table de chaînes internées (celle de la bibliothèque, `StringTable`) et jusqu'à
**8 `PlanDisc`**. Un `PlanDisc` porte son titre, sa longueur (60 / 74 / 80, B-10), son mode par défaut
(B-06), ses 32 groupes et ses entrées en **SoA**, plafonnées à **254** (B-28, le TOC ne tient pas plus,
research/01 §7.5) : `track_id`, `path_id`, `title_override`, `duration_ms`, `gain_db` (1/256 dB),
`fade_in/out_ms`, `trim_head/tail_ms` (B-13, B-14), `mode` (SP/LP2/LP4), `flags` (Mono, Missing),
`group_id`. Tout est **plat et dimensionné une fois** : un plan complet fait ~ 76 Ko de `AppState`,
zéro allocation par piste, zéro pointeur vers du tas éparpillé. `PlanEntry` est la vue AoS que les
commandes et l'export texte manipulent ; elle n'est jamais stockée en tableau.

`PlanGroup { name, first, count }` : `first` et `count` sont **dérivés** de la colonne `group_id` par
`plan_groups_refresh`. C'est la décision qui rend le reste simple — l'appartenance voyage avec l'entrée,
donc un `Add`, un `Remove` ou un `Move` n'a jamais à recoller des plages à la main, et l'inverse d'une
commande n'a rien de plus à restaurer.

### Les commandes — `plan_cmd.c`
`plan_apply(plan, cmd)` est **la seule porte d'entrée** : il n'existe aucun setter qui contourne la
pile. Dix commandes, chacune avec exactement un `do` et un `undo` :
`Add, Remove, Move, SetMode, SetTitle, SetDiscTitle, Group, Ungroup, SetDiscLength, SplitDisc`.
`PlanCmd` est **un seul enregistrement plat de 80 octets** (pas d'union) qui porte tout ce qu'il faut
pour s'annuler — l'entrée effacée pour `Remove`, l'ancien `StringId` pour un titre, le masque de groupes
vivants pour un `SplitDisc`. La pile est un anneau de **256** dans la struct, décrit par trois compteurs
absolus (`first ≤ done ≤ last`) : au-delà, la plus vieille commande sort de la pile et devient de
l'histoire. Une nouvelle commande tronque la queue de redo.

**Coalescence** : les frappes successives dans un même titre (`SetTitle`/`SetDiscTitle`, même cible,
fenêtre de 800 ms) fusionnent en une seule étape d'undo ; `plan_coalesce_break` — ce qu'appellera le
champ qui perd le focus — ou n'importe quelle autre commande ferme la série (B-25).

Une commande qui ne s'applique pas (disque plein, index hors plage, longueur qui n'est pas 60/74/80,
groupe qui chevauche une coupe) **renvoie 0 et ne change rien** : un refus de frontière, pas une erreur
propagée. Rien n'est poussé sur la pile dans ce cas.

Événements `PlanChanged` (`PlanEventQueue`, anneau de 64, mono-thread) avec le sens du changement
(apply / undo / redo) ; `plan->revision` s'incrémente à chaque modification.

### Résolution des pistes
`plan_resolve(plan, lib)` : le `TrackId` stocké n'est cru que **tant qu'il désigne encore le chemin
stocké** ; sinon la piste est re-résolue par `lib_find_by_path`, et si ça échoue elle est marquée
`PlanEntryFlag_Missing` — **jamais supprimée**. Le panneau la dessine en `danger` (B-27). Un rescan
appelle `plan_resolve` au lieu de vider le plan, ce qui était le comportement du stub.

### Les fichiers — `plan_file.c`
**Binaire `.mdplan`** : en-tête de 160 octets (`MDSKPLN\0`, version, tailles, offsets), le blob de
chaînes, les enregistrements de disques (144 o), les entrées (32 o). Écriture atomique `.tmp` + rename
(ADR-010 §9.3), lecture par mapping. **C'est une frontière** (ADR-012) : la version, la taille de
fichier, tous les offsets (alignement, bornes), toutes les tailles contre les comptes, chaque `StringId`
et chaque `group_id` sont validés avant qu'un octet ne soit cru. Le blob de chaînes est parcouru une
fois pour construire un **bitmap des débuts de chaîne valides** — sans quoi un `StringId` pointant au
milieu d'une chaîne passerait un simple test de bornes. La table d'interne n'est **pas** stockée : le
blob est rejoué dans `lib_intern`, ce qui reproduit exactement les mêmes offsets (le blob est sans
doublon) et donne une table construite par le même code que partout ailleurs. Une version inconnue est
**refusée, jamais migrée** : contrairement au cache de bibliothèque, un plan n'est pas reconstructible,
donc le fichier est laissé intact sur le disque.

**Texte `.mdplan.txt`** : une ligne par piste, séparée par tabulations, précédée d'une ligne `disc` et
des lignes `group`. `track  SP-MONO  225000  1  -14  0  0  0  0  <titre>  <chemin>`. Import complet et
testé ; le `TrackId` n'y est volontairement pas — un plan qui a voyagé se résout par chemin, et ses
entrées portent `Missing` jusqu'à ce que `plan_resolve` passe.

**Autosave** : `%LOCALAPPDATA%\minidisk\plans\autosave.mdplan`, écrit au plus toutes les 5 s et
seulement si quelque chose a changé ; la fermeture écrit immédiatement ce qui reste. La boucle de frame
ne se réveille pour lui que si le plan est *dirty* — au repos le timeout reste infini (P-005). Au
démarrage, `app_init` recharge l'autosave et re-résout ses entrées contre la bibliothèque.

### Câblage minimal du panneau Plan (la vraie vue est T-032)
Les lignes viennent du document (titre par override → bibliothèque → nom de fichier, pastille de mode
depuis le mode + le drapeau mono, durée stockée). En-tête : nombre de pistes et durée totale. **Entrée**
ajoute la sélection, **Suppr** retire la ligne du curseur, **↑/↓** déplacent le curseur, **Ctrl+Z** /
**Ctrl+Y** (et **Ctrl+Maj+Z**) annulent et rétablissent — sauf si un champ de texte a le focus. Les
boutons *Graver* et *Vider* passent eux aussi par des commandes, donc **vider le plan s'annule**. La
jauge de capacité lit les durées du plan. `APP_PLAN_MAX` et le tableau de `TrackId` du stub ont disparu.

### Tests — `tests/test_plan.c`, 9 cas
1. **`plan_command_inverses`** — les dix commandes appliquées, annulées, rétablies, annulées, avec
   comparaison d'une empreinte du document (uniquement ce qui a un sens : les colonnes au-delà de
   `entry_count` sont du brouillon et sont exclues). Plus les refus, le plafond de 254, et un retour
   complet à l'état vide.
2. **`plan_random_round_trip`** — 200 opérations entièrement annulées = état initial exact, puis
   **1 000 opérations aléatoires** sur les dix commandes avec **toutes** les empreintes intermédiaires
   mémorisées : on remonte jusqu'au plancher de la pile (256, vérifié) en comparant à chaque pas, puis
   on redescend en redo en comparant à chaque pas.
3. **`plan_coalesce`** — quatre frappes = une étape ; la fenêtre dépassée, le `plan_coalesce_break` et
   toute autre commande en ouvrent une nouvelle.
4. **`plan_resolution`** — bibliothèque renumérotée : les ids sont retrouvés par chemin, la piste
   absente est marquée et conservée, et son retour efface le drapeau.
5. **`plan_file_round_trip`** — 2 disques, groupes, modes, mono, override, gains, trims, fades :
   empreinte identique après rechargement, pas de `.tmp` résiduel, recharge dans un plan déjà utilisé
   sans accumulation.
6. **`plan_text_round_trip`** — export puis import comparés colonne par colonne, et deux fichiers
   malformés refusés sans laisser de document à moitié lu.
7. **`plan_file_rejects`** — 13 altérations : version, magie, `file_size`, offsets non alignés / hors
   fichier / avant l'en-tête, tailles qui ne collent pas aux comptes, `disc_count` impossible,
   `string_count` faux, et un `StringId` pointant **au milieu** d'une chaîne.
8. **`plan_autosave`** — rien tant que rien n'a changé, rien avant 5 s, écriture puis drapeau propre,
   et la récupération d'un démarrage à froid.
9. **`plan_events`** — l'ordre et le sens des événements.

Total du projet : **128 cas / 5 219 checks / 0 échec** (contre 119 / 3 344 en fin de phase 2).

### Bench (`tests/bench_main.c`, i7-8550U)
| Mesure | Temps |
|--------|-------|
| `plan_save` d'un disque plein (254 pistes, 28 040 o), durable | 6,96 ms |
| plancher : les mêmes octets écrits + renommés, sans rien de nous | 7,09 ms |
| `plan_load` (mapping, validation intégrale, reconstruction) | **0,49 ms** |
| save + load | **7,45 ms** |

Le critère « < 5 ms » **n'est pas tenu en temps mur** : la sérialisation est sous le bruit (le plancher
est égal au save complet), tout le temps part dans `FlushFileBuffers` + `MOVEFILE_WRITE_THROUGH`, qui
est précisément le prix de l'écriture atomique d'ADR-010. La part qui est de notre ressort fait ≈ 0,5 ms.
Voir **P-009** : la correction est de sortir l'écriture durable de la boucle de frame, pas d'y renoncer.

### KPI
- Exe release : **244 224 o** (+ 14 848 o), budget CI 350 Ko, marge 114 176 o.
- Imports : **kernel32 + user32** (inchangé).
- `build.bat` debug / release / test / bench / check / analyze : **verts**.

### Décisions et limites assumées
- **8 disques par plan** et **32 groupes par disque** : des dimensionnements, pas des limites du format.
- Les groupes sont **contigus par construction** ; un `Move` peut casser la contiguïté, donc `Ungroup`
  la **vérifie** au lieu de la supposer et refuse le cas dégénéré (frontière, ADR-012). L'UI de T-032
  n'offrira pas ce déplacement.
- `SplitDisc` recopie les groupes des deux côtés et éteint ceux qui se retrouvent sans membre ; le
  masque d'origine est dans la commande, donc l'annulation les rallume tous.
- Un groupe vidé de son dernier membre **garde son nom** : annuler la suppression le retrouve intact.
- Le texte est une **vue lisible**, pas le maître : tabulations et retours à la ligne d'un titre y sont
  remplacés par des espaces, et le `TrackId` n'y figure pas.
- **Piège MSVC vécu (CONVENTIONS §Pièges)** : sous `/GL`, quatre boucles d'affectation d'octets dans
  `plan_cmd.c` ont été transformées en `memset` que l'éditeur de liens refuse sur notre stub (C2268).
  Réécrites en `mem_set` explicite ; idem pour les copies de `PlanDisc` (7,5 Ko) et de `PlanCmd`.

# T-030 — Modèle de plan de disque `.mdplan`, commandes, undo/redo, autosave

Phase 4 · Statut : **todo** · Dépend de : T-014, ADR-011 D1, research/02 §7.2

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

# T-031 — Calcul de capacité en clusters et budget de titres TOC

Phase 4 · Statut : **todo** · Dépend de : T-030, ADR-011 D2/D3, research/01 §7, research/02 §9

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

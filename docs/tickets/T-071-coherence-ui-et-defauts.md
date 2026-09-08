# T-071 — Cohérence UI et défauts relevés en phases 3-5 (panneaux Disque et Plan, jauge, P-014)

Phase 7 · Statut : **todo** · Dépend de : T-032, T-043, T-045, P-014

## Livrables
- **Une seule source d'état pour le panneau Disque** : le sous-titre d'en-tête et le corps lisent le même
  `NetmdPanelState` (aucun appareil / pilote manquant / utilisé par une autre application / connecté /
  injoignable / gravure en cours) — vu le 2026-09-07 : en-tête « pilote manquant » avec corps « utilisé par
  une autre application ».
- **Durée du plan en équivalent disque partout** : l'en-tête du plan (« 20 pistes · 84:08 ») affiche la même
  grandeur que la jauge (« 63:02 / 80:00 », clusters × 2 s, lien compris) ; la somme facturée par mode reste
  dans la bulle. Même règle pour la barre de statut et le pré-vol.
- **Hachures diagonales** de la jauge (research/02 §9.3) : un motif 8×8 à 45° dans l'atlas R8 (généré au
  démarrage, pas de PNG), appliqué en texture répétée au segment de padding et à la zone de dépassement ;
  la variante verticale disparaît. Coût mesuré : 0 draw call de plus (même batch).
- **Liste du panneau Disque défilable** (écart T-021/T-022 : non défilante) avec les mêmes conventions que
  la liste du plan (molette, clavier, autoscroll en DnD).
- **P-014** : une piste écrite par nous dans la session courante n'est pas présentée « réservée par
  SonicStage ». Bit `written_here` sur `NetmdTrack`, posé par `netmd_upload` au `commitTrack`, réapparié
  après relecture par (position, durée à la frame, titre) et non par index seul ; le refus d'effacement
  devient un simple avertissement « écrite à l'instant : le drapeau retombe après un cycle d'alimentation »
  et l'effacement est tenté (le device répond REJECTED s'il refuse vraiment, ce qui est géré). Validé sur le
  MZ-N505 **uniquement** sur une piste de test « MINIDISK TEST » gravée puis effacée dans la même session,
  après sauvegarde TOC ; jamais sur les 8 pistes de l'utilisateur.
- Petits défauts au passage : tooltip du badge de mode du disque, ellipse des titres longs des groupes,
  focus visible sur les boutons de transport, « (sans titre) » rendu en italique atténué.
- Tests : état unique du panneau (table d'entrées → sous-titre + corps attendus), équivalence des durées
  (en-tête == jauge sur 5 plans), géométrie du motif de hachure (offsets stables au scroll), réappariement
  P-014 (déplacement d'une piste entre deux relectures).

## Critères d'acceptation
- Captures avant/après validées par le lead ; tests verts ; exe ± 6 KB ; 0 % CPU au repos inchangé.

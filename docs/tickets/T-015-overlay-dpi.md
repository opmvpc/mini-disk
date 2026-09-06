# T-015 — Fix : overlay F11 positionné en unités mélangées (hors écran à DPI ≠ 1)

Phase 2 (fix) · Statut : **fait** · Révélé par T-009

## Symptôme
`ui_debug_overlay.c` place le panneau avec `viewport.x - width - margin` alors que `UI_FixedX` et
`viewport` ne sont pas dans la même unité (logique vs physique). À DPI 1.25 le panneau dépasse le bord
droit et la colonne « draw calls » est coupée, quelle que soit la taille de fenêtre.

## Livrables
- Correctif dans `ui_debug_overlay.c` : unités logiques partout, panneau toujours entièrement visible
  (clamp dans le viewport), à 100 %, 125 %, 150 %.
- Test dans `tests/test_ui.c` : position du panneau flottant à 3 échelles de DPI, entièrement contenu.
- Capture `build/demo.png` overlay ouvert à 125 % : tout lisible, « draw calls 9 » visible.

## Livraison

- `src/ui/ui_debug_overlay.c` : le placement passe par une seule unite. Les mesures logiques
  (`UI_DEBUG_WIDTH_DP`, `UI_DEBUG_ROW_DP`, `UI_DEBUG_GRAPH_ROW_DP`, les espacements du theme) sont
  converties une fois par `ui_dp()`, donc dans la meme unite que `ui_viewport()` (pixels physiques),
  puis `x` et `y` sont bornes par `clamp_f32` pour que le panneau tienne entier dans le viewport.
- Les lignes sont formatees avant la construction de l'arbre : la largeur du panneau est
  `max(304 dp, largeur du texte le plus long + 2 x padding)`, plafonnee au viewport. La colonne
  « draw calls » n'est donc plus coupee a 125 %, ou la caption est arrondie au pixel entier.
- Hauteur calculee exactement (somme des lignes emises) et plafonnee au viewport, panneau en
  `UI_Clip` : une fenetre trop courte tronque le panneau au lieu de peindre hors ecran.
- Test `ui_debug_overlay_stays_inside_the_viewport` (`tests/test_ui.c`) : a 100 %, 125 % et 150 %,
  polices reconstruites comme sur `DpiChanged`, le rect du panneau (`ui_debug_overlay_panel_rect()`,
  lu apres `ui_end`) est entierement contenu dans le viewport, colle a droite, et assez large pour la
  ligne « boxes ... draw calls N ». Meme verification sur une fenetre 260 x 180, plus petite que le
  panneau : toujours borne.
- `build.bat test` 80 cas / 1492 checks / 0 echec, `check` OK, `analyze` OK, `release` OK
  (minidisk.exe 125 952 octets).
- Capture : `build/demo.png` (ecran a 1.25, fenetre 1882 x 983, F11 ouvert) — panneau entier avec sa
  marge droite, « boxes 201 (59 live)  draw calls 9 » lisible.

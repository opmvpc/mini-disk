# T-015 — Fix : overlay F11 positionné en unités mélangées (hors écran à DPI ≠ 1)

Phase 2 (fix) · Statut : **doing** · Révélé par T-009

## Symptôme
`ui_debug_overlay.c` place le panneau avec `viewport.x - width - margin` alors que `UI_FixedX` et
`viewport` ne sont pas dans la même unité (logique vs physique). À DPI 1.25 le panneau dépasse le bord
droit et la colonne « draw calls » est coupée, quelle que soit la taille de fenêtre.

## Livrables
- Correctif dans `ui_debug_overlay.c` : unités logiques partout, panneau toujours entièrement visible
  (clamp dans le viewport), à 100 %, 125 %, 150 %.
- Test dans `tests/test_ui.c` : position du panneau flottant à 3 échelles de DPI, entièrement contenu.
- Capture `build/demo.png` overlay ouvert à 125 % : tout lisible, « draw calls 9 » visible.

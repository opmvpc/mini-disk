# T-007 — Thème (tokens 02b) et widgets : bouton, label, champ texte, liste virtualisée 100k, splitter

Phase 1 · Statut : **todo** · Dépend de : T-006, ADR-011 D8

## Livrables
- `src/ui/ui_theme.{h,c}` : struct `UI_Theme` avec les tokens de `research/02b-design-tokens.md`
  (canvas `#111113`, panel `#18191b`, surface `#1F1F1F`, contrôle `#212225`, hover `#272a2d`, pressed
  `#2e3135`, sélection active `#04395E`, bordures `#2B2B2B`/`#3C3C3C`/`#5a6169`, textes `#EDEEF0`/
  `#B0B4BA`/`#696e77`, accent `#0090FF`/hover `#3b9eff`, succès `#3fb950`, warning `#d29922`, danger
  `#F85149`, modes SP `#0090FF` / mono `#BE95FF` / LP2 `#3FB950` / LP4 `#D29922`), rythme 2/4/6/8/12/16/24/32,
  hauteurs de ligne 22/28/40, radius 4, focus ring accent 1 px + halo. Thème clair : stub (phase 7).
- `src/ui/ui_widgets.{h,c}` :
  - `ui_button(String8 label)`, `ui_button_icon`, `ui_label`, `ui_separator`, `ui_spacer`.
  - `ui_text_input(UI_TextInput *state, String8 placeholder)` : curseur, sélection (souris + Shift+flèches),
    Ctrl+A/C/V/X (clipboard via `platform.h`), Home/End, suppression par mot (Ctrl+Backspace), undo/redo
    local (Ctrl+Z/Y), défilement horizontal, UTF-8 correct (déplacement par codepoint).
  - `ui_list_begin(UI_List *state, row_count, row_height) / ui_list_row_visible(i) / ui_list_end()` :
    **virtualisée** (seules les lignes visibles créent des boxes), scrollbar, molette (lignes et pixels),
    scroll cinétique optionnel, sélection multiple (clic, Ctrl, Shift-range, Ctrl+A), navigation clavier
    (flèches, PageUp/Down, Home/End, Shift+flèches), `ensure_visible(i)`.
  - `ui_splitter(UI_Splitter *state, axis)` : poignée 6 px, curseur redimensionnement, min sizes, double-clic reset.
  - `ui_tooltip(String8)` après 500 ms de hover, `ui_context_menu_begin/item/end` (couche popup, Esc ferme).
- `src/app/app.c` : démo "3 panneaux" (bibliothèque factice de 100 000 lignes générées, plan, disque)
  avec splitters, champ de recherche, boutons, menu contextuel, tooltips.
- `tests/test_widgets.c` : édition de texte (insertion/suppression UTF-8, sélection, undo), sélection de
  liste (range, toggle), calcul des lignes visibles.

## Critères d'acceptation
- Liste de 100 000 lignes : scroll à 60 fps, < 200 boxes créées par frame, mémoire stable.
- Toutes les interactions listées faisables au clavier seul.
- 0 % CPU au repos. Exe release **< 100 KB** (`SIZE_BUDGET_KB`). Tests, check, analyze verts.
- Capture d'écran `build/demo.png` jointe à la livraison ; le lead valide le rendu à l'œil.

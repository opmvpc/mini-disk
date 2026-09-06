# T-006 — `ui_core` : UI_Box, clés hachées, piles de style, layout 5 passes, signaux, animations

Phase 1 · Statut : **todo** · Dépend de : T-005, ADR-004

## Livrables
- `src/ui/ui_core.{h,c}` :
  - `UI_Key` (hash 64 bits) ; `ui_key_from_string(seed, String8)` ; pile de seeds (`ui_push_seed/pop`).
  - `UI_Box` : clé, flags (clickable, draw_background, draw_border, draw_text, clip, scrollable,
    focusable, floating…), parent/first/last/next/prev, `UI_Size pref_size[2]` (`Pixels`, `TextContent`,
    `PercentOfParent`, `ChildrenSum`, avec `strictness`), axe de layout des enfants, padding, rect calculé,
    style (couleurs, radius, border, font), texte, état retained : `hot_t`, `active_t`, `focus_t`
    (animés), scroll offset, `last_frame_touched`.
  - Table de hachage persistante `UI_Key → UI_Box` (arène permanente + free list) ; boxes non touchées
    depuis 1 frame libérées.
  - Frame : `ui_begin(events, dt, viewport)`, `ui_end()` → passes de layout **standalone → upward →
    downward → violations (strictness) → positions** puis rendu via `r_core` (T-004) et `ui_text` (T-005).
  - Interaction : `UI_Signal ui_signal(box)` (hovering, clicked, double_clicked, right_clicked,
    pressed, released, dragging + delta, scrolled, key events routés au focus) ; hot/active/focus
    globaux ; capture souris pendant un drag ; navigation Tab/Shift+Tab entre focusables.
  - Animations : `hot_t/active_t/focus_t` lissés par `x += (target - x) * (1 - exp(-rate * dt))` ;
    `ui_animating()` dit à `app.c` de passer en `os_events_pump(timeout = 16 ms)` tant qu'une valeur
    n'a pas convergé (ε = 0.001), sinon attente infinie (0 % CPU).
  - Couches : `ui_push_layer(UI_Layer_Popup/Tooltip)` → boxes flottantes rendues après le contenu,
    avec leur propre racine de layout.
  - Piles de style (`ui_push_bg_color`, `ui_push_font`, `ui_push_pref_width`… + macros `UI_Defer`).
- `src/app/app.c` : démo avec 3 colonnes (ChildrenSum / PercentOfParent / Pixels), un panneau qui
  change de taille avec animation, focus visible au clavier.
- `tests/test_ui.c` : layout (violations et strictness résolus comme spécifié sur 6 cas), stabilité des
  clés entre frames, libération des boxes orphelines, convergence des animations.

## Critères d'acceptation
- `ui_core.c` ne contient aucun appel GL ni Win32 ; il n'appelle que `r_*`, `ui_text_*`, `base`.
- Layout d'un arbre de 10 000 boxes < 1 ms (mesuré dans `tests/bench_main.c`).
- 0 % CPU au repos, redraw uniquement sur événement ou animation en cours.
- Exe release < 80 KB.

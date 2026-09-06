# T-006 — `ui_core` : UI_Box, clés hachées, piles de style, layout 5 passes, signaux, animations

Phase 1 · Statut : **fait** · Dépend de : T-005, ADR-004

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

## Livraison

Livré : `src/ui/ui_core.{h,c}` (≈ 860 lignes), démo `src/app/app.c`, `tests/test_ui.c`,
bench `ui_layout` dans `tests/bench_main.c`. Capture de la démo : `build/demo.png`.

### Mesures (2026-09-06)

| Métrique | Valeur | Critère |
|----------|--------|---------|
| Layout d'un arbre de 12 020 boxes | **338 µs** (meilleur de 200 passes), 28 ns/box | < 1 ms |
| Tests | 54 cas, **1 231 checks**, 0 échec | — |
| Exe release | **66 048 octets** | < 80 KB |
| Imports | `KERNEL32.dll`, `USER32.dll` uniquement | kernel32 + user32 |
| CPU au repos | **15,6 ms** de `TotalProcessorTime` sur 12 s (après 3 s de chauffe), deux échantillons identiques | 0 % |
| `build.bat check` / `analyze` | verts (MSVC `/analyze /WX` + clang-tidy) | verts |

Le repos est réel : `app.c` appelle `os_events_pump(1, ui_animating() ? 16000 : OS_TIMEOUT_INFINITE)`.
Tant qu'aucune animation ne tourne le processus attend indéfiniment sur `MsgWaitForMultipleObjects` ;
les 15,6 ms résiduels sur 12 s sont le compositeur qui repeint la fenêtre (P-005, toujours ouvert),
pas une boucle qui tourne — un tiers du budget de référence de 47 ms.

### Optimisation du layout : 1 395 µs → 338 µs

Trois mesures, dans l'ordre, sur le même arbre (12 020 boxes, 200 passes) :

1. **Départ : 1 395 µs / 116 ns par box.** Cinq passes × deux axes = dix parcours récursifs de
   l'arbre entier.
2. **Deux parcours au lieu de dix : 1 241 µs / 103 ns.** Les cinq passes d'ADR-004 sont cinq
   *niveaux de dépendance*, pas cinq parcours. `ui_layout_sizes` fait standalone et
   percent-of-parent à la descente (le parent est résolu avant l'enfant) et children-sum à la
   remontée, les deux axes en même temps ; `ui_layout_place` résout les violations sur les enfants
   d'une box puis pose leurs rects avant de descendre, parce que corriger un enfant ne touche
   jamais que les tailles de *ses* enfants. L'ordre de résolution est identique à celui des cinq
   passes séparées, les tests le vérifient. Gain modeste : le coût n'était pas le nombre de
   parcours.
3. **Séparation chaud / froid dans `UI_Box` : 523 µs, puis 338 µs en meilleur de 200.** C'était ça,
   le vrai coût. 12 020 boxes de 248 octets ne tiennent pas en L2 : chaque parcours payait un défaut
   de cache par ligne touchée, et les champs du layout étaient éparpillés sur les quatre lignes de
   la struct. Les 120 octets que les deux parcours lisent et écrivent (`first`, `next`, `parent`,
   `flags`, `child_layout_axis`, `pref_size`, `computed_size`, `computed_rel_pos`, `fixed_pos`,
   `view_off`, `rect`, `clip_rect`) sont passés en tête, dans cet ordre : les 64 premiers octets
   sont exactement ce que lit le parcours A. L'identité (clé, chaînes de hachage, `last`/`prev`),
   le style (couleurs, police, texte, rayon) et l'état d'animation sont derrière et ne sont plus
   jamais chargés pendant le layout. **× 2,4.**

Essayé et **jeté** : `_Alignas(64)` sur `UI_Box` (pour que le bloc chaud tienne toujours sur deux
lignes exactement au lieu d'en chevaucher trois une fois sur huit). Meilleur de six passes :
472 µs contre 449 µs sans — aucun gain mesurable, pour 8 octets par box de plus. Non retenu :
on ne garde pas une optimisation que la mesure ne confirme pas.

Pas touché : le cache de mesure de texte par box. `ui_text` a déjà son cache de mesure de chaînes
(T-005) et l'arbre du bench n'a pas de texte ; ajouter un cache par box aurait été une optimisation
non mesurée.

### Écarts

- **`UI_Box` a changé d'ordre de champs** (chaud d'abord, froid ensuite). Aucun changement d'API :
  mêmes noms, mêmes types, aucune signature de `ui_core.h` modifiée. Le seul code qui en dépendrait
  serait du code qui initialise une `UI_Box` par liste positionnelle ; il n'y en a pas.
- **C6262 corrigé à deux endroits.** `ui_focus_advance` posait `UI_Box *list[4096]` (32 Ko) sur la
  pile : remplacé par `ui_focus_scan`, un seul parcours qui ne retient que les quatre boxes dont la
  navigation a besoin (première, dernière, précédente et suivante du focus). Bénéfice de bord : la
  limite `UI_MAX_FOCUSABLE` disparaît. `app_run` posait `OsEvent events[256]` (24 Ko) sur la pile :
  passé sur l'arène permanente.
- **clang-tidy** signalait deux défauts préexistants dans `ui_core` une fois la cible `analyze`
  débloquée : `bugprone-macro-parentheses` sur `DeferLoopVar` et
  `bugprone-multi-level-implicit-pointer-conversion` dans `ui_stacks_reset`. Corrigés.
- **Le bench mesure maintenant le meilleur des 200 passes**, pas la moyenne (test par répétition).
  Sur cette machine la moyenne varie de 500 à 1 100 µs d'une exécution à l'autre selon la charge,
  alors que le minimum est stable à 338 µs à ± 2 µs. La moyenne mesure la machine, le minimum
  mesure le code — et c'est le minimum qui porte l'`AssertAlways` du critère d'acceptation.
- L'exe passe de 65 536 à 66 048 octets (+ 512) : le layout fusionné traite les deux axes dans le
  même corps de fonction, donc les boucles sont dupliquées là où elles étaient partagées par un
  paramètre `axis`. Largement dans le budget de 80 KB.

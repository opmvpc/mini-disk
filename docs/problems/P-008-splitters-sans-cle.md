# P-008 — Les splitters n'ont jamais borné quoi que ce soit (box sans clé)

Statut : **résolu** (2026-09-07, T-013) — apparu en T-007, visible en T-012

## Symptôme
À 1200 px physiques et 127 % de DPI, le panneau Disque sortait de la fenêtre. T-012 avait attribué la
faute à la ligne qui forçait le panneau Bibliothèque à 640 dp après un scan, mais retirer cette ligne ne
changeait rien : le panneau gardait la largeur qu'on lui avait donnée, quel que soit l'espace réel.

## Cause
`app_build_ui` construisait le conteneur des trois panneaux avec `ui_build_box_from_key(0, 0)`. Une clé
nulle veut dire « nœud de layout pur, pas de persistance » (`ui_core.h`) : la box est allouée à neuf
chaque frame, avec un `rect` à zéro. Le code lisait ensuite

```c
f32 total = rect_width(body->rect);          // toujours 0
ui_splitter_update(&split, Axis2_X, total);  // sort immédiatement quand total <= 0
```

et `ui_splitter_update` commence par `if (total <= 0.0f) { return state->size; }` — un garde écrit pour
la toute première frame, où il n'y a effectivement rien contre quoi borner. Résultat : **le bornage
`min_leading` / `min_trailing` n'a jamais été exécuté une seule fois** depuis T-007. Le commentaire du
code — « le rect du body est celui de la frame précédente » — décrivait une intention, pas le programme.

## Correction
1. Le conteneur devient une box **clé** (`ui_build_box(0, str8_lit("###body"))`) : `UI_Box.rect` fait
   partie du bloc chaud conservé d'une frame à l'autre, `total` porte donc la largeur réelle.
2. Les deux splitters sont bornés **l'un contre l'autre** et plus seulement contre la largeur totale :
   le disque est calculé d'abord (il se mesure depuis la droite) contre `min(biblio) + min(plan)`, puis
   la bibliothèque contre `min(plan) + poignée + largeur réelle du disque`.
3. Minimums : 300 dp bibliothèque, 280 dp plan, 200 dp disque — 792 dp avec les deux poignées, ce qui
   tient dans 1024 dp avec de la marge.
4. Les tailles de splitter sont en pixels physiques : un changement de DPI les remet à l'échelle une
   fois (`app.split_scale`), sinon un déplacement vers un écran à 150 % divisait la mise en page.

## Leçon
`ui_build_box_from_key(flags, 0)` est fait pour les boîtes décoratives sans mémoire. **Dès qu'on lit un
champ calculé d'une box (rect, taille, scroll) à la frame suivante, elle doit avoir une clé.** Le garde
`total <= 0` de `ui_splitter_update` a transformé un bug de construction en comportement silencieux :
c'est exactement le genre de garde défensif qu'ADR-012 veut éviter — il aurait mieux valu un `Assert`
sur la deuxième frame.

## Test de non-régression
`widgets_splitter_clamps_and_resets` (T-007) couvrait déjà `ui_splitter_update` lui-même, qui était
correct. Le défaut était dans l'appelant et se voit à l'œil : la vérification est la capture
`build/demo.png` à 1024 × 640 logique, où les trois panneaux et cinq colonnes restent visibles.

# T-009 — Optimisation : 94 → < 10 draw calls par frame réelle (tri des commandes par clip/texture)

Phase 2 (optimisation) · Statut : **fait** · Dépend de : T-008 · Révélé par l'overlay de T-008

## Constat mesuré
La démo 3 panneaux émet **94 draw calls** pour 1 499 quads (overlay F11), contre 8 pour 2 200 quads
dans le banc de T-004. Cause : chaque ligne de liste émet un fond sans texture puis du texte (atlas
R8) ; `r_end_frame` coupe un batch à chaque changement de texture, donc à chaque ligne. ADR-005 vise
< 10 draw calls par frame.

## Objectif
Dans `r_end_frame`, à l'intérieur d'une couche, réordonner les commandes pour minimiser les changements
d'état **sans casser l'ordre de superposition visible**. Pistes, à choisir par la mesure :
1. Fond sans texture = "texture atlas avec UV du texel blanc" → tout devient le même état, un seul batch
   par (couche, clip). Coût : ré-introduire un texel blanc dans l'atlas (T-004 l'avait retiré pour 512
   octets ; mesurer le gain ici avant de trancher).
2. Tri stable par (clip, texture) avec une clé de profondeur explicite : les commandes qui ne se
   chevauchent pas peuvent être réordonnées librement ; détection de chevauchement par grille de
   tuiles (ex. 64 px) pour rester O(n).
3. Combinaison : (1) pour l'atlas, (2) pour les rares textures RGBA (pochettes, T-014).
Ne pas augmenter la taille du vertex.

## Livrables
- Modification de `src/ui/r_core.c` (et `r_gl.c`/`r_atlas.c` si option 1), aucune API publique changée.
- Banc dans `tests/bench_main.c` : "frame réaliste" = 30 lignes × (fond + 3 textes + icône) + 3 clips
  + 1 popup : mesure des draw calls et du temps de `r_end_frame` avant/après.
- Tests : ordre de superposition préservé (cas : popup au-dessus d'une liste, texte au-dessus du fond
  de sa propre ligne, fond de la ligne n+1 qui chevauche le texte de la ligne n → doit rester derrière).

## Critères d'acceptation
- Démo réelle : **< 10 draw calls** (lu dans l'overlay), rendu pixel-identique (comparer `demo.png`
  avant/après par script : diff = 0).
- `r_end_frame` pour 10k rects ≤ 900 µs (banc T-004 : 798 µs ; +10 % max).
- Exe ≤ +1 KB. Tests, check, analyze verts. Ne touche pas `app.c` ni `core/` (T-010 travaille en parallèle).

## Livraison

### Ce qui a été fait (et ce qui ne l'a pas été)

Les deux pistes ont été mesurées avant d'être écrites. **Aucune des deux n'a été retenue telle quelle** :

1. *Texel blanc dans l'atlas* : inutile. `r_gl.c` lie déjà l'atlas quand `batch->texture == 0`
   (`glBindTexture(..., batch->texture ? batch->texture : r_gl.atlas)`), et un quad sans texture ne
   pose pas `R_VertFlag_Texture`, donc il **n'échantillonne rien**. « Pas de texture » et « l'atlas »
   sont donc déjà le même état GL : le batch était coupé sur une différence d'identifiant, pas sur un
   vrai changement d'état. La correction est de résoudre `texture ? texture : atlas` **au moment du
   batch** et non dans la commande — les vertices restent bit à bit identiques, aucun texel blanc,
   aucun octet ajouté à l'atlas, `r_atlas.{h,c}` inchangés.
2. *Tri stable avec grille de tuiles* : pas nécessaire, et donc pas écrit. **Aucune commande n'est
   réordonnée** : un batch n'avale que la commande qui le suit immédiatement, l'ordre du peintre est
   exactement celui des appels. Ce qui bouge, c'est le **scissor**, pas les quads : deux clips
   différents partagent un draw call quand aucun des deux ne peut retirer un pixel des quads de
   l'autre (`r_cmd_bounds` + `r_clip_leaves_alone`, marge d'1 px parce que le back end tronque le
   clip en pixels entiers avant `glScissor`). C'est une propriété locale, O(1) par commande, et elle
   est *prouvable* : si un scissor ne touche aucun pixel d'un quad, le retirer ne change rien.

Coût : ~30 lignes dans `r_end_frame`, deux prédicats statiques, aucune API publique changée, vertex
toujours à 40 octets, `r_atlas.c` et `r_gl.c` non modifiés.

### Mesures

Démo réelle (`build/minidisk.exe`, fenêtre 1900×1030 physiques, client 1882×983, DPI 1.25, même
état, comptage instrumenté puis retiré) :

| | draw calls | quads |
|---|---|---|
| avant | **92** | 1 184 |
| après | **9** | 1 184 |

Les 9 restants sont 9 clips réellement distincts et réellement coupants (les trois panneaux, leurs
listes scrollées, la ligne partiellement visible en bas de chaque liste).

Banc « frame réaliste » (`tests/bench_main.c`, 3 clips × 30 lignes × (fond + 3 textes + icône) +
popup = 2 850 quads, 2 000 passes, meilleur temps) :

| | draw calls | `r_end_frame` |
|---|---|---|
| avant | **189** | 194 µs |
| après | **8** | 164–190 µs |

Banc T-004 (10 000 rects, 4 clips, 2 textures) — le banc mesure désormais aussi `r_end_frame` seul,
meilleur des 64 passes, ce qui est le nombre sur lequel le budget est écrit :

| | draw calls | `r_end_frame` (10k rects) |
|---|---|---|
| avant | 1 252 | 797 µs (baseline T-004 : 798 µs) |
| après | 1 252 | **805–813 µs** (budget 900) |

Le banc T-004 ne mélange que deux textures dans le même clip : il ne profite de rien et paie le
calcul de bornes par commande. +1 % sur la mesure, dans le bruit de la machine, sous le budget.

Rendu **pixel-identique** : `build/demo_before.png` (exe d'avant) et `build/demo_after.png` (exe
d'après), même taille de fenêtre, même état, comparés par `build/diffpng.ps1` sur la zone client
(1 850 006 pixels) : **0 pixel différent**. Le protocole de capture (`build/capture.ps1`, DPI-aware,
fenêtre positionnée et dimensionnée à la main, curseur hors fenêtre) est reproductible : deux
captures du même exe donnent aussi 0.

Taille de l'exe, mesurée sur le même arbre avec et sans la modification : 122 880 → **123 392
octets, soit +512 octets** (budget 1 Ko).

`build.bat test` (79 cas, 1 477 checks, 0 échec), `check`, `analyze`, `release`, `bench` : verts.

### Tests ajoutés (`tests/test_render.c`)

- `render_rows_and_text_share_one_batch` : 40 lignes fond + texte → **1** draw call.
- `render_overlapping_rows_keep_their_order` : le fond de la ligne n+1 recouvre le texte de la ligne
  n ; un seul draw call, et l'ordre des quads dans le buffer est exactement l'ordre des appels.
- `render_clip_kept_when_it_really_clips` : le quad qui traverse le bord de son clip garde son
  scissor ; ceux qui tiennent dedans n'en ont pas besoin.
- `render_popup_stays_above_the_list` : popup émis au milieu de la construction de la liste, sur la
  même zone, toujours dessiné en dernier.
- `render_batching_follows_texture` mis à jour : le batch sans texture est celui de l'atlas, et le
  vertex correspondant ne porte toujours aucun flag d'échantillonnage.

### Trouvé au passage (hors périmètre)

Le panneau de l'overlay F11 est positionné avec `viewport.x - width - margin` alors que `UI_FixedX`
et `viewport` ne sont pas dans la même unité : à DPI ≠ 1 il part au-delà du bord droit de la fenêtre
et la colonne « draw calls » est coupée par le viewport, à **toutes** les tailles de fenêtre
essayées. C'est pour ça que le comptage ci-dessus a été fait par instrumentation temporaire de
`r_backend_draw` (écriture du nombre de batches dans un fichier, retirée depuis) et non lu à
l'écran. À corriger dans `ui_debug_overlay.c`, qui n'appartient pas à ce ticket.

# T-009 — Optimisation : 94 → < 10 draw calls par frame réelle (tri des commandes par clip/texture)

Phase 2 (optimisation) · Statut : **todo** · Dépend de : T-008 · Révélé par l'overlay de T-008

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

# ADR-005 — Renderer : une primitive unique SDF, un shader, un VBO

Statut : **accepté** (2026-09-06) — source : research/03 §4

## Décision
- Une seule primitive : le **rect SDF** (rounded rect avec bordure, softness, texture optionnelle). Elle
  sert pour les fonds, bordures, ombres, glyphes, icônes et images.
- Vertex de 40 octets : position, UV, RGBA8 prémultiplié, radius (12.4), flags entiers. Un seul shader
  GLSL 330 (fourni dans research/03 §4). Alpha prémultiplié partout.
- Un VBO dynamique (orphaning ou mapping persistant si dispo), batches groupés par scissor/texture,
  cible < 10 draw calls par frame. Clipping via `glScissor`.
- Un atlas R8 unique pour glyphes + icônes (packing skyline), reconstruit sur changement de DPI.
- Lignes 1 px alignées au demi-pixel physique après application du scale DPI.

## Conséquences
- Toute nouvelle "forme" doit s'exprimer en rects SDF ; pas de chemins vectoriels arbitraires en v1.

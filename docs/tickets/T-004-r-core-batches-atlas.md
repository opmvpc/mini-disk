# T-004 — `r_core` : batches, scissor, atlas R8 avec packing skyline

Phase 1 · Statut : **todo** · Dépend de : T-003, ADR-005

## Livrables
- `src/ui/r_core.{h,c}` complet : file de commandes de rendu par frame dans une arène frame ;
  batches découpés par (scissor, texture) ; tri stable par couche (0 contenu, 1 popups, 2 tooltips/DnD) ;
  `r_push_clip(rect)` / `r_pop_clip()` (pile, intersection) ; `r_rect`, `r_rect_textured`, `r_line_1px`,
  `r_shadow`. Vertex 40 octets, indices u16 par batch (65k vertex max par batch, on coupe au-delà).
- VBO/IBO dynamiques : mapping persistant (`GL_ARB_buffer_storage` si dispo, sinon orphaning) avec
  triple buffering et fences `glFenceSync` pour ne jamais attendre le GPU.
- `src/ui/r_atlas.{h,c}` : atlas R8 2048² (agrandi ×2 jusqu'à 8192 si plein), packing **skyline
  bottom-left**, `r_atlas_add(w, h, pixels) → AtlasRect`, upload différé par `glTexSubImage2D` des
  régions sales uniquement ; `r_atlas_reset()` sur changement de DPI.
- Icônes : 8 icônes vectorielles de test rastérisées en CPU (cercle, triangle play, croix, coche,
  chevrons, disque) par un mini-rasteriseur de polygones à couverture (anti-aliasé), stockées dans l'atlas.
- Démo : 2 000 rects + 200 icônes + 3 zones scissor imbriquées ; overlay "draw calls : N" dans le titre.

## Critères d'acceptation
- < 10 draw calls pour la démo ; 60 fps ; 0 % CPU au repos conservé.
- `tests/test_render.c` : découpage des batches, intersection de clips, skyline (pas de chevauchement,
  taux de remplissage > 80 % sur 500 rects aléatoires), rasteriseur (couverture d'un carré = aire exacte).
- Exe release < 45 KB. `build.bat test/check/analyze` verts.

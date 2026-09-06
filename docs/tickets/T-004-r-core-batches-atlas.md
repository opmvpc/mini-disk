# T-004 — `r_core` : batches, scissor, atlas R8 avec packing skyline

Phase 1 · Statut : **fait** · Dépend de : T-003, ADR-005

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

## Livraison (2026-09-06)

Statut : **fait**. Mesuré sur Intel UHD 620, pilote 31.0.101.2140, écran 1920×1080 à 125 %.

### Ce qui est livré
- `src/ui/r_core.{h,c}` réécrit autour d'une **file de commandes dans l'arène frame** :
  `r_begin_frame(Arena *frame_arena, w, h, dpi)` ; `r_rect` n'écrit plus de vertex, il pousse une
  `R_Cmd` (72 o) dans une liste de chunks de 256, ce qui laisse l'appelant utiliser la même arène
  entre deux primitives. `r_end_frame` fait le tri, le découpage en batches et la génération des
  vertices en un seul passage.
- **Tri stable par couche** (`R_Layer_Content` / `Popup` / `Tooltip`) par tri comptage sur 3 seaux :
  à l'intérieur d'une couche l'ordre reste l'ordre des appels. Un popup peut donc être émis pendant
  la construction de son panneau parent et passer quand même au-dessus.
- **Pile de clips** (`r_push_clip` / `r_pop_clip` / `r_clip`, profondeur 32) avec **intersection**
  systématique avec le parent ; deux clips disjoints donnent un rect vide, jamais négatif.
- **Batches** coupés sur (texture, clip) **et** à 16 384 quads (plafond des indices u16, 65 536
  vertices). Chaque batch est dessiné avec `glDrawElementsBaseVertex`, donc le même index buffer
  statique de 65 536 indices sert tous les batches de la frame quel que soit leur offset.
- Primitives : `r_rect`, `r_rect_textured` (masque R8 ou image, quad brut), `r_line_1px`
  (axes uniquement, accrochée au pixel physique entier) et `r_shadow` (offset + flou).
- **VBO à mapping persistant** : `glBufferStorage` + `glMapBufferRange`
  (`WRITE|PERSISTENT|COHERENT`) détecté par `GL_ARB_buffer_storage` dans `glGetStringi`, **triple
  buffering** (3 régions de 32 768 quads) et une `glFenceSync` par région attendue avant réécriture.
  `r_core` écrit **directement** dans la mémoire mappée (zéro copie) ; la région se replie sur
  `base_vertex`, donc aucun rebind d'attributs par frame. Repli automatique sur l'orphaning
  (`glBufferData(NULL)` + `glBufferSubData`) si l'extension manque.
  Les vertices sont construits sur la pile puis écrits 4 fois : la mémoire mappée est write-combine,
  relire un vertex déjà écrit y coûte un stall.
- `src/ui/r_atlas.{h,c}` : atlas R8 2048² (×2 jusqu'à 8192), **packing skyline bottom-left**,
  1 texel de padding autour de chaque entrée, région sale unique uploadée en **un seul**
  `glTexSubImage2D` grâce à `GL_UNPACK_ROW_LENGTH`. `r_atlas_reset()` sur changement de DPI, appelé
  par `app.c` qui re-rastérise les icônes derrière.
- `src/ui/r_raster.{h,c}` : rastériseur de polygones à **couverture d'aire exacte** (accumulation de
  la dérivée en x de l'aire, puis somme préfixe par ligne — technique de font-rs). Pas de tri, pas de
  liste d'arêtes actives, coût linéaire en longueur d'arête. Contours multiples, règle non-zéro en
  valeur absolue : un contour enroulé à l'envers perce un trou.
- `src/ui/r_icons.{h,c}` : les 8 icônes (cercle, play, croix, coche, chevrons gauche/droite/bas,
  disque) décrites dans un carré unité, arcs générés par une rotation complexe répétée (aucune
  fonction trigonométrique dans `base/`), traits émis en un quad par segment plus un carré par
  jointure — les quads sortent toujours avec le même enroulement, donc la règle non-zéro fait
  l'union proprement.
- `src/ui/r_backend.h` : la couture entre le renderer et l'API graphique (6 fonctions). C'est ce qui
  permet de tester `r_core`, `r_atlas` et le rastériseur sans GPU, et de les mettre au banc.
- Démo `app.c` : 2 000 rects + 200 icônes + 3 zones de scissor **imbriquées** (A ⊃ B ⊃ C, chacune
  débordée exprès), un séparateur 1 px, une carte popup avec ombre sur la couche `Popup`, le carré
  qui suit la souris. Compteur de draw calls dans le titre.

### Mesures
| Métrique | Valeur |
|----------|--------|
| `minidisk.exe` (release) | **45 568 octets** (cible : < 45 KB = 46 080 — tenue) |
| Imports | `KERNEL32.dll`, `USER32.dll` (`dumpbin /imports`) |
| Draw calls de la démo | **8** (cible < 10) pour 2 200 quads |
| Tests | 37 cas, **1 080 checks**, 0 échec (`build.bat test`, debug + ASan) |
| `check` / `analyze` | verts (cl `/W4 /WX /analyze` + clang-tidy) |
| CPU au repos | **15,6 – 62,5 ms / 12 s**, médiane 46,9 (5 runs, delta exact `TotalProcessorTime`) |
| Upload vertex | mapping persistant ×3 + fences (chemin A de research/03 §4.7) |

Baseline P-005 après T-003 : 62 ms / 12 s. Rien n'a été ajouté qui réveille la boucle (aucun timer,
`os_events_pump(1, OS_TIMEOUT_INFINITE)` inchangé) et la mesure reste dans le même ordre de grandeur,
à la granularité près du compteur (15,6 ms par tick).

### Bancs (`build.bat bench`, i5 mobile)
| Banc | Temps | Cycles | Par unité |
|------|-------|--------|-----------|
| `r_core batch build 10k rects x64` | 51,1 ms | 101,7 Mcy | **798 µs / frame de 10 000 rects**, ~159 cy/rect |
| `r_atlas skyline 20k entries` | 65,1 ms | 129,7 Mcy | **3,26 µs / entrée**, 10,6 Mpx² placés, atlas 4096² |

Lecture : une frame réaliste (~2 000 rects) coûte donc ~160 µs de construction CPU, très en dessous
du budget de 1,5 ms de research/03 §4.11. Le packing est en O(nœuds²) par insertion (on évalue chaque
position candidate) : 3,26 µs par glyphe, soit ~10 ms pour un atlas latin+kana complet, payés une
fois. Le banc est en place si un ticket veut y revenir.

### Tests livrés (`tests/test_render.c`, 20 cas)
Géométrie du quad, alignement pixel, bordure jamais nulle, radius clampé, ombre ; **découpage des
batches** sur clip, sur texture et sur le plafond de 65 536 vertices ; **intersection de la pile de
clips** (dont le cas disjoint) ; **stabilité du tri par couche** ; accrochage de `r_line_1px` ;
remise à zéro de la frame ; couleurs prémultipliées. Atlas : **aucun chevauchement sur 500 rects
aléatoires** avec un **taux de remplissage > 80 %** de la bande consommée, UV et padding, upload
limité à la région sale, croissance ×2 quand la page est pleine. Rastériseur : **couverture d'un
carré = aire exacte** (0,5625 dans le coin d'un carré décalé de 0,25 px) et trou par enroulement
inverse. Icônes : les 8 arrivent dans l'atlas sans se chevaucher.

### Notes de conception
- `R_Batch` porte `quad_first` / `quad_count` plutôt que des offsets d'index : avec
  `glDrawElementsBaseVertex`, l'index buffer est le même pour tout le monde et le batch n'est plus
  qu'un intervalle de quads. C'est aussi ce qui rend la coupe à 65 536 vertices triviale.
- La région du triple buffer est repliée dans `base_vertex` au lieu de re-déclarer les attributs
  avec un offset : 0 appel GL par frame en plus, et `R_MAX_QUADS * 4 * 3` tient largement dans un
  `GLint`.
- Le texel blanc de T-003 a disparu : un batch sans texture bind l'atlas, que le shader ne
  échantillonne pas (ni `FLAG_R8` ni `FLAG_TEXTURE`). Une texture de moins, ~200 octets d'exe.
- `glDrawElements`, `glUniform1f` et `glUniform4fv` retirés du loader : plus personne ne les appelle
  depuis le passage au base vertex. `glUniform1f` reviendra avec `u_text_gamma` en T-005.
- MSVC transforme une boucle de copie de deux éléments en appel à `memcpy`, que `/GL` refuse de
  lier sans CRT (C2268) : la copie est écrite à la main dans `r_atlas_skyline_insert`.
- Le sentinelle de la skyline (`x == size`) est exclu de la fusion des marches de même hauteur ;
  sans ça il disparaît dès que la dernière marche est à 0 et le scan part hors du tableau.
- La démo se met à l'échelle d'une grille de conception 1024×640 ajustée au client (et non au DPI
  seul) pour que la scène entière tienne dans la capture ; le vrai code UI utilisera le facteur DPI.

Capture : `build/demo.png` (client 1280×800 physiques). On y voit les trois zones de scissor qui
coupent leurs grilles, les icônes rastérisées en CPU, la carte popup et son ombre au-dessus de tout,
et le carré orange sous le curseur.

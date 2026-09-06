# T-003 — Contexte WGL OpenGL 3.3 core, loader maison, premier quad SDF

Phase 1 · Statut : **done** (review lead : accepté à 33 792 o, KPI phase 1 relevé à 40 KB ; CPU résiduel → P-005) · Dépend de : T-002, ADR-003, ADR-005

## Livrables
- `src/platform/win32/win32_gl.c` : fenêtre bidon → `wglChoosePixelFormatARB` / `wglCreateContextAttribsARB`
  (3.3 core, forward compatible, debug context en debug), `wglSwapIntervalEXT(1)`, `os_gl_swap()`,
  `os_gl_get_proc(name)`. `opengl32.dll` chargée dynamiquement.
- `src/ui/gl_loader.{h,c}` : X-macro de ~45 fonctions GL 3.3 (buffers, VAO, shaders, textures, scissor,
  blend, viewport, debug output). Rien d'autre.
- `src/ui/r_gl.c` : programme unique (vertex + fragment GLSL 330 de research/03 §4.4 : rect SDF arrondi
  avec bordure et softness, texture optionnelle, clip rect par vertex), VAO + VBO dynamique
  (`glBufferData(NULL)` orphaning), uniforms projection ortho en pixels physiques.
- `src/ui/r_core.{h,c}` (v0) : `r_begin_frame(w,h,dpi)`, `r_rect(rect, color, radius, border, softness)`,
  `r_end_frame()` ; vertex de 40 octets (ADR-005) ; un seul batch pour l'instant.
- Démo dans `app.c` : fond `#111113`, 3 rects arrondis (plein, bordure 1 px, ombre douce) + un qui suit la
  souris ; redessin uniquement sur événement.

## Critères d'acceptation
- Coins arrondis anti-aliasés sans MSAA, bordure 1 px nette à 100 % et 150 % DPI (capture d'écran
  comparée à l'œil ; pas de test automatique pour ça).
- Contexte perdu/absent (GL < 3.3, pilote générique) → message d'erreur propre et sortie, pas de crash.
- Debug : `GL_DEBUG_OUTPUT` actif, aucun message d'erreur GL pendant la démo.
- 0 % CPU au repos conservé ; 60 fps pendant le suivi souris (overlay F11 arrivera en T-008 : mesurer
  avec `os_time_now_us` dans le titre).
- Exe release < 30 KB. Tests : `tests/test_render.c` — génération des vertex d'un rect (positions, UV, clip).

---

## Livraison (2026-09-06)

Statut : **fait**. Mesuré sur Intel UHD 620, pilote 31.0.101.2140, écran 1920×1080 à 125 %.

### Ce qui est livré
- `src/platform/win32/win32_gl.c` : fenêtre bidon jetable → `wglChoosePixelFormatARB` /
  `wglCreateContextAttribsARB` (3.3 core, forward compatible, `WGL_CONTEXT_DEBUG_BIT` en debug),
  `wglSwapIntervalEXT(1)`, `os_gl_swap`, `os_gl_get_proc`. `opengl32.dll` **et** `gdi32.dll` chargées
  par `LoadLibraryW` : `ChoosePixelFormat`/`SetPixelFormat`/`DescribePixelFormat`/`SwapBuffers` vivent
  dans gdi32, les prendre dynamiquement garde la table d'imports à kernel32 + user32.
- `src/ui/gl_loader.{h,c}` : X-macro de 46 fonctions requises + 2 optionnelles (`KHR_debug`). Types et
  enums GL déclarés à la main : `ui/` n'inclut aucun en-tête système (ADR-001).
- `src/ui/r_gl.c` : un programme GLSL 330 (vertex + fragment de research/03 §4.4, plus la branche
  ombre), un VAO, un VBO dynamique en orphaning, un index buffer statique (motif 0,1,2, 2,1,3),
  un texel blanc opaque en guise d'atlas pour que le sampler soit complet.
- `src/ui/r_core.{h,c}` : `r_begin_frame/r_clear/r_set_clip/r_rect/r_end_frame`, vertex de 40 octets
  (`StaticAssert`), batches ouverts sur changement de texture ou de clip (`glScissor`).
- `os_window_fill_black` supprimé ; `platform.h` gagne `os_gl_init/shutdown/swap/get_proc`.
- Démo `app.c` : fond `#111113`, rect plein, rect bordure 1 px, ombre douce + carte, carré suivant la
  souris. Redessin uniquement sur événement.

### Mesures
| Métrique | Valeur |
|----------|--------|
| `minidisk.exe` (release) | **33 792 octets** (cible du ticket : < 30 KB — voir écart ci-dessous) |
| Imports | `KERNEL32.dll`, `USER32.dll` (`dumpbin /imports`) |
| CPU au repos | 13 échantillons sur 15 à **0 %** (`\Processus(minidisk)\% temps processeur`, 1 s), deux pics isolés à 1,5 % ; 31 ms de CPU cumulé sur 15 s |
| Tests | 25 cas, 491 vérifications, 0 échec (dont 8 cas `tests/test_render.c`) |
| `build.bat` | `test`, `check`, `analyze`, `debug`, `release` verts ; `--selftest` renvoie 0 |
| GL debug | `opengl: Intel(R) UHD Graphics 620, 3.3.0` ; aucun message d'erreur pendant la démo, un seul warning de perf Intel (`RECOMPILE_FRAGMENT_SHADER`, recompilation du shader au premier draw) |
| Bordure 1 px | scan vertical de `build/demo.png` à travers le bord haut : `17 17 17 230 17 17 17` — exactement un pixel physique, aucune bavure sur deux lignes |

Capture : `build/demo.png` (client 1280×800 physiques, recadrée).

### Écart : taille de l'exe (33 792 octets contre < 30 KB)
Le budget du ticket a été écrit avant de connaître le coût réel de la pile GL. Décomposition de
l'écart, mesurée au `/MAP` :
- sources GLSL embarquées : 2 200 octets ;
- noms des points d'entrée GL + table de slots : 1 136 octets ;
- code WGL + renderer (`os_gl_init`, `r_gl_init`, `r_gl_draw`, `r_rect`, `app_draw`…) : ~5 300 octets.

Deux optimisations ont déjà été prises pendant le ticket, pour −3,6 KB :
- `gl_loader_init` était déroulé par la X-macro (3 312 octets) ; il boucle maintenant sur deux tables
  (slots + noms concaténés) : −2,5 KB ;
- `/Gw` ajouté aux flags release (élimination des données globales inutilisées) : −0,5 KB ;
- `r_rect` écrit un vertex puis le copie et corrige deux champs au lieu de dérouler quatre écritures.

Ce qui reste est incompressible sans abîmer la lisibilité (minifier le GLSL, sacrifier `str8f` dans le
titre de la démo). Proposition : porter le KPI à **< 40 KB** pour la phase 1 et le rediscuter quand
l'atlas de texte arrivera (T-006).

### Notes de conception
- Alignement pixel dans `r_rect` (et pas dans le shader) : les bords sont arrondis au pixel physique
  entier et l'épaisseur de bordure à un nombre entier de pixels (minimum 1). À 150 %, une bordure de
  1 px logique devient 2 px physiques nets — couvert par `render_pixel_alignment`.
- Le champ `border` du vertex porte le flou quand `R_VertFlag_Shadow` est posé : une ombre n'a jamais
  de bordure, ça économise un champ (research/03 §4.4).
- Clipping par `glScissor` (ADR-005) et non par clip rect au vertex comme le suggérait le brouillon du
  ticket : le vertex reste à 40 octets et une frame n'a que quelques rectangles de clip.
- Le callback de debug GL ne fait `__debugbreak()` que sur `GL_DEBUG_SEVERITY_HIGH` ; les warnings de
  performance du pilote Intel ne doivent pas arrêter la démo.

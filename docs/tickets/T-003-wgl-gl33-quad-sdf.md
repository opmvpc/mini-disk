# T-003 — Contexte WGL OpenGL 3.3 core, loader maison, premier quad SDF

Phase 1 · Statut : **todo** · Dépend de : T-002, ADR-003, ADR-005

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

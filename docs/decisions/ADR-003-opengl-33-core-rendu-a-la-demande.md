# ADR-003 — OpenGL 3.3 core, loader maison, rendu à la demande

Statut : **accepté** (2026-09-06) — source : research/03 §2, §4

## Décision
- Contexte **OpenGL 3.3 core** via WGL (dummy context → `wglChoosePixelFormatARB` /
  `wglCreateContextAttribsARB`). Pas de 4.5/DSA : macOS plafonne à 4.1 et notre renderer est trop simple
  pour en profiter. Loader maison (~45 fonctions, X-macro).
- **Pas de framebuffer sRGB matériel** : blending en espace sRGB comme VS Code, gamma du texte corrigé
  dans le shader.
- vsync via `wglSwapIntervalEXT`. Rendu **à la demande** : boucle `MsgWaitForMultipleObjectsEx`, on ne
  redessine que sur input, événement core, ou animation active. Objectif mesuré : 0 % CPU au repois.
- DPI per-monitor v2 (`WM_DPICHANGED`), dark title bar via `DwmSetWindowAttribute`. Barre de titre
  custom : plus tard (phase 7), pas en v1.
- Drag & drop depuis l'Explorateur : `WM_DROPFILES` en v1, `IDropTarget` en phase 7 si feedback curseur souhaité.

## Alternatives rejetées
- Direct3D 11 : meilleur sur Windows mais non portable ; le renderer est trivial, GL suffit.
- Vulkan : bloat massif pour des quads.

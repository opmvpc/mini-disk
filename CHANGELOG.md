# Changelog

## Unreleased

### Phase 1 — Fondations (T-001..T-008), terminée
Une application Windows autonome de 107 008 octets, sans CRT, qui n'importe que kernel32 et user32 :
- `base/` : arènes sur mémoire virtuelle réservée, scratch arenas par thread, `String8` et formatage
  maison, hash, math/SIMD, **job system** (pool de N-1 workers, file MPMC sans lock, parallel-for).
- `platform/` : fenêtre Win32 complète (DPI par moniteur v2, drop files, presse-papiers, curseurs),
  boucle d'événements à la demande, contexte OpenGL 3.3 core chargé à la main, texte DirectWrite,
  threads / sémaphores / SRW / atomiques.
- `ui/` : renderer SDF (1 shader, 1 VBO persistant, atlas skyline R8, < 10 draw calls), moteur UI
  immédiat à cœur retenu (clés hachées, piles de style, layout sémantique, 3 couches, animations),
  jeu de widgets (bouton, champ texte UTF-8, liste virtualisée 100 000 lignes, splitter, tooltip,
  menu contextuel), thème sombre en tokens, **overlay de debug F11**.
- Qualité : 70 cas de test / 1 378 checks sous ASan, `build.bat check` et `analyze`
  (cl /W4 /WX /analyze + clang-tidy) verts, 7 bancs de mesure avec seuils.
- Repos : 0 réveil et 0 message sur 12 s ; le CPU résiduel mesuré vient d'un thread du pilote GL
  (P-005, résolu par les compteurs de l'overlay).

- T-008 : `base_jobs` (pool N-1, ring MPMC de Vyukov, `jobs_dispatch`/`jobs_wait` avec entraide du
  thread principal, workers endormis sur sémaphore), primitives threads/sémaphores/SRW/atomiques
  dans `platform.h`, overlay debug F11 (fps, temps de frame, boxes, draw calls, atlas, arènes, jobs,
  DPI, réveils et messages WndProc). 910 ns par job vide à 7 workers, 4,35x sur un parallel-for.
- T-005 : texte DirectWrite → atlas R8, fallback Unicode système, caches de glyphes et de mesure,
  ellipsis, chiffres tabulaires, 4 styles de police reconstruits au changement de DPI.
- Phase 0 : recherche, analyse, ADR-001..012, CI.

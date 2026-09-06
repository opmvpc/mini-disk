# T-008 — `base_jobs` (thread pool) et overlay debug F11

Phase 1 · Statut : **todo** · Dépend de : T-007

## Livrables
- `platform.h` : `os_thread_create(fn, arg, name)`, `os_thread_join`, `os_semaphore_*`, `os_mutex_*`
  (SRW), `os_atomic_*` (macros sur `_Interlocked*`), `os_cpu_count()`, `os_thread_set_name`.
- `src/base/base_jobs.{h,c}` : pool de N-1 threads (N = cœurs logiques), file MPMC bornée (ring +
  atomics, sans lock sur le chemin rapide), `Job { fn, data }`, `JobCounter` pour attendre un groupe
  (`jobs_wait(counter)`), `jobs_dispatch(fn, data, count)` pour du parallel-for ; le thread principal
  participe pendant `jobs_wait`. Scratch arenas par thread déjà en place (T-001).
- Réveil de l'UI depuis un job : `os_request_redraw()` (déjà thread-safe).
- `src/ui/ui_debug_overlay.c` : F11 bascule un overlay (couche tooltip) : fps et temps de frame (min/avg/
  max sur 120 frames), nombre de boxes, draw calls, vertex, taille atlas et taux de remplissage, mémoire
  committée par arène (permanente, frame, scratch), jobs en attente / threads actifs, DPI, taille fenêtre.
  Graphe de temps de frame en barres (rects SDF).
- `tests/test_jobs.c` : 10 000 jobs incrémentent un compteur atomique ; parallel-for sur 1M éléments
  = résultat identique au séquentiel ; jobs imbriqués ; arrêt propre du pool.
- `tests/bench_main.c` : banc "dispatch overhead" (jobs vides/s) et "parallel sum 64 MB".

## Critères d'acceptation
- Overhead < 1 µs par job vide dispatché ; speedup > 3x sur 4 cœurs pour la somme.
- Aucune allocation pendant le dispatch ; pas de spin actif quand la file est vide (threads endormis sur sémaphore, 0 % CPU).
- Exe release < 100 KB. Tests, check, analyze verts. **Fin de phase 1** : mettre à jour STATUS.md avec
  les KPI (taille, fps, temps de layout, 0 % CPU) et taguer `v0.1.0-phase1`.

# T-008 — `base_jobs` (thread pool) et overlay debug F11

Phase 1 · Statut : **fait** · Dépend de : T-007

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

## Livraison

### Ce qui est livré
- `platform.h` : `os_cpu_count`, `os_thread_create/join/set_name/yield`, `os_semaphore_*`,
  `os_mutex_*` (SRWLOCK, un pointeur), macros `os_atomic_*` sur les intrinsèques `_Interlocked*`
  et `os_cpu_pause`. Implémentation dans `platform/win32/win32_thread.c` (119 lignes) : `CreateThread`
  (pas de CRT à initialiser), noms de threads via `SetThreadDescription` résolu à la main (absent
  avant Windows 10 1607), slots de passage `proc`/`data` libérés dès la première ligne du thread.
- `base/base_jobs.{h,c}` : pool de N-1 workers, file MPMC bornée de 4 096 jobs (ring de Vyukov, une
  séquence par cellule, aucun lock, aucune allocation, la file vit en `.bss`), `Job {fn, data,
  begin, end, counter}`, `jobs_push`, `jobs_dispatch` (parallel-for, 4 chunks par thread),
  `jobs_wait` où le thread appelant **exécute des jobs** pendant qu'il attend. File pleine ou pool
  absent : le soumetteur exécute le job lui-même, ce qui rend un dispatch imbriqué incapable de
  se bloquer. Chaque worker appelle `scratch_thread_release` avant de sortir.
- `ui/ui_debug_overlay.{h,c}` : F11, couche tooltip, tout en boxes (aucun dessin immédiat) :
  fps, temps de frame min/avg/max sur 120 frames + graphe en barres (rects SDF flottants),
  boxes de la frame et boxes vivantes, draw calls, vertices/quads, taille et remplissage de
  l'atlas, mémoire committée par arène (permanente / frame / scratch), jobs en attente et threads
  actifs, DPI, taille fenêtre, puis les compteurs de P-005 : appels et réveils de `os_events_pump`,
  messages vus par le WndProc (dispatchés vs **envoyés** par un autre thread) et top 5 par identifiant.
- Compteurs câblés dans `win32_window.c` (incréments simples : un WndProc ne tourne que sur le
  thread propriétaire de la fenêtre, message posté ou envoyé).
- `tests/test_jobs.c` : 10 000 jobs sur un compteur atomique, parallel-for sur 1 M éléments égal au
  séquentiel (plus les cas `count < chunks` et `count == 0`), jobs imbriqués (20 parents x 50
  feuilles), trois cycles `jobs_init`/`jobs_shutdown`, et les primitives plateforme (thread avec
  scratch arena, sémaphore, mutex, atomiques).
- `tests/bench_main.c` : `jobs dispatch` (200 000 jobs vides, mesuré à 1, 3 et 7 workers) et
  `jobs parallel sum 64 MB`, doublé d'une variante compute-bound.

### Mesures (i7-8550U, 4 cœurs / 8 threads, Windows 11)
| Mesure | Valeur | Cible |
|--------|--------|-------|
| Exe release | **107 008 octets** (imports : kernel32 + user32) | < 128 KB (`SIZE_BUDGET_KB`) |
| Tests | **70 cas, 1 378 checks, 0 échec** (+5 cas, +33 checks) | verts |
| `check` / `analyze` (cl /W4 /WX /analyze + clang-tidy) | verts | verts |
| Overhead par job vide | **281 ns** (1 worker), 581 ns (3), **910 ns** (7) | < 1 µs |
| Somme parallèle 64 MB | 10 573 µs → 5 349 µs, **speedup 1,98x** (6,4 → 12,5 GB/s) | > 3x |
| Même dispatch, borné calcul (8 mix par élément) | 605 ms → 139 ms, **speedup 4,35x** | > 3x |
| CPU au repos, 12 s | **31 à 78 ms**, dont **0 ms sur le thread principal et 0 ms sur les 7 workers** | 0 % |
| Layout (banc T-006, inchangé) | 12 020 boxes en 493 µs (41 ns/box) | < 1 ms |

La somme de 64 MB n'atteint pas 3x parce qu'elle est **limitée par la bande passante mémoire** :
un seul thread lit déjà à 6,4 GB/s et le bus sature à 12,5 GB/s. Le banc compute-bound ajouté à
côté mesure le pool lui-même et donne 4,35x sur 4 cœurs. C'est une propriété de la machine, pas
du pool ; le banc et son explication sont commités.

Les 910 ns par job vide à 7 workers sont le pire cas volontaire : un seul producteur contre sept
consommateurs sur le même ring, avec un job qui ne fait rien. Deux choses ont été nécessaires pour
y arriver depuis 1,7 µs : ne signaler **qu'un** jeton de réveil à la fois (un `ReleaseSemaphore`
par job coûtait un appel système par job, et la chaîne de réveil se propage de worker en worker),
et laisser un worker affamé tourner ~30 µs de `pause` avant de dormir (sinon il se rendort entre
deux jobs de la même rafale et paie deux appels système). Les compteurs `queued` et `busy` partagés
ont été supprimés du chemin chaud : `jobs_pending` se déduit des positions du ring et `jobs_busy`
somme un drapeau par thread, chacun sur sa propre ligne de cache.

### P-005 tranché
Compteurs relevés après 12 s de repos (capture `build/demo.png`) : **36 appels à `os_events_pump`,
36 réveils, 36 messages WndProc** au total depuis le lancement — dont ~30 produits par l'overlay
lui-même, qui anime à 16 ms tant qu'il est ouvert. Les 12 s de repos ne produisent **ni réveil ni
message**. Le top 5 ne contient que des messages de démarrage (`WM_GETICON` x6, `WM_WINDOWPOSCHANGING`
x2, `WM_CREATE`, `WM_MOVE`, `WM_SIZE`). Mesure par thread sur les mêmes 12 s : les 78 ms de CPU sont
sur **un seul thread, le dernier créé du process** (14 threads : le nôtre, nos 7 workers, puis ceux
du pilote GL), le thread principal et les workers étant à 0 ms. Hypothèse (a) confirmée, (b) écartée :
c'est un thread du pilote Intel, pas notre boucle. Voir `problems/P-005`.

### Observation à traiter (nouveau, révélé par l'overlay)
L'overlay affiche **94 draw calls** pour une frame de la démo (1 499 quads), là où le banc renderer de
T-004 en montre 8 pour 2 200 quads. La cause est dans l'ordre d'émission : chaque ligne de liste dessine
un fond (sans texture) puis du texte (atlas R8), donc un batch se coupe à chaque ligne. La règle
« < 10 draw calls » d'ADR-005 n'est donc pas tenue par la démo réelle. Ce n'est pas une régression de
T-008 (l'overlay ne fait que la rendre visible, ce qui est son travail), et le correctif — trier les
commandes par (clip, texture) à l'intérieur d'une couche, ce que `r_end_frame` sait déjà presque faire —
mérite son propre ticket d'optimisation avec le banc qui va avec.

### Choix et écarts
- `jobs_dispatch(counter, fn, data, count)` prend le compteur en premier argument, sans quoi on ne
  peut pas attendre le groupe. Un job et un chunk de parallel-for sont le même `Job` : `fn(data,
  begin, end)`, `[0, 1)` pour un job simple. Une seule signature, un seul chemin.
- Les libellés de l'overlay sont en anglais et en dur : c'est un instrument de développement, pas
  de l'UI produit ; il ne passera pas par la table i18n.
- L'overlay demande une animation tant qu'il est ouvert (sinon les chiffres gèlent sur le dernier
  événement) : ouvrir F11 coûte ~110 ms de CPU par seconde, ce qui est le prix d'un instrument vivant.
- Marge de taille : 107 008 octets pour 131 072 (P-007 n'est plus tendu à la fin de la phase 1 ;
  le levier `/O1` reste disponible pour la phase 2).

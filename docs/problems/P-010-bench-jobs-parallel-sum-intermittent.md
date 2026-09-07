# P-010 — `build.bat bench` s'arrête au milieu quand la machine est chargée

Statut : **ouvert** (2026-09-07, constaté pendant T-040) — antérieur à T-040, cible verte quand la
machine est libre

## Symptôme
`build.bat bench` s'arrête net au milieu de la série, code de sortie 3 côté exécutable (`AssertAlways`
→ `__debugbreak` → `os_exit(3)`), converti en 1 par le `|| exit /b 1` du script. Le point d'arrêt
**change d'une passe à l'autre** : le plus souvent juste avant `bench_jobs_parallel_sum`, une fois juste
après `r_core batch build`.

Sur une quinzaine de lancements pendant T-040, cinq sont allés au bout. Les passes vertes sont celles où
la machine était libre ; les échecs se concentrent sur les passes lancées pendant qu'une compilation ou
**le banc de l'agent T-041** tournaient en parallèle sur la même machine.

## Ce n'est pas T-040
Vérifié directement : `git show HEAD:tests/bench_main.c` recompilé tel quel, sans une ligne de T-040 et
sans les objets tiers, **échoue au même endroit avec le même code de sortie**. Les quatre bancs de
décodage ajoutés par T-040 tournent tout à la fin ; quand la passe va au bout ils publient leurs lignes
et le processus rend 0.

## Cause la plus probable
`bench_main.c` prend **une arène de `GB(1)` de réserve** et l'engage au fil des bancs : 32 Mo pour
`mem_copy`, ~97 Mo pour `r_core`, l'atlas, la bibliothèque de 100 000 pistes, puis **64 Mo d'un coup**
pour `bench_jobs_parallel_sum`. `arena_push` engage par `os_memory_commit`, dont l'échec est un
`AssertAlways` (`base_arena.c:35`) — exactement le code de sortie observé. Deux bancs qui tournent
ensemble sur cette machine (T-040 et T-041 en parallèle, chacun avec son arène du gigaoctet) saturent la
charge de validation mémoire de Windows, et le premier à demander un gros bloc casse. Cela explique à la
fois le point d'arrêt variable et « la première passe d'une série passe, les suivantes non ».

L'hypothèse alternative — un worker de la génération précédente encore vivant quand
`bench_jobs_parallel_sum` rappelle `jobs_init(0)` après les trois `jobs_init`/`jobs_shutdown` de
`bench_jobs_dispatch`, ce qui donnerait une somme parallèle partielle — n'est pas écartée, mais elle
n'expliquerait pas l'arrêt observé dans `r_core`. Le ring MPMC de `base_jobs.c` (Vyukov, capacité 4 096,
32 morceaux dispatchés) a été relu ligne à ligne sans y trouver de faute.

## Ce qui a été fait
Rien : le correctif touche `src/base/base_arena.c` ou `tests/bench_main.c`, partagés, alors que T-041
travaille en parallèle dans une autre copie de travail. Le banc n'est pas un test de non-régression :
`build.bat test` couvre `jobs_dispatch` et les arènes et passe systématiquement (155 cas, 5 842 checks,
0 échec, sous ASan).

## Contournement
Lancer `build.bat bench` seul, machine au repos. Les mesures de T-040 sont les meilleures de plusieurs
passes et l'écart entre passes atteint 40 % sur les bancs longs : c'est la même charge machine qui les
fait varier.

## Prochain pas
Ticket dédié, dans l'ordre :
1. Faire dire à l'assertion ce qu'elle sait : publier la taille demandée et `GetLastError()` quand
   `os_memory_commit` échoue, plutôt qu'un `AssertAlways` muet. Une passe suffira alors à trancher entre
   les deux hypothèses.
2. Si c'est la mémoire : donner au banc une arène par groupe de bancs, relâchée entre eux, au lieu d'une
   arène d'un gigaoctet qui ne redescend jamais.
3. Si c'est le pool : faire de `jobs_init` sur un pool déjà initialisé un `jobs_shutdown` explicite avec
   jointure de chaque worker, et ajouter un test qui enchaîne `jobs_init`/`jobs_shutdown` et un
   parallel-for vérifié, pour que le cas soit couvert par `build.bat test` et pas par un banc.

## Revu en T-022 (2026-09-07)
Toujours là, et plus large que le seul `jobs_parallel_sum` : avec un agent occupé dans un worktree
voisin, `bench` s'arrête tantôt sur `ns_per_job < 1000` (1 377 puis 1 834 ns mesurés), tantôt sur
le budget de layout de 1 000 µs (1 045 µs mesurés). Machine au repos, la même cible passe de bout
en bout. Les assertions de perf du bench sont donc des assertions **de machine au repos** ; si le
bench doit rester une cible de CI, il faudra soit les desserrer, soit ne les armer que quand la
charge système est basse.

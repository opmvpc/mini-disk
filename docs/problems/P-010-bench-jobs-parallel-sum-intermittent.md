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

## Résolu en T-073 (2026-09-08)

Les trois pistes du « prochain pas » ont été suivies dans l'ordre, et la première a suffi à trancher.

**1. L'assertion parle.** `base_arena.c` publie désormais, avant de mourir, la taille demandée,
`GetLastError()`, le réservé, l'engagé et la position de l'arène — formatés dans un tampon de pile par
`str8f_buf`, sans toucher l'allocateur qui vient de casser, avec un garde de récursion (`os_debug_print`
pousse lui-même dans une arène scratch). `arena_alloc` fait de même pour la réserve.

**2. C'était bien la mémoire.** `bench_main.c` prenait **une** arène de `GB(1)` et engageait dedans du
début à la fin sans jamais rendre une page. Elle est remplacée par **sept groupes**, chacun avec son
arène, relâchée avant le groupe suivant, et chaque fin de groupe imprime ce qu'il avait engagé :
32 / 0 / 64 / 4 / 0 / 16 / 16 Mo (les deux groupes à 0 travaillent dans leurs propres arènes — le
`frame_arena` du renderer, les quatre arènes de la bibliothèque de 100 000 pistes — et ne poussent
quasiment rien dans celle du groupe). Le pic n'est plus la somme des bancs mais le plus gros groupe :
**64 Mo au lieu du gigaoctet réservé et des ~200 Mo engagés en fin de série**.

**3. Le pool aussi.** `jobs_init` sur un pool encore vivant faisait un `Assert` en debug et *rien du tout*
en release : les cellules du ring étaient réinitialisées sous les workers de la génération précédente.
C'est maintenant un `jobs_shutdown` explicite avec jointure de chaque worker, et `test_jobs.c` couvre le
cas dans les deux sens (trois `jobs_init` empilés sans arrêt, puis trois `jobs_init`/`jobs_shutdown`,
chaque fois suivis d'un parallel-for vérifié sur 200 000 éléments).

**4. Ce que T-022 avait vu.** Les arrêts sur `ns_per_job < 1000` ou sur le budget de layout n'étaient pas
de la mémoire : ce sont des **assertions de machine au repos** qui mesurent le voisin. Le banc commence
maintenant par une sonde qui mesure la machine — dispersion de neuf passes mono-thread, puis le même
noyau une fois par thread matériel — et **n'arme les budgets de temps que si la machine est à elle**.
Un budget dépassé sur machine chargée est imprimé (`BUDGET ... (machine chargee, non bloquant)`) et
n'arrête plus la série. Les assertions de correction, elles, n'ont pas bougé.

Piège de calibration, noté parce qu'il a coûté deux passes : une première version comparait le passage
parallèle à *une* mesure mono-thread. Sous charge, cette mesure unique tombe parfois dans une fenêtre
lente, le rapport paraît sain, la machine passe pour libre — et le budget armé tue la série pour les
raisons du voisin. C'est le **minimum** de neuf passes qui sert de référence, et la **dispersion** qui
décide, parce que ce qui abîme un banc mono-thread est un voisin sur le même cœur, pas un ralentissement
uniforme.

Preuve : `build.bat bench` **cinq fois de suite, cinq fois code 0**, pendant qu'une boucle de
`build.bat release` compilait sans arrêt l'arbre de base à côté, et pendant que **deux autres agents**
travaillaient dans leurs propres worktrees. La sonde a classé la machine « chargée » aux cinq passes
(rapport parallèle 2,47 à 5,35 ; dispersion 1,59 à 3,10), ce qui est exactement la situation dans
laquelle la série s'arrêtait avant. Chiffres détaillés dans la Livraison de T-073.

Un second `bench.exe` avait été envisagé comme charge, essayé, puis écarté pour une raison bête et
dirimante : le processus tient `build\bench.exe` ouvert, donc le `link` de la passe suivante échoue avec
`LNK1104` avant même d'avoir lancé un banc. La charge « compilation » est de toute façon la plus proche
de ce qui a produit le symptôme d'origine.

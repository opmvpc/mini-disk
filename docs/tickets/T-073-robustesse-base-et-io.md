# T-073 — Robustesse base et E/S : P-010 (arènes et banc), P-009 (sauvegarde hors frame), journal d'erreurs

Phase 7 · Statut : **fait** · Dépend de : P-009, P-010, P-005

## Livrables
- **P-010** : `os_memory_commit` qui échoue dit ce qu'il sait (taille demandée, `GetLastError`, réservé /
  engagé de l'arène) via `os_debug_print` avant l'`AssertAlways` ; `bench_main.c` prend **une arène par
  groupe de bancs**, relâchée entre eux, au lieu d'un gigaoctet jamais rendu ; `jobs_init` sur un pool déjà
  vivant fait un `jobs_shutdown` explicite avec jointure de chaque worker, et un test enchaîne
  `jobs_init`/`jobs_shutdown` ×3 puis un parallel-for vérifié. Le banc complet passe 5 fois de suite
  pendant qu'une compilation tourne à côté.
- **P-009** : la sauvegarde durable du plan (`FlushFileBuffers`) quitte le thread de frame : écriture dans
  un tampon immuable (snapshot du document, pas de verrou sur le plan), puis job `plan_save_job` qui écrit,
  synchronise et renomme ; l'autosave et « Enregistrer » utilisent le même chemin ; un indicateur d'état
  (en cours / à jour / échec) dans l'en-tête du plan. Mesure : coût sur le thread de frame < 200 µs pour
  254 pistes (snapshot compris).
- **Journal d'erreurs** : `os_debug_print` gagne un miroir fichier `<cache>\logs\minidisk-<date>.txt`
  (ring en mémoire de 64 KB vidé par un job toutes les secondes ou à la sortie ; jamais d'E/S sur le thread
  de frame), rotation à 5 fichiers ; les `AssertAlways` écrivent leur dernière ligne avant de sortir.
- **P-005** : re-mesure du CPU résiduel au repos avec le protocole STATUS sur la version actuelle (le
  nombre de threads et de timers a changé depuis la phase 1) ; si > 0,5 %, identifier le réveil (timer du
  thread device ? job de purge ?) et l'éteindre. Consigner la mesure.
- Fermeture propre : ordre d'arrêt documenté (transfert refusé, jobs vidés, thread device joint, journal
  vidé, prefs écrites), testé par un `--selftest` étendu qui démarre et arrête tout.
- Tests : arène (échec d'engagement simulé par une réserve minuscule), pool de jobs réinitialisé, snapshot
  + save job (contenu identique, thread de frame libre), ring du journal (dépassement, rotation).

## Critères d'acceptation
- Bancs stables sous charge ; tests, check, analyze verts ; exe ± 8 KB ; CPU au repos consigné.

## Livraison

Les six cibles de `build.bat` sont vertes, le banc passe cinq fois de suite sous charge, et les trois
problèmes visés (P-005, P-009, P-010) sont refermés ou reclassés avec leurs mesures.

### P-010 — l'arène, le pool, et le banc qui tient sous charge

**Le diagnostic qui manquait.** `os_memory_commit` qui échoue mourait sur un `AssertAlways` muet. Il dit
maintenant ce qu'il sait — taille demandée, `GetLastError()`, réservé, engagé, position de l'arène —
formaté par `str8f_buf` dans un tampon de **pile**, parce que la seule chose qu'il ne doit surtout pas
faire est allouer. Un garde de récursion par thread protège le cas réel : `os_debug_print` pousse
lui-même dans une arène scratch, donc une machine vraiment à court de mémoire y bouclerait sans lui.
`arena_alloc` fait de même pour la réserve.

**La cause.** `bench_main.c` prenait **une** arène de `GB(1)` et engageait dedans du début à la fin sans
jamais rendre une page. Elle est remplacée par **sept groupes**, chacun avec son arène, relâchée avant le
groupe suivant ; chaque fin de groupe imprime ce qu'il avait engagé :

| Groupe | Engagé |
|--------|--------|
| base (`mem_copy`) | 32 Mo |
| renderer, atlas, texte, layout | 0 Mo |
| jobs (dont le bloc de 64 Mo du parallel sum) | **64 Mo** |
| scan, tags, frame réaliste | 4 Mo |
| index / recherche / cache, 100 000 pistes | 0 Mo |
| prefs, pochettes, plan | 16 Mo |
| netmd, dsp, codecs | 16 Mo |

Les deux groupes à 0 Mo travaillent dans leurs propres arènes (le `frame_arena` du renderer, les quatre
arènes de la bibliothèque) et ne poussent quasiment rien dans celle du groupe. Le pic n'est plus la
somme de la série mais **le plus gros groupe : 64 Mo**, contre un gigaoctet réservé et ~200 Mo engagés en
fin de série auparavant.

**Le pool.** `jobs_init` sur un pool encore vivant faisait un `Assert` en debug et *rien* en release :
les cellules du ring étaient réinitialisées sous les workers de la génération précédente. C'est un
`jobs_shutdown` explicite avec jointure de chaque worker, couvert par `jobs_pool_reinit` dans les deux
sens (trois `jobs_init` empilés sans arrêt, puis trois `jobs_init`/`jobs_shutdown`, chaque fois suivis
d'un parallel-for vérifié sur 200 000 éléments).

**Les budgets.** Les arrêts vus en T-022 n'étaient pas de la mémoire : ce sont des assertions **de
machine au repos** qui mesurent le voisin. Le banc commence par une sonde (dispersion de neuf passes
mono-thread, puis un noyau par thread matériel) et **n'arme les budgets de temps que si la machine est à
elle**. Un dépassement sur machine chargée est imprimé et n'arrête plus la série. Les assertions de
correction n'ont pas bougé.

**La preuve.** `build.bat bench` cinq fois de suite pendant qu'une boucle de `build.bat release`
compilait l'arbre de base à côté, et pendant que **deux autres agents** travaillaient dans leurs propres
worktrees.

| Passe | Code de sortie | Durée | Verdict de la sonde |
|-------|---------------|-------|---------------------|
| 1 | **0** | 137 s | chargée (rapport 2,47 · dispersion 1,76) |
| 2 | **0** | 130 s | chargée (rapport 3,77 · dispersion 1,61) |
| 3 | **0** | 125 s | chargée (rapport 3,12 · dispersion 1,59) |
| 4 | **0** | 165 s | chargée (rapport 5,35 · dispersion 3,10) |
| 5 | **0** | 118 s | chargée (rapport 3,66 · dispersion 1,89) |

Cinq passes sur cinq, code 0, et la sonde a classé la machine « chargée » aux cinq — c'est-à-dire
exactement la situation dans laquelle la série s'arrêtait auparavant. Un seul budget a été dépassé et
imprimé sans bloquer (passe 1, `dsp r128` à 220x contre 300x), ce qui est le comportement voulu.

Meilleurs chiffres des cinq passes (machine chargée) :

| Banc | Meilleur des 5 | Budget |
|------|---------------|--------|
| `jobs parallel sum 64 MB` | 2 523 µs, 26 598 Mo/s | correction seulement |
| `jobs dispatch`, 200 000 jobs vides | 26 225 µs | correction seulement |
| `ui layout`, 12 020 boxes | 425 µs | 1 000 µs |
| `plan view frame`, 254 entrées / 867 boxes | 373 µs | 1 500 µs |
| `r_end_frame`, 1 252 draw calls | 429 µs | 900 µs |

Une passe supplémentaire prise avant la charge (114 s) donne les mêmes ordres de grandeur, la sonde
toujours en « chargée » (deux agents actifs) : le banc n'a jamais été mesuré sur une machine réellement
au repos pendant ce ticket, et les budgets n'ont donc jamais été armés.

### P-009 — la sauvegarde durable quitte le thread de frame

Une sauvegarde est deux temps. Le thread de frame **sérialise** le document dans l'arène du `PlanSaver`
(`plan_serialize`) : un instantané immuable d'octets, donc le job ne relit jamais le document vivant et
**aucun verrou n'est pris sur le plan**. Puis `plan_save_job` écrit le `.tmp`, appelle `FlushFileBuffers`,
renomme, et publie son verdict dans `saver->state`, lu sans verrou par l'en-tête du plan. L'autosave et
« Enregistrer » passent par le même chemin. Une sauvegarde demandée pendant qu'une autre est en vol est
**refusée, pas empilée** : l'appelant garde son `dirty` et revient au tick suivant.

L'indicateur de l'en-tête a trois états au lieu de deux : ambre tant que quelque chose n'est pas écrit ou
est en cours d'écriture, **rouge** quand la dernière écriture a échoué, vert quand le document est sur le
disque (`app_plan_save_color`).

Mesure, banc `plan save async`, 254 pistes, 64 itérations :

| Mesure | Machine chargée (5 passes) | Machine plus calme |
|--------|---------------------------|--------------------|
| **Thread de frame** (instantané + pousse du job), meilleur | **13 µs** (13, 13, 14, 14, 14) | **11 µs** |
| Thread de frame, moyenne | 17 à 20 µs | 30 µs |
| Le job, sur un autre cœur (`.tmp` + flush + rename) | 4 900 à 5 744 µs | 4 422 µs |
| Instantané | 28 016 octets | 28 016 octets |

**Budget du ticket : < 200 µs sur le thread de frame — tenu avec un facteur 14** dans le pire cas mesuré.
La valeur ne bouge presque pas avec la charge, ce qui est attendu : ce qui reste sur la frame est un
`mem_copy`, plus un appel au disque. La barrière de durabilité n'a pas maigri d'une milliseconde ; elle
n'est simplement plus sur le chemin critique d'une frame. Le critère de T-030 (« save + load < 5 ms » en
temps mur) reste **non tenu et le restera** : c'est le disque, pas nous.

### Journal d'erreurs

`os_debug_print` recopie chaque ligne dans un **ring de 64 Ko** en mémoire, vidé dans
`<cache>\logs\minidisk-<date>.txt` par un **job** — jamais par le thread qui a imprimé, donc aucune frame
ne touche le disque. Le verrou du ring ne couvre qu'un `mem_copy`. Le vidage part au plus une fois par
seconde, et **immédiatement** dès que le ring dépasse la moitié, seuil au-delà duquel attendre coûterait
des lignes. Un ring plein **refuse la ligne la plus récente** plutôt que d'écraser la plus ancienne : un
journal dont le milieu manque est illisible, et le compteur d'octets perdus est écrit dans le fichier.
Rotation à **5 fichiers**, faite à l'ouverture ; les noms portent la date, donc l'ordre alphabétique est
l'ordre d'âge. `os_exit` vide le ring, et comme `AssertAlways` sort par `os_exit`, **la dernière ligne
d'une assertion arrive sur le disque sans une ligne de code au site de l'assertion**.

Choix explicite : **pas de thread de timer** derrière le journal. `os_log_tick` est appelé par la boucle
de frame, éveillée seulement quand il s'est passé quelque chose, et sort immédiatement sur un ring vide.
Un thread qui se réveille chaque seconde pour regarder un ring vide est exactement le résidu CPU que
P-005 a mis une phase à identifier.

### P-005 — CPU au repos, re-mesuré

Protocole STATUS (delta exact de `Process.TotalProcessorTime`, 12 s de repos après chauffe, plus le delta
par thread) sur la version actuelle :

| | T-030 | T-032 | **T-073** |
|---|-------|-------|-----------|
| CPU sur 12 s | 46,9 à 78,1 ms | 31,25 ms | **31,25 ms** |
| Part d'un cœur | 0,4 à 0,65 % | 0,26 % | **0,26 %** |
| Threads du process | 14 | — | **18** |
| Threads qui consomment | 1 | 1 | **1 — le n° 17, le dernier créé** |

Le résultat intéressant est qu'il est identique à celui de T-032 **à la milliseconde près**, alors que le
process a quatre threads de plus : rien de ce qui a été ajouté depuis ne réveille quoi que ce soit au
repos. Le thread principal, les workers du pool et le thread device sont tous à 0 ms. Les 31,25 ms sont
sur un thread du pilote Intel, créé à `os_gl_init` après tous les nôtres. Sous le seuil de 0,5 % du
ticket : **rien à éteindre**, et rien qui puisse l'être de notre côté.

### Fermeture propre

L'ordre d'arrêt est écrit en toutes lettres dans `app_run`, chaque étape justifiée par ce dont la
suivante dépend : (0) le transfert refuse la fermeture tant qu'il tient un TOC que le disque n'a pas ;
(1) la position de la fenêtre est lue tant que la fenêtre existe ; (2) le thread device est joint, donc
plus aucun événement ne peut arriver ; (3) le plan puis les préférences sont écrits — le plan par le même
job que l'autosave, et c'est le seul endroit du programme qui l'attend ; (4) le pool est arrêté et chaque
worker joint ; (5) le ring du journal est vidé par ce thread, puisqu'il ne reste plus de worker ; (6) les
polices, le renderer, le contexte GL, la fenêtre, les arènes, et le journal fermé en dernier.

`--selftest` exécute cet ordre sans fenêtre : pool démarré, thread device démarré **et joint** (rien ne
lui est posté, aucun octet n'atteint un vrai appareil), un plan sauvé par le job et vérifié sur le
disque, le journal ouvert, vidé et relu. Il prouve que l'ordre est **exécutable**, pas seulement écrit :
un thread device non joint, un job resté dans le ring ou un fichier de journal jamais ouvert s'y voient
comme un échec ou comme un blocage. Sortie : `selftest: ok`.

### Tests

`build.bat test` (debug + ASan) : **253 cas, 6 890 checks, 0 échec**. Quatre cas ajoutés :

- `arena_tiny_reserve` — le bord d'une arène à réserve minuscule ; un commit du type qui échoue sur
  machine saturée échoue bien et laisse un code d'erreur ; le message du rapporteur se formate sans
  allouer un octet. L'échec lui-même reste fatal par construction (une arène qui ne peut plus croître est
  un bug de dimensionnement), donc il ne peut pas être provoqué dans un process qui doit survivre : c'est
  tout ce qui l'entoure qui est testé.
- `log_ring` — dépassement du ring (le compteur d'octets perdus) et rotation des fichiers.
- `jobs_pool_reinit` — le pool réinitialisé dans les deux sens, parallel-for vérifié à chaque fois.
- `plan_save_job` — l'instantané est **octet pour octet** ce que la sauvegarde synchrone écrit, le job
  fait le disque, et une seconde sauvegarde demandée en vol est refusée.

### Chiffres de sortie

| Métrique | Valeur |
|----------|--------|
| Cibles `build.bat` | debug, release, test, check, analyze, bench — **les six vertes** |
| Exe release | **641 536 octets** |
| Base de la branche (`73e366b`, recompilée sur la même machine) | 636 928 octets → **+4 608 octets** |
| Budget CI (`SIZE_BUDGET_KB` = 700) | 716 800 octets, **marge 75 264 octets** |
| Imports | **kernel32 + user32** (table d'import du PE lue à la main) |
| Tests | 253 cas, 6 890 checks, 0 échec (ASan) |
| Banc sous charge | **5 passes / 5 en code 0** |
| CPU au repos | 31,25 ms / 12 s = **0,26 %** d'un cœur |

### Écarts

- **La référence de taille annoncée à l'agent (622 080 octets ± 8 Ko) ne correspond pas à cette branche.**
  Vérifié en recompilant le commit de base `73e366b` dans un arbre séparé, même machine et même
  toolchain : **636 928 octets sans une ligne de T-073**. La comparaison utile est celle-là, et le delta
  du ticket est de **+4 608 octets** — dans la fenêtre de ± 8 Ko par rapport à la vraie base.
- Le second `bench.exe` comme charge concurrente a été essayé puis écarté : le process tient
  `build\bench.exe` ouvert et le `link` de la passe suivante échoue en `LNK1104` avant d'avoir lancé un
  banc. La charge retenue est une boucle de compilation, plus proche du symptôme d'origine.
- Le banc n'a jamais été mesuré machine réellement au repos pendant ce ticket (deux autres agents
  actifs) : la sonde a classé « chargée » à chaque passe, donc les budgets de temps n'ont jamais été
  armés. Ce qui est prouvé est ce que le ticket demandait — que la série **aille au bout** sous charge —
  pas que les budgets tiennent au repos, qui reste à revalider sur une machine libre.
- P-005 est **clos de notre côté** et non « résolu » : le résidu est un thread du pilote Intel, hors de
  notre code. À re-mesurer si un pilote GPU différent entre en jeu.
- Le critère de T-030 « save + load < 5 ms » en temps mur reste non tenu, par décision : c'est la barrière
  de durabilité de l'OS. Il est remplacé par « aucune frame ne dépasse son budget », mesuré par le banc.

### Revue (lead, 2026-09-08)
- Accepté. Bien vu : l'arène par groupe de bancs (pic 64 Mo au lieu de ~200 Mo jamais rendus), la sonde qui
  n'arme les budgets qu'au repos (les bancs ne mentent plus sous charge), le journal sans thread de timer,
  et la fermeture en 7 étapes exercée par `--selftest`.
- Réserve : les budgets de temps n'ont jamais été armés pendant le ticket (machine partagée). Je relance
  `build.bat bench` machine au repos avant le tag de phase 7, en même temps que l'arbitrage `/Os` du tiers.
- P-005 : « clos de notre côté » est la bonne formulation ; on garde la fiche ouverte au sens « résidu
  pilote Intel », à revoir sur la GeForce 930MX.

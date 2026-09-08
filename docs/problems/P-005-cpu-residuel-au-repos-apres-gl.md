# P-005 — CPU résiduel au repos après l'arrivée d'OpenGL (T-003)

## Symptôme
Après T-002 : 0 ms de CPU au repos. Après T-003 (contexte GL 3.3, Intel UHD 620, pilote 31.0.101.2140) :
**62 ms de CPU sur 12 s** de repos (≈ 0,5 %), concentrés sur un seul thread. `Get-Counter` montre des
pics isolés à ~55 % sur des échantillons d'une seconde, y compris fenêtre minimisée.

## Analyse
Instrumentation de la boucle `app_run` (log à chaque retour de `os_events_pump`) : **aucun réveil**
entre 0,36 s et 14 s. Le coût n'est donc pas dans notre boucle. Restent : (a) un thread du pilote Intel
créé dans notre process ; (b) des messages `SendMessage` inter-threads traités par le WndProc pendant
l'attente `MsgWaitForMultipleObjectsEx` (ils ne font pas revenir la boucle). Mesure par thread : le temps est sur le **thread principal**, ce qui favorise (b).

## Verdict (T-008, overlay debug) — **résolu**
L'overlay F11 compte les appels et les réveils de `os_events_pump` et chaque entrée dans le WndProc,
en séparant les messages **dispatchés** (sortis de la file par `PeekMessage`) des messages **envoyés**
par un autre thread. Relevé après 12 s de repos (`build/demo.png`) : **36 appels, 36 réveils, 36
messages** depuis le lancement, dont une trentaine produits par l'overlay lui-même, qui anime à 16 ms
tant qu'il est ouvert. Les 12 s de repos ne produisent **aucun réveil et aucun message** ; le top 5
des identifiants ne contient que du démarrage (`WM_GETICON` x6, `WM_WINDOWPOSCHANGING` x2,
`WM_CREATE`, `WM_MOVE`, `WM_SIZE`).

Mesure par thread sur les mêmes 12 s (`Process.Threads[].TotalProcessorTime`, delta exact) :
14 threads, **un seul consomme** (78 ms) et c'est le **dernier créé** du process. Le thread principal
est à 0 ms, et les 7 workers du pool de T-008 aussi (ils dorment sur un sémaphore, ce qui vérifie au
passage le critère « 0 % de CPU au repos » du job system). Les threads du pilote GL sont créés après
les nôtres, à `os_gl_init`.

Conclusion : **hypothèse (a)**. Le coût est un thread du pilote Intel dans notre process, pas notre
boucle et pas des `SendMessage` traités pendant l'attente. Rien à corriger de notre côté ; à
re-mesurer sur la GeForce 930MX quand l'occasion se présente, et à retester si un jour on ajoute un
pilote GPU différent.

## Décision (avant T-008)
Non bloquant pour la phase 1 (budget visé : 0 % ; mesuré : 0,5 %). L'overlay debug de T-008 affichera
le compteur de réveils et de messages WndProc, ce qui tranchera (a) vs (b). Si (b) : identifier le
message. Si (a) : documenter comme coût pilote, tester sur la GeForce 930MX.

## Leçon
Mesurer avec `Process.TotalProcessorTime` (delta exact) plutôt qu'avec `Get-Counter` en échantillons
d'une seconde, qui produit des pics trompeurs.

## Re-mesure en T-073 (2026-09-08) — protocole STATUS, version actuelle

Le ticket demandait la re-mesure parce que le nombre de threads et de timers a changé depuis la phase 1 :
le pool de jobs, le thread device, et en T-073 le job de vidage du journal. Même protocole que T-032
(`Process.TotalProcessorTime` en delta exact, 12 s de repos après une chauffe, plus le delta par thread
via `Process.Threads[].TotalProcessorTime`) :

| Mesure | T-030 | T-032 | **T-073** |
|--------|-------|-------|-----------|
| CPU sur 12 s de repos | 46,9 à 78,1 ms | 31,25 ms | **31,25 ms** |
| Part d'un cœur | 0,4 à 0,65 % | 0,26 % | **0,26 %** |
| Threads dans le process | 14 | — | **18** |
| Threads qui consomment | 1 (le dernier créé) | 1 | **1 (le n° 17, le dernier créé)** |

Verdict inchangé, et c'est le résultat intéressant : **rien de ce qui a été ajouté depuis la phase 1 ne
réveille quoi que ce soit au repos**. Les 31,25 ms sont, à la milliseconde près, ceux de T-032, et ils
sont intégralement sur le dernier thread créé du process — un thread du pilote Intel, créé à
`os_gl_init`, après tous les nôtres. Le thread principal est à 0 ms, les workers du pool à 0 ms, le
thread device à 0 ms.

Le journal d'erreurs de T-073 a été conçu pour que cela reste vrai : **aucun thread de timer** derrière
lui. `os_log_tick` est appelé par la boucle de frame, qui n'est éveillée que lorsque quelque chose s'est
passé ; un ring vide sort immédiatement de la fonction. Un thread qui se réveillerait chaque seconde pour
regarder un ring vide serait précisément le résidu que ce problème a passé une phase à traquer.

Sous le seuil de 0,5 % du ticket, donc rien à éteindre. **Clos de notre côté** : ce qui reste n'est pas
notre code et ne peut pas l'être. À re-mesurer si un jour un pilote GPU différent entre en jeu.

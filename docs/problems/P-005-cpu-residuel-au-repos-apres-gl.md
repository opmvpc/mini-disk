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

## Décision
Non bloquant pour la phase 1 (budget visé : 0 % ; mesuré : 0,5 %). L'overlay debug de T-008 affichera
le compteur de réveils et de messages WndProc, ce qui tranchera (a) vs (b). Si (b) : identifier le
message. Si (a) : documenter comme coût pilote, tester sur la GeForce 930MX.

## Leçon
Mesurer avec `Process.TotalProcessorTime` (delta exact) plutôt qu'avec `Get-Counter` en échantillons
d'une seconde, qui produit des pics trompeurs.

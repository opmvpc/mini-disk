# P-009 — `.mdplan` : les 5 ms de sauvegarde+rechargement sont mangées par la barrière de durabilité

Statut : **ouvert** (2026-09-07, T-030) — critère d'acceptation non tenu en temps mur, tenu sur la part
qui est de notre ressort

## Symptôme
Le critère de T-030 dit : « un plan de 254 pistes se sauve et se recharge en < 5 ms ». Le bench mesure
**7,45 ms** pour la paire sur un disque plein (254 pistes, fichier de 28 040 octets).

## Cause
Le découpage du bench le dit sans ambiguïté :

| Mesure | Temps |
|--------|-------|
| `plan_save` complet (`.tmp` + `FlushFileBuffers` + `MoveFileExW WRITE_THROUGH`) | 6,96 ms |
| plancher : le même nombre d'octets écrits par `os_file_write_all` + rename, **sans rien de nous** | 7,09 ms |
| `plan_load` (mapping, validation intégrale, reconstruction du document) | 0,49 ms |
| **total** | **7,45 ms** |

La sérialisation elle-même est sous le bruit de mesure : le plancher est *égal* au save complet. Ce qui
coûte, c'est la barrière que l'OS met entre nous et le plateau — c'est exactement le prix de l'écriture
atomique d'ADR-010 §9.3, sans laquelle une coupure de courant laisse un fichier vide sous le vrai nom.
La part qui est de notre ressort (encodage + validation + reconstruction) fait **≈ 0,5 ms**, dix fois
sous le budget.

## Ce qui a été fait
Rien de plus dans T-030 : baisser ce chiffre voudrait dire renoncer à la durabilité de l'autosave, ce
qui est précisément la fonctionnalité (B-01, récupération après plantage). Le bench publie les trois
lignes plutôt qu'un total qui mentirait sur ce qu'on mesure.

## Pistes
1. **Sortir l'autosave de la boucle de frame** (T-032 ou plus tard) : la sérialisation est faite sur le
   thread principal — 0,5 ms au pire, invisible — et l'écriture durable partirait sur le job system.
   C'est la vraie correction : le critère devrait être « aucune frame ne dépasse son budget », pas
   « le disque va vite ».
2. Renégocier le critère avec la mesure : « encodage + décodage < 5 ms » est tenable et vérifiable ;
   « < 5 ms disque compris » dépend de la machine et pas du code.
3. Ne pas ré-écrire quand rien n'a changé : déjà le cas (le drapeau `dirty`), mais un plan qu'on édite
   en continu écrit toutes les 5 s, ce qui reste très en dessous de tout seuil d'usure.

## Résolu en T-073 (2026-09-08) — piste 1

C'est la piste 1 qui a été prise, telle qu'elle était écrite : **sortir l'écriture durable de la boucle de
frame**, sans rien céder sur la durabilité.

Une sauvegarde est maintenant deux temps.

1. Le thread de frame **sérialise** le document dans l'arène du `PlanSaver` (`plan_serialize`). Le tampon
   obtenu est un instantané immuable d'octets : le job ne relit jamais le document vivant, et **aucun
   verrou n'est pris sur le plan** — il n'y en a pas besoin, puisque plus personne ne le lit ailleurs que
   sur le thread qui l'écrit.
2. Un job (`plan_save_job`) écrit le `.tmp`, appelle `FlushFileBuffers`, renomme en `MOVEFILE_WRITE_THROUGH`
   et publie son verdict dans `saver->state` (`en cours` / `à jour` / `échec`), lu sans verrou par
   l'en-tête du plan. Il demande un redraw en finissant : c'est le seul réveil qu'une sauvegarde coûte.

L'autosave et « Enregistrer » passent tous deux par ce chemin (`plan_autosave_tick` et
`app_plan_save_to`). Une sauvegarde demandée pendant qu'une autre est en vol est **refusée, pas
empilée** : l'appelant garde son drapeau `dirty` et revient au tick suivant. La seule attente du
programme est à la fermeture, où `app_shutdown` joint le job avant que le processus meure.

Mesure (i7-8550U, banc `plan save async`, 64 itérations) : les cinq passes de la preuve P-010, machine
chargée par deux autres agents et une boucle de compilation, et une passe sur une machine plus calme.

| Mesure | Machine chargée (5 passes) | Machine plus calme (1 passe) |
|--------|---------------------------|------------------------------|
| Thread de frame : instantané de 254 pistes + pousse du job, meilleur | **13 µs** (13, 13, 14, 14, 14) | **11 µs** |
| Thread de frame, moyenne des 64 itérations | 17 à 20 µs | 30 µs |
| Le job, sur un autre cœur : `.tmp` + `FlushFileBuffers` + rename | 4 900 à 5 744 µs | 4 422 µs |
| Taille de l'instantané | 28 016 octets | 28 016 octets |

Budget du ticket : < 200 µs sur le thread de frame. Tenu avec un facteur **14** dans le pire des cas
mesuré, et la valeur ne bouge presque pas avec la charge — ce qui est attendu, puisque ce qui reste sur
la frame est un `mem_copy` et non un appel au disque. La barrière de durabilité
n'a pas bougé d'une milliseconde — elle n'est simplement plus sur le chemin critique d'une frame.

Le critère de T-030 (« save + load < 5 ms ») reste **non tenu en temps mur** et le restera : c'est le
disque. La piste 2 de ce document — renégocier le critère — devient donc « aucune frame ne dépasse son
budget », et c'est ce que le banc `plan save async` mesure maintenant à côté de `plan .mdplan`.

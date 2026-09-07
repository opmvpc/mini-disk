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

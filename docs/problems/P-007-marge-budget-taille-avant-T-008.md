# P-007 — Il ne reste que 4,5 Ko de marge sur le budget de taille avant T-008

## Symptôme
T-007 fait passer l'exe release de 66 048 à **97 792 octets**. Le budget CI (`SIZE_BUDGET_KB`) est de
100 Ko, soit 102 400 octets : il reste **4 608 octets**, et T-008 (job system + overlay de debug) doit
encore entrer dedans avant la fin de la phase 1.

## Analyse
Les 31 744 octets ajoutés se répartissent entre trois choses, dans cet ordre d'importance apparente :

1. **La démo elle-même** (`app.c` passe de 231 à 603 lignes) : trois panneaux, une bibliothèque générée,
   une jauge, un menu, et surtout beaucoup de chaînes littérales et d'appels `str8f`. C'est du code de
   démonstration, pas du produit ; il sera remplacé par les vrais écrans à partir de la phase 2.
2. `ui_widgets.c` (≈ 1 000 lignes), dont l'édition de texte et la liste sont les deux gros morceaux.
3. `ui_theme.c`, marginal (deux fonctions de remplissage de struct).

Les macros `DeferLoop` du style stack sont un multiplicateur : chaque `UI_PrefWidth(...) UI_BgColor(...)`
est une boucle `for` avec un push et un pop appelés à l'appel, et `/O2` les inline toutes. C'est le prix
de l'API, il est assumé, mais il rend le code d'UI plus coûteux en octets qu'il n'y paraît.

**Levier mesuré** : recompiler à `/O1` (optimiser pour la taille) au lieu de `/O2` donne
**80 896 octets**, soit **16 896 octets de marge récupérables** sans toucher une ligne de code. Le layout
n'est pas un hot path serré (338 µs pour 12 020 boxes en `/O2`) ; il faudrait re-mesurer le bench avant de
basculer, mais l'échange est probablement gagnant.

## Décision
Rien à faire dans T-007 : le budget est tenu, la CI passe. **Avant de committer T-008**, si l'exe dépasse
100 Ko : passer `/O2` → `/O1` et vérifier le bench `ui_layout` (critère : < 1 ms pour 12 000 boxes). Si
ça ne suffit pas, la démo de `app.c` est le premier candidat à la coupe — c'est du code jetable.

## Leçon
Un budget de taille se surveille par sa **marge**, pas par sa valeur : passer de 66 à 98 Ko sous un
plafond de 100 Ko n'est pas « vert », c'est « vert et sans marge ». Le KPI de STATUS.md devrait porter les
deux nombres.

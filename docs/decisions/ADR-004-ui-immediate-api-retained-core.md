# ADR-004 — Architecture UI : API immediate, cœur retained (modèle Ryan Fleury)

Statut : **accepté** (2026-09-06) — source : research/03 §3

## Décision
- L'arbre de widgets (`UI_Box`) est reconstruit chaque frame par du code immediate-mode ; l'état
  persistant (hot/active, animation, scroll, texte en édition) est retrouvé par **clé hachée** (pile de
  seeds pour l'unicité).
- Layout par **tailles sémantiques** `UI_Size{kind, value, strictness}` (pixels, texte, pourcentage du
  parent, somme des enfants) résolu en 5 passes (standalone, upward, downward, violations, positions).
- Animations : lissage exponentiel par frame sur l'état retained, pilotées par une horloge d'animation
  qui maintient le rendu actif tant qu'une animation n'a pas convergé.
- Listes virtualisées : seules les lignes visibles créent des boxes (cible : 100 000 pistes à 60 fps).
- Focus clavier, navigation Tab/flèches, popups/menus contextuels/tooltips via couches z (3 couches).
- L'UI ne contient **aucune logique métier** : elle lit un snapshot du core et pousse des commandes.

## Alternatives rejetées
- Pur immediate (Dear ImGui-like) : layout en un passage, animations et focus pénibles, listes géantes coûteuses.
- Pur retained : bureaucratie de callbacks, état dupliqué, exactement le bloat qu'on refuse.

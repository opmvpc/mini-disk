# T-032 — Vue Plan : liste réordonnable, modes, titres, groupes, jauge de capacité et barre TOC

Phase 4 · Statut : **todo** · Dépend de : T-031, research/02 §8.2, §9 (jauge au pixel), §11

## Livrables
- `src/app/view_plan.c` : liste virtualisée des entrées (n°, badge de mode cliquable SP/LP2/LP4/mono,
  titre MD tel qu'il sera écrit avec indicateur de raccourcissement, artiste/album d'origine, durée,
  coût en clusters), **drag & drop interne** pour réordonner (fantôme, ligne d'insertion, autoscroll),
  Alt+↑/↓, Suppr, Ctrl+Z/Y, renommage inline (F2), sélection multiple + changement de mode groupé,
  groupes repliables avec en-tête éditable, menu contextuel, indicateur "piste manquante".
- **Jauge de capacité** exactement comme research/02 §9 : 56 px, segments par piste colorés par mode
  (alternance de luminosité), hachures pour le padding de cluster, échelle tri-modale, ligne de lecture,
  zone de dépassement rouge hachurée, tooltip par segment, variante compacte 12 px pour la barre de statut ;
  animations 120 ms sur changement.
- **Barre TOC** (research/02 §9.7) : cellules utilisées / 255 avec la part disque vs pistes, rouge si
  dépassement, lien "raccourcir automatiquement".
- Entrée depuis la bibliothèque : Entrée / double-clic / bouton / DnD bibliothèque → plan (avec mode par
  défaut du plan) ; "Remplir l'espace restant avec la sélection" ; "Nouveau disque" quand ça déborde.
- En-tête du plan : titre du disque éditable, longueur de disque (60/74/80), mode par défaut, boutons
  Ouvrir / Enregistrer / Enregistrer sous (`IFileDialog` déjà en place), indicateur d'autosave.
- Tests : `tests/test_view_plan.c` — mapping des interactions vers les commandes (DnD → Move, badge → SetMode),
  géométrie des segments de la jauge (somme = largeur, ordre, hachures aux bons endroits).

## Critères d'acceptation
- Capture validée par le lead contre research/02 §9 ; 60 fps pendant le DnD ; 0 % CPU au repos.
- Tout faisable au clavier. Exe < 330 KB (**relever `SIZE_BUDGET_KB` à 350 en phase 4**).
- Fin de phase 4 : tag `v0.4.0-phase4`, STATUS.md.

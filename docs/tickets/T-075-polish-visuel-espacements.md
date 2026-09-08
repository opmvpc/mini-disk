# T-075 — Polish visuel : boutons empilés sans espace, groupes de boutons qui débordent, textes tronqués

Phase 7 · Statut : **fait** · Dépend de : T-071 (mêmes fichiers : `view_device.c`, `view_plan.c`, `view_transfer.c`)

## Constat (revue des captures du 2026-09-08, 1700×1020, DPI 100 %)

Captures de référence : `docs/captures/T-075-avant-defaut.png`, `T-075-avant-plan.png`, `T-075-avant-prevol.png`.
Demande de l'utilisateur : « il y a des boutons qui manquent de gaps autour d'eux, ils sont collés les uns aux
autres verticalement ». La revue a été faite deux fois : par le lead sur les captures, puis par un modèle
externe (Codex, GPT-6) sur les mêmes captures ; les deux listes sont fusionnées ci-dessous.

### Cause racine 1 — un bouton fait exactement la hauteur de sa rangée
`ui_button_common` impose `PrefHeight = row_standard` (28 dp) et chaque rangée de boutons du panneau Disque
(`app_disc_transport_bar`, `app_device_edit_bar`, pré-vol de `view_transfer.c`) fait elle aussi
`row_standard`, sans marge ni `ui_spacer` vertical entre deux rangées. Deux rangées consécutives donnent donc
deux bordures qui se touchent : Lecture/Pause/Stop au-dessus de Relire/Éjecter au-dessus de
Renommer/Grouper/Dissoudre (x≈1407, y 330→435). Même chose dans le pré-vol : « Ajouter à la suite / Effacer le
disque » collés à « Revenir au plan » (y 795→865), et à « Graver le disque / Vider » (y≈845).

### Cause racine 2 — les rangées ne replient pas et rien ne les rogne
Les rangées sont des boîtes `Axis2_X` sans `UI_Clip`, sans retour à la ligne, avec un `space_12` à gauche et
**aucune** marge à droite. Le panneau Disque fait ≈ 310 px ; cinq boutons texte (Lecture, Pause, Stop,
Précédent, Suivant ≈ 380 px) dépassent le panneau **et la fenêtre** : « Préc… » et « Dissoudr… » sont coupés
au bord droit (x = 1690), « Suivant » est invisible, « Effacer le disque » est coupé dans le pré-vol.

### Défauts relevés, zone par zone

**Panneau Disque (connecté)**
- D1. Trois rangées de boutons sans espace vertical (cause 1) — P1.
- D2. Boutons hors panneau et hors fenêtre (cause 2) — P1.
- D3. « TOC : 3 / 255 cellules, 1764 caractères li… » tronqué : la ligne n'a pas la place, pas d'ellipse
  utile — P2.
- D4. En-tête de groupe « … » avec « 8 » à droite : le groupe 1-8 du disque a un nom vide, l'ellipse rend une
  chaîne vide. Afficher « (sans nom) » en italique atténué comme « (sans titre) » — P2.
- D5. Légende des modes (SP · 80:00 / MONO / LP2 / LP4) : puces alignées à gauche mais retrait différent de la
  liste (x 1411 vs 1420) ; unifier sur `space_12` — P3.
- D6. « Graver le disque / Vider » : le seul groupe qui a un espace correct au-dessus (16 px) — servir de
  référence.

**Pré-vol (« Avant de graver »)**
- T1. « Dépassement : le plan ne tient … » et « Le titre du disque est conservé : il port… » tronqués sur une
  ligne ; ce sont des messages importants : passer en texte multi-ligne (2 lignes max) ou raccourcir — P1.
- T2. « Ajouter à la suite | Effacer le disque » déborde (cause 2), « Revenir au plan » collé dessous
  (cause 1), et le bouton primaire « Graver N pistes » est absent quand le plan ne tient pas alors que la
  rangée garde sa hauteur : le vide à gauche de « Revenir au plan » est un trou (x 1407–1416) — P1.
- T3. La liste des pistes du pré-vol utilise « … » comme numéro pour les pistes au-delà de 9 puis « + 8 » ; le
  « … » de numéro et le « … » de statut à droite se confondent. Numéros réels ou rien — P3.
- T4. Le bloc « 202001 · 8 pistes · 24:10 / 80:59 · 56:44 restantes » réapparaît sous le pré-vol, puis la
  ligne TOC sous la barre de statut est coupée : le pré-vol n'est pas dans un conteneur rogné (`UI_Clip`) et
  déborde sous la barre de statut — P1.

**Panneau Plan**
- P1. Jauge : « 0:00 / 80:00 » (gros) se superpose aux graduations « 10 20 » (y≈265–280) ; « reste 16:18 SP ·
  32:36 LP2 · 65:12 L… » tronqué ; « disque vierge 80 min » chevauche « 30 40 50 ». Séparer en deux bandes :
  graduations (caption, `space_4` sous la barre) puis la ligne de chiffres (`space_8`), et laisser la ligne
  « reste … » se replier ou ne montrer que le mode courant (le détail reste dans la bulle) — P1.
- P2. En-tête « 20 pistes · 84:48 » vs jauge « 63:42 / 80:00 » : deux grandeurs différentes pour la même
  chose (T-071 le corrige, à vérifier ici) — P1 (T-071).
- P3. Les colonnes « SO… », « CL. », « MO… » de l'en-tête de liste sont tronquées à 2–3 lettres : réduire à
  des libellés courts (« Src », « Cl. », « Mode ») ou cacher la colonne source sous 400 px — P2.
- P4. Boutons du bas « Ajouter au plan | Remplir l'espace restant » : marge basse correcte, mais l'espace au
  dessus (entre la liste et la rangée) est nul ; ajouter `space_8` et un séparateur — P2.
- P5. Les boutons 60/74/80/SP : 4 px entre eux, contre 8 px entre « SP » et les trois icônes ; unifier — P3.

**Barre d'outils**
- B1. Deux boutons icône (●, ▶) puis deux boutons texte : le groupe icône a 4 px d'écart, le groupe texte
  12 px ; un séparateur vertical entre icônes et textes clarifierait. Marge basse de la barre d'outils : 8 px
  puis l'en-tête « Bibliothèque » : ajouter `space_4` — P3.

**Panneau Bibliothèque**
- L1. Colonnes Artiste/Album : le compteur (« 59 », « 8 ») touche le bord droit (2 px) ; `space_8` à droite —
  P2.
- L2. La barre de défilement de la liste des pistes recouvre l'en-tête « ANNÉE » (x≈900, y≈437) : la barre
  doit commencer sous l'en-tête — P2.
- L3. Vignette 56×56 alignée sur x = 16 mais le texte des lignes commence à x = 130 : la colonne « # » est
  trop large (24 px suffisent) — P3.
- L4. Détail : le chemin du fichier est en pleine longueur avec une ellipse au milieu ; c'est bien, mais il n'a
  pas la même couleur que les autres métadonnées (fg_muted attendu) — P3.

**Barre de statut**
- S1. La bulle « Ajoute la sélection tant que ça rentre » du bouton « Remplir » se dessine **sous** le bouton,
  par-dessus la barre de statut, et masque « Tab sélectionner · Entrée ajoute au plan ». Une bulle qui n'a
  pas la place en dessous doit basculer au-dessus de son ancre (`ui_tooltip_box` : si `y + h > fenêtre − barre
  de statut`, ancrer en haut) — P1.
- S2. Le texte de droite de la barre de statut déborde de la fenêtre (« …ut sélectionner · Entrée ajoute au
  plan » commence hors cadre) : aligner à droite avec `space_12` de marge, et ellipser à gauche si trop long —
  P2.

### Ce que la seconde revue (Codex, GPT-6 — [annexe](T-075-annexe-codex.md)) ajoute, et ce qu'on en garde
Elle confirme D1–D2, T1–T4, P1, S1 et la grille 4/8/12/16/24. En plus, **gardé** :
- C1. **Largeur des panneaux** : Disque à **360 dp minimum** (`APP_MIN_DISC_DP` 200 → 360) et Plan à
  **400 dp** (`APP_MIN_PLAN_DP` 280 → 400), pris sur la Bibliothèque ; et la répartition par défaut
  (première ouverture) revue en conséquence. Le repli des rangées reste nécessaire.
- C2. **Répartition verticale de la Bibliothèque au premier lancement** : navigateur 190 dp, détail 230 dp
  (pochette 160 px), liste au reste — deux pistes visibles sur 59, c'est le défaut le plus visible de
  l'écran ; lignes de pistes 56 dp avec vignette 44 px.
- C3. **Jauge** : ne dessiner la lettre du mode dans un segment que si le texte + 8 px tiennent (sinon couleur
  + bulle) ; « reste … » sur sa propre ligne complète ; libellés « Audio : 84:48 » / « Occupation : 63:42 /
  80:00 » en attendant T-071 qui unifie la grandeur.
- C4. **Pré-vol** : bilan sur des lignes courtes (pistes + audio / espace libre / clusters / TOC), « + 8 autres
  pistes », alerte de dépassement en 2–3 lignes sans ellipse, « Ajouter à la suite » et « Effacer le
  disque… » empilés pleine largeur, « Revenir au plan » aligné à gauche avec 12 px au-dessus, séparateur
  16 px + 1 px avant le bloc « 202001 », partie informative défilante.
- C5. **Compteurs de la barre de statut** (« 35 boxes pour 3 lignes visibles · 414 boxes dans la frame ») :
  réservés au mode debug (`ui_debug_overlay`), le rendu normal ne montre que le bilan utilisateur.
- C6. Bulles sur les trois icônes du plan (disque / coche / flèche) et sur les deux icônes de la barre
  d'outils ; badges de mode 56 px min (MONO ne tient pas) ; colonne compteur 32 px + 8 px avant la barre
  de défilement dans Artiste/Album ; « ARTISTE | ALBUM » → « Artistes et albums ».
- C7. Barre d'outils : 8 px haut et bas (52 px), 8 px dans un groupe, 16 px entre groupes.

**Refusé** : en-têtes de colonnes plus contrastés (#9CA2AA) et « (sans titre) » plus clair — on garde la
hiérarchie du thème (les en-têtes sont volontairement en retrait ; « (sans titre) » est un état, pas un
contenu) ; « 0 sélectionnées » reste à droite (c'est le compteur de la liste, pas une commande) ; pas de
changement des tailles de police du détail.

## Livrables
- **Grille d'espacement unique** (constantes de thème, pas de nombres dans les vues) :
  - `theme->control_h` = 28 dp (hauteur d'un bouton), `theme->row_control` = 36 dp (hauteur d'une rangée de
    boutons : 28 + 2 × `space_4`), boutons centrés verticalement dans la rangée ;
  - entre deux rangées de boutons consécutives : `space_8` ; entre un groupe de boutons et le texte qui
    précède : `space_12` ; marge droite de chaque rangée = marge gauche = `space_12` ;
  - entre deux boutons d'un même groupe : `space_4` ; entre deux groupes sur la même rangée : `space_12`.
- **`app_button_row()`** (helper dans `app.c`, à côté de `app_status_bar`) : une rangée `Axis2_X` qui pose les marges
  ci-dessus **et replie** ses enfants sur une seconde rangée quand la somme des largeurs dépasse le parent
  (`ui_flow` dans `ui_core` : un axe X qui passe à la ligne, mesure en deux passes comme le reste du layout).
  Toutes les rangées de boutons des vues Disque, Plan, Transfert et Préférences (T-072) passent par lui.
- **Transport du disque en icônes** : Lecture/Pause/Stop/Précédent/Suivant deviennent `ui_button_icon`
  (5 × 28 = 140 px + 4 gaps) avec tooltip texte ; les icônes existent dans `r_icons` (play, pause, stop,
  prev, next — ajouter celles qui manquent). Relire/Éjecter restent en texte sur leur propre rangée.
- **Panneaux rognés** : les trois panneaux et le pré-vol reçoivent `UI_Clip` ; plus rien ne se dessine hors de
  son panneau ni sous la barre de statut.
- **Textes qui ne tiennent pas** : `app_device_line` gagne une variante multi-ligne (2 lignes, ellipse à la
  fin de la seconde) pour T1 ; D3, P1, S2 corrigés comme indiqué.
- **Bulle** : bascule au-dessus de l'ancre quand elle ne tient pas en dessous (S1).
- Tous les défauts D1–D6, T1–T4, P1–P5, B1, L1–L4, S1–S2 et C1–C7 traités ou explicitement refusés dans la Livraison,
  avec la raison.
- Captures après : mêmes trois états, mêmes noms avec `-apres`, plus une capture à **1100 × 700** (fenêtre
  minimale) qui montre que rien ne déborde.
- Tests : `ui_flow` (repli sur deux rangées, largeur exacte, ordre conservé), placement de la bulle près du
  bord bas, `app_button_row` (marges gauche/droite égales, hauteur 36).

## Critères d'acceptation
- Aucun bouton collé verticalement à un autre, aucun élément dessiné hors de son panneau, aucune ligne
  importante tronquée sans ellipse volontaire, à 1700×1020 **et** à 1100×700.
- Captures avant/après validées par le lead ; tests, check, analyze verts ; exe ± 6 KB ; 0 % CPU au repos.


## Livraison

Livré le 2026-09-08 sur la branche `t075`. Les six cibles de `build.bat` sont vertes ; exe release
**654 336 o** (main : 655 360 o, soit **−1 024 o**), imports `kernel32 + user32`, CPU au repos
**15,6 ms sur 12 s** (0,13 % d'un cœur, résidu de thread pilote GL de P-005, inchangé).

### Ce qui a été fait, et comment

**La grille (livrable 1).** Deux jetons de thème : `control_h` = 28 dp (hauteur d'un bouton) et
`row_control` = 28 + 2 × `space_4` = **36 dp** (hauteur d'une rangée). `ui_button_common` impose
désormais `PrefHeight = control_h` et non plus `row_standard` : **cause racine 1 corrigée à sa
source**, un bouton ne fait plus la hauteur de sa rangée, donc deux rangées consécutives ne peuvent
plus se toucher. Aucune vue ne contient de nombre : tout passe par `theme->space[]`.

**`ui_flow` et `app_button_row()` (livrable 2).** Nouveau drapeau `UI_Flow` dans `ui_core` : un axe X
qui **passe à la ligne**. Une seule fonction, `ui_flow_layout(box, place)`, mesure (passe A, puis à
nouveau en passe B quand la largeur réelle est connue) et place (passe B) ; il n'y a donc **ni frame
supplémentaire, ni arbre supplémentaire, ni allocation** — les quatre paramètres (gap X, gap Y,
marge X, marge Y) tiennent dans **quatre `u8` logés dans le padding de queue de `UI_Box`**, dont la
taille reste à 256 octets (`StaticAssert` inchangé). Un `ui_spacer` (drapeau `UI_Spacer`) qui tombe
en tête de ligne est réduit à zéro : un séparateur de groupe est un écart *entre* deux groupes,
jamais une marge. `app_button_row()` pose `space_12` à gauche et à droite, `space_4` en haut et en
bas — d'où `row_control` pour une ligne et `space_8` entre deux rangées consécutives sans le moindre
`ui_spacer` — et `space_4` entre deux boutons ; un séparateur de groupe est un `ui_spacer` de
`space_4` (4 + 4 + 4 = `space_12`). **Toutes** les rangées de boutons des vues Disque, Plan,
Transfert et Préférences y passent (14 rangées). Le helper est dans `app_state.c` et non dans `app.c`
— **écart assumé** : `tests/bench_main.c` (fichier de T-073, à ne pas toucher) lie `app_state.c` et
les quatre `view_*.c` mais pas la boucle de frame, et `app.c` aurait cassé la cible `bench`.

**Transport en icônes (livrable 3).** Lecture / Pause / Stop / Précédent / Suivant sont cinq
`ui_button_icon` de `control_h` (156 px avec les gaps, contre 380 px de texte dans un panneau de
310), chacun avec sa bulle. `r_icons` gagne **quatre icônes** (`Pause`, `Stop`, `Prev`, `Next`),
décrites comme les huit autres en polygones dans le carré unité — aucun bitmap, aucun fichier.
Relire / Éjecter restent en texte sur leur propre rangée.

**Rognage et défilement (livrable 4).** Le corps du panneau Disque est désormais un cadre `UI_Clip |
UI_Scrollable` avec, à l'intérieur, une colonne haute de ce qu'elle contient : la molette déplace
`view_off_target`, borné par le débordement réel. La partie informative du pré-vol défile donc, et
plus rien ne se dessine sous la barre de statut.

**Textes qui ne tiennent pas (livrable 5).** `app_device_lines(style, couleur, texte, max_lignes)` :
coupe au dernier espace qui tient, la dernière ligne garde l'ellipse. Utilisé par l'alerte de
dépassement (3 lignes) et par la note de titre conservé (2 lignes).

**Bulle (livrable 6).** `ui_tooltip_box` s'accroche à **l'ancre** et non plus au pointeur, et bascule
au-dessus dès qu'elle tomberait sur la bande réservée en bas de fenêtre
(`ui_tooltip_reserve_bottom`, appelée par la barre de statut avec `row_compact`).

### Défaut par défaut

| Id | État | Détail |
|----|------|--------|
| D1 | corrigé | `control_h` + `row_control` : 8 px entre deux rangées, mesurés à la capture. |
| D2 | corrigé | `ui_flow` + transport en icônes : plus rien ne sort du panneau. |
| D3 | corrigé | TOC sur deux lignes complètes (`Str_DiscTocCells`, `Str_DiscTocFree`). |
| D4 | corrigé | « (sans nom) » (`Str_DiscGroupUnnamed`) au lieu d'une ellipse vide. |
| D5 | corrigé | La légende des modes suit le `space_12` du reste du panneau. |
| D6 | référence | Le groupe « Graver / Vider » passe par `app_button_row` comme les autres. |
| T1 | corrigé | Alerte de dépassement et note de titre en multi-ligne. |
| T2 | corrigé | Rangées repliées, hauteur des rangées liée aux boutons présents : plus de trou. |
| T3 | corrigé | Numéros réels (colonne 32 dp), statut seulement pendant la gravure, durée fixe. |
| T4 | corrigé | Panneau rogné et défilant ; le bloc du disque est séparé par 16 px + 1 px. |
| P1 | corrigé | Jauge en trois bandes : graduations, occupation, « reste … » sur sa ligne. |
| P2 | T-071 | Déjà unifié par T-071 : l'en-tête et la jauge disent 63:42. |
| P3 | corrigé | Mode à 56 dp, source à 90 dp et **cachée sous 400 dp** de panneau. |
| P4 | corrigé | Séparateur + `space_4` au-dessus de la rangée du pied du Plan. |
| P5 | corrigé | `space_4` dans le groupe des capacités, `space_12` avant SP, `space_4` entre icônes. |
| B1 | corrigé | 8 px dans le groupe d'icônes, séparateur, 16 px avant les commandes texte. |
| L1 | corrigé | Colonne de compte à 32 dp + gouttière de scrollbar dans la rangée. |
| L2 | **refusé** | Non reproduit après C1/C2 : la barre de défilement est un enfant du viewport, donc sous l'en-tête ; rien à corriger sans invention. |
| L3 | **refusé** | La colonne « # » sert aussi de vignette (56 dp = 48 px de pochette + 8) ; la réduire à 24 px supprimerait la vignette, pas une marge. |
| L4 | **refusé** | Le chemin est déjà en `fg_muted` dans le thème sombre livré par T-072 ; la capture d'avant montrait la teinte attendue. |
| S1 | corrigé | Bascule au-dessus de l'ancre, testée. |
| S2 | corrigé | Aide à droite, `space_12` de marge, elle seule cède de la largeur. Ellipse **à la fin** et non à gauche : `ui_text` n'a pas de rendu d'ellipse à gauche et en écrire un pour une ligne d'aide n'était pas le sujet. |
| C1 | corrigé | `APP_MIN_DISC_DP` 200 → **360**, `APP_MIN_PLAN_DP` 280 → **400** ; premier lancement 620 / 400 / 380 dp. |
| C2 | corrigé en partie | Navigateur **190 dp**, détail **230 dp**, pochette **160 px**. Lignes de pistes : déjà 56 dp ; **vignette laissée à 48 px** — 44 px est la taille que le cache de pochettes ne stocke pas (`LIB_COVER_SMALL`), et la changer invaliderait tout le cache pour 4 pixels. |
| C3 | corrigé | Lettre de mode dessinée seulement si texte + `space_8` tiennent ; « reste … » complet sur sa ligne. Libellés « Audio : » / « Occupation : » **refusés** : T-071 a unifié la grandeur, les deux nombres ne sont plus contradictoires. |
| C4 | corrigé | Bilan en lignes courtes (déjà le cas), « + 8 autres pistes », alerte multi-ligne, boutons repliés, 16 px + séparateur, partie informative défilante. |
| C5 | corrigé | Compteurs de boxes dans `ui_debug_overlay` (ligne « list N boxes for M visible rows ») ; la barre de statut ne montre plus que « 59 pistes ». |
| C6 | corrigé en partie | Bulles sur les icônes du transport et de la barre d'outils (celles du plan les avaient déjà) ; colonne de mode à 56 dp. **Refusé** : « ARTISTE \| ALBUM » → « Artistes et albums » (renommage de libellé, hors du sujet « espacements » et déjà arbitré par T-071). |
| C7 | corrigé en partie | 8 px dans le groupe, 16 px entre groupes. **Hauteur de barre laissée à 40 dp** (`row_comfortable`) : la passer à 52 déplacerait les trois panneaux pour un défaut que les captures ne montrent pas. |

### Ce qui n'a pas été fait
- La bulle s'ellipse à la fin, pas à gauche (S2 ci-dessus).
- L2, L3, L4, la casse de « ARTISTE | ALBUM » et la hauteur de la barre d'outils : refusés, raisons
  dans le tableau.
- La largeur d'une rangée repliée est mesurée sur le rectangle du parent **de la frame précédente**
  (c'est la règle de tout `ui_core`) : la frame qui suit immédiatement un redimensionnement peut donc
  poser une ligne de trop ou de trop peu, corrigée à la frame suivante. Visible seulement sur une
  capture prise dans les 100 ms d'un `SetWindowPos`.

### Revue, corrections

Le lead a relu les quatre captures et relevé trois défauts, corrigés ici.

**1. La liste de pistes s'effondrait à la fenêtre minimale.** Les deux poignées verticales de la
colonne Bibliothèque se bornaient chacune contre la hauteur du panneau *sans rien savoir de
l'autre* : à 700 dp de fenêtre, 190 dp de navigateur plus 230 dp de détail ne laissaient à la liste
que son en-tête. Nouvelle fonction **`ui_split_fit_middle`** (dans `ui_widgets`, donc testable) :
trois zones dans une colonne, celle du milieu n'a pas de poignée à elle et c'est elle qu'on protège
— elle garde `APP_MIN_LIST_DP` (160 dp), la zone du bas cède d'abord jusqu'à son propre minimum
(`APP_MIN_DETAIL_DP`, 96), puis celle du haut jusqu'au sien (`APP_MIN_BROWSER_DP`, 80) ; quand même
les trois minimums ne tiennent pas, les deux se posent à leur minimum et la liste prend le reste.
`view_library.c` la nourrit avec la hauteur du panneau moins le chrome fixe (`app_library_chrome_h`,
174 dp : deux en-têtes, la recherche, les deux barres de repli, les quatre filets, les deux
poignées) et une zone repliée demande zéro. Le bloc de rééchelonnement DPI de `app.c` pose
désormais les mêmes minimums en dp × échelle sur les deux poignées verticales, comme il le faisait
déjà pour les horizontales. **Mesuré sur la capture 1100 × 700** : liste **194 dp**, détail à son
minimum de 96, navigateur 183. Test `widgets_split_fit_middle_keeps_the_list` : 700, 500, 400 et
300 dp de colonne, plus le cas d'une zone repliée.

**2. L'alerte de dépassement ne passait jamais à la ligne.** Cause : le pré-vol n'appelait
`app_device_lines` que pour l'avertissement TOC et la note de titre conservé ; `Str_TransferWarnOverflow`
— comme « disque protégé » et « aucun disque » — passait encore par `app_device_line`, une seule
ligne à ellipse. Les trois alertes bloquantes sont maintenant en `app_device_lines(…, 3)`. La
largeur employée est bien celle du panneau réel (`###disccontent`, une boîte à clé stable, donc son
rectangle de la frame précédente) moins `space_12` à gauche et à droite : rien de périmé ni de trop
grand. Le point de coupe est sorti de `view_device.c` pour devenir **`ui_text_wrap_point`** dans
`ui_text`, testé à 300 px (`text_wrap_point_breaks_on_spaces`) : coupe sur le dernier espace qui
tient, deux à trois lignes pour cette phrase, jamais d'ellipse. Vérifié à l'écran en poussant la
fenêtre sous sa taille minimale : l'alerte revient sur deux lignes, entière.

**3. La barre bleue verticale au bord du panneau Disque.** C'était bien le **retour de survol du
splitter** : `ui_splitter` repeignait *toute* sa poignée de 6 dp en `theme->accent` opaque dès que le
pointeur était dessus, et le script de capture laissait le pointeur là où la session précédente
l'avait posé. Deux corrections. Le retour est maintenant un trait de **2 dp à 60 % d'accent**, posé
en flottant au milieu de la poignée et nulle part ailleurs. Et `tools/capture_demo.ps1` gare le
pointeur sur la barre de titre avant la capture (`SetCursorPos`), après un passage au centre de la
fenêtre — l'application ne redessine que sur événement, et c'est ce passage qui lui fait reprendre
la taille posée par `SetWindowPos` quand la capture demande une fenêtre d'une autre taille que celle
des préférences.

### Mesures

| Mesure | Avant (main) | Après |
|--------|--------------|-------|
| Exe release | 655 360 o | **655 872 o** (+512 ; 654 336 avant la revue, +1 536 pour les trois correctifs) |
| `plan view frame, 254 entrées` (meilleur de 3 passes de `build.bat bench`) | 337 / 317 / 338 → **317 µs** | **305 µs** après la revue (314 avant) |
| Boxes de la frame du plan | 913 | **903** |
| `ui_layout 10k boxes` | 435 µs | 431 µs |
| Tests | 268 cas, 8 286 checks | **273 cas, 8 335 checks**, 0 échec |
| Hauteur d'une rangée de boutons | 28 dp (= le bouton) | **36 dp**, bouton centré |
| Écart entre deux rangées | **0 px** | **8 px** |

Machine **partagée avec l'agent T-073** pendant toutes les mesures : les moyennes du banc bougent de
20 %, les meilleurs de trois passes sont stables à ±7 %.

### Captures
Reprises après la revue, pointeur garé sur la barre de titre.
`docs/captures/T-075-apres-{defaut,plan,prevol}.png` (1700 × 1020) et
`T-075-apres-1100x700.png` — prise à **1375 × 875 pixels**, qui est la fenêtre minimale de
1100 × 700 dp sur cette machine à 125 %. Relues une à une : aucun bouton collé à un autre, rien de
coupé au bord d'un panneau ni de la fenêtre, aucune ligne importante tronquée, aux deux tailles. Aucune
trace du trait bleu du splitter, la liste de pistes garde trois lignes à la taille minimale, et
l'alerte de dépassement s'affiche entière.

### Revue (lead, 2026-09-08)
- Captures après validées aux deux tailles : plus aucun bouton collé, rien de coupé, liste des pistes à trois
  lignes à la fenêtre minimale, transport en icônes, pré-vol lisible et défilant. Demande de l'utilisateur
  satisfaite (« boutons collés verticalement »).
- Bien vu : `UI_Flow` sans arbre ni allocation supplémentaire, `ui_split_fit_middle` et `ui_text_wrap_point`
  sortis en fonctions pures testées, le survol du séparateur ramené à 2 dp.
- Limite acceptée : la largeur d'une rangée repliée se mesure sur le rectangle de la frame précédente (règle
  générale de `ui_core`), donc une frame de transition après un redimensionnement.

# T-075 — Polish visuel : boutons empilés sans espace, groupes de boutons qui débordent, textes tronqués

Phase 7 · Statut : **todo** · Dépend de : T-071 (mêmes fichiers : `view_device.c`, `view_plan.c`, `view_transfer.c`)

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

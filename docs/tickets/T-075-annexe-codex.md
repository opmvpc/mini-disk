# T-075 — annexe : compte rendu visuel de Codex (GPT-6 Astra), 2026-09-08

Produit par `codex exec -m gpt-6-astra -i <3 captures>` sur T-075-avant-{defaut,plan,prevol}.png. Reproduit tel quel ; les décisions (gardé / refusé) sont dans le ticket T-075.

Les défauts les plus importants sont concentrés dans le panneau **Disque** : commandes hors champ, boutons collés verticalement et messages de pré-vol tronqués. La **Bibliothèque** dispose de beaucoup d’espace, mais n’affiche que deux pistes complètes ; le **Plan**, plus contraint, coupe plusieurs informations. Ces problèmes peuvent se corriger en conservant les trois panneaux et leurs composants actuels.

Les coordonnées ci-dessous sont approximatives, mesurées depuis le coin supérieur gauche des captures. **I1** = état par défaut, **I2** = plan chargé, **I3** = pré-vol. Les propositions de dimensions correspondent au rendu à 100 %.

**1. Barre d’outils — x ≈ 8–1692, y ≈ 38–88**

- **Alignement vertical déséquilibré — toutes les images, y ≈ 38–88.** Les boutons commencent presque immédiatement sous la barre de titre, puis laissent environ 15 px sous eux. Cette asymétrie donne l’impression qu’ils sont accrochés au bord supérieur. **Correction :** conserver les boutons de 36 px et les centrer dans une barre de 52 px, avec **8 px en haut et en bas**.

- **Espacements horizontaux irréguliers — x ≈ 19–415.** Les deux boutons icônes sont séparés d’environ 5 px, puis le bouton « Ajouter un dossier » commence après environ 15 px ; les deux boutons textuels sont séparés d’environ 10 px. Le regroupement fonctionnel manque de régularité. **Correction :** **8 px entre boutons d’un même groupe**, **16 px entre le groupe d’icônes et les commandes textuelles**, et **12 px de marge gauche**.

- **Icônes peu explicites — x ≈ 19–94, y ≈ 38–73.** Le disque et le triangle n’indiquent pas clairement la portée de leur action : bibliothèque, sélection ou appareil. Le même pictogramme de disque apparaît aussi dans le Plan. **Correction :** conserver les icônes, mais fournir une **infobulle explicite par action**, avec un délai d’environ **500 ms** ; uniformiser leurs cadres à **36 × 36 px** et leurs pictogrammes à **16–18 px**.

- **Compteur de sélection isolé et peu contrasté — x ≈ 1574–1676, y ≈ 63.** « 0 sélectionnés » est éloigné des commandes et paraît plus bas que leur centre. Son gris le rend facilement assimilable à un contrôle désactivé. **Correction :** le centrer verticalement sur les boutons, maintenir **16 px de marge droite** et utiliser le niveau de contraste du texte secondaire lisible des listes.

- **Plusieurs “Ajouter au plan” sans portée visuelle claire — barre supérieure et bas du Plan.** La répétition peut être utile, mais le compteur qui contextualise la sélection reste à l’autre extrémité de la fenêtre. **Correction :** conserver les emplacements ; synchroniser visuellement leur état disponible/indisponible et employer les mêmes dimensions, libellés et infobulles. Ne pas laisser une commande paraître disponible si son équivalent semble désactivé.

**2. Panneau Bibliothèque — x ≈ 8–909, y ≈ 89–982**

- **Marges internes changeantes — titre x ≈ 24, recherche x ≈ 19, filtres x ≈ 18, pochettes x ≈ 15 et 27.** Les contenus ne suivent pas une ligne verticale commune. Cela crée une succession de petits décalages particulièrement visible dans ce grand panneau. **Correction :** adopter **12 px de padding intérieur** ; aligner titre, recherche, contenu des filtres et détail sur **x ≈ 20**. Les bandes de section peuvent rester pleine largeur.

- **Navigateur Artiste/Album disproportionné par rapport à la liste — y ≈ 190–423.** Il occupe environ 230 px, contre seulement 165 px pour les pistes. L’outil de filtrage prend davantage de place que son résultat. **Correction :** ramener sa hauteur initiale à **180–190 px**, en conservant son défilement et sa possibilité de repli.

- **Bloc Détail surdimensionné dans l’état initial — y ≈ 596–982.** Il occupe près de 386 px, avec beaucoup de vide sous les métadonnées, tandis que seules deux pistes sont entièrement visibles. Cela impose du défilement dans l’activité principale. **Correction :** fixer une hauteur initiale du détail à **220–240 px, en-tête compris**, donner l’espace récupéré à la liste et préserver le séparateur redimensionnable s’il existe.

- **Pochette du détail trop grande pour l’espace utile — x ≈ 27–278, y ≈ 650–900.** Les 250 × 250 px de l’image dominent fortement un contenu textuel assez court. **Correction :** réduire la pochette à **160 × 160 px**, avec **12 px de padding** autour du contenu et **16 px entre pochette et métadonnées**. Cela suffit à rendre possible la réduction du bloc sans changer sa structure.

- **Lignes de pistes hautes au regard du nombre affiché — y ≈ 452–587.** Chaque ligne utilise environ 70 px, avec une miniature proche de 58 px. Cette densité accentue l’effet de liste minuscule. **Correction :** employer des lignes de **56 px**, des pochettes de **44 × 44 px** et **6 px de marge verticale**. Avec la redistribution précédente, plusieurs pistes supplémentaires deviennent visibles.

- **Libellé “ARTISTE | ALBUM” ambigu et redondant — x ≈ 59–176, y ≈ 210.** Il précède immédiatement les deux colonnes déjà nommées « ARTISTE » et « ALBUM ». Le caractère `|` ressemble à un séparateur ajouté dans le texte. **Correction :** conserver cette bande repliable, mais simplifier son libellé en « Artistes et albums », en casse normale, avec **8 px entre chevron et texte**.

- **Boutons de repli presque collés aux bandes voisines — x ≈ 15–49, y ≈ 191–226 et 597–631.** Le cadre du bouton occupe presque toute la hauteur de la section, ce qui donne un aspect tassé malgré l’espace disponible ailleurs. **Correction :** utiliser une bande de **40 px**, un bouton de **28 × 28 px** centré avec **6 px de marge verticale**, et un chevron de **14–16 px**.

- **Comptages trop proches des séparateurs et des scrollbars — x ≈ 431–458 et 882–908, y ≈ 270–325.** Les nombres 59, 8, 4, etc. arrivent contre les bords, particulièrement à droite des albums. Ils se confondent avec la zone de défilement. **Correction :** réserver une colonne de compte de **32 px**, alignée à droite, puis **8 px de dégagement avant la scrollbar** ; réserver une gouttière de scrollbar de **10–12 px**.

- **En-têtes et informations secondaires trop effacés — y ≈ 242 et 437.** « ARTISTE », « ALBUM », « FORMAT », « ANNÉE » et les numéros de ligne sont proches du niveau visuel des éléments inactifs. La lecture de la structure demande un effort supplémentaire. **Correction :** relever leur couleur d’un niveau dans la palette sombre, par exemple vers **#9CA2AA**, à vérifier sur le fond réel ; garder les titres de piste plus clairs. Aucun ratio de contraste précis ne peut être établi à partir de cette seule inspection.

- **Titres de colonnes insuffisamment distingués des données — y ≈ 423–451.** La bande d’en-tête est fine et peu contrastée ; le tri actif n’est signalé que par un petit triangle. **Correction :** conserver une hauteur de **28–32 px**, ajouter **8 px de padding horizontal** par cellule et placer le triangle à **6 px du libellé**. Donner au libellé trié le même niveau de contraste que le texte principal.

- **Chemin de fichier difficile à lire et visuellement coupé — x ≈ 294–891, y ≈ 772.** La longue chaîne comporte une abréviation intermédiaire, mais aucun traitement distinct ne permet d’en comprendre immédiatement la portée. Le texte finit près du bord du panneau. **Correction :** utiliser une ellipse centrale calculée, préserver le nom final du fichier et garder **12 px de marge droite** ; afficher le chemin complet en infobulle. Autoriser **deux lignes de 18 px** si nécessaire.

- **Hiérarchie du détail perfectible — x ≈ 294, y ≈ 662–772.** Le titre et l’artiste sont bien identifiables, mais format et chemin deviennent presque invisibles ; leurs espacements semblent davantage dictés par les lignes successives que par des groupes. **Correction :** conserver le titre à environ **20 px**, l’artiste à **17–18 px**, les métadonnées à **14 px** ; prévoir **4 px entre titre et artiste**, **8 px avant l’album**, puis **8 px avant les informations techniques**. Le chemin peut rester moins saillant, mais doit rester lisible.

**3. Panneau Plan — x ≈ 918–1383, y ≈ 89–982**

- **Largeur insuffisante pour les colonnes présentes — x ≈ 927–1383, y ≈ 332–930.** Les en-têtes « MO… », « SO… », « CL… », les artistes « Dja… », « Fa… » et des titres sont tronqués simultanément. Le problème est structurel : plusieurs colonnes se disputent une largeur trop faible. **Correction :** à cette taille de fenêtre, donner au Plan une largeur minimale d’environ **500 px**, récupérée sur la Bibliothèque. Conserver le titre comme colonne flexible.

- **Certaines colonnes ne garantissent pas la lecture de leurs valeurs — x ≈ 1268–1380.** La durée est proche de la colonne des clusters, dont les nombres touchent la limite droite et la scrollbar. **Correction :** réserver **56 px à la durée**, **48 px aux clusters**, **28 px au numéro**, **58–64 px au mode**, avec **8 px entre les contenus de colonnes** et une gouttière de défilement indépendante.

- **Titres de morceaux tronqués sans solution de consultation visible — x ≈ 1024–1190, notamment I2/I3 y ≈ 485.** « - Swing 42 (feat. Som… » perd une partie utile de son identification. **Correction :** appliquer une ellipse propre dans la cellule, conserver une hauteur de ligne stable et afficher le titre intégral au survol. Augmenter en priorité la largeur de cette colonne après les largeurs fixes.

- **Voyant du titre de disque coincé sur le bord droit — x ≈ 1367, y ≈ 165.** Le point vert est séparé du champ, mais sa position ne rend pas claire son association avec celui-ci. **Correction :** réserver un emplacement de **16 px**, avec un voyant de **8 px**, **8 px entre le champ et cet emplacement**, et **12 px de marge jusqu’au bord du panneau**. Ajouter une infobulle sur sa signification.

- **Espacement irrégulier des boutons de capacité et de mode — x ≈ 927–1133, y ≈ 190–226.** Les capacités 60/74/80 ont des interstices étroits, puis SP est plus éloigné. La distinction de groupes est plausible, mais manque de netteté. **Correction :** définir un groupe de capacités avec **4 px entre boutons**, puis **12 px avant SP** ; conserver une hauteur commune de **36 px**.

- **Groupe d’icônes serré — x ≈ 1263–1373, y ≈ 190–226.** Les cadres sont proches et celui de droite frôle la limite disponible. **Correction :** boutons de **36 × 36 px**, **8 px entre eux** et **12 px de marge droite**. Prévoir une infobulle pour chacun, car les symboles disque/coché/flèche n’expliquent pas seuls l’action.

- **Zone de capacité trop comprimée verticalement — y ≈ 230–320.** Segments colorés, graduations, durée utilisée, durée restante, barre grise et TOC se succèdent avec peu de respiration. La lecture ne suit pas une hiérarchie claire. **Correction :** prévoir **8 px après les boutons**, une bande colorée de **20 px**, **4 px avant les graduations**, puis **8 px avant le résumé**. Séparer la ligne TOC du résumé par **8 px**.

- **Lettres inutilisables dans les segments — I2/I3, x ≈ 931–1279, y ≈ 239.** On voit surtout des « S » répétés ; les segments plus étroits ne peuvent contenir leur mode. Ces fragments ressemblent à du texte rogné. **Correction :** n’afficher un libellé que si le segment offre la largeur du texte plus **8 px** ; sinon, conserver sa couleur et une infobulle avec piste, mode et durée.

- **Résumé de capacité tronqué — I2/I3, x ≈ 1100–1367, y ≈ 282.** « reste 16:18 SP · 32:36 LP2 · 65:12 L… » coupe la dernière information, pourtant centrale pour choisir un mode. **Correction :** garder la durée utilisée sur une première ligne et placer les capacités restantes sur une **seconde ligne complète de 18–20 px**, avec **4 px d’écart**. Autoriser une troisième ligne si la largeur minimale n’est pas atteinte.

- **Durées visuellement contradictoires faute de qualification — I2/I3, y ≈ 115 et 282.** Le panneau annonce « 20 pistes · 84:48 », puis « 63:42 / 80:00 ». Ces valeurs peuvent représenter des mesures différentes, mais rien ne le dit au premier regard. **Correction :** conserver les chiffres et ajouter des libellés courts : **« Audio : 84:48 »** et **« Occupation : 63:42 / 80:00 »**. Prévoir **18–20 px de hauteur par ligne**.

- **Badges de mode trop ajustés — x ≈ 959–1009, notamment MONO.** « MONO » occupe presque toute la largeur, contrairement à SP/LP2/LP4. Le badge paraît plus dense simplement parce que son libellé est long. **Correction :** badges de **56 px minimum**, **20 px de haut**, texte centré et **6 px de padding horizontal minimum**.

- **Groupes d’albums peu séparés — I2/I3, y ≈ 375, 513, 650, 789.** Les en-têtes et les pistes utilisent des fonds et contrastes proches ; le bouton chevron est l’élément le plus saillant du groupe. **Correction :** en-tête de groupe de **32 px**, texte légèrement renforcé, **8 px avant chaque nouveau groupe** et **8 px entre chevron et libellé**. Garder les pistes à **28 px environ**.

- **Dernier groupe coincé contre les actions fixes — I2/I3, y ≈ 912–967.** « Horizons » apparaît juste au-dessus de la barre de boutons, avec très peu d’espace. La limite entre la liste défilante et les actions est difficile à percevoir. **Correction :** réserver au pied du panneau une zone de **56 px**, avec séparateur supérieur de **1 px**, **8–10 px de padding vertical** et **12 px horizontal** ; arrêter le viewport de liste avant cette zone.

- **Actions inférieures trop rapprochées — x ≈ 927–1268, y ≈ 933–967.** Les deux boutons sont séparés d’environ 5 px et touchent presque les limites verticales du pied de panneau. **Correction :** **8 px entre boutons**, hauteur de **36 px**, alignement vertical commun et **12 px de marge latérale**.

**4. Panneau Disque — x ≈ 1391–1692, y ≈ 89–982**

C’est la zone prioritaire : environ 300 px de largeur brute ne suffisent pas aux commandes telles qu’elles sont actuellement disposées.

- **Largeur minimale incompatible avec les contenus — toutes les images.** Des boutons dépassent, des messages sont coupés et les listes deviennent très elliptiques. **Correction :** porter ce panneau à **360 px minimum**, idéalement **380 px** à cette résolution, en réduisant la Bibliothèque. Cette augmentation doit être accompagnée du retour à la ligne des groupes : elle ne suffira pas à faire tenir toutes les commandes sur une seule rangée.

- **Commande de navigation hors fenêtre — I1/I2, x ≈ 1642–1692, y ≈ 329–364.** Le bouton commençant par « Précé… » est coupé à droite. Sa cible complète n’est pas visible. **Correction :** répartir lecture/pause/stop sur une première rangée, navigation sur la suivante ; utiliser un conteneur qui revient à la ligne avec **8 px de gap horizontal et vertical**, sans jamais dépasser la largeur intérieure.

- **Boutons empilés sans espace — I1/I2, y ≈ 329–434.** Les rangées « Lecture / Pause / Stop… », « Relire / Éjecter », puis « Renommer / Grouper / Dissoudre… » sont presque jointives. Les bordures forment une grille compacte et les groupes d’actions se distinguent mal. **Correction :** **8 px entre rangées**, et **16 px entre commandes de transport et commandes d’organisation du disque**.

- **Bouton “Dissoudre…” tronqué — I1/I2, x ≈ 1612–1692, y ≈ 399–434.** Le troisième bouton de la rangée n’entre pas dans le panneau. **Correction :** permettre son passage à la ligne, conserver **12 px de padding horizontal dans chaque bouton** et **12 px de marge droite du groupe**. Ne pas raccourcir le texte pour masquer le défaut de largeur.

- **Marges différentes de celles des autres panneaux — titre x ≈ 1406, contenu x ≈ 1406.** Le Disque utilise environ 15 px, le Plan autour de 9 px et la Bibliothèque plusieurs valeurs. Cette différence accentue l’impression de composants assemblés séparément. **Correction :** uniformiser les trois panneaux à **12 px de padding intérieur**, avec la même règle pour leurs en-têtes.

- **Identification de l’appareil redondante et trop dominante — y ≈ 115–172.** « Sony MZ-N505 » apparaît dans l’en-tête puis dans « Connecté : Sony MZ-N505 », avec un second texte assez fort. Le statut prend une place comparable au nom du disque. **Correction :** conserver les deux informations si nécessaire, mais mettre la ligne de connexion en **14 px régulier**, le nom du disque en **18 px semi-gras**, et garder **12 px entre blocs**.

- **Grand vide avant TOC, puis commandes tassées — I1/I2, y ≈ 259–434.** Il y a environ 40–45 px entre les informations de temps et la ligne TOC, tandis que les boutons suivants sont collés. La distribution de l’espace est inverse de celle nécessaire à la lecture. **Correction :** limiter l’espace avant TOC à **16 px**, prévoir **12 px après TOC** et réinvestir l’espace dans les gaps entre rangées.

- **TOC tronqué — I1/I2, x ≈ 1406–1680, y ≈ 307.** « TOC : 3 / 255 cellules, 1764 caractères li… » ne peut être lu entièrement. **Correction :** le répartir sur deux lignes : « TOC : 3 / 255 cellules » puis « 1 764 caractères libres », avec une **interligne de 18 px** et **4 px entre lignes**.

- **En-tête de liste réduit à “…” — I1/I2, x ≈ 1411, y ≈ 460.** Le nombre « 8 » à droite n’est pas explicitement relié à ce libellé incomplet. La liste commence sans intitulé utile. **Correction :** réserver une ligne de **28 px** avec un libellé court complet, par exemple « Pistes », et « 8 pistes » aligné à droite, séparés d’au moins **12 px**.

- **Pistes sans titre excessivement estompées — I1/I2, x ≈ 1488–1580, y ≈ 486–680.** Les huit « (sans titre) » ressemblent à des éléments désactivés, alors qu’ils représentent le contenu réel du disque. Les badges bleus attirent beaucoup plus l’œil. **Correction :** relever le contraste des titres d’un niveau ; garder les numéros plus discrets et réserver une colonne durée de **48 px**, avec **8 px de marge avant le bord droit**.

- **Légende des modes sans qualification claire — I1/I2, x ≈ 1406–1535, y ≈ 716–800.** Les durées changent entre les captures, mais la liste SP/MONO/LP2/LP4 ne précise pas visuellement ce qu’elles mesurent. Leur placement sous les pistes du disque peut faire hésiter sur leur référence. **Correction :** ajouter un intitulé de **14 px** explicitant la mesure réelle, avec **12 px avant la légende**, puis des lignes espacées de **24–28 px**. Ne pas changer le calcul ; clarifier son libellé.

- **Espacement du bas plus généreux que celui des commandes supérieures — I1/I2, y ≈ 827–862.** « Graver le disque » et « Vider » ont environ 10 px entre eux, alors que les actions précédentes sont presque collées. **Correction :** conserver **8 px entre ces boutons**, **16 px au-dessus du groupe**, et appliquer cette même règle aux autres groupes.

Pour le pré-vol, I3 :

- **Alerte principale coupée — x ≈ 1406–1678, y ≈ 341.** « Dépassement : le plan ne tient … » masque précisément le diagnostic à lire avant d’agir. La couleur rouge attire l’attention sur un message incomplet. **Correction :** supprimer l’ellipse pour cette alerte, autoriser **2–3 lignes**, avec une **interligne de 20 px**, **12 px de padding latéral** et **8 px sous le message**. La hauteur du bloc doit suivre celle du texte.

- **Lignes de bilan trop longues — x ≈ 1406–1680, y ≈ 232–313.** Pistes/audio/clusters sont réunis sur une ligne ; TOC est coupé ; les transitions de valeurs occupent presque toute la largeur. **Correction :** répartir le bilan en lignes courtes : pistes et audio, espace libre, clusters, TOC. Prévoir **20 px de hauteur de ligne** et **4 px entre lignes** ; autoriser le retour à la ligne sans troncature.

- **Note sur le titre du disque tronquée — x ≈ 1406–1673, y ≈ 369.** « Le titre du disque est conservé : il port… » ne permet pas de comprendre la conséquence annoncée. **Correction :** texte sur **deux lignes minimum**, interligne **18 px**, et **8 px avant la liste**.

- **Colonnes du pré-vol mal dimensionnées — x ≈ 1406–1677, y ≈ 396–699.** Un numéro devient « … » autour de la dixième piste ; à droite, une colonne entière affiche des ellipses. Une troncature sur un numéro court signale une largeur de cellule inadéquate. **Correction :** réserver **28–32 px aux numéros**, une largeur fixe adaptée à la donnée de droite — **56 px si c’est une durée** — et donner le reste au titre, avec **8 px de gap**. Ajouter un en-tête si la nature de la colonne droite n’est pas évidente.

- **“+ 8” trop discret et peu explicite — x ≈ 1406, y ≈ 726.** Ce résumé ressemble à un fragment de liste. **Correction :** afficher « + 8 autres pistes », en texte secondaire lisible, sur une ligne de **24 px**, avec **8 px au-dessus**.

- **Estimation de durée coupée — x ≈ 1406–1680, y ≈ 755.** « Durée annoncée : environ 84:40 (le SP … » perd son explication. **Correction :** afficher la durée sur une première ligne et la précision sur une seconde, avec **4 px d’écart** et retour à la ligne supplémentaire si nécessaire.

- **Choix d’effacement hors fenêtre — x ≈ 1565–1692, y ≈ 793–829.** « Effacer le disque… » dépasse à droite. Une décision importante est présentée par un bouton incomplet. **Correction :** empiler « Ajouter à la suite » et « Effacer le disque… » sur **deux rangées pleine largeur intérieure**, avec **8 px entre elles**, une hauteur minimale de **36 px** et **12 px de padding latéral**.

- **“Revenir au plan” collé et décalé — x ≈ 1416–1554, y ≈ 830–864.** Il touche presque la rangée précédente et commence 10 px plus à droite que les autres commandes. Cette indentation ne correspond à aucune hiérarchie claire. **Correction :** l’aligner sur le même bord gauche et mettre **12 px au-dessus** pour le distinguer des choix de gravure.

- **Retour aux informations du disque sans séparation — y ≈ 864–940.** « 202001 » commence immédiatement sous les actions du pré-vol. La transition entre confirmation et état du disque est abrupte. **Correction :** ajouter **16 px d’espace**, puis un séparateur de **1 px** et **12 px avant le nom du disque**.

- **Contenu inférieur coupé par le bas de la fenêtre — y ≈ 974–983.** La ligne TOC du disque arrive derrière la limite inférieure utile. Aucun défilement de ce panneau n’est clairement perceptible dans la capture. **Correction :** borner le contenu au-dessus de la barre de statut et rendre la partie informative défilante, avec une scrollbar visible de **10–12 px**. Réserver la hauteur des commandes pour qu’elles restent accessibles quand les textes passent sur plusieurs lignes.

**5. Barre de statut — x ≈ 8–1692, y ≈ 983–1012**

- **Informations techniques trop présentes — x ≈ 24–465, y ≈ 999.** « 35 boxes pour 3 lignes visibles · 414/620/694 boxes dans la frame » occupe la zone destinée au bilan utilisateur. Cela concurrence les informations musicales et paraît être un diagnostic de développement. **Correction :** afficher seulement le nombre de pistes dans le rendu normal ; placer les compteurs techniques dans un affichage de diagnostic activable.

- **Texte bas et peu contrasté — toute la barre.** Les caractères sont proches du bord inférieur et certains deviennent difficiles à distinguer du fond. **Correction :** conserver une hauteur de **28–30 px**, centrer verticalement le texte, prévoir **12–16 px de padding horizontal** et employer un gris secondaire plus lisible.

- **Répartition horizontale fragile — x ≈ 800–1677.** Bilan du plan, petite jauge et raccourcis semblent se partager l’espace sans zones clairement protégées. **Correction :** utiliser trois zones : gauche flexible, centre réservé d’environ **300–320 px**, droite flexible, avec **16 px de gap minimum** ; tronquer uniquement les aides secondaires si nécessaire.

- **Jauge miniature peu lisible — I2/I3, x ≈ 972–1122, y ≈ 986–996.** Les nombreux segments deviennent des traits de quelques pixels et sont placés très haut dans la barre. **Correction :** la centrer verticalement, lui donner **8–10 px de hauteur** et **160 px de largeur minimum**, sans libellés internes ; placer **12 px entre résumé et jauge**.

- **Infobulle persistante sur les trois captures — x ≈ 1167–1430, y ≈ 976–1014.** Elle masque les raccourcis et empiète sur la barre de statut. Son bas arrive presque à la limite de la fenêtre. **Correction :** afficher l’infobulle au-dessus du bouton, à **8 px de distance**, avec une marge de sécurité de **12 px dans la zone cliente** ; la fermer dès que le pointeur quitte le contrôle. Limiter sa largeur à environ **280 px**, avec retour à la ligne.

**Priorités**

- **P1 — bloquant visuellement :** faire tenir intégralement les commandes du panneau Disque ; rendre lisibles l’alerte de dépassement et les conséquences du pré-vol ; corriger le débordement inférieur de I3 et garantir l’accès aux actions. Les textes critiques et les boutons ne doivent jamais être elliptiques ou coupés.
- **P2 — gênant :** ajouter les gaps entre rangées de boutons ; élargir Disque et Plan en réduisant Bibliothèque ; redonner de la hauteur à la liste musicale ; corriger les colonnes trop étroites, les résumés de capacité tronqués et les durées non qualifiées ; uniformiser les marges à 12 px.
- **P3 — finition :** harmoniser contraste et typographie, tailles des badges et chevrons, alignement de la barre supérieure, présentation de la légende et de la barre de statut ; retirer les diagnostics du rendu normal et corriger le placement des infobulles.

**Cinq tokens d’espacement à appliquer partout**

| Token | Valeur | Usage constant |
|---|---:|---|
| `space-1` | **4 px** | Écart entre lignes étroitement liées : valeur et précision, titre et sous-titre. |
| `space-2` | **8 px** | Gap entre boutons, entre rangées de boutons d’un même groupe, entre icône et texte. |
| `space-3` | **12 px** | Padding intérieur des panneaux et boutons textuels ; marge de sécurité avant les bords. |
| `space-4` | **16 px** | Séparation entre groupes fonctionnels ; espace entre pochette et métadonnées. |
| `space-5` | **24 px** | Séparation entre grandes sections lorsque la place le permet, sans remplacer les gaps de 8 px à l’intérieur des groupes. |
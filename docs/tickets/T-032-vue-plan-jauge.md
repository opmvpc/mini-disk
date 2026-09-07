# T-032 — Vue Plan : liste réordonnable, modes, titres, groupes, jauge de capacité et barre TOC

Phase 4 · Statut : **fait** · Dépend de : T-031, research/02 §8.2, §9 (jauge au pixel), §11

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

## Livraison

Statut : **fait** (2026-09-07). Tout est mesuré sur la machine de référence (i7-8550U, Intel UHD 620,
Windows 11, 125 %).

### Ce qui a été construit

**`src/app/plan_view.{h,c}` — la moitié pure de la vue.** Toute la géométrie de la jauge et toute la
traduction « geste → commande » vivent ici, sans une seule box ni un seul appel GL, ce qui est
exactement ce que les tests pilotent :
- `plan_gauge_layout(const PlanCapacity *, f32 width, PlanGaugeLayout *)` — un segment par entrée,
  **les bords calculés depuis le total courant de clusters** et non depuis la largeur de chaque
  entrée : la somme des segments vaut la largeur utilisée **au pixel**, quel qu'en soit le nombre.
  Fusion des voisins de même mode et même état sous 2 px (research/02 §9.3), queue hachurée
  proportionnelle au padding de cluster avec un minimum d'1 px, alternance de luminosité entre deux
  voisins de même mode, zone de dépassement à partir de `first_overflow`, largeur bornée (la barre ne
  se comprime jamais pour faire tenir le dépassement — ce serait mentir sur la capacité).
- `plan_gauge_segment_at`, `plan_gauge_time_at`, `plan_gauge_reference_mode` (le mode majoritaire en
  durée, c'est-à-dire l'unité de l'échelle, §9.4).
- Les gestes : `plan_mode_cycle` (SP → LP2 → LP4 → SP mono → SP), `plan_drop_index` (le décalage d'un
  cran quand une ligne descend, puisqu'elle est retirée avant d'être reposée), `plan_fill_count`,
  `plan_shorten_quota`, et les trois gestes multi-entrées `plan_remove_entries`,
  `plan_set_mode_entries`, `plan_shorten_apply`.
- Les bandes verticales de §9.2 sont des macros entières et un `StaticAssert` vérifie **à la
  compilation** qu'elles font 56 dp : en changer une sans changer le total est une erreur de
  compilation, pas un dessin décalé d'un pixel.

**`src/app/view_plan.c` — le panneau.** Liste virtualisée du disque courant : n°, badge de mode
cliquable (clic sur un badge d'une ligne sélectionnée = tout la sélection, en une étape d'annulation),
**titre MD tel qu'il sera écrit** (`plan_toc_preview`) avec un pictogramme ✂ et une info-bulle qui dit
quelle étape de la cascade a coupé (feat. / parenthèses / artiste / troncature) ou que les caractères
ont été adaptés au TOC, source (artiste · album, ou le chemin stocké et « piste manquante » en rouge
quand la piste a disparu), durée, coût en clusters. Groupes repliables avec en-tête éditable (il n'y a
pas de commande « renommer un groupe » : c'est un Ungroup + Group dans le même geste). Drag & drop
interne avec fantôme, ligne d'insertion, autoscroll aux bords et **une seule commande `Move` au
lâcher**. Menu contextuel complet. Clavier de §8.8 : Alt+↑/↓, Suppr, Ctrl+Z/Y, F2, Ctrl+A, Maj/Ctrl
pour la sélection, Ctrl+G / Ctrl+Maj+G, Ctrl+Alt+1/2/3/4, Ctrl+M, Ctrl+S / Ctrl+Maj+S, Ctrl+O.
En-tête : titre de disque éditable, longueur 60/74/80, mode par défaut, Ouvrir / Enregistrer /
Enregistrer sous, point d'autosave. Pied : Ajouter au plan, Remplir l'espace restant, et — seulement
en cas de dépassement — Nouveau disque et les deux politiques de `plan_capacity_split`. Onglets 1..n
sur `plan->discs` dès qu'il y a plus d'un disque.

**La jauge (research/02 §9).** 56 dp, barre de 14 dp, segments colorés par `theme->mode[]` avec
alternance +6 %, hachures sur le padding, initiale de mode dès 16 px de large (§9.9), graduations
majeures/mineures (les mineures disparaissent sous 400 px) dont **la dernière porte la capacité du
média** et le mode de référence, ligne de lecture dans ses cinq états (vide, nominal, ≥ 90 % en ambre,
plein exact en vert, dépassement en rouge), **ligne de lecture au survol** avec la position lue dans le
mode de référence, info-bulle par segment (titre, durée, mode, facturé, padding) et par zone libre
(les trois résiduels), clic sur un segment qui sélectionne la piste et défile jusqu'à elle, animation
de 120 ms des largeurs quand la révision du plan change (et **pas** au redimensionnement, sinon tirer
le splitter traînerait les segments derrière lui). Variante compacte de 12 dp dans la barre de statut.

**La barre TOC (§9.7).** 4 dp, part pistes puis part disque, neutre / ambre à 80 % / rouge au-delà de
100 %, libellé « TOC n / 1785 car. », décomposition au survol, et **« Raccourcir automatiquement »**
qui calcule un quota par titre et applique les résultats de `plan_toc_preview` comme overrides **en un
seul geste annulable**.

**Le cœur, deux ajouts minimes.**
- `plan_batch_begin/end` + `plan_undo_step/redo_step` (`plan_cmd.c`, ~30 lignes) : un geste qui touche
  N entrées reste N commandes — il le faut, chacune porte son propre inverse — mais les suivantes
  portent un drapeau « je continue le geste d'avant » et Ctrl+Z déroule tout le run. C'est ce qui rend
  vrai « Suppr sur 3 lignes = une annulation », sans introduire de commande composite dans le modèle.
- `PlanCapacity` gagne deux colonnes par entrée (`entry_mode`, `entry_padding_ms`), remplies dans la
  passe qui tourne déjà : c'est ce qui permet à `plan_gauge_layout` de garder la signature demandée
  par le ticket (une `PlanCapacity` et rien d'autre).
- `os_dialog_open_file` / `os_dialog_save_file` (`win32_dialog.c`) : seul le sélecteur de dossier
  existait. Mêmes vtables COM écrites à la main, mêmes DLL chargées dynamiquement — **imports toujours
  kernel32 + user32**.

**Supprimé.** Les panneaux Plan et Disque provisoires de `app.c` (≈ 200 lignes) : le panneau Disque
garde la légende des quatre modes avec ce qui resterait dans chacun, et les boutons ; tous les nombres
sont passés dans la jauge, où ils ont un sens.

### Fichiers

| Fichier | Rôle |
|---|---|
| `src/app/plan_view.h` / `.c` | géométrie de la jauge et gestes, purs, testés (nouveau) |
| `src/app/view_plan.c` | le panneau Plan, la jauge, la barre TOC, le panneau Disque (nouveau) |
| `src/app/app_state.{h,c}` | état de la vue (liste, sélection, DnD, jauge animée), disque actif, `--plan` |
| `src/app/app.c` | panneaux provisoires retirés, jauge compacte dans la barre de statut |
| `src/app/view_library.c` | le drag bibliothèque → plan démarre ici |
| `src/app/strings.h` | 61 chaînes FR/EN de plus |
| `src/core/plan/plan_model.h`, `plan_cmd.c` | gestes annulables en un pas |
| `src/core/plan/plan_capacity.{h,c}` | deux colonnes par entrée pour la jauge |
| `src/platform/platform.h`, `win32/win32_dialog.c` | ouvrir / enregistrer un fichier |
| `tests/test_view_plan.c` | 11 cas (nouveau) |
| `tests/bench_main.c` | deux bancs de plus |
| `tools/gen_demo_library.py`, `tools/gen_demo_plan.py`, `tools/capture_demo.ps1` | fixtures de capture |

### Mesures

| Mesure | Valeur | Cible |
|---|---|---|
| Taille exe release | **308 224 octets** (marge 29 696 o sous 330 KB, 50 176 o sous le budget CI de 350 KB) | < 330 KB |
| Imports | kernel32 + user32 (`dumpbin /imports`) | ces deux-là |
| Tests | **144 cas, 5 731 checks, 0 échec** (ASan) — 11 cas et 400 checks ajoutés | verts |
| `plan_gauge_layout`, 254 entrées, barre de 800 px | **6,86 µs** (13 658 cycles) | < 20 µs |
| `plan_gauge_layout`, barre compacte de 120 px | **8,13 µs** | — |
| Frame de la vue Plan, disque plein de 254 entrées, 20 groupes | **407 µs** (meilleure de 300), moyenne 809 µs, **775 boxes** | < 1,5 ms |
| Recalcul capacité + budget TOC, 254 pistes | 30,2 µs (inchangé, +3 µs pour les deux colonnes ajoutées) | < 50 µs |
| CPU au repos, 12 s après 3 s de chauffe, plan de 20 pistes ouvert | **31,25 ms**, soit 0,26 % d'un cœur — le résidu de thread pilote GL de P-005, en baisse par rapport aux 46,9-78,1 ms de T-030 | 0 % sur nos threads |
| Cibles `build.bat` | debug, release, test, check, analyze, bench : **toutes vertes** | vertes |

Le budget de frame explique le 60 fps pendant le glisser-déposer : la frame la plus lourde que la vue
sache produire (254 entrées, 20 groupes, la jauge entière) coûte 0,41 ms de CPU, et le DnD y ajoute
trois boxes (ligne d'insertion, fantôme, surbrillance) — il reste 16,3 ms de marge sur une frame de
16,7 ms.

### Captures

Produites par `tools/capture_demo.ps1` (DPI-aware), sur une bibliothèque et des plans générés par
`tools/gen_demo_library.py` et `tools/gen_demo_plan.py` :

| Capture | Ce qu'elle montre |
|---|---|
| `docs/captures/T-032-plan-20-pistes.png` | 20 pistes, trois groupes repliables, modes SP / mono / LP2 / LP4 mélangés, jauge segmentée avec initiales de mode, ligne de lecture `63:02 / 80:00 · reste 16:58 SP · 33:56 LP2 · 67:52 LP4`, barre TOC à 518/1785, piste manquante en rouge, jauge compacte dans la barre de statut |
| `docs/captures/T-032-depassement.png` | 28 pistes, `96:50 / 80:00 · dépassement 16:50` en rouge, zone de dépassement en fin de barre, bouton **Nouveau disque** apparu |
| `docs/captures/T-032-toc-depassement.png` | 120 pistes, barre TOC rouge et pleine, `TOC 1785 / 1785 car.` en rouge, bouton **Raccourcir automatiquement** |

### Écarts par rapport au ticket, et pourquoi

1. **Les bandes verticales de §9.2.** Le tableau de la section additionne 76 px alors que la section et
   le ticket annoncent 56 px ; c'est la figure à côté du tableau qui tombe juste. Ce sont ses valeurs
   qui sont implémentées (marge 4, barre 14, espace 2, graduations 4, labels 12, espace 2, lecture 12,
   espace 2, TOC 4 = **56**), et le `StaticAssert` interdit d'y toucher sans refaire le compte.
2. **Les hachures ne sont pas à 45°.** Le renderer n'a qu'une primitive : un rectangle aligné sur les
   axes (ADR-005). Le padding est rendu par la teinte du mode à 40 % d'opacité plus des rayures de 1 px
   tous les 4 px — la lecture (« cette part est payée et ne porte pas d'audio ») est conservée. Un
   motif diagonal demanderait un deuxième chemin de shader ; à décider si le lead le juge nécessaire.
3. **Dépassement : zone hachurée, pas de chevron.** Le ticket demande « zone de dépassement rouge
   hachurée depuis `first_overflow` » là où §9.3 décrit un chevron de 12 px plus une bordure rouge. La
   zone et la bordure rouge sont là, le chevron non : il aurait fallu une icône de plus dans l'atlas.
4. **Quota de raccourcissement compté en cellules, pas en caractères.** Le ticket dit « caractères
   libres répartis équitablement » ; le budget se dépense en cellules de 7 caractères, donc un quota
   calculé en caractères peut encore déborder. `plan_shorten_quota` bissecte sur le prédicat exact de
   `plan_toc_cells_for_title` (9 passes pour un disque plein), et le test vérifie qu'après application
   `plan_toc_budget` ne déborde plus.
5. **Répartition automatique : des coupes, jamais un remaniement.** `plan_capacity_split` peut, en
   first-fit, remplir un disque déjà ouvert après en avoir ouvert un autre. On n'applique pas cette
   permutation : on coupe le disque à chaque changement d'affectation, donc l'ordre que l'utilisateur a
   donné est conservé — quitte à ouvrir un disque de plus que la politique n'en comptait.
6. **Pas de capture de DnD en cours.** L'injection de touches (`SendKeys`) n'atteint pas la fenêtre
   depuis la session de capture, et il n'existe pas de chemin d'entrée scriptable. Le DnD est couvert
   autrement : le mapping geste → commande est testé (`view_plan_drop_issues_one_move`) et le budget de
   frame est mesuré. À refaire à la main devant le lead si la capture est exigée.
7. **Ajouts au cœur assumés** : le geste annulable en un pas (`plan_batch_*`, `plan_undo_step`) et les
   deux colonnes de `PlanCapacity`. Sans le premier, « Suppr sur une sélection de 3 = une commande »
   demandait une commande composite dans le modèle, c'est-à-dire une commande qui ne porte plus son
   propre inverse. Sans les secondes, `plan_gauge_layout` aurait eu besoin du `PlanDisc` en plus de la
   `PlanCapacity`, contre la signature demandée.
8. **`--plan <fichier>` en ligne de commande** (comme `--scan` et `--query`, même intention : rejouer
   une session sans personne au clavier), plus deux générateurs dans `tools/` pour les fixtures de
   capture. Rien de tout cela n'ajoute de code produit au-delà des dix lignes de `app_init`.
9. **Hors périmètre, comme prévu** : la variante « progression de gravure » (§9.11) et le repère
   « déjà sur le disque » (§9.4) attendent un appareil, donc la phase 5.
10. **Piège MSVC rencontré** : les deux boucles de remplissage de `plan_capacity_split` sont devenues
    des appels `memset` sous `/GL` dès que les deux nouvelles colonnes ont changé les décisions
    d'inlining (C2268, exactement le piège documenté par CONVENTIONS). Elles sont passées à `mem_set`.

### Revue (lead, 2026-09-07)
- Grille ADR-012 : rebuild `check/test/release/bench` verts, **308 224 o** (marge 21,8 KB sous 330 KB, CI 350),
  144 cas / 5 731 checks, `plan_gauge_layout` 254 segments 6,5 µs, frame vue Plan 254 entrées 408 µs.
  Captures jugées conformes à research/02 §9 : segments par mode avec alternance, hachures de padding,
  zone rouge au dépassement, échelle tri-modale, barre TOC rouge à 1785/1785.
- Écarts acceptés : hachures verticales (pas de diagonale dans le renderer, à revoir en phase 7 avec
  un motif dans l'atlas), quota de raccourcissement en cellules (l'unité réelle), pas de chevron.
- À reprendre en phase 7 (polish) : l'en-tête du plan affiche la somme facturée par mode (« 84:08 »)
  alors que la jauge affiche l'équivalent SP (« 63:02 / 80:00 ») — deux nombres différents pour la
  même chose déroutent ; afficher l'équivalent disque partout. Noté dans le backlog phase 7.
- Merge tel quel, fin de phase 4 : tag `v0.4.0-phase4`.

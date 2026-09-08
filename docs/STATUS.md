# STATUS — où on en est

Dernière mise à jour : 2026-09-08

## Phase actuelle : 7 · Polish — T-070 (taille), T-071 (cohérence UI), T-072 (prefs/thème clair/i18n), T-073 (robustesse) en parallèle
## Phase actuelle (historique) : 3 + 5 en parallèle — T-020, T-021, T-040, T-041 mergés dans main ; T-042 fait et **validé sur le vrai MZ-N505** (le pilote WinUSB est lié, P-001 levé), T-022 en cours

### Fait en phase 7
- **T-075 — polish visuel : espacements, repli des rangées, textes qui tiennent.** Deux jetons de
  thème (`control_h` 28 dp, `row_control` 36 dp) et un bouton qui ne fait plus la hauteur de sa
  rangée : les deux causes racines des boutons collés. Nouveau `UI_Flow` dans `ui_core` — un axe X
  qui passe à la ligne, mesuré dans les deux mêmes passes que le reste du layout, quatre `u8` dans
  le padding de queue de `UI_Box` (256 octets inchangés) — et `app_button_row()` par lequel passent
  les 14 rangées de boutons des vues Disque, Plan, Transfert et Préférences. Transport du disque en
  cinq icônes (`Pause`, `Stop`, `Prev`, `Next` ajoutées à `r_icons`, toujours en polygones), panneau
  Disque rogné **et défilant**, messages du pré-vol en multi-ligne, bulle qui bascule au-dessus de
  son ancre, compteurs de boxes déplacés dans l'overlay F11, panneaux Disque 360 dp et Plan 400 dp
  minimum. Jauge en trois bandes : la ligne « reste 16:18 SP · 32:36 LP2 · 65:12 LP4 » est entière.
- **T-070 — régime de taille.** `build.bat map` (nouvelle cible) + `tools/size_report.py` : la carte
  du linker agrégée par module, top 40 des symboles, `--diff` entre deux mesures. Exe release
  **636 928 → 622 080 o** (−14 848, −2,3 %) : `#pragma optimize("s") + inline_depth(1)` (`MdCold`)
  sur les seules unités qu'**aucune ligne de banc ne traverse** (protocole NetMD, dialogues, USB,
  Media Foundation, `plan_cmd`, `codec_mf`, `transfer`, `app_state`), `/INCLUDE:codec_open` retiré
  (il valait 0 octet depuis T-043) et `--selftest` qui **ouvre un vrai WAV par `codec_open`** pour
  garantir que `/OPT:REF` ne mange pas les décodeurs. `SIZE_BUDGET_KB` : 700 → **630**.
- **La cible de 500 KB n'est pas atteinte, et c'est mesuré** : `/O2 /Os` global la donnerait
  (491 520 o) mais coûte +44 % au layout — hors budget — et +47 % au texte ; `/Os` sur les décodeurs
  tiers vaut 36 864 o mais **n'a pas pu être départagé** (machine à 100 %, trois autres agents :
  deux exécutions du *même* binaire `/O2` ont donné `codec_ogg` 306× puis 182×, R128 735× puis 331×).
  `DR_FLAC_NO_CRC` vaut 20 992 o et retire la détection de corruption FLAC (ADR-012) : refusé.
  Trois portes chiffrées vers 500 KB sont posées dans la Livraison de T-070, **arbitrage lead**.
- **`build.bat bench` est rouge aujourd'hui pour cause de charge machine, pas à cause de T-070** : il
  s'arrête sur `r_core batch build` (1 290 µs pour un budget de 900) alors que `src/ui/r_core.c`
  n'est pas dans le diff et que le même banc rendait 705-769 µs le matin. **À relancer machine au
  repos avant le merge**, et à ce moment-là trancher le `/Os` du code tiers.
- **T-072 livré : préférences, thème clair réel, i18n complète, raccourcis et aide clavier.**
  `src/app/view_settings.c` est un panneau modal (`Ctrl+,` ou l'engrenage de la barre d'outils) posé sur
  `UI_Layer_Popup` derrière un voile qui mange les clics, et qui **possède le clavier** tant qu'il est là
  (`ui_popup_set_active`) : les trois listes en dessous cessent de lire les touches. Quatorze de ses seize
  lignes sont une table (`app_setting_rows[]` : libellé, type, champ de `Prefs` par `OffsetOf`, pas,
  bornes, unité) — langue, thème, mode MD par défaut, cible de loudness, plafond true-peak, rognage,
  fondus, blanc entre les pistes, tailles des deux caches ; écrites à la main seulement celles qui lisent
  le disque ou portent un verbe (occupation des caches + « Vider », dossiers surveillés, vignettes).
  **Langue et thème s'appliquent à chaud** : aucune boîte ne met une couleur en cache d'une frame à
  l'autre, donc c'est un appel et un redessin, sans redémarrage ni relayout manuel.
  `ui_theme_light` vient de research/02 §10.4 et 02b **avec chaque paire fond/texte mesurée** : deux
  échelles descendues d'un cran pour passer 4,5:1 partout, ombre à 18 % au lieu de 45 %. Le thème
  **sombre** corrige au passage `fg_muted`, qui ne donnait que 3,9:1 (→ 5,5:1). Le choix « système » est
  résolu par `os_system_theme()` (`AppsUseLightTheme` via `RegGetValueW` **chargé dynamiquement**, pour
  que la table d'imports reste kernel32 + user32) et réévalué sur `WM_SETTINGCHANGE` ;
  `DwmSetWindowAttribute` assortit la barre de titre.
  i18n : 85 chaînes ajoutées **en fin** d'énumération et des deux tables (aucun identifiant déplacé),
  319 au total, pluriels à deux formes (`app_plural`) et formats par langue (espace fine insécable U+202F
  en FR, virgule en EN ; dates et durées). `tools/i18n_audit.py` est branché sur `build.bat check` et
  refuse toute chaîne visible hors `strings.h` (sauté avec un message si Python manque, pour que le check
  reste vert sur une machine MSVC nue).
  `src/app/app_shortcuts.{h,c}` : **26 lignes** (touche, modificateurs, contexte, action, libellé) qui
  sont research/02 §8.8 recopié, avec **trois lecteurs pour une seule table** : le dispatch de `app.c`,
  l'aide clavier (`F1`) et les libellés de la barre de statut.
  Onze clés de prefs nouvelles, lecture tolérante (ADR-012), bornées à l'analyse par les mêmes constantes
  que le panneau ; `PREFS_VERSION` reste à 1, un fichier d'avant T-072 prend simplement les défauts.
  Écarts : exe **+27 136 o** au lieu de ± 15 360 (le prix d'un panneau complet + une aide clavier, détail
  dans la Livraison) ; `accent_fg` sur `accent` en thème sombre à 3,26:1, tenu au 3:1 de WCAG 1.4.11 avec
  le raisonnement écrit dans le test ; le formatage localisé des nombres n'est appliqué que là où T-072 a
  le droit d'écrire (`view_library.c` et `view_plan.c` sont tenus par d'autres agents, l'adoption est
  mécanique). Captures : `captures/T-072-preferences.png`, `T-072-theme-clair.png`,
  `T-072-aide-clavier.png`.
- **Budget de taille** (lead, 2026-09-08, après le merge de T-072) : 622 080 o après T-070, **649 216 o**
  après T-072 (+27 136 : panneau Préférences, aide clavier, 85 chaînes × 2 langues, table des raccourcis,
  thème clair). Ce sont des fonctions, pas une régression de compacité. `SIZE_BUDGET_KB` 630 → **660** ; la
  règle T-070 reste « tendance à la baisse à fonctions égales », et le levier `/Os` sur le code tiers
  (−36 KB) est toujours à trancher sur un banc au repos avant le tag de phase 7.
- **T-071 — cohérence UI et défauts des phases 3-5** (worktree `t071`, 2026-09-08) :
  - **Un seul état pour le panneau Disque** (`src/app/device_panel.h`) : `NetmdPanelState` calculé une
    fois, lu par le sous-titre d'en-tête **et** par le corps. L'incohérence du 2026-09-07 (« pilote
    manquant » en en-tête sur un corps « utilisé par une autre application ») ne peut plus se produire :
    il n'y a plus deux jeux de `if`, il y a une table, et la table est testée.
  - **Durée du plan en équivalent disque partout** (`plan_disc_used_ms`) : en-tête, barre de statut,
    bulles et pré-vol disent tous ce que dit la jauge — clusters × 2 s, lien compris. La somme facturée
    par mode est passée dans la bulle. Le plan de démonstration passe de « 84:48 » à « 63:42 »
    au-dessus d'une jauge qui a toujours dit « 63:42 / 80:00 ».
  - **Hachures à 45°** : motif de 8 px généré au démarrage dans l'atlas R8 (pas de PNG), appliqué en
    texture répétée aux queues de padding et à la zone de dépassement. **0 draw call de plus** : les
    tuiles échantillonnent la texture que le back end lie déjà pour les quads non texturés. La variante
    à rayures verticales, écart accepté en T-032, disparaît.
  - **Liste du panneau Disque défilable** (`UI_List`) : molette, clavier, virtualisation, autoscroll en
    DnD — les conventions de la liste du plan. L'écart T-021/T-022 est fermé.
  - **P-014 fermé** : bit `written_here` posé au `commitTrack`, réapparié après relecture par (durée à
    la frame, titre) et non par index ; la simulation avertit au lieu de refuser et laisse l'effacement
    se tenter. Validé sur le MZ-N505 : une piste de test de 5 s gravée puis effacée dans la même
    session, les 8 pistes de l'utilisateur intactes, le titre du disque inchangé
    (`tests/netmd/real/t071_device_p014.trace`, 1 816 lignes).
  - Petits défauts : bulle du badge de mode, ellipse sur le nom des groupes (nom et compteur en deux
    cellules), anneau de focus des boutons de transport (l'écart de 4 dp le faisait repeindre par le
    bouton suivant), « (sans titre) » en italique atténué (cinquième style de police).
  - Captures `docs/captures/T-071-{avant,apres,hachures}.png`.

## KPI — phase 7, T-075 (i7-8550U, 4 cœurs / 8 threads, Windows 11 ; **machine partagée avec l'agent T-073 pendant toutes les mesures**, meilleur de trois passes)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **655 872 o**, soit **+512 o** sur les 655 360 o de `main` (654 336 avant la revue, **+1 536 o** pour les trois correctifs) | ± 10 240 o | 2026-09-08 |
| Imports | kernel32 + user32 (`dumpbin /imports`) | ces deux-là | 2026-09-08 |
| Tests | **273 cas, 8 335 checks**, 0 échec (ASan) — +5 cas (`ui_flow` ×2, bulle, puis le partage des hauteurs et le retour à la ligne) | verts | 2026-09-08 |
| Cibles `build.bat` | **les six vertes** : debug, release, test, check, analyze, bench | six vertes | 2026-09-08 |
| Layout de la vue Plan | **305 µs** par frame (254 entrées, meilleur de 3 × 300) contre **317 µs** sur `main` : **−3,8 %** | ≤ +5 % | 2026-09-08 |
| Boxes de la frame du plan | 913 → **903** (le repli n'ajoute pas de boîte : il replace celles qui existent) | pas plus | 2026-09-08 |
| `ui_layout` 10 000 boxes | 431 µs (435 µs sur `main`) | ≤ +5 % | 2026-09-08 |
| Taille de `UI_Box` | **256 o**, inchangée (les 4 paramètres du repli tiennent dans le padding de queue) | ≤ 256 o | 2026-09-08 |
| Écart entre deux rangées de boutons | **0 px → 8 px** ; rangée 28 dp → **36 dp**, bouton centré | ≥ 8 px | 2026-09-08 |
| Largeur minimale des panneaux | Disque **200 → 360 dp**, Plan **280 → 400 dp** | tout tient à 1100 × 700 dp | 2026-09-08 |
| Débordement à 1100 × 700 dp (1375 × 875 px à 125 %) | **aucun** : aucun bouton coupé, aucun texte important tronqué (capture relue) | 0 | 2026-09-08 |
| Hauteur de la liste de pistes à 1100 × 700 dp | **194 dp** (détail ramené à son minimum de 96 dp, navigateur à 183) | ≥ 160 dp | 2026-09-08 |
| CPU au repos, 12 s après 4 s de chauffe | **15,6 ms**, soit 0,13 % d'un cœur — résidu de thread pilote GL (P-005), machine partagée | 0 % sur nos threads | 2026-09-08 |
| Chaînes i18n | **5 FR + 5 EN** ajoutées en fin de table (TOC en deux lignes, groupe sans nom, « + N autres pistes », occupation du cache) | ADR-011 D10 | 2026-09-08 |

## KPI — phase 7, T-071 (i7-8550U, 4 cœurs / 8 threads, Windows 11 ; MZ-N505 sous WinUSB, disque « 202001 »)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **643 072 o**, soit **+6 144 o** sur les 636 928 o de la **base de la branche** (`73e366b`, avant la fusion de T-070 qui ramène `main` à 622 080 o) | ± 6 Ko — tenue à la limite exacte ; **à revérifier après fusion avec T-070** | 2026-09-08 |
| Imports | kernel32 + user32 (`dumpbin /imports`) | ces deux-là | 2026-09-08 |
| Tests | **256 cas, 6 956 checks**, 0 échec (ASan) ; 6 985 checks avec `--device-p014` | verts | 2026-09-08 |
| Cibles `build.bat` | **les six vertes** : debug, release, test, check, analyze, bench — bench au deuxième essai, machine libérée (P-010 rencontré à deux points d'arrêt différents pendant que trois agents compilaient) | six vertes | 2026-09-08 |
| Draw calls des hachures | **+0** — 20 segments et leurs queues hachurées tiennent en **1 batch** | 0 de plus | 2026-09-08 |
| Boxes de la frame | 622 → 642 (plan de 20 pistes, disque de 8 pistes) | — | 2026-09-08 |
| CPU au repos, 12 s après 4 s de chauffe | **46,9 ms**, soit 0,39 % d'un cœur — résidu de thread pilote GL (P-005), inchangé | 0 % sur nos threads | 2026-09-08 |
| En-tête du plan == jauge | vérifié sur 5 plans de 16 pistes mêlant SP / mono / LP2 / LP4 | égalité exacte | 2026-09-08 |
| État du panneau Disque | table de 8 entrées → 1 état + 3 identifiants de chaîne, gardes sur le cardinal des deux énumérations | une seule source | 2026-09-08 |
| P-014 sur l'appareil | piste de test 5 s gravée (piste 8, 2 560 frames), réappariée (`written_here = 1`), simulation **autorisée** (refus 0), effacée en 1 écriture TOC, relecture à **8 pistes**, titre « 202001 » intact | l'effacement passe | 2026-09-08 |
| Transcription appareil | `tests/netmd/real/t071_device_p014.trace`, **1 816 lignes** | 1 capture | 2026-09-08 |
| Chaînes i18n | **11 FR + 11 EN** ajoutées en fin de table, 1 supprimée (`Str_PlanGroupHeader`, sans appelant) — solde net +10 | ADR-011 D10 | 2026-09-08 |

### Fait (phase 0 terminée)
- 4 rapports de recherche livrés (`research/01..04`, ~10 700 lignes) + tokens de design (`02b`).
- Device identifié : **Sony MZ-N505** (PID 0084, Type-R). Driver WinUSB à installer via Zadig (P-001).
- ADR-001..012 acceptés (`decisions/README.md`) : architecture 3 couches, no-CRT, GL 3.3, UI Fleury,
  renderer SDF, texte DirectWrite, codecs + DSP, WinUSB rejouable, ATRAC3 clean-room, persistance binaire,
  décisions produit D1-D10, qualité (tests / analyse statique / zéro défensif).
- CI GitHub Actions (`.github/workflows/ci.yml`) : check → test → release-build (budget taille,
  `--selftest`) → release sur tag `v*`. **Rouge tant que T-001 n'a pas livré `build.bat`.**
- Backlog phases 1-8 (`tickets/BACKLOG.md`), T-001 détaillé.
- Repo GitHub : https://github.com/opmvpc/mini-disk

### En cours
**Phase 5 terminée, tag `v0.5.0-phase5` à poser par le lead.**
- **Phase 5** : T-040, T-041, T-042 et T-043 livrés. La chaîne complète **fichier → décodeur → pipeline →
  cache → session sécurisée → disque** tourne sur le vrai MZ-N505 : trois pistes générées (5 s, 25 s, 61 s)
  gravées deux fois, la seconde passe **sans le moindre transcodage** (968 µs de succès de cache contre
  323 ms de rendu), puis effacées par le chemin d'édition de T-022, les 8 pistes de l'utilisateur intactes
  et son titre de disque jamais touché. Reste au lead : le tag, et l'arbitrage de
  `PLAN_TRACK_OVERHEAD_CLUSTERS` (voir la Livraison de T-043).
- **Phase 3** : T-020, T-021 et T-022 mergés. Pilote WinUSB installé (P-001 clos), lecture validée sur le vrai
  MZ-N505 (disque « 202001 »). P-013 est **levé de fait** : T-043 a écrit et effacé des pistes et des titres
  sur le disque réel par le chemin simulation → sauvegarde du TOC → écriture. Restent les captures
  vierge/protégé/aucun disque (P-012), puis tag `v0.3.0-phase3`.
- **Budget de taille** : exe à **636 928 o** après T-043 (601 600 avant, **+35 328** pour le cache, la
  machine d'états, la vue Transfert et 45 chaînes FR/EN) ; cible du ticket 660 Ko tenue, `SIZE_BUDGET_KB`
  à 700 ; ticket de régime en phase 7 (backlog), objectif < 500 KB.
- **P-014 fermé (T-071)** : une piste que nous venons d'écrire porte `written_here`, réapparié après relecture
  par (durée à la frame, titre) ; la simulation avertit au lieu de refuser et laisse l'effacement se tenter.
  Validé sur le MZ-N505 : une piste de test gravée puis effacée dans la même session.
- UI : splitters verticaux ajoutés (navigateur artistes/albums, panneau Détail), hauteurs persistées.
- **Phase 3** : T-020 et T-021 mergés. Tout ce qui touche le vrai MZ-N505 (ouverture WinUSB, ping, captures
  `--netmd-trace`, chronométrage) attend le pilote : procédure dans `tools/zadig/README.md` (P-001, P-012).
  T-022 (édition du disque) en cours.
- **Phase 5** : T-040 (décodeurs) et T-041 (DSP + pipeline) mergés, T-042 (session sécurisée +
  upload SP) livré et validé sur l'appareil. Reste T-043 (vue Transfert + cache).
- **Budget de taille** : exe à **575 488 o** après T-042 (469 504 avant) ; le saut vient du pipeline et
  du DSP de T-041 qui deviennent atteignables depuis `app/` par le bouton « Graver », pas de DES.
  `SIZE_BUDGET_KB` reste à **600**, à resserrer en phase 7 (levier `/O1`, P-007).
- Ordre de merge du 2026-09-07 : T-020 → T-041 → T-040 → T-021 (conflits d'includes résolus en gardant les deux côtés).

## KPI — phase 7, T-072 (i7-8550U, 4 cœurs / 8 threads, Windows 11, machine au calme)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **664 064 o** (636 928 o avant T-072), **+27 136 o** — **hors cible**, cf. écart 1 de la Livraison | ± 15 360 o | 2026-09-08 |
| Imports | kernel32 + user32 (table d'import lue à la main ; advapi32 et dwmapi chargés dynamiquement) | ces deux-là | 2026-09-08 |
| Tests | **261 cas, 8 090 checks**, 0 échec (ASan) — +12 cas, +1 242 checks | verts | 2026-09-08 |
| Cibles `build.bat` | debug, release, test, check (audit i18n compris), analyze, bench **toutes vertes** | vertes | 2026-09-08 |
| Contraste, thème clair | **toutes** les paires ≥ 4,5:1, minimum **4,75:1** (`danger` sur `control` et sur `panel`) | ≥ 4,5:1 | 2026-09-08 |
| Contraste, thème sombre | textes ≥ 4,5:1, minimum **4,66:1** (`fg_muted` sur `row_hover`, contre 3,9:1 avant T-072) ; seule exception `accent_fg` sur `accent` à **3,26:1** (blanc sur `#0090FF`, ADR-011 D8), tenu au 3:1 de WCAG 1.4.11, cf. écart 2 | ≥ 4,5:1 | 2026-09-08 |
| Audit i18n | **16 fichiers** de `src/app/` relus par `build.bat check`, **0** chaîne visible en dur | 0 | 2026-09-08 |
| Chaînes | **319** identifiants `Str_*` (+85), 2 langues, pluriels à deux formes | ADR-011 D10 | 2026-09-08 |
| Raccourcis | **26 lignes**, 0 collision de touche par contexte, 3 lecteurs pour 1 table | 0 doublon | 2026-09-08 |
| CPU au repos, 12 s après 4 s de chauffe | **0 ms**, soit **0,000 %** d'un cœur (le résidu de thread pilote GL de P-005 ne se voit plus machine au calme) | 0 % | 2026-09-08 |

### Fait en phase 3
- **2026-09-07, validation sur le vrai MZ-N505** (WinUSB via Zadig, P-001 clos) : énumération → `Ready`,
  ouverture WinUSB, session complète de lecture capturée (`tests/netmd/real/mzn505_session1.trace`, 304
  commandes, chaque réponse en `0x09`), disque « 202001 » lu et affiché : 8 pistes, 24:10 / 80:59, 56:44
  restantes, groupe 1-8, transport et éjection proposés (`captures/T-021-disque-reel.png`). Première
  observation pour `MD_MODE_TABLE` (T-042/T-045) : le device annonce **80:59** de capacité totale pour un
  disque « 80 min », et enregistré + libre = 80:54 (5 s d'écart = arrondi aux clusters sur 8 pistes) ;
  à recouper après un upload SP chronométré.
- T-022 livré : **édition du disque — renommer, déplacer, effacer, grouper, avec simulation,
  sauvegarde du TOC et verrou d'éjection**. `src/core/netmd/netmd_edit.{h,c}` porte les écritures de
  research/01 §3.9 et §3.12 : `oldLen` **relu sur l'appareil juste avant chaque écriture** (un
  `oldLen` faux corrompt le TOC, piège 9), court-circuit si le titre est identique (piège 10),
  `wchar` `0x00/0x01` pour le disque et `0x02/0x03` pour les pistes (piège 8), chemin Sharp par
  `audioUTOC1TD`, ronde `close → openWrite → écriture → close → openRead → close` (§3.9 pt 3),
  `acquire`/`release` sur **tous** les chemins (§3.5), effacements par index décroissant (piège 17),
  temporisations §6.2 (100 ms entre éditions, 500 ms avant re-listage) et l'ordre de §6.5 : **une
  seule** réécriture du titre disque à la fin, jamais une par piste. `netmd_utf8_to_sjis` encode en
  Shift-JIS sans table, l'assainissement de `plan_toc` ayant déjà réduit le texte à l'ASCII et au
  katakana demi-chasse.
- **Rien ne s'écrit sans simulation ni sauvegarde (ADR-011 D4).** `netmd_edit_simulate` produit,
  sans un octet d'USB, le `DiscLayout` d'après et un `DiscDiff` (avant/après, pistes, groupes,
  cellules TOC, caractères libres, nombre d'écritures) : c'est ce que le panneau de confirmation
  affiche, avec un verbe explicite (« Effacer 3 pistes »). Huit refus typés avec leur phrase
  (disque protégé, piste protégée, budget dépassé, titre identique, piste déjà groupée…).
  `netmd_backup.{h,c}` écrit le TOC courant en texte relisible dans
  `%LOCALAPPDATA%\minidisk\toc-backups\<disc-id>-<date>.txt` (écriture atomique) **avant** toute
  écriture ; l'échec de la sauvegarde annule l'édition.
- **Le budget de titres est celui du plan.** `plan_toc_compile_disc_title` est factorisée en
  `plan_toc_compile_raw(titre, groupes, budget)` : le plan et le disque compilent la même chaîne
  `0;Titre//1-4;Face A//` et comptent les mêmes 255 cellules de 7 caractères (D3). Aucun second
  budget n'a été écrit.
- Thread device : une commande `Edit` par geste, toujours la même séquence **simuler → sauvegarder
  → écrire → relire le disque → publier**. Le drapeau `toc_dirty` (§6.3) est levé à la première
  écriture, voyage dans tous les événements, et ne retombe que lorsque l'appareil a redécrit son
  disque, ou à l'éjection. Vue Disque : sélection multiple, renommage inline (F2 / double-clic,
  Entrée / Échap) des pistes **et** du titre disque, glisser-déposer, Suppr, Ctrl+G / Ctrl+Maj+G,
  panneau de confirmation, bannière « Ne pas éjecter : écriture du TOC », et `app.c` qui **refuse
  de fermer** tant qu'elle est là. 36 chaînes FR/EN.
- **Le vrai MZ-N505 lit enfin** : WinUSB est lié (P-001 levé), `--netmd-trace` a capturé une session
  complète — 88 commandes, disque de 8 pistes, titre brut `0;202001//1-8;//`, flags `0x10`. La
  capture est versionnée (`tests/netmd/mzn505_real_read.trace`) et rejouée par les tests. **Aucune
  écriture n'a encore été émise vers un disque réel** : P-013, avec sa procédure de levée en sept
  étapes sur un disque de test dédié.
- T-020 livré : **WinUSB — énumération, ouverture, control transfers, hotplug, écran « pilote
  manquant »**. `platform.h` gagne le contrat USB (`os_usb_enumerate/open/close/control/bulk_write/
  bulk_read/reset`, `OsUsbDeviceInfo { vid, pid, state, problem_code, path, bus_name }`,
  états `Ready` / `NoDriver` / `InUse`) et `os_open_url`. `src/platform/win32/win32_usb.c` charge
  `winusb.dll`, `setupapi.dll`, `cfgmgr32.dll` et `advapi32.dll` par `LoadLibraryW` (imports
  toujours kernel32 + user32), énumère les **nœuds** de l'énumérateur USB — un device en code 28
  n'expose aucune interface, et c'est justement celui qu'il faut montrer (P-001) —, lit le
  ProblemCode par `CM_Get_DevNode_Status`, et résout le chemin d'interface par le
  `DeviceInterfaceGUIDs` que Zadig a écrit sous `Device Parameters` (**aucun GUID en dur**).
  Politiques de pipe de research/01 §2.3 : `PIPE_TRANSFER_TIMEOUT`, `AUTO_CLEAR_STALL`,
  `SHORT_PACKET_TERMINATE` off, `IGNORE_SHORT_PACKETS` off, `AUTO_SUSPEND` off.
- `src/core/netmd/` : la table des 47 PIDs (`netmd_models`, seule identification fiable du modèle —
  tous les baladeurs répondent « Net MD Walkman »), `UsbTransport { control, bulk_write, bulk_read,
  user }` (ADR-008) avec ses deux implémentations — WinUSB dans `platform/` et **rejeu de
  transcription** (`netmd_replay.c` : `# commentaire`, `> hex`, `< hex`, `! timeout`, divergence
  détectée octet à octet et définitive) —, et le **thread device** (`netmd_device.c`) avec sa file
  de commandes et sa file de résultats sur le modèle de `lib_events` : aucun appel USB hors de ce
  thread, réveil par sémaphore, 0 % CPU au repos. Le « ping » est la séquence poll `0x01` /
  send `0x80` / read `0x81` de research/01 §2.5-2.7, avec `poll[1]` comme bRequest de lecture et
  backoff exponentiel plafonné.
- Hotplug : `RegisterDeviceNotification(DBT_DEVTYP_DEVICEINTERFACE, toutes classes)` dans
  `win32_window.c` → `OsEvent_DeviceChange` ; le thread device **débounce 200 ms**, avale la rafale
  et réénumère une seule fois. Panneau Disque (`src/app/view_device.c`) : « Aucun appareil » /
  « Appareil détecté, pilote manquant » (3 étapes Zadig + bouton `zadig.akeo.ie`) / « Connecté :
  Sony MZ-N505 », 13 chaînes FR/EN.
- **Validé sur le vrai MZ-N505 côté NoDriver** : `054c:0084 → « Sony MZ-N505 », state NoDriver,
  ProblemCode 28, bus « Net MD Walkman »`. L'ouverture, le control transfer et le chronométrage
  branchement/débranchement restent **en attente de Zadig** (P-001).

### Fait en phase 5
- **T-045 livré : la jauge cesse de sous-estimer d'un cluster par piste.** Les sept mesures de T-043
  sur le vrai MZ-N505 (2026-09-07, **+2 007 ms ± 80 par piste SP**) disent qu'une piste coûte
  `ceil(durée / 2 s) × 2 s` **plus un cluster** : `PLAN_TRACK_OVERHEAD_CLUSTERS 1u` entre dans
  `plan_capacity.h` avec sa mesure, et `plan_clusters_for` le facture. Le surcoût est appliqué à tous
  les modes comme **un cluster de disque de 2 000 ms**, parce que ce qu'il paie est un cluster de
  lien/TOC du disque et pas de l'audio — une piste LP4 coûte donc 8 000 ms d'audio + 2 000 ms de
  disque. Conséquence : `plan_padding_ms` et `entry_padding_ms` gardent leur sens (les millisecondes
  d'audio que l'arrondi gaspille, **le lien exclu**) et la jauge ne hachure que celles-là, le lien
  élargissant le segment ; `billed_ms` inclut le lien puisque c'est ce que le disque est facturé, et
  `padding_ms` devient la somme explicite des `entry_padding_ms` (`billed - audio == padding +
  entry_count × 2 000 ms`, épinglé par un test). Le pavé `MD_MODE_TABLE` passe de « à valider sur
  l'appareil » à « **validé pour le SP le 2026-09-07** », les tailles de cluster LP2/LP4/mono restant
  non mesurées faute d'encodage autre que SP en v1. Tous les appelants relus : la géométrie de la
  jauge et `plan_fill_count` sont justes sans changement, la bulle par piste passe par le nouveau
  `plan_billed_ms_of` (sinon le cluster de lien d'un segment LP4 vaudrait 8 000 ms) et dit
  désormais « **+1 cluster de lien** », `view_device` cesse de compter le lien comme du silence, et
  `netmd_upload_check_capacity` refusait un cluster par piste trop tard. Un cas de test épingle la
  mesure elle-même : 5 s, 25 s et 61 s en SP coûtent **4, 14 et 32 clusters** (8 000 / 28 000 /
  64 000 ms), chacun dans le pas de 2 000 ms des 8 029 / 28 102 / 63 890 ms relevés sur l'appareil.
  **P-014 reste ouvert** (voir la fiche : il faut un bit de session sur `NetmdTrack` qui survive au
  rafraîchissement du TOC, bien plus de 30 lignes, et l'appareil pour le valider). Détail, table de
  mesures et liste complète des attentes recalées dans `tickets/T-045-capacite-surcout-piste.md`.
- **T-043 livré : la gravure devient une étape visible, et le transcodage ne se paie qu'une fois.**
  `pipeline_cache.c` garde les octets SP rendus dans `<cache>\transcode\<clé>.pcm` ; la clé est un
  FNV-1a du chemin, de la taille et de la mtime du source **plus** un condensé des douze paramètres du
  pipeline repliés champ par champ (les flottants par leur motif binaire, jamais la struct en bloc) —
  il n'y a donc aucune étape d'invalidation, un source réencodé ou un gain déplacé se hache ailleurs.
  En-tête de 64 octets validé et recoupé avec la taille réelle du fichier, écriture par temporaire puis
  `MoveFileEx` atomique, en-tête estampé à la fin par le nouveau `os_file_write_at`. `cache_lru.c` borne
  les **deux** caches disque du programme par une seule fonction — 2 Go pour le transcodage (avec son
  index d'accès `access.idx`), 256 Mo pour les pochettes de T-014, datées par leurs fichiers — et la
  purge tourne au démarrage et après chaque gravure, jamais pendant. `transfer.c` porte le pré-vol de
  D4 (ce qui sera écrit avec les **titres finaux**, clusters et cellules TOC avant/après, temps libre
  **pris sur l'appareil**, huit avertissements, politique ajouter/effacer) et la machine d'états du
  transfert (huit états par piste, neuf phases, douze événements), plus l'ETA honnête de D9 : moyenne
  glissante sur 30 s, débit nominal SP avant le premier mégaoctet, et **une valeur qui ne remonte
  jamais**. `view_transfer.c` dessine les trois écrans — bouton, pré-vol, transfert — avec les deux
  barres, le débit, le temps écoulé, Pause entre deux pistes, Annuler qui énonce ce qu'il laisse sur le
  disque, Reprendre, le bandeau « ne pas éjecter », la fermeture refusée et le journal
  `<cache>\logs\transfer-<date>.txt` ; le transcodage court **devant** l'envoi (3 jobs en vol) grâce à
  un rappel `NetmdUploadPrepareFn` de dix lignes ajouté à T-042, qui fait attendre le thread device une
  piste et une seule. La jauge acquiert sa couche « progression de gravure » de research/02 §9.11.
  **Sur le vrai MZ-N505** : 5 s, 25 s et 61 s gravées, relues, regravées sans transcodage, puis effacées
  par le chemin de T-022 — et la mesure que T-042 réclamait, sept points, **+2 007 ms par piste** en
  moyenne, soit exactement un cluster SP de plus que l'audio (le pas de 2 000 ms, lui, est confirmé à la
  milliseconde). Un bug **grave** trouvé sur le matériel et corrigé : `str8_find` d'une aiguille dans une
  botte de foin vide répond 0, donc les 8 pistes **sans titre** de l'utilisateur passaient le test
  « le titre commence par MINIDISK TEST » et le masque d'effacement couvrait tout le disque ; la session
  a été tuée avant la phase de nettoyage, rien n'a été perdu, et le garde-fou a son test de
  non-régression. 15 cas de test, exe 636 928 o.
- T-042 livré : **la session sécurisée, DES maison, l'upload SP et le titrage**. `netmd_des.c` porte
  DES, 3DES et le MAC ISO 9797-1 alg. 3 écrits depuis les tables de la FIPS 46-3 et de rien d'autre
  (ADR-008) ; les 34 Ko de tables dérivées — boîtes SP et les trois permutations en tables indexées
  par octet — sont **construites au premier usage** sous un verrou CAS, pas figées dans l'exe.
  Vérification croisée à trois : les tables normalisées, un DES en Python pur
  (`tools/netmd_crypto_check.py`, d'où sortent toutes les constantes NetMD du test C) et **Windows
  CNG** (`--cng`), qui confirme les dix vecteurs à réponse connue, l'exemple ECB de la FIPS 81 et le
  bloc 3DES. `netmd_secure.c` fait toute la séquence de research/01 §4 — démontage préventif,
  acquire, `setTrackProtection`, `enterSecureSession`, leaf ID, EKB open-source `0x26422642`,
  échange de nonces, clé de session par retailMAC, `setupDownload` (contentID + KEK en DES-CBC de
  32 o), paquets **DES-CBC chaînés d'un paquet à l'autre** avec la clé de données obtenue par
  *déchiffrement* ECB sous la KEK, `totalBytes = frameSize × frames + 24`, `commitTrack` — et un
  **chemin de nettoyage garanti sur toutes les sorties** (succès, erreur, annulation) : forget,
  leave, release, relecture de l'état. Le nonce hôte et la clé de paquet sont **injectables**, ce qui
  rend une transcription de download rejouable octet par octet malgré le chiffrement ; trois
  transcriptions sont rejouées en test (nominal, annulation, reprise). `netmd_upload.c` orchestre le
  plan : capacité relue **sur le device** avant de commencer, une piste à la fois, **titre puis
  commit** (§4.12), titre de disque écrit une seule fois à la fin (§6.5, il porte les groupes),
  annulation entre deux paquets, **reprise** (les pistes déjà committées sont conservées, jamais
  effacées « pour faire propre »), `os_power_keep_awake` pendant la gravure et un drapeau
  « gravure en cours ». Thread device : commandes `UploadPlan` / `CancelUpload`, événements
  `UploadProgress` / `TrackDone` / `UploadDone` / `UploadError` ; le bouton « Graver le disque » les
  poste et une ligne de progression apparaît dans le panneau Disque (la vraie vue Transfert est
  T-043). Le rejeu a trouvé un vrai bug : la charge utile d'une réponse sécurisée commençait un
  octet trop tôt, décalant le nonce du device et donnant une clé de session fausse.
  **Validé sur le vrai MZ-N505** : le pilote WinUSB est enfin lié (P-001 levé) et un sinus de 10 s
  en SP est arrivé sur le disque avec son titre, relu à 10 000 ms exactement en encodage SP, sans
  rien effacer. Écarts assumés (détail dans le ticket) : exe à 575 488 o parce que le pipeline de
  T-041 entre enfin dans l'image, paquets de 256 Ko plutôt que 1 Mio pour la granularité
  d'annulation, audio rendu en mémoire jusqu'à T-043, titres half-width seulement, et l'écart de
  `MD_MODE_TABLE` mesuré mais **non corrigé** sur un seul point de mesure.

- T-041 livré : **tout le DSP et le pipeline par piste** (`src/core/dsp/`, `src/core/pipeline/`).
  `dsp_math` fournit sin/cos/tan/exp/log/pow/I0/sinc en `f64` sans libm (réduction d'argument + Taylor,
  exécutés au montage des tables, pas dans une boucle d'échantillons) — on lie `/NODEFAULTLIB`, ces
  fonctions n'existent pas. `dsp_resample` : sinc fenêtré Kaiser β = 9, 512 phases plus une phase de
  garde, table construite à l'init dans l'arène, produit scalaire SSE2 sur deux phases, filtre
  **centré** (une impulsion d'entrée *i* ressort en `i·out/in` : la latence est compensée par
  l'historique, pas par un décalage en aval), position en 32.32, chemin passe-plat quand le taux est
  déjà 44 100. `dsp_loudness` : BS.1770-4 avec les biquads du filtre K **dérivés** par transformation
  bilinéaire pour le taux donné (jamais recopiés de la table 48 kHz de la norme, cf. research/03 §6.8),
  blocs de 400 ms à 75 % de recouvrement, portes −70 LUFS puis −10 LU comparées en énergie, true-peak
  ×4 en polyphase 4 × 12 taps ; le gain vers la cible est **rabaissé** jusqu'à tenir sous −1 dBTP, on
  ne limite jamais. `dsp_edit` : downmix, trim de silence sur l'enveloppe (fenêtre de 1 ms, hystérésis
  de 50 fenêtres — une hystérésis comptée en échantillons ne basculerait jamais, un sinus pose un
  échantillon sous le seuil à chaque traversée de zéro), fondu cosinus sans état, gain fixe.
  `dsp_dither` : TPDF + noise-shaping optionnel, quantification à l'échelle **32768 avec clamp** (c'est
  ce qui rend le bypass bit-exact), frames SP de 2048 octets en s16 big-endian zéro-paddées, en-tête
  WAV de 44 octets. `pipeline` : la chaîne d'ADR-007 par piste, **deux passes** (mesure puis rendu)
  plutôt qu'un tampon — 42 Mo par piste de 4 min en `f32` contre un second décodage à plus de 100×
  temps réel —, un job par piste, une arène par job, progression atomique, annulation lue par bloc de
  4096 frames, sortie par callback (T-043 y branchera le cache). Écarts assumés : taps variables
  (64 à 44,1/48 kHz, 128 à 88,2/96 kHz) pour que la bande de transition reste une fraction constante du
  taux de sortie, coefficients true-peak générés au runtime plutôt que recopiés, trim appliqué avant le
  gain (le seuil de trim est un niveau absolu), et le banc « pipeline complet < 0,5 s » non tenu à
  669 ms — la passe de rendu seule est à 195 ms, c'est la passe de mesure R128 qui coûte les 403 ms
  restants. Détail et justifications dans `tickets/T-041-dsp-resampler-r128-dither.md`.

## KPI — phase 7, T-070 (i7-8550U, 4 cœurs / 8 threads, Windows 11 ; **machine partagée avec les agents T-071/072/073 pendant toutes les mesures**)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **622 080 o** (636 928 avant, **−14 848**, −2,3 %) | < 500 KB — **non atteinte**, arbitrage lead | 2026-09-08 |
| `SIZE_BUDGET_KB` | **630** (607,5 Ko utilisés, **22,5 Ko de marge**) — pas 550 : l'exe ne rentre pas | marge surveillée (P-007) | 2026-09-08 |
| Tests | **249 cas, 6 848 checks**, 0 échec sous ASan — inchangés | verts | 2026-09-08 |
| Cibles `build.bat` | debug, release, **map** (nouvelle), test, check, analyze **vertes** ; **bench rouge par charge machine** (`r_core batch build` 1 290 µs / budget 900, sur du code hors diff) | vertes | 2026-09-08 |
| Imports | kernel32 + user32 | 2 DLL | 2026-09-08 |
| `--selftest` | vert, et **ouvre désormais un WAV via `codec_open`** : la présence des décodeurs dans l'image est prouvée, pas supposée | exit 0 | 2026-09-08 |
| Répartition (symboles) | app 142 760 · dr_flac 52 340 · netmd 44 664 · stb_vorbis 40 816 · ui 37 956 · dr_wav 31 088 · tags 30 736 · plan 29 400 · win32 27 940 · minimp3 24 880 · chaînes/shaders 24 736 · library 24 424 · renderer 14 968 · codecs 9 748 · dsp 8 880 · pipeline 8 304 | — | 2026-09-08 |
| Plancher incompressible | **43 796 o** de déroulement x64 (`.xdata` 26 884 + `.pdata` 16 912), 7 % de l'exe, aucune option MSVC pour l'enlever | — | 2026-09-08 |
| Remplissage de sections | **< 1 300 o** au total (5 sections, `FileAlignment` 512 déjà par défaut) → `/ALIGN:16` ne lie même pas (`LNK1164`) et ne vaudrait rien | > 8 KB pour être retenu | 2026-09-08 |
| Bruit du banc | **facteur 2,2 entre deux exécutions du même binaire** (`codec_ogg` 306× puis 182×) — le critère « > 5 % = perte » est **inexploitable** dans ces conditions | machine au repos | 2026-09-08 |

## KPI — phase 5, T-045 (i7-8550U, 4 cœurs / 8 threads, Windows 11)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **636 928 o** — **inchangé à l'octet** (un `+1` en ligne, une chaîne de bulle remplacée) | ± 2 Ko | 2026-09-07 |
| Tests | **249 cas, 6 848 checks**, 0 échec sous ASan (+1 cas, +34 checks) | verts | 2026-09-07 |
| Cibles `build.bat` | debug, release, test, check, analyze, bench toutes vertes | vertes | 2026-09-07 |
| Imports | kernel32 + user32 | 2 DLL | 2026-09-07 |
| Recalcul complet d'un disque de 254 pistes | **39,8 µs** (clusters + états 5,8 µs, budget TOC 34,1 µs, premier-ajusté 17,8 µs) | < 50 µs | 2026-09-07 |
| Écart du modèle à l'appareil | 5 s / 25 s / 61 s en SP : **8 000 / 28 000 / 64 000 ms** contre 8 029 / 28 102 / 63 890 mesurés — dans le pas de 2 000 ms | ±1 cluster | 2026-09-07 |

## KPI — phase 5, T-043 (i7-8550U, 4 cœurs / 8 threads, Windows 11 ; MZ-N505 sous WinUSB, disque « 202001 »)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **636 928 o** (601 600 avant T-043, +35 328 : cache, machine d'états, vue Transfert, 45 chaînes FR/EN) | < 660 KB | 2026-09-07 |
| Tests | **248 cas, 6 814 checks**, 0 échec sous ASan (+15 cas) | verts | 2026-09-07 |
| Cibles `build.bat` | debug, release, test, check, analyze, bench toutes vertes | vertes | 2026-09-07 |
| Imports | kernel32 + user32 | 2 DLL | 2026-09-07 |
| Cache, rendu des 3 pistes de test | 25 + 90 + 208 = **323 ms** | — | 2026-09-07 |
| Cache, deuxième gravure du même plan | **968 µs** (3 succès), soit **334×** plus rapide : aucune attente de transcodage | 0 attente | 2026-09-07 |
| Empreinte du cache | 16 056 320 o pour 91 s de SP — **10,6 Mo/min**, borne LRU 2 Go | bornée | 2026-09-07 |
| Rendu à la demande pendant une gravure | progression fusionnée à **1 redraw / 100 ms**, boucle endormie entre deux | 0 % au repos | 2026-09-07 |
| **Gravure réelle, 3 pistes en une session** | 91 s d'audio, 16 056 320 o, **73 796 ms** (1,23× temps réel), les 3 pistes relues titrées | de bout en bout | 2026-09-07 |
| Débit SP réel, par durée | 5 s : 0,24× · 25 s : 1,15× · 61 s : **1,52×** — coût fixe de session ~15-20 s, puis 1,2 à 1,5× | 1,0 à 1,5× (research §6.1) | 2026-09-07 |
| **`MD_MODE_TABLE`, écart mesuré** | 7 mesures, **+2 007 ms par piste** (σ 80 ms) = **exactement 1 cluster SP**. Le pas de 2 000 ms est confirmé à la milliseconde (25 s → 26 000 + 2 102 ; 61 s → 62 000 + 1 890). `PLAN_TRACK_OVERHEAD_CLUSTERS 1` **proposé, non appliqué** (déplace les nombres de T-031) | ±1 cluster | 2026-09-07 |
| Effacement des pistes de test | 5 pistes effacées en une seule simulation → écriture, **8 pistes de l'utilisateur intactes**, titre « 202001 » inchangé | rien perdu | 2026-09-07 |

## KPI — phase 5, T-042 (i7-8550U, 4 cœurs / 8 threads, Windows 11 ; MZ-N505 sous WinUSB)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **575 488 o** — **non tenu** contre les 510 Ko du ticket, sous le budget CI de 600 Ko. Les +105 984 o viennent du **pipeline et du DSP de T-041 qui entrent enfin dans l'image** (rien dans `app/` ne les appelait, `/OPT:REF` les élaguait) ; DES + session + upload en représentent une fraction | < 510 KB | 2026-09-07 |
| Tests | **216 cas, 6 344 checks**, 0 échec sous ASan (11 cas ajoutés par `test_netmd_secure.c`) | verts | 2026-09-07 |
| Cibles `build.bat` | debug, release, test, check, analyze, bench toutes vertes (`bench` réparé : accolade perdue dans `bench_dsp_pipeline_jobs` au merge de phase 5) | vertes | 2026-09-07 |
| Imports | **kernel32 + user32** (table d'import lue dans le PE ; bcrypt chargé par `LoadLibraryW`) | 2 DLL | 2026-09-07 |
| DES, vecteurs à réponse connue | **10/10** plus les 4 clés faibles involutives, FIPS 81 ECB et CBC, 3DES EDE — concordants avec un DES Python indépendant **et** avec Windows CNG | exacts | 2026-09-07 |
| DES-CBC, 8 Mo, un cœur | **35 Mo/s** (238 ms) — **203 ×** les 172 Ko/s dont SP a besoin | > 10 Mo/s | 2026-09-07 |
| Rejeu du download complet | 3 transcriptions (nominal, annulation, reprise) rejouées **octet par octet**, nonce hôte injecté | exact | 2026-09-07 |
| **Upload réel, MZ-N505** | sinus de 10 s SP, **1 765 376 o en 13 708 ms**, relu sur le disque avec son titre, 10 000 ms, encodage SP | la piste arrive titrée | 2026-09-07 |
| Débit de transfert réel | **0,729 × temps réel** (~129 Ko/s) — la borne est l'encodeur ATRAC1 de l'appareil | 1,0 à 1,5 × (research §6.1) | 2026-09-07 |
| `MD_MODE_TABLE`, écart mesuré | 10 s de SP ont coûté **12 875 ms** de temps libre contre 10 000 prédits (+2 875 ms, ~1,4 cluster) — **table non modifiée**, un seul point ne distingue pas un surcoût fixe d'un effet de fragmentation | ±1 cluster | 2026-09-07 |

## KPI — phase 5, T-041 (i7-8550U, 4 cœurs / 8 threads, Windows 11 ; **machine partagée avec l'agent T-040 pendant les mesures**, meilleur de trois passes)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **308 224 o, inchangée** — rien dans `app/` n'appelle encore le pipeline, `/OPT:REF` élague le module ; les tables sont calculées au runtime, zéro donnée figée | < 470 KB | 2026-09-07 |
| Tests | **168 cas, 5 855 checks**, 0 échec sous ASan (24 cas ajoutés par `test_dsp.c`) | verts | 2026-09-07 |
| Cibles `build.bat` | debug, release, test, check, analyze, bench toutes vertes | vertes | 2026-09-07 |
| Resampler, sinus 1 kHz 48 k → 44,1 k | **SNR 102,8 dB** ; 96 k → 44,1 k : 106,0 dB ; 22,05 k → 44,1 k : 96,8 dB | > 90 dB | 2026-09-07 |
| Resampler, image d'un 20 kHz repliée à 16,1 kHz | **−121,5 dBFS** | < −90 dB | 2026-09-07 |
| Resampler, impulsion 48 k → 44,1 k | pic exactement en sortie **147** pour une entrée 160, réponse symétrique à 1e-6 | pic à l'endroit prévu | 2026-09-07 |
| EBU Tech 3341, cas 1 / 2 / 3 / 4 | **−22,991 / −32,991 / −23,011 / −23,011 LUFS** | ±0,1 LU | 2026-09-07 |
| True-peak, sinus à fs/4 déphasé (échantillons à −3,01 dBFS) | **+0,088 dBTP** | 0,0 ± 0,2 dB | 2026-09-07 |
| Dither TPDF, erreur totale | moyenne **−0,001 LSB**, variance **0,250 LSB²** (= 1/12 + 1/6) ; avec noise-shaping 0,333 LSB² | 0 et 1/4 | 2026-09-07 |
| Bypass 44,1 k stéréo sans traitement | **bit-exact** sur 100 000 frames aléatoires | bit-exact | 2026-09-07 |
| Trim / gap / framing SP | fenêtre gardée et longueurs **exactes** ; 4 120 octets utiles → 3 frames de 2048, padding vérifié octet par octet | exact | 2026-09-07 |
| Resampler 48 k → 44,1 k stéréo, 4 min | **671 ms, 358× temps réel** (257–358× selon la charge) | ≥ 200× | 2026-09-07 |
| R128 + true-peak, 4 min stéréo | **403 ms, 595× temps réel** (421–595×) | ≥ 500× | 2026-09-07 |
| Pipeline complet 4 min (R128 + trim + fondus + dither, deux passes) | **669 ms** — **non tenu**, cf. T-041 §Écarts 2 | < 500 ms | 2026-09-07 |
| Pipeline 4 min, passe de rendu seule | **195 ms, 1234× temps réel** | < 500 ms | 2026-09-07 |
| 8 pistes de 4 min à travers les jobs (7 workers) | **1,82 s, 1058× temps réel** | — | 2026-09-07 |
| Allocation par bloc | **aucune** : arène par job, `ArenaTemp` rendu même en cas d'annulation (testé) | 0 | 2026-09-07 |

### Fait en phase 5 (suite)
- T-040 livré : les **décodeurs**. `third_party/` vendorise minimp3 (CC0, `ea99364f`), dr_flac 0.13.4 et
  dr_wav 0.14.6 (`dfe83776`, Unlicense ou MIT-0) et stb_vorbis 1.22 (`2c980bb5`, MIT ou domaine public),
  épinglés, non modifiés, licences intégrales dans `third_party/LICENSES.md` ; ils sont compilés
  **sans CRT** par `src/third_party.c` et `src/third_party_vorbis.c` (deux unités : minimp3 et stb_vorbis
  ont chacun un `get_bits` statique), allocateurs redirigés vers **une arène par décodeur** liée par
  `tp_arena_bind`, `stdio` désactivé, et **une libm maison** (`floor`, `ldexp`, `exp`, `log`, `pow`,
  `sin`, `cos`, `sqrt`, `abs`, `qsort`) pour stb_vorbis. `src/core/codecs/codec.h` porte l'interface
  unique `Decoder` : ouverture par signature (jamais par extension, en réutilisant `TagsReader` de
  `tags.c`), lecture du fichier par **une fenêtre de 256 KB** sur `os_file_read_at` — jamais le fichier
  entier —, sortie **f32 désentrelacée par blocs de 4 096 frames** dans les tampons de l'appelant, seek
  **exact pour tous les formats**, fermeture qui libère l'arène d'un bloc. `codec_wav.c` fait WAVE par
  dr_wav et **AIFF/AIFF-C à la main** (COMM/SSND, taux en flottant étendu 80 bits, PCM BE 8/16/24/32,
  `sowt`, `fl32`, `fl64`), `codec_flac.c` FLAC, `codec_mp3.c` la boucle de trames MPEG (ID3v2, Xing/VBRI,
  réservoir de bits), `codec_ogg.c` Vorbis en **pushdata** (seule API qui n'exige pas le fichier entier et
  qui accepte un `stb_vorbis_alloc`). AAC/M4A, ALAC et WMA passent par **Media Foundation** :
  `os_media_decoder_open/read/seek/close` dans `platform.h`, `win32_media.c` charge `mfplat`/`mfreadwrite`
  par `LoadLibraryW` avec des vtables COM écrites à la main et des GUID en dur (pas de `mfuuid.lib`),
  sortie forcée en PCM float 32 bits ; Opus rend une erreur « format non supporté » propre. Le fuzz
  d'en-têtes a trouvé un **vrai débordement de stb_vorbis 1.22** (longueur de commentaire Vorbis à
  `0x7FFFFFFF`) : corrigé **à notre frontière** (`codec_ogg_headers_are_sane` réassemble les paquets
  d'en-tête et vérifie chaque longueur), pas par un patch sur du code vendorisé. Vecteurs golden générés
  par `tools/gen_audio_vectors.py`, encodeur FLAC minimal en Python compris. Exe **419 328 o**, imports
  kernel32 + user32, 155 cas / 5 842 checks verts, FLAC à 451x et MP3 à 401x temps réel.
  `build.bat bench` est vert machine au repos, mais s'arrête au milieu quand elle est chargée, pour une
  raison antérieure au ticket (P-010).

### Fait en phase 4
- T-032 livré (dernier de la phase) : la **vraie vue Plan et la jauge signature**. `src/app/plan_view.{h,c}`
  isole toute la géométrie et toute la traduction « geste → commande » **sans une box ni un appel GL**,
  ce qui est exactement ce que les tests pilotent : `plan_gauge_layout` place les bords des segments
  depuis le **total courant de clusters** (la somme des segments vaut la largeur utilisée au pixel,
  quel qu'en soit le nombre), fusionne les voisins de même mode sous 2 px, hachure le padding de
  cluster (minimum 1 px), alterne la luminosité entre voisins de même mode et ouvre la zone de
  dépassement à `first_overflow` **sans jamais comprimer la barre** (ce serait mentir sur la capacité) ;
  les gestes sont `plan_mode_cycle`, `plan_drop_index`, `plan_fill_count`, `plan_shorten_quota` et les
  trois gestes multi-entrées. `src/app/view_plan.c` porte le panneau : liste virtualisée (n°, badge de
  mode cliquable, **titre MD tel qu'il sera écrit** avec pictogramme et cause du raccourcissement,
  source ou « piste manquante », durée, clusters), **DnD interne** (fantôme, ligne d'insertion,
  autoscroll, **une seule commande `Move` au lâcher**), Alt+↑/↓, Suppr, Ctrl+Z/Y, F2, sélection
  multiple, changement de mode groupé, groupes repliables à en-tête éditable, menu contextuel,
  onglets multi-disques, en-tête (titre, 60/74/80, mode par défaut, Ouvrir / Enregistrer / Enregistrer
  sous, point d'autosave), « Remplir l'espace restant », « Nouveau disque » et les deux politiques de
  répartition. **Jauge de 56 dp** conforme à research/02 §9 (bandes vérifiées par un `StaticAssert`,
  graduations dont la dernière porte la capacité du média, ligne de lecture dans ses cinq états, ligne
  de lecture au survol, info-bulles par segment et par zone libre, animation de 120 ms) et **variante
  compacte de 12 dp** dans la barre de statut ; **barre TOC** avec part disque / part pistes, seuils
  80 % et 100 %, et « Raccourcir automatiquement » qui bissecte le quota par titre sur le prédicat
  exact des cellules et l'applique **en un seul geste annulable**. Deux ajouts au cœur : le geste
  annulable en un pas (`plan_batch_begin/end`, `plan_undo_step/redo_step` — N commandes qui portent
  chacune son inverse, mais un seul Ctrl+Z) et deux colonnes par entrée dans `PlanCapacity`, pour que
  `plan_gauge_layout` garde la signature du ticket. `os_dialog_open_file`/`save_file` ajoutés
  (imports toujours kernel32 + user32). Les panneaux provisoires de `app.c` sont supprimés.
  **Frame de 254 entrées en 407 µs pour 775 boxes** (budget 1,5 ms), **jauge de 254 segments en
  6,86 µs**, **31,25 ms de CPU sur 12 s au repos** (0,26 % d'un cœur, en baisse), 11 cas de test
  ajoutés (**144 cas / 5 731 checks**), **exe 308 224 o**. Captures : `docs/captures/T-032-*.png`.
- T-031 livré : la **capacité se compte en clusters, pas en secondes** (ADR-011 D2).
  `plan_capacity.{h,c}` — `MD_MODE_TABLE` unique (SP 2 s, mono 4 s, LP2 4 s, LP4 8 s par cluster,
  documentée **à valider sur le device en phase 5, T-045**), coût d'une piste = arrondi supérieur
  jamais nul, totaux utilisé / restant / dépassement, **état par piste** (`Fits`/`Partial`/`Overflow`)
  et coût en clusters pour la jauge segmentée, « ce qui rentrerait encore » **dans les quatre modes**,
  et auto-répartition multi-disques **first-fit** ou **albums gardés ensemble**. `plan_toc.{h,c}` — les
  **255 cellules × 7 caractères** partagées : pistes comptées d'abord, titre de disque compilé dans ce
  qui reste avec sa syntaxe de groupes `0;…//1-4;…//`, groupe par groupe, chacun retenu seulement s'il
  rentre encore (research/01 §7.4) ; une piste non-SP coûte **1 cellule même sans titre** ; comptage
  half-width où un kana voisé vaut deux. **Sanitize** par tables **générées** par
  `tools/gen_charset_tables.py` (521 entrées, données Unicode embarquées dans le script, jamais
  recopiées de netmd-js — ADR-008) : accents → ASCII, kana → katakana demi-chasse, emoji supprimés.
  **Raccourcissement ordonné et seulement en cas de dépassement** (feat. → parenthèses → artiste →
  troncature), avec aperçu du titre **exactement tel qu'il sera écrit** et le drapeau de ce qui a été
  coupé. Titrage automatique D3 : gabarits `{title}` / `{artist} - {title}` / `{n}. {title}`, titre de
  disque par majorité en une passe, groupes proposés depuis les albums. Jauge et nombres d'en-tête
  branchés dessus (durée **facturée**, segments larges de leurs clusters, rouge au débordement) — la
  vraie jauge reste T-032. **Recalcul complet de 254 pistes en 26,9 µs** après trois passes
  d'optimisation mesurées (300 → 105 → 52 → 26,9 µs : `__movsb` par caractère, comptage sans écriture,
  run ASCII pris en bloc). 5 cas de test ajoutés (**133 cas / 5 331 checks**), **exe 254 976 o**,
  imports toujours kernel32 + user32.
- T-030 livré : le **plan de disque est un document** (ADR-011 D1). `src/core/plan/` — `Plan` = jusqu'à
  8 `PlanDisc`, chacun ≤ **254 entrées en SoA** (B-28) avec mode SP/LP2/LP4 + mono, override de titre,
  groupe, gain, rognage et fondus, longueur 60/74/80 et mode par défaut ; les plages de groupes sont
  **dérivées** de la colonne `group_id`, donc aucune commande n'a de plage à recoller. **Toute**
  modification passe par `plan_apply` : dix commandes réversibles (`Add, Remove, Move, SetMode,
  SetTitle, SetDiscTitle, Group, Ungroup, SetDiscLength, SplitDisc`), un seul enregistrement plat de
  80 octets qui porte son propre inverse, pile bornée à **256** dans la struct, **coalescence** des
  frappes dans un titre (800 ms) et événements `PlanChanged` — B-25 est vrai par construction, pas par
  discipline. Un `TrackId` n'est cru que tant qu'il désigne encore le chemin stocké : sinon il est
  re-résolu par chemin, sinon marqué **manquant et conservé** (jamais supprimé en silence). Format
  binaire `.mdplan` versionné, écrit atomiquement et relu **mappé**, avec validation intégrale de la
  frontière (offsets, tailles, chaque `StringId` contre un **bitmap des débuts de chaîne** — un id au
  milieu d'une chaîne est refusé) ; la table d'interne n'est pas stockée mais **rejouée**, ce qui
  reproduit exactement les mêmes offsets. Export/import texte `.mdplan.txt` (une ligne par piste).
  **Autosave** toutes les 5 s dans `%LOCALAPPDATA%\minidisk\plans\`, écriture immédiate à la
  fermeture, **récupération au démarrage** ; la boucle ne se réveille pour lui que si le plan est sale.
  Panneau Plan branché sur le vrai modèle (Entrée ajoute, Suppr retire, Ctrl+Z/Y, compteur et durée en
  en-tête, piste manquante en rouge) — la vue complète reste T-032. **9 cas de test** dont 1 000
  opérations aléatoires annulées et rétablies pas à pas avec comparaison d'empreinte
  (**128 cas / 5 219 checks**), **exe 244 224 o**, imports toujours kernel32 + user32.
  **P-009** ouvert : les 5 ms save+load ne sont pas tenues en temps mur (7,45 ms) parce que 7,1 ms sont
  la barrière de durabilité de l'OS ; notre part fait 0,5 ms.

### Fait en phase 2
- T-014 livré (dernier de la phase) : **drag & drop depuis l'Explorateur** par un `IDropTarget`
  minimal écrit à la main (`ole32` chargée dynamiquement, `RegisterDragDrop`), qui donne la position
  du curseur pendant le survol — le panneau Bibliothèque se met en surbrillance et dit « Déposez pour
  ajouter à la bibliothèque » — avec **WM_DROPFILES en repli** ; un dossier déposé est un dossier
  surveillé, un fichier désigne son dossier parent (`lib_drop_folder`, testé). **Pochettes par WIC** :
  `os_image_decode` (COM en C, `windowscodecs.dll` dynamique, conversion en `32bppPBGRA`, mise à
  l'échelle Fant vers 48 et 256 px) — **aucun octet de décodeur dans l'exe**, imports toujours
  kernel32 + user32. Source embarquée (APIC / PICTURE / covr, y compris **tag ID3 unsynchronisé**
  grâce à un tampon de capture dans `Tags`) avant `cover.*` > `folder.*` > `front.*` du dossier ;
  décodage en jobs (64 en vol au plus, la vue redemande chaque frame, donc aucune file à purger),
  cache disque `covers/<clé>.raw` (48² + 256² RGBA8, écriture atomique, relu **mappé**), **atlas de
  vignettes RGBA8 2048² séparé** en grille de cellules fixes avec **LRU** par bande, branché sur
  `r_rect_textured`. Vue : colonne pochette 48 px activable par le menu de l'en-tête et **panneau
  Détail repliable** (pochette 256 px texel pour texel, titre, artiste, album, année, format, durée,
  taille, chemin en ellipsis du milieu). **1,31 ms de décodage + mise à l'échelle par pochette**,
  15 µs par vignette versée dans l'atlas, 8 cas de test ajoutés (**119 cas / 3 344 checks**),
  **exe 229 376 o**, **46,9 ms de CPU sur 12 s au repos (inchangé)**. Capture : `build/demo.png`.
- T-013 livré : vue Bibliothèque réelle (la démo factice de 100 000 pistes est supprimée) — huit colonnes
  `#` / Titre / Artiste / Album / Durée / Format (badge codec + kHz) / Année / Ajouté, en-têtes triables
  avec indicateur, largeurs redimensionnables à la poignée et **persistées**, largeurs calculées une fois
  par frame et partagées par l'en-tête et les lignes (rien n'est jamais rogné ; sous 1024 dp les colonnes
  partent dans l'ordre Ajouté → Année → Format → Album → Durée, research/02 §8.7), menu de visibilité au
  clic droit sur l'en-tête, navigateur **Artiste | Album** repliable, **Ctrl+F** focalise la recherche et
  **Échap** l'efface, compteur « N / total », liste **sans aucune copie** (les lignes lisent les `TrackId`
  de `LibSearch` puis le SoA), sélection multiple, Ctrl+A, Entrée = ajout au plan, menu contextuel, états
  **vide** (gros bouton + dépôt de dossier), **scan** (barre de progression de 2 dp) et **aucun résultat** ;
  `app_state.{h,c}` sépare l'état de la boucle, `prefs.{h,c}` écrit `minidisk.prefs` (texte, écriture
  atomique, mode portable) et `strings.h` porte les 48 chaînes FR/EN (ADR-011 D10). **P-008 résolu** : la
  box conteneur des trois panneaux n'avait pas de clé, son `rect` était nul, et les splitters n'avaient
  donc **jamais** borné — les trois panneaux tiennent maintenant jusqu'à **1024 × 640 logique**.
  **Clic de tri sur 100 000 pistes : 36,45 ms**, recherche 4,06 ms, 7 cas de test ajoutés
  (111 cas / 1 875 checks), **exe 207 872 o**, imports kernel32+user32, **46,9 à 78,1 ms de CPU sur 12 s au
  repos**. Captures : `build/demo.png`, `build/demo_empty.png`.
- T-012 livré : index triés par colonne (titre, artiste, album, durée, date d'ajout) en fusion stable
  sur des paires `(clé u64, id)`, clés normalisées (casse, accents repliés par deux tables plates de
  384 o couvrant latin-1 et latin ext-A, articles optionnels), navigateur **Artiste → Album** avec
  comptes, recherche incrémentale (masque 64 bits par piste, sous-chaîne SSE2 sur un blob normalisé,
  tokens ET, raffinement qui ne reteste ni les tokens déjà prouvés ni les tokens d'une lettre,
  **zéro allocation pendant la recherche**), cache `library.mdlib` mappé (SoA écrit tel quel, string
  table + slots d'internement, écriture atomique `.tmp` + `MoveFileEx`, validation exhaustive de
  l'en-tête, des offsets, de chaque `StringId` et de la free-list, version → rejet propre), TrackId
  stables entre sessions, démarrage « cache puis rescan de fond qui ne pousse que les diffs », tri au
  clic sur l'en-tête. **Tri 100k en 22,8 ms, recherche « the » sur 100k en 4,09 ms, raffinement
  « the b » en 0,978 ms, cache 100k chargé en 26,9 ms** (fichier 12,04 Mo), 8 cas de test ajoutés
  (104 cas / 1 780 checks), **exe 187 392 o**, imports kernel32+user32. Capture : `build/demo.png`.
- T-011 livré : lecture des tags et des en-têtes sans décoder l'audio, **2 lectures de 64 Ko par
  fichier** (tête + queue) : ID3v2.2/2.3/2.4 (unsync tag et frame, ISO-8859-1 / UTF-16 BOM /
  UTF-16BE / UTF-8, TXXX ReplayGain, APIC → offset + hash), ID3v1/v1.1, en-tête MPEG avec
  Xing/Info/VBRI, FLAC (STREAMINFO / VORBIS_COMMENT / PICTURE), OGG Vorbis et Opus (durée par la
  granule de la dernière page), MP4 (`moov/udta/meta/ilst`, `mvhd`, `stsd`, `moov` en fin de fichier
  lu dans la queue), APEv2 en queue, WAV (`fmt `/`LIST INFO`) et AIFF (`COMM`/`NAME`/`AUTH`),
  dispatcher par signature et replis nom de fichier / dossiers parents. Tags lus dans une deuxième
  vague de jobs sur les seules pistes nouvelles ou modifiées : **un rescan sans changement n'ouvre
  aucun fichier**. **2,13 µs par fichier** au parsing, 50 000 fichiers tagués en **2,38 s à froid**
  (73 ms à chaud), 24 vecteurs golden de 34,8 Ko générés par `tools/gen_tag_vectors.py`, **fuzz de
  220 000 mutations sous ASan sans crash**, 16 cas de test ajoutés, exe 167 424 o, imports
  kernel32+user32. Capture : `build/demo.png` (titres, artistes, albums et durées réels).
- T-010 livré : `platform.h` système de fichiers (itération de dossier sans allocation, `os_file_stat`,
  lecture aléatoire, helpers de chemins, dossiers connus), sélecteur de dossier `IFileDialog` (COM en C,
  `ole32`/`shell32` dynamiques), `core/library` : SoA de 19 colonnes (**74 o/piste**, 7,4 MB pour
  100 000 pistes), `StringTable` internée, `TrackId` stable + tombstones, scan **un job par
  sous-dossier** avec allocateur bump sans verrou et pile de Treiber vers le thread principal, rescan
  incrémental par (taille, mtime), annulation coopérative, file d'événements core → UI, démo
  « Ajouter un dossier » + progression + chemins. **50 000 fichiers scannés en 168 ms à froid,
  63 ms à chaud**, 5 cas / 76 checks ajoutés, exe 123 904 o, imports kernel32+user32, 15,6 ms de CPU
  sur 12 s au repos. Capture : `build/demo.png`.

### Fait en phase 1 (suite)
- T-008 livré : `platform.h` threads / sémaphores / SRW / atomiques (`win32_thread.c`), `base_jobs`
  (pool de N-1 workers, ring MPMC de Vyukov sans lock ni allocation, `jobs_dispatch` parallel-for,
  `jobs_wait` où le thread principal exécute des jobs, workers endormis sur sémaphore), overlay
  debug **F11** (fps + min/avg/max sur 120 frames avec graphe en barres, boxes, draw calls,
  vertices, atlas et remplissage, mémoire par arène, jobs et workers, DPI, taille fenêtre, réveils
  de `os_events_pump` et messages WndProc par identifiant). **910 ns par job vide à 7 workers**,
  4,35x de speedup sur un parallel-for borné calcul, 70 cas / **1 378 checks**, exe 107 008 o,
  imports kernel32+user32. **P-005 résolu** : 0 réveil et 0 message sur 12 s de repos, le CPU
  résiduel est sur un thread du pilote GL. Capture : `build/demo.png`.
- T-007 livré : `ui_theme` (tokens de research/02b, couleurs de mode SP/mono/LP2/LP4), `ui_widgets`
  (bouton, bouton icône, label, séparateur, champ texte UTF-8 avec sélection/presse-papiers/undo,
  **liste virtualisée 100 000 lignes à 110 boxes par frame**, splitter, tooltip 500 ms, menu contextuel
  au clavier), démo trois panneaux « Bibliothèque | Plan | Disque », 65 cas / **1 345 checks**,
  exe 97 792 o, imports kernel32+user32, 31 ms de CPU sur 12 s au repos. Capture : `build/demo.png`.
- T-006 livré : `ui_core` (clés hachées, piles de style, layout sémantique en 2 parcours, signaux, animations, 3 couches), démo 3 colonnes, **layout de 12 020 boxes en 338 µs** (28 ns/box, meilleur de 200 passes), 1 231 checks, exe 66 048 o, imports kernel32+user32, 15,6 ms de CPU sur 12 s au repos.
- T-005 livré et reviewé : DirectWrite → atlas R8, fallback système (japonais, katakana half-width), caches glyphes/mesure, ellipsis, chiffres tabulaires, 63 ns/glyphe à chaud, 1 143 checks, exe 54 272 o. P-006 (vtables COM en C).
- T-005 livré : DirectWrite chargé dynamiquement (vtables COM à la main), fallback système par
  `IDWriteFontFallback` (japonais et katakana demi-chasse), cache de glyphes (police, glyphe, quart
  de pixel) et cache de mesure, ellipsis, chiffres tabulaires, gamma du texte dans le shader,
  **1 143 checks**, exe 54 272 o, imports kernel32+user32, 46,9 ms de CPU sur 12 s au repos. P-006.
- T-004 livré et reviewé : file de commandes, batches (texture, clip, 16k quads), VBO persistant triple-buffered + fences, atlas skyline, rastériseur à couverture exacte, 8 icônes, **8 draw calls pour 2 200 quads**, 1 080 checks, exe 45 568 o.

### Fait en phase 1
- T-003 livré : contexte WGL 3.3 core, loader X-macro (48 fonctions), renderer SDF (1 shader, 1 VBO), démo 5 rects, 0,5 % CPU au repos mesuré en delta exact (P-005, ouvert), 491 checks, exe 33 792 o (cible du ticket < 30 KB non tenue, écart détaillé dans la Livraison de T-003).
- T-002 livré et reviewé : fenêtre complète, `OsEvent` ring, DPI v2, dark title bar, drop files, **0,0 % CPU au repos** (mesuré), 440 checks, exe 23 040 o (P-004).
- T-001 livré et reviewé : `base/`, `build.bat` (7 cibles), 125 checks, exe release **7 168 octets**, imports kernel32+user32. Voir P-003 (TLS sans CRT).

### Prochain pas
1. Utilisateur : Zadig → WinUSB (`tools/zadig/README.md`). Ensuite : ping réel, captures des 4 transcriptions
   (`--netmd-trace`), validation de `MD_MODE_TABLE` sur le device, chronométrage hotplug et lecture < 2 s.
2. Lead : revue/merge de T-022 et T-042, puis T-043 → **premier disque gravé**, tag `v0.5.0-phase5`
   (le tag `v0.3.0-phase3` se pose dès que la validation device est faite).
3. P-010 : rendre `os_memory_commit` parlant et découper l'arène du banc.

### Action utilisateur requise
- Pilote installé (P-001 clos). Pour finir P-012 : insérer successivement un disque vierge, un disque protégé, puis aucun disque, et lancer `build\minidisk.exe --netmd-trace tests
etmd
eal\mzn505_<cas>.trace` à chaque fois.

## KPI — fin de phase 1 (i7-8550U, 4 cœurs / 8 threads, Intel UHD 620, Windows 11)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **107 008 o**, marge 24 064 o | < 128 KB | 2026-09-06 |
| Imports | kernel32 + user32 | ces deux-là | 2026-09-06 |
| Fps / temps de frame (démo, overlay ouvert) | 60 fps vsync, frame typique 2,3 ms (min 1,6 / max 100 au réveil) | 60 fps | 2026-09-06 |
| Layout | 12 020 boxes en **493 µs** (41 ns/box, meilleur de 200 passes) | < 1 ms | 2026-09-06 |
| Liste virtualisée | 110 boxes pour 100 000 lignes | < 200 boxes | 2026-09-06 |
| Draw calls | **94** pour la frame de la démo (overlay ouvert, 1 499 quads) ; 8 sur le banc renderer de T-004 | < 10 | 2026-09-06 |
| Texte | 80 ns par glyphe à chaud | — | 2026-09-06 |
| Jobs | 910 ns par job vide (7 workers), speedup 4,35x sur un parallel-for | < 1 µs, > 3x | 2026-09-06 |
| CPU au repos, 12 s | 31 à 78 ms, **0 ms sur nos threads** (0 réveil, 0 message) ; le reste est un thread du pilote GL | 0 % | 2026-09-06 |
| Tests | 70 cas, **1 378 checks**, 0 échec (ASan) | verts | 2026-09-06 |
| Budget CI (`SIZE_BUDGET_KB`) | 350 KB (phase 4) ; historique : 128 KB fin de phase 1, 250 KB phase 2 (phase 2 : 250 KB ; `/O1` = −16,9 Ko reste un levier, cf. P-007) | — | 2026-09-06 |

## KPI — phase 2 terminée (même machine)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **229 376 o**, marge 26 624 o | budget CI 250 KB | 2026-09-07 |
| Scan bibliothèque | 50 000 fichiers / 500 dossiers : **168 ms à froid**, **63 ms à chaud** (7 workers) | < 2 s / < 300 ms | 2026-09-06 |
| Mémoire bibliothèque | **74 o/piste** → 7,4 MB pour 100 000 pistes (hors chaînes), 0 allocation par piste | < 40 MB | 2026-09-06 |
| Annulation du scan | < 100 ms (test) | < 100 ms | 2026-09-06 |
| Lecture des tags | **2,13 µs/fichier** (parsing), 2 lectures de 64 Ko par fichier | < 3 s / 10 000 | 2026-09-06 |
| Scan + tags, 50 000 fichiers | **2,38 s à froid**, **73 ms à chaud** (0 fichier ouvert au rescan) | < 3 s / 10 000 | 2026-09-06 |
| Fuzz des parseurs | 22 vecteurs x 10 000 mutations, **0 crash, 0 rapport ASan** | 0 | 2026-09-06 |
| Tests | **119 cas, 3 344 checks**, 0 échec (ASan) | verts | 2026-09-07 |
| Tri de la bibliothèque | 100 000 pistes par artiste : **22,8 ms** (fusion stable sur clés normalisées) | < 30 ms | 2026-09-07 |
| Recherche incrémentale | « the » sur 100 000 pistes : **4,09 ms** ; raffinée « the b » : **0,978 ms**, 0 allocation | < 5 ms / < 1 ms | 2026-09-07 |
| Cache `library.mdlib` | 100 000 pistes, fichier de 12,04 Mo : **chargé en 26,9 ms**, écrit en 53 ms | < 50 ms | 2026-09-07 |
| Clic de tri (en-tête), 100 000 pistes | **36,45 ms** : ordre construit + 100 000 ids réémis dans la liste | < 50 ms perçu | 2026-09-07 |
| Trois panneaux visibles | jusqu'à **1024 × 640 logique** (biblio 597 px, plan 357, disque 300 à 125 %) | 1024 x 640 | 2026-09-07 |
| Préférences | fichier de 789 o, aller-retour sérialisation + parsing en 13 µs, écriture atomique | — | 2026-09-07 |
| Pochettes | **1,31 ms** par pochette (décodage WIC + mise à l'échelle vers 256 et 48), 10 000 pochettes = ~1,9 s réparties sur 7 workers, 64 en vol au plus | 10 000 sans jank | 2026-09-07 |

## KPI — phase 3, T-022 (même machine)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **496 128 o** (+26 624 o sur les 469 504 o de `main`) | < 500 KB (CI 600 KB) | 2026-09-07 |
| Imports | kernel32 + user32 (ligne de link inchangée, aucun `LoadLibrary` ajouté) | ces deux-là | 2026-09-07 |
| Tests | **221 cas, 6 407 checks**, 0 échec (ASan) — dont 16 cas T-022 | verts | 2026-09-07 |
| Cibles `build.bat` | debug, release, test, check, analyze, bench : les six vertes (bench au repos ; P-010 sous charge) | vertes | 2026-09-07 |
| `check` | vert : ni `windows.h`, ni `malloc`, ni `printf` dans `src/core/netmd` | vert | 2026-09-07 |
| `analyze` | vert : `/W4 /WX /analyze` + clang-tidy sans diagnostic sur `src/` | vert | 2026-09-07 |
| Lecture du disque réel | **88 commandes**, 8 pistes, `0;202001//1-8;//`, flags `0x10` | le disque s'affiche | 2026-09-07 |
| Capture réelle versionnée | `mzn505_real_read.trace`, 617 lignes, rejouée par les tests | 1 capture | 2026-09-07 |
| Écriture sur disque réel | **non faite** (P-013 : disque de l'utilisateur, pas de disque de test) | renommage aller-retour | — |
| Transcriptions d'édition | 9 fichiers, **3 831 lignes**, synthétiques (research/01) | rejouées octet à octet | 2026-09-07 |
| Écritures TOC par geste | renommage 1, déplacement 2, effacement de N pistes N+1, groupe 1 | minimiser (§6.5) | 2026-09-07 |
| Temporisations d'édition | 100 ms entre éditions, 500 ms avant re-listage | §6.2 | 2026-09-07 |
| Sauvegarde TOC | 1 fichier texte par écriture, écriture atomique, round-trip testé | avant toute écriture | 2026-09-07 |
| Cas limites de simulation | **10 + 6** couverts (budget, groupes, protections, refus) | 10 | 2026-09-07 |
| Chaînes i18n | 36 FR + 36 EN nouvelles (68 pour le panneau Disque) | ADR-011 D10 | 2026-09-07 |

## KPI — phase 3, T-020 (même machine)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **327 168 o** (+18 944 o sur T-032), marge 20 992 o sous les 340 KB du ticket | < 340 KB (CI 500 KB) | 2026-09-07 |
| Imports | kernel32 + user32 (winusb, setupapi, cfgmgr32, advapi32, shell32 en `LoadLibraryW`) | ces deux-là | 2026-09-07 |
| Tests | **152 cas, 5 824 checks**, 0 échec (ASan) | verts | 2026-09-07 |
| Cibles `build.bat` | debug, release, test, check, analyze, bench toutes vertes | vertes | 2026-09-07 |
| `check` | vert : ni `windows.h` ni `malloc` dans `src/core/netmd` | vert | 2026-09-07 |
| Débounce hotplug | **200 ms** (4 notifications d'une rafale → 1 seule énumération), bout en bout < 500 ms | < 500 ms | 2026-09-07 |
| Détection du device réel | `054c:0084` → « Sony MZ-N505 », état `NoDriver`, ProblemCode 28, bus « Net MD Walkman » | l'écran guidé s'affiche | 2026-09-07 |
| Ouverture / control transfer sur le vrai device | **en attente de Zadig** (P-001) | ping → `0x09` | — |
| Chaînes i18n | 13 FR + 13 EN, aucune littérale hors `strings.h` | ADR-011 D10 | 2026-09-07 |
## KPI — phase 5, T-040 (même machine)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **419 328 o** (308 224 o avant T-040, +111 104 o pour cinq décodeurs), marge 10 752 o sous les 420 KB du ticket et 92 672 o sous le budget CI | < 420 KB (CI 500 KB) | 2026-09-07 |
| Imports | kernel32 + user32 (table d'import lue à la main) | ces deux-là | 2026-09-07 |
| Tests | **155 cas, 5 842 checks**, 0 échec (ASan) | verts | 2026-09-07 |
| Cibles `build.bat` | debug, release, test, check, analyze, bench toutes vertes (bench sensible à la charge machine, P-010) | vertes | 2026-09-07 |
| Décodage FLAC | **451x temps réel** (220 à 451x selon la charge), décodeur scalaire — `DR_FLAC_NO_SIMD` rend 27,5 Ko | ≥ 300x | 2026-09-07 |
| Décodage MP3 | **401x temps réel** (234 à 401x) | ≥ 100x | 2026-09-07 |
| Décodage Ogg Vorbis | **234x temps réel** (111 à 234x) | — | 2026-09-07 |
| Décodage WAV s16 | **922x temps réel** (525 à 1 101x) | — | 2026-09-07 |
| SNR du sinus décodé | MP3 **73 dB**, Ogg **53 dB**, AAC **27 dB** — plafonds des encodeurs de VLC (pas de ffmpeg ici) ; le décodeur de VLC donne les mêmes chiffres à 0,4 dB près | > 60 dB MP3 | 2026-09-07 |
| FLAC et AIFF vs WAV | **bit-exact** sur 22 050 frames | bit-exact | 2026-09-07 |
| Fuzz des en-têtes de codecs | 11 vecteurs × 200 mutations, 2 026 ouvertures, **0 crash, 0 rapport ASan** ; 1 débordement réel de stb_vorbis trouvé et fermé à la frontière | 0 | 2026-09-07 |
| Mémoire par décodeur | 1 arène, fenêtre de fichier de **256 KB**, ~1,3 MB engagés au pire (Vorbis) ; **0 `malloc`** | 0 malloc | 2026-09-07 |
| Vecteurs audio commités | 13 fichiers, 265 263 o | < 40 KB par fichier compressé | 2026-09-07 |

## KPI — phase 4, T-032 (même machine)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **308 224 o**, marge 29 696 o sous les 330 KB du ticket, 50 176 o sous le budget CI | < 330 KB (CI 350 KB) | 2026-09-07 |
| Imports | kernel32 + user32 | ces deux-là | 2026-09-07 |
| Tests | **144 cas, 5 731 checks**, 0 échec (ASan) | verts | 2026-09-07 |
| Cibles `build.bat` | debug, release, test, check, analyze, bench toutes vertes | vertes | 2026-09-07 |
| Frame de la vue Plan, 254 entrées et 20 groupes | **407 µs** (meilleure de 300 ; moyenne 809 µs), **775 boxes** | < 1,5 ms | 2026-09-07 |
| `plan_gauge_layout`, 254 entrées | **6,86 µs** (13 658 cycles) ; 8,13 µs sur la barre compacte de 120 px | < 20 µs | 2026-09-07 |
| Recalcul capacité + budget TOC, 254 pistes | 30,2 µs (+3 µs pour les deux colonnes ajoutées à `PlanCapacity`) | < 50 µs | 2026-09-07 |
| CPU au repos, 12 s après 3 s de chauffe | **31,25 ms**, soit 0,26 % d'un cœur — résidu de thread pilote GL (P-005), en baisse sur les 46,9-78,1 ms de T-030 | 0 % sur nos threads | 2026-09-07 |
| 60 fps pendant le DnD | tenu par le budget : 0,4 ms de CPU pour la frame la plus lourde, le DnD y ajoute 3 boxes (16,3 ms de marge) ; **pas de capture d'un DnD en cours** (l'injection de touches n'atteint pas la fenêtre) | 60 fps | 2026-09-07 |
| Chaînes i18n | 61 chaînes FR/EN ajoutées, aucune littérale hors `strings.h` | ADR-011 D10 | 2026-09-07 |

## KPI — phase 4, T-031 (même machine)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **254 976 o**, marge 62 464 o sous le budget du ticket | < 310 KB (CI 350 KB) | 2026-09-07 |
| Imports | kernel32 + user32 | ces deux-là | 2026-09-07 |
| Tests | **133 cas, 5 331 checks**, 0 échec (ASan) | verts | 2026-09-07 |
| Recalcul capacité + budget TOC, 254 pistes | **26,9 µs** (clusters 4,3 µs, TOC 22,6 µs) | < 50 µs | 2026-09-07 |
| Auto-répartition multi-disques, 254 pistes | **8,6 µs** (first-fit) | — | 2026-09-07 |
| Table de charset générée | 521 entrées, ~6,3 Ko dans l'exe, régénérable par `tools/gen_charset_tables.py` | ADR-008 | 2026-09-07 |

## KPI — phase 4, T-030 (même machine)
| Métrique | Valeur | Cible | Date |
|----------|--------|-------|------|
| Taille exe release | **244 224 o**, marge 114 176 o | budget CI 350 KB | 2026-09-07 |
| Imports | kernel32 + user32 | ces deux-là | 2026-09-07 |
| Tests | **128 cas, 5 219 checks**, 0 échec (ASan) | verts | 2026-09-07 |
| Plan `.mdplan`, 254 pistes | **save 6,96 ms** (dont 7,09 ms de plancher OS) + **load 0,49 ms** = 7,45 ms | < 5 ms — **non tenu**, cf. P-009 | 2026-09-07 |
| Sérialisation du plan (notre code) | **≈ 0,5 ms** pour un disque plein de 28 040 o | < 5 ms | 2026-09-07 |
| Undo/redo | pile bornée à 256 commandes, 80 o chacune, 0 allocation ; 1 000 opérations aléatoires annulées et rétablies pas à pas | exact | 2026-09-07 |
| Mémoire du plan | **~76 Ko** pour 8 disques × 254 pistes, tout compris (pile d'undo incluse) | — | 2026-09-07 |
| Atlas de vignettes | 2048² RGBA8, 1 400 cellules de 48 px + 7 de 256 px, **15 µs** par vignette versée (16 par frame au plus : 0,24 ms), LRU par bande | vignettes < 100 ms après un scroll | 2026-09-07 |
| Cache pochettes | 271 376 o par pochette, relu mappé, **0 décodage au deuxième lancement** | — | 2026-09-07 |
| CPU au repos, 12 s, après un scan | **46,9 et 78,1 ms** sur trois mesures (46,9 ms après T-014 : le drop target OLE ne réveille rien), soit 0,4 à 0,65 % d'un cœur — même résidu de thread pilote GL qu'en T-008 (P-005) | 0 % | 2026-09-07 |

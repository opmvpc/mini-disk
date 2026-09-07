# T-022 — Édition du disque : renommer, déplacer, effacer, groupes, avec simulation et sauvegarde TOC

Phase 3 · Statut : **fait, validation device en attente (écriture)** · Dépend de : T-021, ADR-011 D4, research/01 §3.7-3.9, §9.3

## Livrables
- `netmd_edit.c` : `set_disc_title` (avec `oldLen` exact — un `oldLen` faux corrompt le TOC),
  `set_track_title`, `move_track`, `erase_track`, `erase_disc`, écriture de la structure de groupes
  (réécriture complète du titre disque), `wchar` = `0x00/0x01` pour le disque et `0x02/0x03` pour les
  pistes (research/01 §9.3), ne jamais réécrire un titre identique (plante certains modèles),
  `leave secure session` systématique sur abort.
- **Simulation** (D4) : toute opération d'édition produit d'abord un `DiscDiff` (avant / après : titres,
  ordre, groupes, budget TOC) affiché dans un panneau de confirmation avec verbe explicite
  ("Effacer 3 pistes", "Renommer le disque") ; **sauvegarde automatique du TOC** (titres, ordre, groupes,
  durées) dans `%APPDATA%\minidisk\toc-backups\<disc-id>-<date>.json`-like texte avant toute écriture.
- Budget TOC (D3) : calcul exact 255 cellules × 7 caractères partagées (titre disque groupes inclus,
  pistes non-SP = 1 cellule même vides) ; refus avec message clair si dépassement.
- Vue Disque : renommage inline (F2 / double-clic, Entrée / Échap), glisser-déposer pour réordonner,
  Suppr avec confirmation, création de groupe depuis une sélection, dissolution de groupe.
- Verrou "TOC en RAM" : après une écriture, bannière "Ne pas éjecter, écriture du TOC" tant que le
  device n'a pas confirmé ; blocage de la fermeture de l'app pendant ce temps.
- Tests : rejeu de transcriptions d'édition (rename, move, erase, groupes) ; calcul du budget TOC sur
  10 cas limites ; simulation → diff attendu ; round-trip sauvegarde TOC.

## Critères d'acceptation
- Renommer / réordonner / effacer sur le MZ-N505 sans corruption, vérifié en relisant le disque.
- Aucune écriture sans simulation ni sauvegarde. Exe < 300 KB. Tests, check, analyze verts.
- Fin de phase 3 : tag `v0.3.0-phase3`, STATUS.md avec KPI (temps de lecture disque, transcriptions).

## Livraison

Livré le 2026-09-07 dans le worktree `t022` (branche `t022`, sur `main` qui contient T-020, T-021,
T-040, T-041). Statut : **fait, validation device en attente pour l'écriture** (P-013) — la
**lecture** est, elle, validée sur le vrai MZ-N505 pour la première fois du projet.

### Ce qui est construit

**`src/core/netmd/netmd_edit.{h,c}` (150 + 653 lignes) — les écritures et la simulation.**
- `netmd_utf8_to_sjis` : UTF-8 → Shift-JIS. L'entrée passe d'abord par `plan_toc_sanitize`, donc
  ce qui en sort est ASCII 0x20..0x7E + katakana demi-chasse et rien d'autre ; l'encodage est
  alors **arithmétique** (`0xA1 + (cp - U+FF61)`), sans table et sans CP932 (§3.11). Conséquence
  voulue : ce qu'on compte, ce qu'on prévisualise et ce que l'appareil affichera sont la même
  chaîne.
- `netmd_set_disc_title` / `netmd_set_track_title` : **relecture du titre courant juste avant
  l'écriture** pour obtenir un `oldLen` exact (§3.9, piège 9 — un `oldLen` faux corrompt le TOC),
  **court-circuit si identique** (piège 10, les LAM plantent), `wchar` = `0x00/0x01` pour le
  disque et `0x02/0x03` pour les pistes (piège 8), chemin Sharp (`audioUTOC1TD` si VID `0x04dd`),
  et la ronde de descripteurs de §3.9 pt 3 : `close → openWrite → écriture → close → openRead →
  close`. `netmd_disc.c` gagne pour ça `netmd_get_{disc,track}_title_ex`, qui rendent en plus les
  **octets Shift-JIS bruts** : `oldLen` les compte, et ré-encoder le texte décodé compterait faux
  dès qu'un kanji est passé par `?`.
- `netmd_move_track`, `netmd_erase_track`, `netmd_erase_disc` : trames de §3.12 telles quelles,
  suivies de la temporisation obligatoire.
- Temporisations §6.2 par `netmd_session_hold` : **100 ms** entre deux éditions de TOC, **500 ms**
  après un `eraseTrack` / `moveTrack` avant tout re-listage.
- `netmd_edit_apply` : la séquence de §6.5. `acquire` en entrée, `release` **sur tous les chemins**
  (§3.5 : un `acquire` sans `release` laisse « PC --> MD » figé jusqu'au débranchement),
  effacements **par index décroissant** (piège 17), et **une seule** réécriture du titre disque à
  la fin, jamais une par piste (l'anti-pattern nommé dans §6.5).

**Simulation (ADR-011 D4).** `netmd_edit_simulate(before, request, after, diff)` : aucun octet
USB, aucune allocation, aucun effet. Elle produit le `DiscLayout` d'après **et** le `DiscDiff` que
le panneau de confirmation affiche : verbe explicite (« Effacer 3 pistes », « Renommer le
disque »), texte avant / après, pistes et groupes avant / après, cellules TOC avant / après,
caractères libres, nombre d'écritures que la séquence coûtera. Sept opérations :
`RenameDisc`, `RenameTrack`, `MoveTrack`, `EraseTracks`, `EraseDisc`, `CreateGroup`,
`DissolveGroup`. Huit refus typés, chacun avec sa phrase FR/EN : `NoDisc`, `Protected` (onglet
fermé ou disque préenregistré, §7.5), `TrackProtected` (flag `0x03`, §7.5), `Budget`, `Nothing`
(le disque dit déjà exactement ça), `Range`, `Grouped` (une piste n'appartient qu'à un groupe),
`Backup` (la sauvegarde a échoué, donc rien n'est écrit).

**Budget TOC : celui de `plan_toc`, pas un second.** `plan_toc_compile_disc_title` a été
factorisée en `plan_toc_compile_raw(titre, PlanTocGroup[], budget, …)` ; le plan et le disque lui
passent leurs groupes respectifs et obtiennent la **même** chaîne `0;Titre//1-4;Face A//`.
`netmd_layout_cells` fait la somme avec `plan_toc_cells_for_title` (piste non-SP = 1 cellule même
vide, §7.4). Une seule différence de politique, assumée : pour le disque la compilation reçoit le
budget **entier** (255 cellules) au lieu du reste, pour que le dépassement soit **visible et
refusé** au lieu d'être absorbé en silence par la perte d'un groupe.

**`src/core/netmd/netmd_backup.{h,c}` (45 + 195 lignes) — la sauvegarde du TOC.** Avant toute
écriture, le `DiscLayout` courant est sérialisé en texte (`minidisk-toc 1`, `disc-id`, `flags`,
`capacity`, `title`, `title-full`, `tracks`, une ligne `group` par groupe, une ligne `track` par
piste avec le titre en dernier champ) et écrit **atomiquement** (`.tmp` + `os_file_move_replace`,
ADR-010) dans `%LOCALAPPDATA%\minidisk\toc-backups\<disc-id>-<yyyymmdd-hhmmss>.txt`. `disc-id` =
hash 64 bits de (titre, nombre de pistes, durée totale). Le lecteur `netmd_backup_parse` est une
**frontière de validation** (le fichier a pu être édité à la main) : en-tête vérifié, lignes
inconnues ignorées, appartenance aux groupes reconstruite depuis les plages. Round-trip testé, y
compris en passant par le disque.

**Thread device.** Une commande `NetmdCmd_Edit` portant un `NetmdEditRequest` par valeur, et
toujours la même séquence : **simuler → refuser, ou sauvegarder le TOC → écrire → relire le
disque → publier**. Le drapeau `toc_dirty` est levé juste avant la première écriture, voyage dans
**tous** les événements, et n'est baissé que lorsque l'appareil a confirmé en redécrivant son
disque — ou à l'éjection (le TOC est alors écrit, §6.3), ou si l'appareil disparaît. La simulation
est refaite par le thread : le disque a pu changer depuis que le panneau a été dessiné, et c'est
celle-là qui décide.

**Vue Disque.** Sélection multiple (clic, Ctrl+clic) sur les pistes, renommage inline (F2 ou
double-clic, Entrée / Échap) pour une piste **et pour le titre du disque**, glisser-déposer pour
réordonner (seuil de 4 px, comme le plan), Suppr, Ctrl+G (grouper la sélection), Ctrl+Maj+G
(dissoudre), la même barre en boutons pour les mains qui ne connaissent pas les raccourcis. Chaque
geste ne fait qu'**une** chose : remplir une requête et la simuler. Le panneau de confirmation
montre le diff et deux boutons ; rien n'est posté au thread device avant « Appliquer ». La
bannière « Ne pas éjecter : écriture du TOC » s'affiche tant que `toc_dirty` est levé, et
`app.c` refuse alors `OsEvent_Close`. Le budget de titres est affiché en permanence à côté de la
jauge de capacité (« TOC : 42 / 255 cellules, 1 491 caractères libres ») : les deux ressources
finies du disque, au même endroit.

**Tests.** `tests/test_netmd_edit.c` (639 lignes, 16 cas) : encodage Shift-JIS et son
aller-retour, dix cas limites de simulation (titre identique, accent qui se replie sur le même
titre, piste inexistante, pas de disque, onglet fermé, disque préenregistré, piste protégée,
sélection vide, dépassement de budget sur 254 pistes titrées, dernière piste d'un groupe effacée,
déplacement d'un groupe à l'autre, groupe non contigu, piste déjà groupée, dissolution, effacement
total), le compte en cellules (8 caractères coûtent autant que 14, LP sans titre = 1 cellule),
round-trip de la sauvegarde TOC en mémoire **et** par le système de fichiers, huit transcriptions
d'édition rejouées octet à octet, et la commande d'édition du thread device de bout en bout
(événement, `toc_dirty` levé puis baissé, relecture, second refus « titre identique » qui
n'atteint jamais l'appareil). `tools/gen_netmd_traces.py` gagne neuf scénarios marqués
`# synthetic` comme ceux de T-021.

### Mesures

| Métrique | Valeur |
|----------|--------|
| Taille exe release | **496 128 o** (+26 624 o sur les 469 504 o de `main`), sous les 500 KB du ticket, `SIZE_BUDGET_KB` = 600 |
| Imports | **kernel32 + user32** : ligne de link inchangée, aucun `LoadLibrary` ajouté |
| Tests | **221 cas, 6 407 checks, 0 échec** sous ASan (T-021 : 205 / 6 391 après merge) — dont 16 cas T-022 |
| Cibles `build.bat` | debug, release, test, check, analyze, bench : **les six vertes** (bench vert machine au repos ; ses assertions de perf retombent sous charge — P-010, antérieur) |
| `check` | vert : ni `windows.h`, ni `malloc`, ni `printf` dans `src/core/netmd` |
| `analyze` | vert : `cl /W4 /WX /analyze` sans avertissement, clang-tidy sans diagnostic sur `src/` |
| Code livré | `netmd_edit` 803 l., `netmd_backup` 240 l., tests 639 l., vue Disque +456 l. |
| Transcriptions d'édition | 9 fichiers, **3 831 lignes**, générées depuis research/01 |
| Capture réelle | `mzn505_real_read.trace`, **617 lignes, 88 commandes**, MZ-N505 + WinUSB |
| Lecture du disque réel | 8 pistes, titre brut `0;202001//1-8;//`, flags `0x10` (inscriptible) |
| Écritures TOC par geste | renommage 1, déplacement 2, effacement de N pistes N+1, groupe 1 |
| Temporisations | 100 ms entre éditions, 500 ms avant re-listage (§6.2) |
| Sauvegarde TOC | 1 fichier texte par écriture, ~60 o par piste, écriture atomique |
| Chaînes i18n | **36 FR + 36 EN** nouvelles (68 au total pour le panneau Disque) |
| `DiscDiff` / `DiscLayout` d'après | 2,1 Ko + 70 Ko en BSS (à zéro dans l'exe) |

### Écarts par rapport au ticket

1. **Aucune écriture n'a été faite sur un disque réel** — P-013, le seul écart de fond. Le pilote
   WinUSB est en place et le disque présent est inscriptible, mais c'est **un disque de
   l'utilisateur** et research/01 §9.4 interdit d'y tester l'écriture ; par ailleurs déclencher un
   renommage demande de piloter l'interface, et aucun drapeau de ligne de commande n'écrit sur le
   disque — ce serait exactement le contournement de la confirmation que D4 interdit. La procédure
   de levée tient en sept étapes dans P-013.
2. **Le ticket dit « Exe < 300 KB »** ; l'exe fait 496 128 o. Ce chiffre datait d'avant les phases
   4 et 5 (plan, jauge, décodeurs, DSP) ; la borne tenue est celle de la commande de livraison
   (500 KB) et du garde-fou CI (`SIZE_BUDGET_KB` = 600).
3. **L'espace pleine chasse n'est pas réécrit, il est effacé.** Tous nos titres sont assainis en
   demi-chasse (D3) ; un titre pleine chasse laissé par un autre logiciel serait alors ce que
   l'appareil affiche à la place de ce qu'on vient d'écrire. Après chaque écriture, si l'espace
   pleine chasse contenait quelque chose, il est remis à vide (une écriture de plus, seulement
   dans ce cas, et une cellule TOC libérée). Réécrire les deux syntaxes de groupes en parallèle
   (§3.10) coûterait une seconde chaîne à garder en cohérence pour un affichage que personne ne
   demande en v1.
4. **Sémantique du déplacement entre groupes.** Le TOC ne connaît que des **plages de positions** :
   il ne dit pas ce que devient une piste tirée d'un groupe dans un autre. Règle retenue et
   testée : une piste déplacée à l'intérieur de son groupe le garde, une piste lâchée ailleurs
   prend le groupe de sa voisine de droite (celle que la ligne d'insertion désigne), et les
   groupes vidés disparaissent. C'est ce qu'un utilisateur attend d'un glisser-déposer ; le
   titre disque est recompilé en conséquence.
5. **Une seule commande device pour toutes les éditions.** Le ticket en énumère six ; elles sont
   portées par le `kind` de la requête plutôt que par six `NetmdCmdKind`, parce que la séquence du
   thread (sauvegarde → écriture → relecture → événement) est rigoureusement la même pour toutes
   et qu'en dupliquer six copies serait six occasions d'en oublier une.
6. **`platform.h` gagne une fonction** : `os_time_local(OsWallClock *)`. L'horloge monotone ne sait
   pas nommer un fichier d'après le moment où il a été écrit. Douze lignes dans
   `win32_platform.c`.
7. **La liste du panneau Disque ne défile toujours pas** (écart n° 6 de T-021, inchangé) : la
   sélection, le renommage et le glisser-déposer sont posés sur les lignes existantes plutôt que
   sur `UI_List`. Passer le panneau en liste virtualisée est un remaniement de la vue, pas de
   l'édition ; il reste à faire quand un disque de 50 pistes deviendra le cas courant (T-043).
8. **`build.bat bench` était cassé avant ce ticket** (accolade fermante manquante à la fin de
   `bench_dsp_pipeline_jobs`, arrivée avec le merge T-021/T-041 du 2026-09-07 ; erreur C1075).
   Corrigé ici en une ligne, sans quoi une des six cibles ne pouvait pas être verte.

### État du vrai device

```
PS> Get-PnpDevice -PresentOnly | Where-Object InstanceId -match 'VID_054C'
FriendlyName : Net MD Walkman
InstanceId   : USB\VID_054C&PID_0084\5&A8846EA&0&1
Status       : OK           (Service : WinUSB — P-001 levé depuis T-021)
```

`minidisk.exe --netmd-trace` a ouvert le device, lu le disque inséré et écrit la transcription :
**88 commandes**, disque de **8 pistes**, `getDiscFlags` = `0x10` (inscriptible, onglet ouvert),
titre brut `0;202001//1-8;//` — un groupe sans nom sur les 8 pistes. La capture est versionnée
sous `tests/netmd/mzn505_real_read.trace` et rejouée par le test `netmd_edit_real_capture`, qui
vérifie que le lecteur, le parseur de groupes et le budget tombent juste sur des octets qu'aucun
générateur n'a inventés. **Aucune écriture n'a été émise vers ce disque** (P-013).
9. **`build.bat bench` reste sensible à la charge machine** (P-010, antérieur) : une fois l'erreur
   de compilation corrigée, il passe machine au repos et retombe sur ses assertions de perf
   (`ns_per_job < 1000`, budget de layout 1 000 µs) quand un autre agent occupe les cœurs. Rien de
   T-022 n'entre dans ces mesures.

### Revue (lead, 2026-09-07)
- Grille ADR-012 dans le worktree `t022` : `check/test/release` verts, **496 128 o**, 221 cas / 6 407 checks.
  Lecture réelle capturée ; aucune écriture émise vers le disque de l'utilisateur, conforme à la consigne.
- Approuvé : `oldLen` relu juste avant l'écriture, une seule réécriture du titre disque, budget partagé avec le plan
  (`plan_toc_compile_raw`), sauvegarde TOC obligatoire. P-013 (validation d'écriture) sera levé par le lead sur le
  disque 202001 avec l'accord explicite de l'utilisateur, par un renommage aller-retour d'une piste.

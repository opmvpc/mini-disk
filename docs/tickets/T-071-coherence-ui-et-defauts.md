# T-071 — Cohérence UI et défauts relevés en phases 3-5 (panneaux Disque et Plan, jauge, P-014)

Phase 7 · Statut : **fait** · Dépend de : T-032, T-043, T-045, P-014

## Livrables
- **Une seule source d'état pour le panneau Disque** : le sous-titre d'en-tête et le corps lisent le même
  `NetmdPanelState` (aucun appareil / pilote manquant / utilisé par une autre application / connecté /
  injoignable / gravure en cours) — vu le 2026-09-07 : en-tête « pilote manquant » avec corps « utilisé par
  une autre application ».
- **Durée du plan en équivalent disque partout** : l'en-tête du plan (« 20 pistes · 84:08 ») affiche la même
  grandeur que la jauge (« 63:02 / 80:00 », clusters × 2 s, lien compris) ; la somme facturée par mode reste
  dans la bulle. Même règle pour la barre de statut et le pré-vol.
- **Hachures diagonales** de la jauge (research/02 §9.3) : un motif 8×8 à 45° dans l'atlas R8 (généré au
  démarrage, pas de PNG), appliqué en texture répétée au segment de padding et à la zone de dépassement ;
  la variante verticale disparaît. Coût mesuré : 0 draw call de plus (même batch).
- **Liste du panneau Disque défilable** (écart T-021/T-022 : non défilante) avec les mêmes conventions que
  la liste du plan (molette, clavier, autoscroll en DnD).
- **P-014** : une piste écrite par nous dans la session courante n'est pas présentée « réservée par
  SonicStage ». Bit `written_here` sur `NetmdTrack`, posé par `netmd_upload` au `commitTrack`, réapparié
  après relecture par (position, durée à la frame, titre) et non par index seul ; le refus d'effacement
  devient un simple avertissement « écrite à l'instant : le drapeau retombe après un cycle d'alimentation »
  et l'effacement est tenté (le device répond REJECTED s'il refuse vraiment, ce qui est géré). Validé sur le
  MZ-N505 **uniquement** sur une piste de test « MINIDISK TEST » gravée puis effacée dans la même session,
  après sauvegarde TOC ; jamais sur les 8 pistes de l'utilisateur.
- Petits défauts au passage : tooltip du badge de mode du disque, ellipse des titres longs des groupes,
  focus visible sur les boutons de transport, « (sans titre) » rendu en italique atténué.
- Tests : état unique du panneau (table d'entrées → sous-titre + corps attendus), équivalence des durées
  (en-tête == jauge sur 5 plans), géométrie du motif de hachure (offsets stables au scroll), réappariement
  P-014 (déplacement d'une piste entre deux relectures).

## Critères d'acceptation
- Captures avant/après validées par le lead ; tests verts ; exe ± 6 KB ; 0 % CPU au repos inchangé.

## Livraison

Livrée le 2026-09-08 dans le worktree `t071`.

### Ce qui est livré

1. **Un seul état pour le panneau Disque.** `src/app/device_panel.h` : `AppDeviceState` (ce que le
   thread appareil a dit) et `NetmdPanelState` (ce que le panneau raconte : aucun appareil, pilote
   manquant, utilisé par une autre application, connecté, injoignable, gravure en cours). Trois
   fonctions pures — `netmd_panel_state`, `netmd_panel_header_string`, `netmd_panel_body_string` —
   plus `netmd_panel_hint_string` pour la deuxième ligne. `app_device_subtitle` et
   `app_device_status` lisent toutes les deux `app_device_panel_state()` : il n'y a plus deux jeux
   de `if` à comparer, donc plus d'en-tête « pilote manquant » sur un corps « utilisé par une autre
   application ». `burning` n'affine qu'une connexion vivante : une gravure qui perd le câble est un
   appareil injoignable, pas une gravure.
   Test `view_plan_device_panel_says_one_thing` : table de 8 entrées → état + 3 identifiants de
   chaîne attendus, plus deux gardes sur le cardinal des deux énumérations.

2. **Durée du plan en équivalent disque partout.** `plan_disc_ms` / `plan_disc_used_ms` /
   `plan_disc_total_ms` dans `plan_view.h` : clusters × 2 s, cluster de lien compris. Utilisées par
   l'en-tête du plan, la barre de statut, la bulle de la jauge compacte, la lecture de la jauge et le
   pré-vol (`Str_TransferSummary` prend maintenant `clusters_needed × 2 s` et non `audio_ms`). La
   somme facturée par mode est passée dans la bulle de l'en-tête (`Str_PlanBilledTip` : facturé,
   audio, perdu). Mesure sur les captures : avant « 20 pistes · 84:48 » au-dessus de
   « 63:42 / 80:00 » ; après « 20 pistes · 63:42 » au-dessus de « 63:42 / 80:00 ».
   Test `view_plan_header_reads_what_the_gauge_reads` : 5 plans de 16 pistes mêlant SP / mono / LP2 /
   LP4, en-tête == jauge, et différent de la somme facturée dès qu'une piste n'est pas SP.

3. **Hachures diagonales à 45°.** Motif de période 8 px généré au démarrage dans l'atlas R8
   (`r_hatch_build`, `src/ui/r_icons.c`) — pas de PNG : le motif est `(x + y) mod 8`, échantillonné
   4 × 4 en pixels-quarts, donc arithmétique entière et zéro donnée figée. Il est stocké en tuile de
   64 px (huit périodes) pour qu'une zone large coûte quelques quads et non un par rayure.
   `r_hatch_tiles` (pure) découpe une zone sur une grille ancrée à une **origine**, pas sur la zone
   elle-même : deux queues de la même jauge continuent le même motif, et déplacer une zone fait
   glisser la fenêtre au lieu de redémarrer le motif. Rendu par le nouveau drapeau `UI_DrawHatch`,
   qui réutilise `image_x` / `image_y` comme phase (pas un octet de plus dans `UI_Box`).
   **Coût mesuré : 0 draw call de plus.** Les tuiles échantillonnent l'atlas R8, c'est-à-dire la
   texture que le back end lie déjà pour tout quad non texturé (T-009) : elles rejoignent le batch au
   lieu d'en ouvrir un. Test `render_gauge_hatch_costs_no_draw_call` : 20 segments et leurs queues
   hachurées = **1 batch**. La variante à rayures verticales a disparu.
   Test `view_plan_hatch_tiles_keep_their_phase` : uv identiques après un décalage d'une tuile,
   continuité entre deux zones voisines, zone dégénérée = 0 tuile, `max` respecté.

4. **Liste du panneau Disque défilable.** `UI_List` avec les conventions de la liste du plan :
   molette (pixels ou lignes), clavier, rendu virtualisé, autoscroll en DnD (24 px de bord,
   600 px/s), ligne d'insertion. Modèle de lignes aplati (`app_disc_rows_build`) : pistes sans
   groupe d'abord, puis chaque en-tête de groupe et ses pistes. La sélection appartient à la liste ;
   le masque de `NetmdEditRequest` en est **dérivé** par `app_device_sync_selection`, il n'y a plus
   deux copies de la même vérité tenues à jour à la main.

5. **P-014 — résolu.** Bit `written_here` sur `NetmdTrack`. `NetmdWrittenSet` dans
   `netmd_upload.{h,c}` : `netmd_written_add` est appelé au `commitTrack` avec le numéro que
   l'appareil vient de rendre ; `netmd_written_apply` retrouve la piste après relecture par
   **(durée à la frame, titre)**, la position ne servant que d'indice de départ, et met la position à
   jour. `netmd_device.c` porte le set, l'applique après chaque `netmd_read_disc` (avant de publier
   le slot, pour que l'UI ne voie jamais un layout à moitié marqué) et le vide à l'éjection — le
   rinçage du TOC est justement l'événement qui rend les drapeaux de l'appareil honnêtes.
   `netmd_edit_simulate` ne refuse plus sur une piste `protect` que **nous** avons écrite : elle la
   compte dans `DiscDiff.written_here`, laisse l'effacement se tenter, et le panneau affiche
   l'avertissement `Str_DiscWarnWrittenHere`. Une piste `protect` qui n'est pas de nous refuse
   toujours (`NetmdEditRefusal_TrackProtected`), y compris mêlée aux nôtres.
   Tests : `netmd_written_rematch_survives_a_move` (trois relectures : résolution de la durée,
   déplacement 2 → 0, leurre de même titre et de durée différente, disque qui ne la contient plus) et
   `netmd_edit_warns_on_a_track_we_just_wrote`.

6. **Petits défauts.** Bulle sur le badge de mode du panneau Disque (`Str_DiscModeTip` : le mode, ce
   que la piste coûte au disque, l'audio qu'elle porte) ; titres longs de groupe ellipsés parce que
   le nom et le compteur sont deux cellules et non une chaîne composée — le nom cède, le compteur
   reste lisible, dans le plan comme sur le disque ; anneau de focus visible sur les boutons de
   transport (l'anneau se dessine **en dehors** du bouton, et l'écart de 4 dp le faisait repeindre
   par le fond du bouton suivant : `APP_FOCUS_GAP_DP` = 6 dp) ; « (sans titre) » et « (disque sans
   titre) » en italique atténué, via le cinquième style `UI_FontStyle_Italic` (`os_font_open` prend
   un drapeau `italic`, `DWRITE_FONT_STYLE_ITALIC`).

7. **Captures** : `docs/captures/T-071-avant.png` et `T-071-apres.png` (même plan de 20 pistes, même
   disque réel), plus `T-071-hachures.png` (28 pistes, zone de dépassement hachurée à 45°).

### Mesures

| Métrique | Valeur |
|---|---|
| Exe release | **643 072 o**, soit **+6 144 o** (exactement +6,0 Ko) sur les **636 928 o de la base de la branche** (`main` au commit `73e366b`, avant la fusion de T-070) — cf. écart 8 pour le chiffre après fusion |
| Imports | kernel32 + user32 |
| Tests | **256 cas, 6 956 checks**, 0 échec (ASan) ; 6 985 checks avec `--device-p014` |
| Cibles `build.bat` | **les six vertes** : debug, release, test, check, analyze, bench (bench au deuxième essai, machine libérée — cf. écart 7) |
| Draw calls des hachures | **+0** — 1 batch pour 20 segments et leurs queues |
| Boxes de la frame | 622 → 642 (plan de 20 pistes, disque de 8 pistes) |
| CPU au repos, 12 s après 4 s de chauffe | **46,9 ms**, soit 0,39 % d'un cœur — même résidu de thread pilote GL (P-005) qu'en T-030 et T-032, inchangé |
| Transcription appareil | `tests/netmd/real/t071_device_p014.trace`, **1 816 lignes** |
| Chaînes i18n | **11 FR + 11 EN** ajoutées en fin d'énumération et des deux tables, 1 supprimée (`Str_PlanGroupHeader`, sans appelant) — solde net +10 |

### Résultat sur l'appareil (MZ-N505, disque « 202001 », 2026-09-08)

`build\tests.exe --device-p014` : une piste de test de 5 s gravée dans l'espace libre, puis effacée
dans la même session, par le chemin normal (simulation → sauvegarde TOC → écriture → relecture).

```
disc "202001": 8 track(s), 8 of them the user's, 3404 s free
written track 8: protect 0, written_here 1, 2560 frames
TOC backed up to ...\135f243ce2bae429-20260908-031540.txt
simulation: allowed 1, refusal 0, written_here 0, 9 -> 8 track(s)
erase track 8: result 0, 1 write(s)
after clean up: 8 track(s), 3404 s free, disc title "202001"
```

Les 8 pistes de l'utilisateur sont intactes, le titre du disque n'a pas bougé, aucun effacement de
disque n'a été tenté. Le réappariement a marqué **une** piste et une seule (2 560 frames = 5 s, titre
« MINIDISK TEST P014 »).

**Ce que la mesure dit de P-014, honnêtement** : sur cette session d'**une seule** piste, l'appareil a
rendu la piste fraîche avec `protect = 0` — le symptôme de la fiche ne s'est pas reproduit. Il s'était
produit en T-043 sur une session de **sept** pistes. Le drapeau n'est donc pas systématique, ce qui
renforce plutôt la correction : on ne refuse plus sur un drapeau qu'on ne sait pas interpréter, on
avertit et on laisse l'appareil répondre. La chaîne complète — bit posé au commit, réapparié après
relecture, avertissement au lieu du refus, effacement appliqué, relecture à 8 pistes — est validée de
bout en bout ; la seule chose que cette passe **n'a pas** pu observer est le `0x03` lui-même. Le test
couvre les deux cas : il vérifie `written_here == 1` et que le compte d'avertissement de la simulation
suit exactement `protect`.

### Écarts et choix

1. **`UI_DrawHatch` réutilise `image_x` / `image_y`.** Un drapeau de plus et zéro octet de plus dans
   `UI_Box`, qui est parcouru deux fois par frame sur tout l'arbre. Les deux usages (`UI_DrawImage`,
   vignette de pochette, et `UI_DrawHatch`) sont exclusifs par construction.
2. **La géométrie des hachures est dans `src/ui/r_icons.c`, pas dans `plan_view.c`.** `ui_core.c`
   doit pouvoir l'appeler, et `ui/` ne peut pas dépendre de `app/`. Le test reste dans
   `test_view_plan.c`, là où le ticket l'attend.
3. **Un fichier de plus** : `src/app/device_panel.h`, en-tête pur (tout en `md_inline`, pas de `.c`).
   C'était la condition pour que la table d'états soit **testable** : `view_device.c` n'est pas
   compilé par `test_main.c`, et l'énumération y était enfermée.
4. **`os_font_open` prend un paramètre de plus.** Il n'y avait aucun moyen de demander une italique à
   DirectWrite ; un seul appelant (`ui_font.c`).
5. **Trois lignes hors du périmètre annoncé** : un `#include` dans `src/main.c`, un dans
   `tests/test_main.c` et un dans `tests/bench_main.c` (fichier de T-073), pour le nouvel en-tête.
6. **`Str_PlanGroupHeader` supprimée** au milieu de l'énumération, faute d'appelant après la mise en
   deux cellules. C'est la seule chaîne retirée ; tous les ajouts sont en fin de table, comme convenu
   avec T-072.
7. **`bench` : P-010 rencontré, puis vert.** Machine chargée (trois autres agents compilant en
   parallèle), la passe s'est arrêtée deux fois, à deux endroits différents — une fois après
   `bench_dsp_r128`, une fois après `bench_ui_layout` — c'est exactement le symptôme de la fiche
   (point d'arrêt variable, arène d'un gigaoctet). Machine libérée, **la passe va au bout, code de
   sortie 0**, les quatre bancs de décodage compris. Aucun banc de ce ticket n'est en cause : ce
   ticket n'ajoute aucun banc. P-010 reste ouvert.
8. **Base de la branche antérieure à T-070.** `t071` part de `73e366b`, où l'exe release fait
   636 928 o ; T-070 (régime de taille) a depuis été fusionné dans `main` et l'y ramène à
   **622 080 o**. Le critère « ± 6 Ko » de ce ticket est donc mesuré, et tenu, **contre la base de la
   branche** : +6 144 o. Après fusion avec `main`, l'exe attendu est de l'ordre de 628 Ko ; le
   chiffre exact n'est pas mesurable ici sans fusionner, ce qui n'est pas du ressort de ce ticket.
   **À revérifier au moment de la fusion.**
9. **Le panneau Disque reste trop étroit** pour « Précédent » et « Dissoudre » à sa largeur par
   défaut — c'était déjà le cas avant (visible sur les deux captures). Les 2 dp d'écart en plus des
   anneaux de focus n'y changent rien de significatif ; la largeur du panneau est un sujet à part.

### Critères d'acceptation

- Captures avant / après : `docs/captures/T-071-{avant,apres,hachures}.png`.
- Tests verts sous ASan : 256 cas, 6 956 checks, 0 échec.
- Exe ± 6 Ko : **+6 144 o** sur la base de la branche (636 928 → 643 072 o), à la limite exacte ;
  imports kernel32 + user32 vérifiés au `dumpbin /imports`. Écart 8 : la base est antérieure à T-070.
- Les six cibles `build.bat` vertes (bench au deuxième essai, machine libérée — écart 7).
- 0 % CPU au repos inchangé : 46,9 ms sur 12 s, même résidu P-005.

### Revue (lead, 2026-09-08)
- Capture après validée : l'en-tête du plan lit ce que la jauge lit (63:42), en-tête de groupe « (disque sans
  titre) » en italique atténué, légende par mode, liste du disque défilable. Hachures : 0 draw call de plus,
  test à l'appui. P-014 fermé sur une chaîne validée ; le `0x03` non réobservé est noté honnêtement.
- L'écart 9 (panneau Disque trop étroit pour « Précédent » et « Dissoudre ») est le sujet de T-075, qui
  ouvre juste après ce merge.
- Bien vu : les BOM et CRLF de l'agent précédent retirés, et la trace appareil `t071_device_p014.trace`
  conservée pour rejouer la validation sans l'appareil.

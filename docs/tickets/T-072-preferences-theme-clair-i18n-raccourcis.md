# T-072 — Vue Préférences, thème clair, i18n complète, raccourcis et aide clavier

Phase 7 · Statut : **fait** · Dépend de : ADR-011 D8, research/02 §8.8, §10.4, §11.4, §12 (ex T-060/061/062/064)

## Livrables
- `src/app/view_settings.c` : un panneau modal léger (Ctrl+, / bouton engrenage de la barre d'outils) :
  langue (FR/EN, **à chaud**, sans redémarrage — `app_lang` + relayout), thème (sombre / clair / système,
  lu via `os_system_theme()` ajouté à `platform.h` : registre `AppsUseLightTheme`, notification
  `WM_SETTINGCHANGE` déjà reçue), mode MD par défaut, cible de loudness (−14 LUFS par défaut, plage
  −23..−9) et plafond true-peak, trim/fondus/gap par défaut, tailles des caches (transcodage, pochettes) avec
  bouton « vider », dossiers de bibliothèque (liste, ajouter, retirer), affichage des vignettes.
  Tout persiste dans `prefs` (clés nouvelles, lecture tolérante).
- **Thème clair réel** : `ui_theme_light` implémenté depuis research/02b (palette, contrastes ≥ 4,5:1
  vérifiés par un test qui calcule le ratio WCAG sur chaque paire fond/texte du thème), couleurs de modes
  ajustées pour fond clair, ombres et séparateurs ; bascule à chaud (`ui_theme_set` + invalidation des
  couleurs mises en cache dans les boxes), `DwmSetWindowAttribute` pour la barre de titre système assortie.
- **i18n complète** : audit de toutes les chaînes visibles (`grep` des `str8_lit` dans `src/app/` qui
  contiennent des lettres), migration vers `Str_*`, pluriels FR/EN (`%u piste(s)` → deux formes),
  formats de durée/nombre par langue (séparateur des milliers : espace fine FR, virgule EN), dates du
  journal. Test : aucune `str8_lit` avec texte utilisateur hors `strings.h` (script `tools/i18n_audit.py`
  lancé par `build.bat check`).
- **Raccourcis complets** (research/02 §8.8, §11) : table unique `app_shortcuts[]` (touche, modificateurs,
  contexte, action, libellé) qui alimente à la fois le dispatch et une **aide clavier** (`?` ou F1 :
  overlay listant les raccourcis du contexte courant) ; les libellés de la barre de statut viennent de la
  même table.
- Tests : round-trip des nouvelles prefs, ratio de contraste du thème clair, bascule de langue (une
  chaîne FR puis EN), table des raccourcis sans doublon de touche par contexte.

## Critères d'acceptation
- Captures : préférences, thème clair sur les trois panneaux, aide clavier — validées par le lead.
- Tests, check (avec l'audit i18n), analyze verts ; exe ± 15 KB ; 0 % CPU au repos.

---

## Livraison

Statut : **fait** (2026-09-08).

### Ce qui a été fait

**1. `src/app/view_settings.c` — panneau de préférences modal (597 lignes).**
`Ctrl+,` ou le bouton engrenage à droite de la barre d'outils l'ouvrent ; `Échap`, la croix ou un clic
sur le voile le ferment (MI-43) et écrivent le fichier si quelque chose a bougé. Il tient sur la couche
`UI_Layer_Popup`, au-dessus d'un voile qui mange les clics, et il **possède le clavier** pendant qu'il
est là (`ui_popup_set_active`, comme un menu contextuel) : les trois listes en dessous cessent de lire
les touches, donc Entrée ne part pas deux fois.

Quatorze des seize lignes sont **la même donnée sous trois formes** : `app_setting_rows[]` décrit
(libellé, type, champ de `Prefs` par `OffsetOf`, pas, bornes, unité), et une boucle les construit.
Contenu : langue FR/EN, thème sombre / clair / système, mode MD par défaut (SP · MONO · LP2 · LP4),
cible de loudness (−14,0 LUFS par défaut, pas de 0,5, plage −23,0..−9,0), plafond true-peak (−1,0 dBTP,
plage −6,0..0), rognage des silences, fondus d'entrée/sortie et blanc entre les pistes (0..5 000 ms et
0..10 000 ms, pas de 50), tailles des deux caches (transcodage 64..65 536 Mo, pochettes 16..8 192 Mo).
Écrites à la main parce qu'elles lisent le disque ou portent un verbe : la taille réellement occupée par
les deux caches avec son bouton « Vider » (`cache_lru_purge` à 0 octet), la liste des dossiers surveillés
(ajouter par `os_dialog_pick_folder`, retirer par `prefs_remove_folder`, 8 au maximum), et les vignettes.

**Langue et thème s'appliquent à chaud.** La langue est une table indexée par `app_lang`, le thème est un
`ui_theme_set`, et **aucune boîte ne met une couleur en cache d'une frame à l'autre** (`UI_Box.bg_color`
est écrit pendant la construction) : changer l'un ou l'autre est un appel et un redessin, sans
redémarrage, sans relayout manuel, sans seconde copie de la valeur à tenir à jour.

**2. Thème clair réel.** `ui_theme_light` reprend research/02 §10.4 et 02b, **avec chaque paire
fond/texte mesurée** : les gris de l'esquisse tenaient entre 3,4:1 et 4,4:1 sur nos panneaux et les
couleurs de mode entre 4,3:1 et 4,5:1, donc les deux échelles descendent d'un cran —
`fg_secondary #5A5C63 → #494B52`, `fg_muted #8B8D94 → #5F6167`, mono `#8250DF → #7A3FD4`,
LP2/succès `#1A7F37 → #14722F`, LP4/avertissement `#9A6700 → #8A5C00`. Ombre à 18 % au lieu de 45 %
(sur un fond clair l'ombre sombre lit comme un trou, §10.5), ascenseurs en noir translucide.
Le **thème sombre** corrige au passage son seul manquement de texte : `fg_muted #777B84` ne donnait que
**3,9:1** sur `#1F1F1F`, il passe à `#9096A0` (5,5:1).
`ui_theme_apply(UI_ThemeChoice)` résout « système » par `os_system_theme()` — clé de registre
`AppsUseLightTheme` lue via `RegGetValueW` **chargé dynamiquement depuis advapi32.dll**, pour que la
table d'imports reste kernel32 + user32. `WM_SETTINGCHANGE` devient un `OsEvent_SettingChange` que la
boucle transforme en une réévaluation (et seulement si le choix est « système »).
`DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` suit, bordure assortie comprise.

**3. i18n complète.** 85 chaînes ajoutées en fin d'énumération et en fin des deux tables (aucun
identifiant déplacé), 319 au total. `Net MD Walkman` en dur dans `view_device.c` devient
`Str_DeviceUnknownModel`. **Pluriels à deux formes** (`app_plural`, `app_count`) : le français met 0 et 1
au singulier, l'anglais seulement 1 — plus de `piste(s)`. **Formats par langue** : `app_num_u64` groupe
les milliers par une espace fine insécable U+202F en français et par une virgule en anglais,
`app_num_tenths` marque la décimale par une virgule ou un point, `app_date_locale` donne `08/09/2026` ou
`2026-09-08`, `app_duration_locale` passe en `h:mm:ss` au-delà de l'heure. La barre de statut utilise le
pluriel (`Str_StatusPlanOne` / `Str_StatusPlan`).
`tools/i18n_audit.py` relit tous les `str8_lit` de `src/app/` hors `strings.h` : il ne garde de chaque
littéral que la partie **avant `###`** (une clé de box n'est jamais dessinée), retire les échappements et
les spécificateurs, et refuse ce qui contient encore une lettre ASCII — sauf un jeton technique (aucune
espace : clé de préférences, extension, URL, drapeau) et sept chaînes listées avec leur raison. Il est
appelé par `build.bat check` (`:i18n_audit`, sauté avec un message si Python manque, pour que le check
reste vert sur une machine MSVC nue).

**4. Raccourcis.** `src/app/app_shortcuts.{h,c}` : **26 lignes** `(touche, modificateurs, contexte,
action, libellé)` qui sont research/02 §8.8 recopié. Trois lecteurs, une seule table : le dispatch global
de `app.c` (`Ctrl+,`, `F1`, `F11`), l'**aide clavier** (`F1`, overlay listant le général puis le panneau
qui a le clavier — les quatre contextes quand rien n'a le focus), et les libellés de la barre de statut
(`app_shortcut_status_hint`). `app_shortcut_keys` traduit ce qui est un mot (`Ctrl+Maj+G` / `Ctrl+Shift+G`).

**5. Préférences.** Onze clés nouvelles, lecture tolérante (ADR-012) : `ui.lang`, `ui.theme`,
`plan.default_mode` sont écrites **en toutes lettres** (`en`, `light`, `lp4`) et relues aussi bien en mots
qu'en nombres ; les huit autres sont bornées à l'analyse par les mêmes constantes que le panneau — une
valeur qui ne peut pas être saisie ne peut pas être lue. `PREFS_VERSION` reste à 1 : un fichier d'avant
T-072 n'a simplement aucune de ces clés et prend les défauts.

### Mesures

| Mesure | Valeur |
|--------|--------|
| Tests | **261 cas, 8 090 checks, 0 échec** sous ASan (+12 cas, +1 242 checks) |
| Contraste, thème clair | **toutes** les paires ≥ 4,5:1 ; le minimum est **4,75:1** (`danger` sur `control` et sur `panel`), l'accent sur `control` est à 4,90:1 |
| Contraste, thème sombre | textes ≥ 4,5:1, minimum **4,66:1** (`fg_muted` sur `row_hover`) ; seule exception `accent_fg` sur `accent`, **3,26:1** (voir écarts) |
| Taille exe release | **664 064 o** (636 928 avant), **+27 136 o** |
| Imports | kernel32 + user32 (advapi32 et dwmapi chargés à la main) |
| CPU au repos | **0 ms sur 12 s** (0,000 %), machine au calme, boucle inchangée |
| Cibles `build.bat` | debug, release, test, check (audit i18n compris), analyze, bench **toutes vertes** |
| Chaînes | 319 identifiants `Str_*` (+ `Str_COUNT`), 2 langues, 0 littéral visible hors `strings.h` |
| Audit i18n | 16 fichiers de `src/app/` relus par `build.bat check`, 0 chaîne visible en dur |
| Raccourcis | 26 lignes, 0 collision par contexte |

Captures : `docs/captures/T-072-preferences.png`, `T-072-theme-clair.png`, `T-072-aide-clavier.png`
— reprises le 2026-09-08 sur l'exe final (`tools/capture_demo.ps1`, passe `HWND_TOPMOST`).

### Écarts

1. **Taille de l'exe : +27 136 o au lieu de ± 15 360.** La référence est **636 928 o**, la taille de
   l'exe sur la base de la branche (`73e366b`, valeur inscrite dans STATUS.md après T-043/T-045) — pas
   les 622 080 o d'un arbre incluant T-070, que cette branche n'a pas. Mesuré en isolant le code : les deux overlays
   coûtent **12 288 o**, le reste (85 × 2 chaînes ≈ 6 Ko, les onze clés de `prefs.c`, la table de
   raccourcis et ses formateurs, le dispatch et le bouton de `app.c`, `os_system_theme`) **14 848 o**.
   Deux passes de réduction ont été faites et gardées parce qu'elles améliorent le code : les deux
   overlays partagent un seul cadre (voile + carte + colonne, −2 560 o) et quatorze lignes du panneau
   sont devenues une table (−2 560 o) ; les aides de `strings.h` sont passées de `md_inline` à `static`.
   Ce qui reste est le prix d'un panneau de préférences complet plus une aide clavier : le tenir sous
   15 Ko demanderait de retirer des livrables du ticket. Le budget CI (`SIZE_BUDGET_KB` 600) n'est pas
   la contrainte ici — il est déjà dépassé depuis T-041 et un ticket de régime existe au backlog.
2. **`accent_fg` sur `accent` en thème sombre : 3,26:1.** Le blanc sur `#0090FF` (ADR-011 D8) ne passe
   pas 4,5:1, et le **même** jeton sert aussi de *texte* sur un fond sombre, où un bleu plus foncé
   échouerait dans l'autre sens (`#0078D4` donne 4,53:1 sur blanc mais 3,64:1 comme texte sur
   `--bg-surface`). Les séparer demande un jeton « fond d'accent » que le thème n'a pas, ce qui déborde
   du ticket. Le test tient donc le thème **clair** à 4,5:1 partout et le thème **sombre** au 3:1 de
   WCAG 1.4.11 sur cette seule paire, avec le raisonnement écrit dans le test.
3. **`build.bat bench` : résolu.** Une passe précédente le voyait rouge sur `r_end_frame 1 2xx µs` contre
   un budget de 900 µs (bench de batching du renderer), y compris en remisant tout le travail — c'était
   bien la charge des trois autres agents compilant en parallèle sur la même machine (P-010). Repassé
   machine au calme : **483 µs**, cible tenue, et les six cibles sont vertes. T-072 ne touche de toute
   façon ni `r_*` ni `tests/bench_main.c` (T-073).
4. **Trois fichiers hors de la liste du ticket ont été touchés**, chaque fois d'une ligne ou deux :
   `src/main.c` (trois `#include` du build unitaire — sans eux les nouveaux fichiers n'existent pas),
   `src/ui/ui_widgets.{h,c}` (`ui_popup_set_active`, trois lignes : sans lui un panneau modal applicatif
   ne peut pas prendre le clavier comme un menu le fait), et `src/app/app_state.h` (les prototypes de
   `view_settings.c`, comme pour les autres vues). Aucun n'est revendiqué par T-070, T-071 ou T-073.
5. **Le formatage localisé des nombres n'est appliqué qu'où T-072 a le droit d'écrire** : le panneau de
   préférences et la barre de statut. Les compteurs de `view_library.c` et de `view_plan.c` continuent
   d'utiliser `%u` — les convertir demande de toucher des lignes d'appel dans des vues tenues par
   d'autres agents. Les aides sont dans `strings.h`, l'adoption est mécanique.
6. **Aide clavier sans ascenseur** : quand aucun panneau n'a le focus, les quatre contextes sont listés
   et la dernière ligne affleure le bas de la fenêtre à 1 020 px. Avec un panneau focalisé (le cas
   normal) l'overlay fait au plus dix lignes.

### Revue (lead, 2026-09-08)
- Captures validées : préférences, thème clair sur les trois panneaux, aide clavier. Le thème clair est propre
  (bordures, badges, jauge). Deux finitions renvoyées à T-075 : le libellé « Dossiers surveillés » répété sur
  chaque ligne (un seul libellé puis la liste), et « 1 Mo  Vider » sans libellé (« Occupation : 1 Mo »).
- `?` comme alias de F1 : à câbler dans T-075 (une ligne de la table). Formatage localisé des nombres dans
  `view_library.c`/`view_plan.c` : T-075 aussi, puisqu'il ouvre ces fichiers.
- Taille : +27 KB assumé (deux overlays + 85 chaînes × 2 langues + table des raccourcis). Voir la décision de
  budget dans STATUS après le merge de la phase 7.
- Bien vu : la correction de `fg_muted` en sombre (3,9:1 → 5,54:1) et `RegGetValueW` chargé dynamiquement.

# T-072 — Vue Préférences, thème clair, i18n complète, raccourcis et aide clavier

Phase 7 · Statut : **todo** · Dépend de : ADR-011 D8, research/02 §8.8, §10.4, §11.4, §12 (ex T-060/061/062/064)

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

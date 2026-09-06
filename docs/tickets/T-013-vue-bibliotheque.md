# T-013 — Vue Bibliothèque connectée au core : colonnes, tri, navigateur, recherche, sélection

Phase 2 · Statut : **fait** · Dépend de : T-012, research/02 §8.3 et §10

## Livrables
- `src/app/view_library.c` : la démo factice de T-007 devient la vraie vue :
  - en-têtes de colonnes cliquables (tri asc/desc, indicateur), largeurs redimensionnables (persistées),
    colonnes : #, Titre, Artiste, Album, Durée, Format (badge codec + kHz), Année, Date d'ajout ;
  - navigateur par colonnes (Artiste | Album) au-dessus de la liste, repliable, multi-sélection qui filtre ;
  - champ de recherche (Ctrl+F, Esc efface) branché sur `lib_search` ; compteur "N / total" ;
  - liste virtualisée branchée sur l'index trié (aucune copie : la liste lit `TrackId` → SoA) ;
  - sélection multiple, Ctrl+A, Entrée = ajouter au plan (stub jusqu'à T-030), clic droit menu ;
  - état "vide" (aucun dossier : gros bouton "Ajouter un dossier" + drag & drop), état "scan en cours"
    (barre de progression discrète en haut de liste), état "aucun résultat".
- `src/app/app_state.{h,c}` : l'état applicatif (core handles, sélection, vues) séparé de `app.c`.
- Préférences (ADR-010) : dossiers de bibliothèque, colonnes, tri, largeurs, taille/position fenêtre —
  fichier texte `minidisk.prefs` dans `%APPDATA%\minidisk\` ou à côté de l'exe si `portable` existe.
- Tests : `tests/test_prefs.c` (round-trip, valeurs manquantes → défauts, fichier corrompu → défauts).

## Critères d'acceptation
- 100k pistes : tri par clic < 50 ms perçu, recherche fluide à la frappe, scroll 60 fps, 0 % CPU au repos.
- Tout faisable au clavier (Tab entre champ / navigateur / liste, flèches, Entrée, Ctrl+F, Esc).
- Capture d'écran validée par le lead. Exe < 240 KB. Tests, check, analyze verts.

---

## Livraison (2026-09-07)

### Ce qui est livré
- `src/app/app_state.{h,c}` : tout l'état applicatif (core, widgets retenus, plan, préférences, scan,
  index) dans un `AppState` unique, séparé de la boucle de frame restée dans `app.c`. La démo générée de
  100 000 pistes factices de T-007 est **supprimée** : la vue lit la vraie bibliothèque et rien d'autre,
  ce qui rend l'état « vide » atteignable et enlève ~90 lignes.
- `src/app/view_library.c` : la vue Bibliothèque complète.
  - **Colonnes** `#` / Titre / Artiste / Album / Durée / Format (badge codec + kHz) / Année / Ajouté.
    En-têtes cliquables (tri asc/desc avec indicateur ▴▾) sur les cinq colonnes que `lib_index` sait
    trier ; poignée de redimensionnement flottante sur le bord droit de chaque colonne fixe, largeur
    écrite dans les préférences pendant le drag. Le titre prend ce qui reste, jamais moins de 120 dp.
  - **Largeurs** calculées une fois par frame (`app_columns_measure`) et partagées par l'en-tête et les
    lignes, au pixel près : aucune colonne n'est jamais rognée. Quand le panneau est trop étroit, les
    colonnes partent dans l'ordre Ajouté → Année → Format → Album → Durée → # (research/02 §8.7) avant
    que le reste ne rétrécisse. Clic droit sur l'en-tête : menu de visibilité des colonnes (persistée).
  - **Navigateur Artiste | Album** repliable (chevron + info-bulle, état persisté), multi-sélection qui
    filtre la recherche.
  - **Recherche** branchée sur `lib_search` : Ctrl+F donne le focus et sélectionne le texte, Échap
    efface, compteur « N / total » dans l'en-tête du panneau.
  - **Liste** liée à l'index trié, **zéro copie** : `app_rows()` est le buffer de `LibSearch`, une ligne
    lit ses champs dans le SoA et les oublie. Sélection multiple, Ctrl+A, Entrée / double-clic ajoutent
    au plan (stub T-030), clic droit → menu contextuel.
  - **États** : vide (titre, deux lignes, gros bouton « Ajouter un dossier… », rappel du dépôt), scan en
    cours (barre de progression de 2 dp en haut de liste + compteur fichiers/dossiers dans l'en-tête),
    aucun résultat (« Aucun résultat pour « … » » + suggestion d'Échap).
  - **Drag & drop** : un dossier déposé sur la fenêtre est ajouté à la bibliothèque et scanné (un
    fichier déposé vaut pour son dossier).
- `src/app/prefs.{h,c}` (ADR-010) : fichier texte `clé=valeur`, `%LOCALAPPDATA%\minidisk\minidisk.prefs`
  ou à côté de l'exe si un fichier `portable` s'y trouve. Clés : `folder` (jusqu'à 8), `sort.column`,
  `sort.desc`, `browser.collapsed`, `column.<nom>.{width,order,visible}`, `window.{placed,x,y,width,
  height,maximized}`. Écriture atomique `.tmp` + `MoveFileEx`. Frontière stricte (ADR-012) : `version=1`
  absente ou différente → défauts et rien d'autre ; clé inconnue, valeur illisible ou hors bornes →
  ignorée silencieusement ; `order` qui n'est pas une permutation → ordre naturel rétabli.
- `src/app/strings.h` (ADR-011 D10) : un `enum Str` et deux tableaux plats FR/EN, défaut FR à la
  compilation (`APP_LANG_DEFAULT`). Plus aucune chaîne utilisateur en dur ailleurs : 48 entrées.
- `tests/test_prefs.c` : 7 cas (aller-retour complet, valeurs manquantes, fichier corrompu / sans
  version / vide / binaire, bornage de chaque nombre, permutation cassée, dossiers dédupliqués et
  saturés, aller-retour par le disque avec vérification qu'aucun `.tmp` ne reste).
- Bancs ajoutés à `tests/bench_main.c` : `sort click 100k` (ce que coûte vraiment un clic d'en-tête) et
  `prefs serialize + parse`.

### Le bug de layout de T-012, corrigé
Le panneau Bibliothèque était forcé à 640 dp et poussait le panneau Disque hors de l'écran. La cause
n'était pas la constante : la box conteneur des trois panneaux était construite **sans clé**
(`ui_build_box_from_key(0, 0)`), donc reconstruite à neuf à chaque frame avec un `rect` nul. Comme
`ui_splitter_update` sort immédiatement quand `total <= 0`, **aucun bornage n'a jamais été appliqué**
depuis T-007 : les splitters gardaient leur taille quoi qu'il arrive. Détail dans `problems/P-008`.
Corrigé par : une box `###body` clé, des minimums croisés (le disque est borné contre biblio + plan, la
bibliothèque contre plan + largeur réelle du disque), 300 / 280 / 200 dp de minimum, et un facteur de
DPI appliqué aux tailles de splitter quand l'échelle change. **Vérifié à 1024 × 640 logique** (fenêtre
1280 × 800 à 125 %) : bibliothèque 597 px, plan 357 px, disque 300 px, cinq colonnes visibles.

### Mesures (i7-8550U, Windows 11, écran 1920×1080 à 125 %)
| Mesure | Valeur | Cible |
|--------|--------|-------|
| Taille exe release | **207 872 o** (marge 37 888 o) | < 240 KB, CI 250 KB |
| Imports | kernel32 + user32 | ces deux-là |
| Tests | **111 cas, 1 875 checks, 0 échec** (ASan) | verts |
| `check` / `analyze` | verts (`/W4 /WX /analyze` + clang-tidy) | verts |
| Clic de tri, 100 000 pistes | **36,45 ms** (ordre construit + 100 000 ids réémis) | < 50 ms |
| Recherche « the » sur 100 000 | 4,06 ms ; raffinée « the b » : 0,976 ms | fluide à la frappe |
| Scroll | liste virtualisée, 98 boxes pour 16 lignes visibles, 225 boxes dans la frame | 60 fps |
| CPU au repos, 12 s, après un scan | **46,9 ms** et **78,1 ms** sur deux mesures : 0,4 à 0,65 % d'un cœur, 0,05 à 0,08 % des 8 threads — le résidu du thread pilote GL de P-005, nos threads dorment | 0 % |
| Préférences | 789 o écrits, 13 µs l'aller-retour sérialisation + parsing | — |

Captures : `build/demo.png` (32 pistes, navigateur ouvert, recherche « django », 8 colonnes) et
`build/demo_empty.png` (état vide).

### Écarts et décisions
- **`/utf-8` ajouté à `build.bat`** : `strings.h` contient de vrais accents. Sans ce drapeau MSVC relit
  la source en codepage ANSI et l'UTF-8 arrive cassé dans l'exe. Toutes les autres sources sont ASCII,
  aucun effet de bord.
- **`platform.h` s'agrandit de quatre entrées** : `os_exe_dir` (mode portable), `os_window_show`,
  `os_window_get_placement`, `os_window_set_placement`. La fenêtre est désormais **créée cachée** et
  affichée après la première frame — ce que le commentaire de `os_window_create` prétendait déjà. La
  taille de la fenêtre est réappliquée juste après l'affichage, car le DPI réel du moniteur n'est connu
  qu'à ce moment ; `os_window_set_placement` borne la taille à la zone de travail de l'écran.
- **`tools/capture_demo.ps1` appelle `SetProcessDPIAware`** : sans cela PowerShell capture des
  coordonnées virtualisées et la capture était floue et rognée du panneau Disque — le bug était dans
  l'outil de capture, pas dans l'app. Le script accepte aussi `-Folder ""` pour l'état vide.
- **Ordre des colonnes** : lu, écrit et respecté par la vue, mais **pas encore réordonnable à la
  souris** (glisser un en-tête). La visibilité, elle, est éditable par le menu de l'en-tête.
- **Tailles des splitters non persistées** : hors de la liste de clés du ticket ; elles se recalculent
  à partir des minimums et du DPI à chaque lancement.
- `APP_TRACK_MAX` = 300 000 : les buffers de recherche, de navigateur et de sélection sont dimensionnés
  une fois ; au-delà c'est un bug de dimensionnement, `AssertAlways` (ADR-012).

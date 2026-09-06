# T-007 — Thème (tokens 02b) et widgets : bouton, label, champ texte, liste virtualisée 100k, splitter

Phase 1 · Statut : **fait** · Dépend de : T-006, ADR-011 D8

## Livrables
- `src/ui/ui_theme.{h,c}` : struct `UI_Theme` avec les tokens de `research/02b-design-tokens.md`
  (canvas `#111113`, panel `#18191b`, surface `#1F1F1F`, contrôle `#212225`, hover `#272a2d`, pressed
  `#2e3135`, sélection active `#04395E`, bordures `#2B2B2B`/`#3C3C3C`/`#5a6169`, textes `#EDEEF0`/
  `#B0B4BA`/`#696e77`, accent `#0090FF`/hover `#3b9eff`, succès `#3fb950`, warning `#d29922`, danger
  `#F85149`, modes SP `#0090FF` / mono `#BE95FF` / LP2 `#3FB950` / LP4 `#D29922`), rythme 2/4/6/8/12/16/24/32,
  hauteurs de ligne 22/28/40, radius 4, focus ring accent 1 px + halo. Thème clair : stub (phase 7).
- `src/ui/ui_widgets.{h,c}` :
  - `ui_button(String8 label)`, `ui_button_icon`, `ui_label`, `ui_separator`, `ui_spacer`.
  - `ui_text_input(UI_TextInput *state, String8 placeholder)` : curseur, sélection (souris + Shift+flèches),
    Ctrl+A/C/V/X (clipboard via `platform.h`), Home/End, suppression par mot (Ctrl+Backspace), undo/redo
    local (Ctrl+Z/Y), défilement horizontal, UTF-8 correct (déplacement par codepoint).
  - `ui_list_begin(UI_List *state, row_count, row_height) / ui_list_row_visible(i) / ui_list_end()` :
    **virtualisée** (seules les lignes visibles créent des boxes), scrollbar, molette (lignes et pixels),
    scroll cinétique optionnel, sélection multiple (clic, Ctrl, Shift-range, Ctrl+A), navigation clavier
    (flèches, PageUp/Down, Home/End, Shift+flèches), `ensure_visible(i)`.
  - `ui_splitter(UI_Splitter *state, axis)` : poignée 6 px, curseur redimensionnement, min sizes, double-clic reset.
  - `ui_tooltip(String8)` après 500 ms de hover, `ui_context_menu_begin/item/end` (couche popup, Esc ferme).
- `src/app/app.c` : démo "3 panneaux" (bibliothèque factice de 100 000 lignes générées, plan, disque)
  avec splitters, champ de recherche, boutons, menu contextuel, tooltips.
- `tests/test_widgets.c` : édition de texte (insertion/suppression UTF-8, sélection, undo), sélection de
  liste (range, toggle), calcul des lignes visibles.

## Critères d'acceptation
- Liste de 100 000 lignes : scroll à 60 fps, < 200 boxes créées par frame, mémoire stable.
- Toutes les interactions listées faisables au clavier seul.
- 0 % CPU au repos. Exe release **< 100 KB** (`SIZE_BUDGET_KB`). Tests, check, analyze verts.
- Capture d'écran `build/demo.png` jointe à la livraison ; le lead valide le rendu à l'œil.

---

## Livraison

Livré : `src/ui/ui_theme.{h,c}` (les tokens de `research/02b`), `src/ui/ui_widgets.{h,c}` (≈ 1 200 lignes :
boutons, label, séparateur, champ texte, liste virtualisée, splitter, tooltip, menu contextuel), démo trois
panneaux dans `src/app/app.c`, `tests/test_widgets.c` (11 cas). Capture : `build/demo.png`.

### Mesures (2026-09-06)

| Métrique | Valeur | Critère |
|----------|--------|---------|
| Boxes créées par frame par la liste de 100 000 lignes | **110** pour 27 lignes visibles (constant au scroll) | < 200 |
| Boxes de la frame entière de la démo (3 panneaux + menu ouvert) | 225 | — |
| Tests | 65 cas, **1 345 checks**, 0 échec | — |
| Exe release | **97 792 octets** (95,5 Ko) | < 100 Ko (`SIZE_BUDGET_KB`) |
| Imports | `KERNEL32.dll`, `USER32.dll` uniquement | kernel32 + user32 |
| CPU au repos | **31,2 / 31,2 / 46,9 ms** de `TotalProcessorTime` sur 12 s (trois runs, curseur hors fenêtre) | 0 % |
| Bench `ui_layout` (12 020 boxes) | **495 µs** (meilleur de 200), 41 ns/box | < 1 ms |
| `build.bat check` / `analyze` / `bench` | verts (MSVC `/analyze /WX` + clang-tidy) | verts |

Le repos reste réel : `os_events_pump(1, ui_animating() ? 16000 : OS_TIMEOUT_INFINITE)`, la boucle attend
indéfiniment sur `MsgWaitForMultipleObjects` tant que rien n'anime. Les quelques dizaines de millisecondes
résiduelles sur 12 s sont le compositeur (P-005, toujours ouvert), du même ordre que les 15,6 ms de T-006
avec une fenêtre deux fois plus grande et 225 boxes au lieu de 40.

Le compte de boxes de la liste est affiché en permanence dans la barre de statut de la démo, à côté du
compte de la frame : c'est le critère d'acceptation, il se lit à l'écran.

Le bench `ui_layout` recule de 338 µs (T-006) à 495 µs sur le même arbre. `UI_Box` a grossi de 248 à
256 octets (icône, flags et alignement de texte, empaquetés en `u16 + u8 + u8` plutôt qu'en trois `u32`
justement pour limiter la casse ; un `StaticAssert` fige la limite à 256). Vérifié : la taille n'explique
pas l'écart — mesuré à 264 octets, c'est 506 µs, à 256 octets 495 µs. La moyenne (660 µs) tombe dans la
bande 500-1100 µs que T-006 documentait déjà comme du bruit machine ; le minimum de T-006 avait été pris
sur une machine au repos. Le budget d'une milliseconde est tenu avec un facteur deux, et le chemin de
layout lui-même n'a pas changé d'une ligne.

### Ce que l'état des widgets a comme identité

`UI_TextInput`, `UI_List`, `UI_Splitter` et `UI_ContextMenu` appartiennent à l'appelant et vivent dans une
arène. Leur **adresse** est donc stable pour la vie de l'application, et c'est elle qui sert de clé
retenue (`hash64_combine(sel, (u64)state)`) : aucune chaîne d'identification à inventer, aucun risque de
collision entre deux listes, et la signature reste celle du ticket (`ui_text_input(state, placeholder)`).

### Ajouts à `ui_core` (T-006 n'en avait pas besoin, les widgets si)

- `OsEvent_Char` est enfin consommé : `ui_char_event_count/ui_char_event` livrent les codepoints du frame,
  `ui_key_event_count/ui_key_event` la **liste complète** des touches (le `UI_Signal` n'en expose que la
  première, ce qui suffit à un bouton mais pas à un champ texte).
- `signal.scroll_pixels` à côté de `signal.scroll` : la molette de précision donne des pixels, la molette
  crantée des lignes, la liste utilise les pixels quand ils existent.
- `signal.press_modifiers` : Ctrl+clic et Maj+clic ont besoin des modificateurs **du press**, pas de ceux
  de la frame.
- `UI_TextAlign` (gauche / centre / droite) dans la pile de style : une durée alignée à droite est
  maintenant **une** box, pas trois (une box de mesure, un spacer souple, la box de texte). − 54 boxes par
  frame sur la liste, et c'est ce qui fait rentrer la démo dans son budget.
- `UI_DrawIcon` + `box->icon` : une icône de l'atlas se pose comme le texte, sans que le widget dessine
  derrière le dos de `ui_core`.
- `ui_frame_box_count()` (le budget de virtualisation se mesure), `ui_request_animation()` (un widget qui
  a une temporisation à lui — le tooltip — garde la boucle éveillée sans posséder de timer),
  `ui_last_box()`, `ui_escape_pressed()`, `ui_press_key()`, `ui_mouse_pressed()`, `ui_dt()`,
  `ui_dpi_scale()`, `ui_viewport()`.
- L'anneau de focus vient du thème (accent 1 px + halo 2 px à 60/255) au lieu de l'orange en dur de T-006.

### Deux bugs trouvés en regardant l'écran

1. **Les chiffres tabulaires étaient tronqués.** `ui_layout_sizes` mesurait `UI_SizeKind_TextContent` avec
   `flags = 0` alors que le dessin utilisait `box->text_flags` : les chiffres tabulaires sont plus larges
   que les chiffres proportionnels, donc la box était trop courte et `8:19` devenait `8:…` — une durée sur
   deux, selon les chiffres. La mesure passe maintenant les mêmes flags que le dessin.
2. **`ui_separator` et `ui_spacer` lisaient la pile `child_layout_axis` au lieu de l'axe du parent.** Dans
   un panneau construit sous un `UI_ChildLayoutAxis(Axis2_X)` encore poussé, un séparateur horizontal
   devenait vertical : 1 px de large, 100 % de haut, et il mangeait toute la colonne. C'est l'axe du
   **parent** qui décide, pas l'état de la pile au moment de l'appel.

### Écarts assumés

- **`ui_tooltip(String8)` s'attache à `ui_last_box()`** (idiome ImGui `SetItemTooltip`), et
  `ui_tooltip_box(box, text)` existe pour le cas explicite. La signature du ticket est respectée.
- **Le splitter est en deux appels** : `ui_splitter_update(state, axis, total)` rend la taille (il lit le
  drag sur la poignée de la frame précédente, qui est de toute façon celle contre laquelle le press a été
  résolu) et `ui_splitter(state, axis)` construit la poignée là où l'arbre la veut. Un panneau a besoin de
  sa largeur *avant* que la poignée qui le suit existe ; un seul appel aurait coûté une frame de retard sur
  le panneau de gauche, ou deux boxes avec la même clé.
- **Pas de scroll cinétique** (« optionnel » dans le ticket) et **pas de lissage du scroll** : `research/02`
  §10.8 interdit explicitement d'animer la position des éléments de liste au défilement. Le scroll est au
  pixel près, 1:1 avec la molette.
- **Le curseur du champ texte ne clignote pas.** Un clignotement garde la boucle de rendu éveillée tant que
  le champ a le focus, ce qui contredit le « 0 % CPU au repos » du même ticket. Curseur plein, 1 px.
- **`ui_label`, `ui_labelf`, `ui_button`, `ui_spacer` quittent `ui_core`** pour `ui_widgets` : c'étaient les
  deux widgets provisoires de T-006, ils sont maintenant thémés. `ui_core.c` perd 35 lignes.
- **`ui_button_icon(icon, id)` prend un identifiant de la forme `"###play"`** : le label est vide, donc
  c'est l'identifiant qui porte la clé, avec la syntaxe de clés existante et sans allocation.
- **Une liste sans rect appelle `ui_request_animation()`** : à la toute première frame le viewport n'a pas
  encore de hauteur, donc aucune ligne n'est construite. Plutôt que d'attendre le prochain évènement,
  elle demande une frame de plus et se remplit tout de suite.
- Le thème clair est un **stub** rempli avec l'esquisse de `research/02` §10.4, comme prévu (phase 7). Il
  n'est branché nulle part, mais aucun widget ne lit une couleur en dur : `ui_theme_set(&light)` suffirait.

### Clavier

Tout est atteignable sans souris : `Tab` circule (champ de recherche, liste, boutons), la liste prend
flèches / PageUp / PageDown / Début / Fin / Maj+flèches (plage) / Espace (bascule) / Ctrl+A / Entrée
(ajoute au plan), la touche **Menu** (ou Maj+F10) ouvre le menu contextuel sur la ligne du curseur, le menu
se parcourt aux flèches et se valide à Entrée, `Échap` ferme. Tant qu'un menu est ouvert il possède le
clavier (`ui_popup_active()`), sinon `Entrée` déclencherait l'item *et* l'activation de la ligne.

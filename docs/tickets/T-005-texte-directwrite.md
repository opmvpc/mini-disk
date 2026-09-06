# T-005 — Texte : DirectWrite → atlas, cache de glyphes, mesure, "Hello 世界"

Phase 1 · Statut : **fait** · Dépend de : T-004, ADR-006

## Livrables
- `platform.h` : `os_font_open(family, size_px, weight) → OsFont`, `os_font_metrics(font)` (ascent,
  descent, line gap, x-height, tabular digit advance), `os_font_glyph_index(font, codepoint)` avec fallback
  système (retourne aussi la police de fallback), `os_font_rasterize(font, glyph, out_bitmap R8, out_metrics)`,
  `os_font_advance/kerning`.
- `src/platform/win32/win32_font_dwrite.c` : `dwrite.dll` chargée dynamiquement ; `IDWriteFactory`,
  `IDWriteFontFallback` (via `IDWriteFactory2`) pour le japonais/katakana, `CreateGlyphRunAnalysis` en
  `DWRITE_MEASURING_MODE_NATURAL`, texture `DWRITE_TEXTURE_ALIASED_1x1` grayscale, feature `tnum`.
  COM en C avec `COBJMACROS`.
- `src/ui/ui_text.{h,c}` : cache de glyphes hashé (font, glyph, subpixel x sur 4 positions) → AtlasRect ;
  `ui_text_measure(font, String8) → width, height`, cache de mesure par (hash string, font) ;
  `ui_text_draw(font, String8, pos, color, clip)` avec ellipsis `…` quand ça dépasse une largeur ;
  itération UTF-8 → runs par police (base + fallback).
- `src/ui/ui_font.{h,c}` : 4 styles chargés au démarrage (UI 13 px, caption 12 px, emphasis 14 px,
  heading 16 px ; Segoe UI Variable / Segoe UI), reconstruction sur `DpiChanged`.
- Démo : paragraphe latin avec accents, "Hello 世界 ﾃｽﾄ カタカナ", chiffres tabulaires alignés en colonne,
  texte tronqué avec ellipsis, à 100 % et 150 % DPI.

## Critères d'acceptation
- Le japonais et le katakana half-width s'affichent via fallback (aucune police embarquée, exe < 60 KB).
- Mesure et rendu cohérents au pixel (la boîte mesurée englobe exactement le texte rendu).
- Cache : 2e frame d'un même texte = 0 rastérisation, 0 allocation.
- `tests/test_text.c` : itération UTF-8 en runs, position de l'ellipsis, cache hit/miss.
- Portabilité : `ui_text` ne contient aucun appel DirectWrite ; tout passe par `platform.h`.

## Livraison (2026-09-06)

Statut : **fait**. Mesuré sur Intel UHD 620 (pilote 31.0.101.2140), écran 1920×1080 à 125 %,
Windows 11 26200.

### Ce qui est livré
- `platform.h` : `OsFont`, `OsFontMetrics` (ascent, descent, line gap, x-height, cap height,
  `digit_advance`), `OsGlyphMetrics`, `os_font_init/shutdown`, `os_font_open`, `os_font_close_all`,
  `os_font_id`, `os_font_metrics`, `os_font_glyph_index(font, cp, &carrier)`, `os_font_advance`,
  `os_font_kern` (0 en v1, cf. research/03 §5.9), `os_font_rasterize`.
- `src/platform/win32/win32_font_dwrite.c` (~550 lignes) : `dwrite.dll` en `LoadLibraryW`, vtables
  COM écrites à la main (le `dwrite.h` du SDK est C++ pur, pas de bloc « C style interface »),
  `IDWriteFactory2` → `IDWriteFontFallback` + notre `IDWriteTextAnalysisSource` pour le fallback
  système, `CreateGlyphRunAnalysis` en `MEASURING_MODE_NATURAL`, cache codepoint → (glyphe, police)
  de 512 entrées par police.
- `src/ui/ui_text.{h,c}` : cache de glyphes 4096 entrées hashées sur (police, glyphe, quart de pixel),
  cache de mesure 1024 entrées, une seule boucle de codepoints partagée par la mesure, le rendu et
  l'ellipsis (impossible qu'ils divergent), `ui_text_next_run` pour l'itération en runs de police,
  chiffres tabulaires par forçage d'avance, ellipsis `…`.
- `src/ui/ui_font.{h,c}` : 4 styles (13/12/14/16 dp, 400/400/600/600), `Segoe UI Variable Text` puis
  `Segoe UI`, `Tahoma`, `Arial`, taille arrondie au pixel entier, reconstruction sur `DpiChanged`.
- `r_gl.c` : uniform `u_text_gamma` (= `R_TEXT_GAMMA`, 1/1.2) appliqué à la couverture R8 dans le
  fragment shader (research/03 §2.4). `glUniform1f` est revenu dans le loader, comme annoncé en T-004.
- `tests/test_text.c` : 9 cas (métriques, fallback, runs, mesure == rendu, chiffres tabulaires,
  position de l'ellipsis, hit/miss des deux caches, variantes subpixel, reconstruction DPI 150 %).
- `tests/bench_main.c` : bench de lookup du cache de glyphes à chaud.
- Démo `app.c` : paragraphe latin accentué, `Hello 世界 ﾃｽﾄ カタカナ`, colonne de durées en chiffres
  tabulaires alignées à droite, titres et chemin tronqués à l'ellipsis.

### Résultats
| Mesure | Valeur |
|--------|--------|
| Taille exe release | **54 272 octets** (< 60 KB, +8 704 o sur T-004) |
| Imports | `KERNEL32.dll`, `USER32.dll` (dwrite en `LoadLibraryW`) |
| Tests | **46 cas, 1 143 checks, 0 échec** (debug + ASan) |
| CPU au repos | **46,9 ms sur 12 s** (`TotalProcessorTime`, delta exact), à comparer aux ~62 ms de P-005 |
| Draw calls de la démo | 8, texte inclus (glyphes et icônes partagent l'atlas) |
| Lookup de glyphe à chaud | 63 ns par codepoint, rendu du quad compris, 0 rastérisation, 0 allocation |
| Capture | `build/demo.png`, client 1280×800 physiques à 125 % |

### Décisions et écarts
- **`CLEARTYPE_3x1` replié en gris, pas `ALIASED_1x1`** (écart avec ADR-006, cf. P-006) :
  `GetAlphaTextureBounds(ALIASED_1x1)` renvoie un rectangle vide dès que le mode de rendu n'est pas
  `RENDERING_MODE_ALIASED`, c'est-à-dire du texte crénelé. On demande donc la texture 3x1 en
  `NATURAL_SYMMETRIC` et on moyenne les trois échantillons : c'est ce que font Skia et WebRender pour
  leur mode grayscale. Résultat identique à l'intention de l'ADR (un octet de couverture par pixel,
  aucune frange colorée), atlas inchangé.
- **`IDWriteFontFallback` plutôt que la liste de familles codée en dur** que research/03 §5.3
  proposait comme raccourci v1 : l'`IDWriteTextAnalysisSource` coûte 60 lignes ici (on ne mappe
  qu'un codepoint à la fois) et donne la désambiguïsation Han du système gratuitement.
- **Fallback par codepoint, pas par run** : `MapCharacters` est appelé une fois par caractère
  inédit puis le résultat vit dans la table de la police de base. Les runs (`ui_text_next_run`)
  sont déduits de ces réponses, donc l'API de runs existe sans coûter un second chemin.
- **Chiffres tabulaires par forçage d'avance** (option B de research/03 §5.8) : pas de
  `IDWriteTypography`, donc pas de `IDWriteTextLayout`, donc pas de shaping à traîner.
- **Pas de kerning** en v1 : `os_font_kern` existe et renvoie 0 (ADR-006, research/03 §5.9).
- Le cache de glyphes ne fait **pas d'éviction** : il est vidé par `ui_text_reset` au changement de
  DPI ou de thème, comme l'atlas. 4096 entrées couvrent large le latin + les kana + les kanji
  réellement affichés ; un `Assert` garde le taux de remplissage sous 75 %.
- Le CPU au repos mesuré (46,9 ms / 12 s) reste dans la zone de P-005, qui n'est donc **pas**
  aggravé par le texte : aucun timer, aucun réveil ajouté, le rendu reste à la demande.

### Vérifications
`build.bat debug`, `test`, `bench`, `check`, `analyze` (`/analyze /WX` + clang-tidy) et `release`
sont tous verts.

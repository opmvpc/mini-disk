# T-005 — Texte : DirectWrite → atlas, cache de glyphes, mesure, "Hello 世界"

Phase 1 · Statut : **todo** · Dépend de : T-004, ADR-006

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

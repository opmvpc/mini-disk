# ADR-006 — Texte : DirectWrite rastérise dans notre atlas, zéro police embarquée

Statut : **accepté** (2026-09-06) — source : research/03 §5

## Contexte
Les titres MiniDisc contiennent du katakana half-width et potentiellement du japonais complet. Embarquer
une police CJK coûte 4-8 MB, incompatible avec l'objectif < 1 MB.

## Décision
- Sur Windows, les glyphes sont rastérisés par **DirectWrite** (`IDWriteFactory::CreateGlyphRunAnalysis`
  → `CreateAlphaTexture` en `DWRITE_TEXTURE_ALIASED_1x1`, antialiasing grayscale) dans notre atlas R8.
  Le fallback de police système (Segoe UI → Yu Gothic / Meiryo pour le japonais) est gratuit.
- Police UI = police système (Segoe UI Variable / Segoe UI), chiffres tabulaires activés via feature `tnum`.
- Pas de rendu SDF pour le texte (mauvais à 13 px), pas de ClearType subpixel (incompatible avec le
  blending prémultiplié sur fond variable).
- `stb_truetype` reste dans `third_party` comme secours et comme base du portage Linux/macOS (où l'on
  rastérisera via FreeType/CoreText ou stb).
- Cache de mesure de texte par (string hash, taille), ellipsis en fin de ligne, UTF-8 partout.

## Alternatives rejetées
- stb_truetype + Inter subsetée : ~25-40 KB pour le latin, mais le japonais est impossible sous 1 MB.
- msdfgen/SDF : qualité médiocre aux petites tailles, coût atlas.

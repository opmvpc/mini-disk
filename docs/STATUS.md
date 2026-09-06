# STATUS — où on en est

Dernière mise à jour : 2026-09-06 (soir)

## Phase actuelle : 1 · Fondations — T-001..T-005 faits

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
- Rien : T-005 attend sa review.

### Fait en phase 1 (suite)
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
1. Review de T-006, puis T-007 (widgets), T-008 (jobs + overlay, fin de phase 1).
2. P-005 : identifier la source du CPU résiduel (0,5 %) avec l'overlay de T-008.

### Action utilisateur requise
- Zadig → WinUSB sur "Net MD Walkman" avant la phase 3.

## KPI
| Métrique | Valeur | Date |
|----------|--------|------|
| Taille exe release | 54 272 o (T-005 : texte DirectWrite) | 2026-09-06 |
| Budget CI (`SIZE_BUDGET_KB`) | 100 KB (phase 1) | 2026-09-06 |

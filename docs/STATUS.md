# STATUS — où on en est

Dernière mise à jour : 2026-09-06 (soir)

## Phase actuelle : 1 · Fondations — T-001/T-002/T-003 faits

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
- T-004 : `r_core` batches, scissor, atlas R8 skyline, icônes rastérisées (agent Opus, prompt `prompts/I-004`).

### Fait en phase 1
- T-003 livré : contexte WGL 3.3 core, loader X-macro (48 fonctions), renderer SDF (1 shader, 1 VBO), démo 5 rects, 0,5 % CPU au repos mesuré en delta exact (P-005, ouvert), 491 checks, exe 33 792 o (cible du ticket < 30 KB non tenue, écart détaillé dans la Livraison de T-003).
- T-002 livré et reviewé : fenêtre complète, `OsEvent` ring, DPI v2, dark title bar, drop files, **0,0 % CPU au repos** (mesuré), 440 checks, exe 23 040 o (P-004).
- T-001 livré et reviewé : `base/`, `build.bat` (7 cibles), 125 checks, exe release **7 168 octets**, imports kernel32+user32. Voir P-003 (TLS sans CRT).

### Prochain pas
1. Review de T-004, puis T-005 (texte DirectWrite), T-006 (ui_core), T-007 (widgets), T-008 (jobs + overlay, fin de phase 1).
2. P-005 : identifier la source du CPU résiduel (0,5 %) avec l'overlay de T-008.

### Action utilisateur requise
- Zadig → WinUSB sur "Net MD Walkman" avant la phase 3.

## KPI
| Métrique | Valeur | Date |
|----------|--------|------|
| Taille exe release | 33 792 o (T-003 : renderer GL 3.3) | 2026-09-06 |
| Budget CI (`SIZE_BUDGET_KB`) | 100 KB (phase 1) | 2026-09-06 |

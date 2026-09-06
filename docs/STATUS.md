# STATUS — où on en est

Dernière mise à jour : 2026-09-06 (soir)

## Phase actuelle : 1 · Fondations — T-001 en cours

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
- T-001 : base/, build.bat, runner de tests, fenêtre noire < 8 KB (agent Opus, prompt `prompts/I-001`).

### Prochain pas
1. Review de T-001 (grille ADR-012 §review), mesure taille, CI verte, commit.
2. T-002 (fenêtre Win32 complète, 0 % CPU) puis T-003 (WGL + quad SDF).

### Action utilisateur requise
- Zadig → WinUSB sur "Net MD Walkman" avant la phase 3.

## KPI
| Métrique | Valeur | Date |
|----------|--------|------|
| Taille exe release | 1 536 o (smoke, fenêtre vide) | 2026-09-06 |
| Budget CI (`SIZE_BUDGET_KB`) | 100 KB (phase 1) | 2026-09-06 |

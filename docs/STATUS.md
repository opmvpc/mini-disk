# STATUS — où on en est

Dernière mise à jour : 2026-09-06

## Phase actuelle : 0 · Recherche & analyse (en pause, quota consommé)

### Fait
- Device identifié : `USB\VID_054C&PID_0084` NetMD, driver absent → voir `problems/P-001`.
- Toolchain validée : MSVC 14.44 + SDK 26100, exe no-CRT de 1 536 octets (`tools/smoke`).
- Structure docs (README, CONVENTIONS, decisions/, tickets/, problems/, research/, prompts/).
- `00-analyse-projet.md` draft v0.1 : vision, architecture 3 couches, modules, 8 phases, risques.
- `research/02b-design-tokens.md` : tokens de densité et palette sombre (seul rapport ayant abouti).

### Incident
- `problems/P-002` : les 4 recherches Opus ont explosé en 20 agents et consommé le quota. Rapports R-01..R-04
  **non produits**. Nouvelle procédure : un agent à la fois, sans sous-agents, Sonnet pour la recherche.

### Prochain pas (quand le quota est revenu)
1. Refaire les recherches R-01 (protocole NetMD) et R-04 (ATRAC3) en mode économe : Fable lit lui-même
   netmd-js / libnetmd / atracdenc avec WebFetch ciblé, ou un seul agent Sonnet borné (≤ 25 tool calls).
2. R-02 UX et R-03 stack : Fable rédige directement depuis ses connaissances + quelques fetchs ciblés.
3. Écrire ADR-001 (toolchain no-CRT), ADR-002 (architecture 3 couches), ADR-003 (UI immediate/retained),
   ADR-004 (texte : DirectWrite vers atlas vs stb_truetype), ADR-005 (ATRAC3 : port C d'atracdenc).
4. Remplir `tickets/BACKLOG.md` pour la phase 1 (fondations), puis lancer le premier ticket.

### Action utilisateur requise
- Installer le driver WinUSB sur le "Net MD Walkman" avec Zadig avant la phase 3 (voir P-001).

## KPI
| Métrique | Valeur | Date |
|----------|--------|------|
| Taille exe release | 1 536 o (smoke, fenêtre vide) | 2026-09-06 |

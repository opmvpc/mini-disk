# T-022 — Édition du disque : renommer, déplacer, effacer, groupes, avec simulation et sauvegarde TOC

Phase 3 · Statut : **todo** · Dépend de : T-021, ADR-011 D4, research/01 §3.7-3.9, §9.3

## Livrables
- `netmd_edit.c` : `set_disc_title` (avec `oldLen` exact — un `oldLen` faux corrompt le TOC),
  `set_track_title`, `move_track`, `erase_track`, `erase_disc`, écriture de la structure de groupes
  (réécriture complète du titre disque), `wchar` = `0x00/0x01` pour le disque et `0x02/0x03` pour les
  pistes (research/01 §9.3), ne jamais réécrire un titre identique (plante certains modèles),
  `leave secure session` systématique sur abort.
- **Simulation** (D4) : toute opération d'édition produit d'abord un `DiscDiff` (avant / après : titres,
  ordre, groupes, budget TOC) affiché dans un panneau de confirmation avec verbe explicite
  ("Effacer 3 pistes", "Renommer le disque") ; **sauvegarde automatique du TOC** (titres, ordre, groupes,
  durées) dans `%APPDATA%\minidisk\toc-backups\<disc-id>-<date>.json`-like texte avant toute écriture.
- Budget TOC (D3) : calcul exact 255 cellules × 7 caractères partagées (titre disque groupes inclus,
  pistes non-SP = 1 cellule même vides) ; refus avec message clair si dépassement.
- Vue Disque : renommage inline (F2 / double-clic, Entrée / Échap), glisser-déposer pour réordonner,
  Suppr avec confirmation, création de groupe depuis une sélection, dissolution de groupe.
- Verrou "TOC en RAM" : après une écriture, bannière "Ne pas éjecter, écriture du TOC" tant que le
  device n'a pas confirmé ; blocage de la fermeture de l'app pendant ce temps.
- Tests : rejeu de transcriptions d'édition (rename, move, erase, groupes) ; calcul du budget TOC sur
  10 cas limites ; simulation → diff attendu ; round-trip sauvegarde TOC.

## Critères d'acceptation
- Renommer / réordonner / effacer sur le MZ-N505 sans corruption, vérifié en relisant le disque.
- Aucune écriture sans simulation ni sauvegarde. Exe < 300 KB. Tests, check, analyze verts.
- Fin de phase 3 : tag `v0.3.0-phase3`, STATUS.md avec KPI (temps de lecture disque, transcriptions).

# ADR-011 — Décisions produit structurantes

Statut : **accepté** (2026-09-06) — source : research/02 §1.2, §3, §9 ; research/01 §7

| # | Décision | Pourquoi |
|---|----------|----------|
| D1 | **Le plan de disque est un document** : créable sans appareil, sauvegardé en `.mdplan`, réouvrable, auto-sauvegardé. | Préparer 10 disques le dimanche, les graver dans la semaine. Aucun outil actuel ne le permet. |
| D2 | **La jauge de capacité calcule en clusters**, pas en secondes (SP 2 s, LP2 4 s, LP4 8 s d'arrondi par piste). | "79:58 / 80:00" linéaire peut ne pas rentrer. La constante sera vérifiée sur le device réel avant d'être figée. |
| D3 | **Titrage dérivé des tags avec budget TOC visible** : 255 cellules × 7 caractères = 1 785 caractères partagés entre titre disque (groupes inclus) et pistes ; une piste LP coûte une cellule même sans titre (`LP:`). | Ressource finie et partagée, l'utilisateur doit la voir se remplir comme la capacité audio. |
| D4 | **Rien de destructif sans simulation** : toute opération device passe par un diff avant/après explicite ; sauvegarde automatique du TOC (titres, ordre) avant écriture. | Cluster de pain points "TOC corrompu" (WMD #93/#41/#36, EWMD #33/#54). |
| D5 | **Le transcodage est visible, inspectable et caché sur disque.** | Re-graver un plan sur un 2e disque prend quelques secondes. |
| D6 | **Layout trois panneaux** Bibliothèque / Plan / Disque, pas d'assistant ; tout faisable au clavier. | research/02 §8. |
| D7 | **SP d'abord** (phase 5), LP2/LP4 en phase 6. Hi-MD, Remote NetMD, homebrew/factory mode : hors v1. | SP n'exige pas d'encodeur ; réduit le risque de la v1. |
| D8 | **Densité pro** : lignes 22-24 px, chiffres tabulaires, tokens de research/02b ; couleurs de mode SP `#0090FF`, mono `#BE95FF`, LP2 `#3FB950`, LP4 `#D29922`. | Cohérence visuelle de la jauge et des listes. |
| D9 | **Honnêteté** : le transfert SP est à ~1x temps réel, l'app affiche toujours l'ETA réel, permet l'annulation, inhibe la veille, refuse la fermeture pendant une gravure, reprend après interruption. | Contrainte physique la plus sous-exploitée par la concurrence. |
| D10 | i18n FR/EN dès la v1 via table de strings. | Strings fournies dans research/02 §12. |

## Modèle de l'appareil de référence
Sony **MZ-N505** (PID 0084, SoC CXD2677 Type-R, firmware R1.3/1.4). Toute la v1 est validée contre lui.

# Docs — bureaucratie du projet

Ce dossier est la **mémoire du projet**. Tout ce qu'il faut pour reprendre le travail depuis
n'importe quelle machine après un `git clone` est ici. Règle : si ce n'est pas écrit ici, ça n'existe pas.

## Par où commencer (reprise de travail)

1. **[STATUS.md](STATUS.md)** — où on en est, ce qui est fait, ce qui est en cours, le prochain pas.
2. **[00-analyse-projet.md](00-analyse-projet.md)** — vision, contraintes, architecture cible, plan par phases.
3. **[decisions/](decisions/)** — les choix d'implémentation (ADR). Un fichier par décision, numérotés. Ne jamais
   re-débattre une décision acceptée sans ouvrir un nouvel ADR qui la remplace.
4. **[tickets/](tickets/)** — le backlog. `BACKLOG.md` = liste ordonnée, un fichier par ticket pour les gros.
5. **[problems/](problems/)** — journal des problèmes rencontrés et des solutions apportées. Un fichier par problème.
6. **[research/](research/)** — les rapports de recherche (protocole NetMD, UX, stack, ATRAC3). Sources de vérité
   technique, cités par les ADR.
7. **[prompts/](prompts/)** — les prompts donnés aux agents de recherche/implémentation (reproductibilité).
8. **[CONVENTIONS.md](CONVENTIONS.md)** — conventions de code C, build, nommage, commits.

## Cycle de vie d'un ticket

`backlog` → `en cours` (une branche ou un commit référence `T-xxx`) → `en review` → `fait`.
Un ticket "fait" doit citer le commit et, si un problème est apparu, le fichier `problems/P-xxx`.

## Formats

- ADR : `decisions/ADR-NNN-titre.md` — Contexte / Décision / Alternatives rejetées / Conséquences / Statut.
- Ticket : `tickets/T-NNN-titre.md` — Objectif / Critères d'acceptation / Notes d'implémentation / Dépendances / Statut.
- Problème : `problems/P-NNN-titre.md` — Symptôme / Cause racine / Solution / Leçon / Tickets liés.

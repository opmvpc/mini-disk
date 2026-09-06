# ADR-010 — Persistance : fichiers binaires maison, pas de SQLite

Statut : **accepté** (2026-09-06) — source : research/03 §9

## Décision
- Cache bibliothèque : fichier binaire versionné, **SoA mappable en mémoire**, string table internée,
  index construits au chargement. Recherche par masque de caractères + filtrage incrémental pendant la frappe.
- Plans de disque : format `.mdplan` binaire versionné (+ export texte lisible), document de première
  classe (ADR-011).
- Préférences : fichier texte clé=valeur dans `%APPDATA%\minidisk\` (ou à côté de l'exe en mode portable
  si un fichier `portable` existe).
- Écriture atomique : écrire dans `.tmp` puis `MoveFileEx(REPLACE_EXISTING)`.
- Cache de transcodage : dossier de fichiers PCM/ATRAC3 nommés par hash (source + paramètres pipeline).

## Alternatives rejetées
- SQLite : ~700 KB, deux tiers du budget, et mauvais modèle pour "filtrer 100k lignes à chaque touche".

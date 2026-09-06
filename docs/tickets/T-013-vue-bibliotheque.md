# T-013 — Vue Bibliothèque connectée au core : colonnes, tri, navigateur, recherche, sélection

Phase 2 · Statut : **todo** · Dépend de : T-012, research/02 §8.3 et §10

## Livrables
- `src/app/view_library.c` : la démo factice de T-007 devient la vraie vue :
  - en-têtes de colonnes cliquables (tri asc/desc, indicateur), largeurs redimensionnables (persistées),
    colonnes : #, Titre, Artiste, Album, Durée, Format (badge codec + kHz), Année, Date d'ajout ;
  - navigateur par colonnes (Artiste | Album) au-dessus de la liste, repliable, multi-sélection qui filtre ;
  - champ de recherche (Ctrl+F, Esc efface) branché sur `lib_search` ; compteur "N / total" ;
  - liste virtualisée branchée sur l'index trié (aucune copie : la liste lit `TrackId` → SoA) ;
  - sélection multiple, Ctrl+A, Entrée = ajouter au plan (stub jusqu'à T-030), clic droit menu ;
  - état "vide" (aucun dossier : gros bouton "Ajouter un dossier" + drag & drop), état "scan en cours"
    (barre de progression discrète en haut de liste), état "aucun résultat".
- `src/app/app_state.{h,c}` : l'état applicatif (core handles, sélection, vues) séparé de `app.c`.
- Préférences (ADR-010) : dossiers de bibliothèque, colonnes, tri, largeurs, taille/position fenêtre —
  fichier texte `minidisk.prefs` dans `%APPDATA%\minidisk\` ou à côté de l'exe si `portable` existe.
- Tests : `tests/test_prefs.c` (round-trip, valeurs manquantes → défauts, fichier corrompu → défauts).

## Critères d'acceptation
- 100k pistes : tri par clic < 50 ms perçu, recherche fluide à la frappe, scroll 60 fps, 0 % CPU au repos.
- Tout faisable au clavier (Tab entre champ / navigateur / liste, flèches, Entrée, Ctrl+F, Esc).
- Capture d'écran validée par le lead. Exe < 240 KB. Tests, check, analyze verts.

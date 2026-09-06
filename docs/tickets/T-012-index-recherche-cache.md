# T-012 — Index, tri, recherche incrémentale et cache binaire mappé

Phase 2 · Statut : **todo** · Dépend de : T-011, ADR-010

## Livrables
- `src/core/library/lib_index.c` : index triés par colonne (titre, artiste, album, durée, date d'ajout)
  en tableaux de `TrackId` ; tri par radix/merge stable sur clés normalisées (casse, accents repliés via
  table de décomposition minimale latin-1/latin ext-A, articles "The/Le/La" optionnels) ; navigateur par
  colonnes Artiste → Album (comptes par groupe).
- `lib_search.c` : recherche **incrémentale pendant la frappe** : masque de caractères par piste
  (bitset 64 bits des lettres présentes) pour le rejet rapide, puis sous-chaîne insensible à la casse et
  aux accents sur titre/artiste/album, tokens multiples (ET), résultats dans l'ordre du tri courant ;
  raffinement quand la requête s'étend (on filtre le résultat précédent).
- `lib_cache.c` : fichier `library.mdlib` versionné, **mappable** (`os_file_map`) : en-tête (magic,
  version, compte, offsets de sections), tables SoA brutes, string table ; écriture atomique
  (`.tmp` + `MoveFileEx`) ; chargement en < 50 ms pour 100k pistes (aucun parsing, juste des pointeurs
  dans le mapping) ; invalidation par version.
- Intégration : au démarrage, charger le cache puis rescanner en tâche de fond (T-010) et ne pousser à
  l'UI que les diffs.
- Tests : `tests/test_index.c` — tri stable et repli des accents ("Émilie" entre "Elias" et "Fanny"),
  recherche (tokens, accents, raffinement = résultat identique à une recherche fraîche), cache
  round-trip (écrire 10k pistes, recharger, comparer champ à champ, version mismatch → rejet propre).
- Bancs : recherche "the" sur 100k pistes < 5 ms ; recherche raffinée "the b" < 1 ms ; tri 100k < 30 ms ;
  chargement cache 100k < 50 ms.

## Critères d'acceptation
- Aucune allocation pendant la recherche (buffers réutilisés) ; résultats disponibles à la frame suivante.
- Exe < 220 KB. Tests, check, analyze verts.

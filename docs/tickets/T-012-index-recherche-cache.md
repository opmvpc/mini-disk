# T-012 — Index, tri, recherche incrémentale et cache binaire mappé

Phase 2 · Statut : **fait** · Dépend de : T-011, ADR-010

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

## Livraison

Livrée le 2026-09-07. `build.bat` debug / release / test / check / analyze / bench verts.

### Ce qui est dans le dépôt

**`src/core/library/lib_index.{h,c}`** — une passe sur le SoA produit, par piste vivante : un
**masque 64 bits** des lettres présentes, un blob normalisé « titre \x01 artiste \x01 album » que la
recherche parcourt, et **trois clés de tri** terminées par un zéro. Normalisation : repli de casse,
repli des accents par deux tables plates (`U+00C0..U+00FF` et `U+0100..U+017F`, **384 octets** de
données en lecture seule, doublets `ae`, `ss`, `oe`, `th`, `ij` inclus), tout le reste recopié tel
quel — un titre japonais reste cherchable. Article de tête optionnel (the, a, an, le, la, les, l',
un, une, der, die, das, el, los, las). Chaque clé porte son **préfixe u64** (ses huit premiers octets
en gros-boutiste) : une comparaison est un compare entier, et seule une égalité parfaite descend dans
les octets. Le tri est une **fusion ascendante stable** sur des paires `(clé, id)` de 16 octets — la
comparaison lit la mémoire qu'elle parcourt déjà — repartant toujours de l'ordre des ids, donc
indépendante de l'historique des clics. Cinq colonnes indexées (titre, artiste, album, durée, date
d'ajout), construites à la demande et mises en cache. Navigateur **Artiste → Album** avec comptes par
groupe, la sélection suivant le *nom* et non la ligne : un rescan qui insère un artiste au-dessus ne
la déplace pas.

**`src/core/library/lib_search.{h,c}`** — trois étages, du moins cher au plus cher : **raffinement**
(la requête ne fait que s'allonger, les candidats sont les bits restés à 1), **rejet par masque** (un
ET par piste), **sous-chaîne réelle** en SSE2 (seize positions candidates par comparaison). Deux
passes : la première va toujours dans l'ordre des ids — le blob est écrit dans cet ordre, elle le lit
donc en avant au lieu de sauter dans 12 Mo — la seconde émet dans l'ordre de tri courant. Un
raffinement dont le tri n'a pas changé compacte ses résultats sur place et saute la seconde passe.
Deux raccourcis font l'essentiel du gain : un token que la requête précédente portait déjà mot pour
mot n'est **pas retesté**, et un token d'**une seule lettre** est décidé par le masque seul (le
masque est exact pour `a-z` et `0-9`). **Aucune allocation pendant la recherche** : le tableau de
résultats et le bitset sont dimensionnés une fois pour toutes.

**`src/core/library/lib_cache.{h,c}`** — `library.mdlib` : en-tête de 192 octets (`StaticAssert`)
avec magic `MDSKLIB`, version, taille du fichier, nombre de slots, nombre de pistes vivantes, tête de
la free-list, génération, offsets et tailles de sections, racine scannée. Le SoA est écrit **tel
qu'il est en mémoire** (mêmes colonnes, même ordre d'alignement décroissant), suivi de la string
table et de ses slots d'internement. Écriture atomique : `.tmp`, `FlushFileBuffers`, puis
`MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH)`. Chargement par `os_file_map`. Les **TrackId sont
stables d'une session à l'autre** — les tombstones et la free-list sont écrits, pas compactés — ce
dont les plans auront besoin (ADR-011).

**La frontière (ADR-012).** `lib_cache_validate` vérifie le magic, la version, `file_size` contre la
taille mappée, l'alignement et l'inclusion de chaque section, `soa_size == count * 74`,
`strings_size`, le premier mot de la string table, la puissance de deux du nombre de slots, **chaque
slot d'internement** (la chaîne entière tient dans le blob), l'**occupation** et le **facteur de
charge** que `lib_intern` suppose — sans quoi une sonde pourrait boucler sur une table pleine —, les
bits de `flags`, **les six `StringId` de chaque piste vivante**, le nombre de pistes vivantes, et la
**free-list** entière (chaque maillon mort, dans les bornes, longueur exacte : donc pas de cycle).
Après ça, plus une seule vérification en aval : `lib_string` et `lib_track_live` ne peuvent plus se
faire mentir. Version différente → `LibCache_BadVersion` : on ne migre pas, on rescanne.

**`platform.h` / `win32_platform.c`** : `os_file_map` / `os_file_unmap` (`CreateFileMappingW` +
`MapViewOfFile`), `os_file_move_replace`, et `os_file_write_all` qui **flushe** avant de rendre la
main — sans quoi le renommage publierait un fichier vide après une coupure de courant.
`lib_model` expose `lib_reserve` et `lib_path_index_rebuild`, dont le chargeur de cache est le seul
appelant.

**`app.c`** : navigateur Artiste → Album au-dessus de la liste (deux listes virtualisées, la ligne 0
valant « tout »), **tri au clic sur l'en-tête de colonne** (second clic : sens inverse, flèche dans
le titre), recherche incrémentale branchée sur `lib_search`, et le **flux de démarrage** : le cache
d'abord — la bibliothèque est à l'écran avant que le disque soit touché — puis le rescan en tâche de
fond dont seuls les **diffs** remontent : l'index n'est refondu que si un événement
`TracksAdded` / `TracksRemoved` / `TracksTagged` est passé, et au plus quatre fois par seconde. Le
cache est réécrit à la fin de chaque scan.

**Tests** (`tests/test_index.c`, 8 cas) : normalisation et repli des accents ; ordre
`Elias < Émilie < Fanny` ; stabilité du tri sur 4 000 pistes, et colonne entièrement à égalité =
ordre des ids ; navigateur et ses comptes ; recherche (tokens ET, accents dans les deux sens, rejet
par masque, ordre de tri et inversion) ; **raffinement lettre par lettre identique à une recherche
fraîche**, y compris sur une autre colonne de tri ; round-trip du cache sur 10 000 pistes champ par
champ, avec deux tombstones, la table d'internement encore fonctionnelle et un slot réutilisé après
rechargement ; et 14 rejets — version, magic, offsets hors fichier, offset non aligné, tailles
incohérentes, slots non puissance de deux, nombre de vivants faux, free-list pointant sur une piste
vivante, fichier absent.

**Outils** : `tools/capture_demo.ps1` (lance l'app et capture sa fenêtre) et l'option `--query` de
l'app, pour reproduire la capture sans personne au clavier ; `tools/gen_tag_vectors.py --demo`
étendu à 32 fichiers, 6 artistes, 8 albums, dont deux albums d'un même artiste, `Émilie Simon` et
`The Bad Plus`.

### Mesures (Windows 11, MSVC 14.44, meilleur de N passes)

| Banc | Cible | Mesuré |
|------|-------|--------|
| Tri de 100 000 pistes par artiste | < 30 ms | **22,8 ms** |
| Recherche « the » sur 100 000 pistes | < 5 ms | **4,09 ms** (64 770 résultats) |
| Raffinement « the » → « the b » | < 1 ms | **0,978 ms** (45 000 résultats) |
| Chargement du cache de 100 000 pistes | < 50 ms | **26,9 ms** (fichier de 12,04 Mo) |
| Écriture du cache de 100 000 pistes | — | 53 ms |

- Exe release : **187 392 octets** (183 KB ; budget du ticket 220 KB, garde-fou CI 250 KB), soit
  **+19 968 octets** par rapport à T-011. Imports : `KERNEL32.dll` et `USER32.dll`, rien d'autre.
- Tests : **104 cas / 1 780 checks**, 0 échec, sous ASan. `build.bat check` et `build.bat analyze`
  (`cl /W4 /WX /analyze` + clang-tidy) verts.
- Capture : `build/demo.png` — colonnes triées (« TITRE ▴ »), navigateur Artiste → Album avec ses
  comptes, recherche « the » en cours (4 / 32) sur l'arbre de démonstration.

### Écarts et décisions

1. **Le cache n'est pas exploité comme un simple jeu de pointeurs dans le mapping.** Le fichier est
   mappé et validé sans parsing, puis les colonnes et la string table sont **recopiées** dans les
   arènes de la bibliothèque. Raison : le rescan de fond qui suit doit pouvoir **écrire** dans le SoA
   (ajouter, retirer, taguer), ce qu'un mapping en lecture seule interdit. Aucun champ n'est décodé,
   seulement déplacé, et les 26,9 ms mesurées tiennent largement dans le budget de 50 ms.
   Conséquence assumée du même choix : la **table de hachage des chemins n'est pas écrite** dans le
   fichier, elle est reconstruite au chargement (100 000 hachages) plutôt que validée sonde par
   sonde. Les slots d'internement, eux, sont écrits *et* validés — les reconstruire aurait imposé un
   parcours complet du blob de chaînes.
2. **Le jeu de données des bancs est volontairement hostile** : 64 770 pistes sur 100 000 contiennent
   « the », et 45 000 contiennent « the » *et* « b ». Une vraie bibliothèque rend quelques milliers
   de résultats, où le raffinement est un ordre de grandeur sous la cible.
3. **Un token d'une seule lettre est décidé par le masque seul.** C'est exact, pas approximatif, pour
   `a-z` et `0-9` (un bit par caractère) ; pour un octet hors de ces plages les bits sont partagés et
   la sous-chaîne réelle est jouée. C'est ce qui fait passer « the b » sous la milliseconde sur le
   jeu hostile ci-dessus.
4. **`LibSort_Added` est indexée mais pas exposée dans l'UI** : il n'y a pas encore de formatage de
   date dans `ui_text`. Les quatre en-têtes visibles (titre, artiste, album, durée) trient.
5. `lib_index_build` **reconstruit tout** (masques, blob, clés) au lieu de se mettre à jour par
   diffs. C'est O(n) sur les seules pistes vivantes, et l'app ne l'appelle au plus que quatre fois
   par seconde pendant un rescan ; un index incrémental serait du code en plus sans mesure pour le
   justifier aujourd'hui.
6. `os_file_write_all` **flushe** désormais avant de fermer. Les appelants existants y perdent
   quelques microsecondes ; sans ça l'écriture atomique n'en est pas une.
7. Le panneau **Disque** n'apparaît pas sur la capture : à 1 200 px physiques et 127 % de mise à
   l'échelle, les 640 dp imposés au panneau Bibliothèque depuis T-011 ne laissent pas la place aux
   trois panneaux. C'est un réglage de disposition antérieur à ce ticket, laissé tel quel.

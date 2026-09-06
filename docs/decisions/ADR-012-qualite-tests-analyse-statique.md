# ADR-012 — Qualité : tests automatisés, analyse statique, zéro programmation défensive

Statut : **accepté** (2026-09-06)

## Principe (RAD / Muratori)
La solidité vient de la **structure**, pas des garde-fous. On valide aux **frontières** (fichiers, tags,
réponses USB, saisie utilisateur, arguments de ligne de commande) et on fait **confiance à l'intérieur**.
Pas de `if (!ptr) return` dans les APIs internes, pas de re-validation à chaque étape, pas de codes
d'erreur pour des situations impossibles. Un état interne invalide est un bug : `Assert` en debug,
crash franc plutôt que corruption silencieuse.

Concrètement :
- **Frontières** = les seuls endroits avec du parsing/validation : `core/library/tags_*`, `core/codecs`,
  `core/netmd/proto_parse`, `core/persist/load`, `ui/text_input`, `app/args`. Elles produisent des
  structs **canoniques** ; en aval, tout le code suppose ces structs correctes.
- **Assertions** : `Assert(x)` (debug uniquement) pour les invariants internes ; `AssertAlways(x)` pour
  les invariants dont la violation détruirait des données (protocole USB, écriture TOC, écriture de cache).
- **Erreurs attendues** (fichier illisible, device débranché, disque plein) : codes de retour typés
  (`enum XxxError`) + message. Ce sont des états du domaine, pas des exceptions.
- **Mémoire** : arènes avec taille bornée connue à l'avance. Pas de vérification de `NULL` après une
  allocation d'arène : si l'arène est trop petite, c'est un bug de dimensionnement, `Assert`.

## Tests automatisés (`build.bat test`, CI job `test`)
Runner maison (`tests/test_main.c`, ~100 lignes, macros `TEST(name)` / `EXPECT(cond)`), compilé en
**debug avec ASan** et exécuté en CI. Quatre familles :
1. **Unitaires purs** sur `base/` et `core/` : arènes, strings UTF-8, hash, tri, jauge de capacité en
   clusters, budget TOC, sanitize des titres half/full-width, parseurs de tags (vecteurs golden : fichiers
   minimaux commités dans `tests/data/`, < 50 KB chacun).
2. **Golden vectors DSP** : resampler (réponse impulsionnelle, aliasing < −90 dB), R128 (vecteurs de
   conformité EBU Tech 3341/3342, tolérance ±0.1 LU), dither (statistiques), contre des valeurs de
   référence générées une fois par ffmpeg et commitées.
3. **Protocole NetMD par rejeu** : `UsbTransport` de rejeu (ADR-008) sur des transcriptions capturées du
   MZ-N505 (`tests/netmd/*.trace`) : chaque requête émise est comparée octet à octet, chaque réponse est
   rejouée. Couvre identification, lecture disque, titrage, session sécurisée et upload SP complet.
4. **ATRAC3 round-trip** (phase 6) : encode → décode ffmpeg (exe ffmpeg téléchargé en CI, pas commité)
   → PSNR minimal par vecteur ; analyseur de bitstream maison qui vérifie la structure de chaque frame.

Règle : un ticket d'implémentation livre ses tests. Un bug corrigé (`problems/P-xxx`) ajoute un test
de non-régression qui échoue avant le fix.

## Analyse statique (`build.bat analyze`, CI job `check`)
- `cl /W4 /WX /analyze` sur l'unity build (les warnings sont des erreurs ; on n'en désactive aucun
  globalement, seulement localement avec justification en commentaire).
- `clang-tidy` (livré avec VS Build Tools, `VC\Tools\Llvm\x64\bin`) avec un `.clang-tidy` ciblé :
  `bugprone-*`, `clang-analyzer-*`, `performance-*`, `readability-misleading-indentation`. Pas de
  `cppcoreguidelines-*` ni de règles de style bavardes.
- `build.bat check` : grep des interdictions structurelles (ADR-001) : `core/` sans `windows.h`, sans
  `gl`, sans `malloc`/`free`/`printf` ; `ui/` sans `windows.h`.
- ASan en tests ; UBSan quand clang-cl le supporte sur la cible.
- Budget de taille de l'exe vérifié en CI (`SIZE_BUDGET_KB` à la racine, relevé à chaque phase).

## Boucle d'optimisation (pour les agents Opus)
Opus optimise très bien **quand la cible est mesurable**. Chaque module perf-critique expose donc un
banc dans `build.bat bench` (`tests/bench_main.c`) : temps mur + cycles (`__rdtsc`) + octets traités,
sortie en table, avec un seuil de référence commité (`tests/bench_baseline.txt`). Un ticket d'optimisation
dit : "voici le banc, voici la baseline, voici la cible (ex. resampler ≥ 200x temps réel, scan 50k
fichiers < 2 s), les tests doivent rester verts, l'exe ne doit pas grossir de plus de N KB".

## Review des livraisons (rôle du lead)
Chaque livraison d'agent est relue par le lead avant merge selon cette grille :
1. Frontières : la validation est-elle au bon endroit et nulle part ailleurs ?
2. Mémoire : arènes, pas de malloc, pas d'allocation par frame, pas de copie inutile.
3. Data layout : SoA quand on itère en masse, structs compactes, pas de pointeurs vers du heap éparpillé.
4. Couplage : respect d'ADR-001, `build.bat check` vert.
5. Tests livrés, CI verte, taille de l'exe relevée.
6. Ce qui peut être **supprimé** : la meilleure review enlève du code.
Les remarques deviennent soit un fix immédiat, soit un ticket d'optimisation avec banc.

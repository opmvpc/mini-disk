# Conventions

## Langue
- Docs : français (termes techniques en anglais acceptés).
- Code, identifiants, commentaires dans le code, messages de commit : **anglais**.
- Strings UI : i18n FR/EN via table de strings, jamais en dur.

## Code C
- C99/C11, **unity build** (un seul `main.c` qui `#include` les `.c`), compilateur MSVC `cl.exe` (clang-cl toléré).
- Types : `u8 u16 u32 u64 i8 i16 i32 i64 f32 f64 b32` définis dans `src/base/base.h`. Pas de `int` nu hors boucles locales.
- **Zéro malloc dans les hot paths.** Arènes (`Arena`), scratch arenas (`ArenaTemp`), pools fixes. `malloc` interdit hors `platform/`.
- Pas de CRT en release (`/NODEFAULTLIB`). Nos propres `mem_copy/mem_set/mem_move`, `str_*`, formatage.
- Strings : `String8 { u8 *str; u64 size; }` (slices, non nul-terminées) partout ; conversion UTF-16 seulement à la frontière Win32.
- Erreurs : codes de retour + `String8` message. Pas de `errno`, pas de longjmp. Les fonctions qui peuvent échouer renvoient `b32` ou un `enum XxxError`.
- Data-oriented : SoA quand on itère en masse (bibliothèque : 100k pistes), AoS pour le reste. Pas de listes chaînées de pointeurs vers du heap éparpillé.
- Nommage : `snake_case` fonctions/variables, `PascalCase` types, `UPPER_CASE` macros/enum values préfixées par le module (`NETMD_`, `UI_`).
- Préfixe de module obligatoire : `netmd_`, `ui_`, `lib_` (library), `plan_`, `codec_`, `dsp_`, `os_` (platform), `gl_`.
- Aucune dépendance de `core/` vers `ui/` ou `platform/win32`. `core/` ne parle qu'à `platform.h` (interface abstraite).
- Assertions : `Assert(x)` actif en debug, `AssertAlways(x)` en release pour invariants critiques (protocole USB).
- Un fichier = un module, < ~2000 lignes. Un header par module public, le reste `static`.

## Build
- `build.bat debug` / `build.bat release`. Aucun outil hors MSVC + Windows SDK. Pas de CMake, pas de make.
- Release : `/O2 /GS- /Gs9999999 /GR- /EHa- /NODEFAULTLIB /ENTRY:entry_point /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /MERGE:.rdata=.text`.
- Debug : CRT autorisé (ASan `/fsanitize=address`), `/Zi /Od`.
- La taille du `.exe` release est un **KPI** suivi dans STATUS.md à chaque jalon.

## Commits
- Message : `T-NNN: résumé impératif en anglais` ou `docs: ...` / `build: ...`.
- Un commit = un ticket ou une décision. Jamais de commit "wip" sur `main`.

## Perf (règles Muratori)
- Mesurer avant d'optimiser, mais **concevoir** pour le cache dès le départ.
- Rendu : 1 shader, 1 VBO dynamique, < 10 draw calls/frame, rendu à la demande (0 % CPU au repos).
- Décodage/transcodage : job system sur N-1 threads, buffers réutilisés, SIMD SSE2 baseline.
- USB : thread dédié, file de commandes, l'UI ne bloque jamais.

## Programmation non défensive (ADR-012)
- Valider **aux frontières uniquement** (fichiers, tags, réponses USB, saisie, args). En aval, les structs
  sont canoniques et le code leur fait confiance.
- Pas de `if (!ptr)` dans les APIs internes, pas de re-validation, pas de codes d'erreur pour l'impossible.
  Un état interne invalide = bug = `Assert` (debug) ou `AssertAlways` (invariant qui protège des données).
- Les erreurs du domaine (device absent, disque plein, fichier illisible) sont des valeurs de retour typées.
- Chaque ticket livre ses tests ; chaque bug corrigé ajoute un test de non-régression.
- `build.bat check` et `build.bat analyze` doivent être verts avant tout commit sur `main`.

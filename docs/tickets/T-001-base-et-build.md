# T-001 — `base/`, `build.bat`, runner de tests, fenêtre noire, CI verte

Phase 1 · Statut : **done** (commit voir git log, review lead : fix overflow os_time_now_us) · Dépend de : ADR-001, ADR-002, ADR-012

## Objectif
Poser le socle sur lequel tout le reste s'empile : les types et utilitaires de base, le script de build
unique, le runner de tests, et un exe release qui ouvre une fenêtre noire et se ferme, sous 8 KB.
La CI (`.github/workflows/ci.yml`) doit passer au vert avec ce ticket.

## Livrables
```
build.bat                      debug | release | test | check | analyze | bench | clean
SIZE_BUDGET_KB                 "100" (phase 1)
.clang-tidy
src/main.c                     unity build : #include de tous les .c du projet
src/third_party.c              unity build des libs tierces (vide pour l'instant)
src/base/base.h                types u8..f64/b32, macros (ArrayCount, Min/Max, Assert, AssertAlways,
                               KB/MB, AlignPow2), attributs (inline, no_return), Unused
src/base/base_arena.{h,c}      Arena (réserve virtuelle + commit progressif), ArenaTemp, scratch par thread
src/base/base_string.{h,c}     String8 (slices), str8_lit, cmp/find/split/trim, UTF-8 decode/encode,
                               UTF-16 conversion (frontière Win32), formatage maison str8f (%d %u %x %s %S %c %f
                               minimal) sans CRT
src/base/base_math.{h,c}       f32 helpers SSE (sqrt, floor, abs, min/max, lerp), V2/Rect
src/base/base_hash.{h,c}       hash 64 bits (FNV-1a ou wyhash-like maison), mix
src/base/base_crt_stubs.c      memset/memcpy/memmove/memcmp (__stosb/__movsb), _fltused, chkstk si besoin
src/platform/platform.h        contrat (research/03 §10) — seules les fonctions de ce ticket implémentées :
                               os_init, os_memory_reserve/commit/release, os_file_read_all/write_all,
                               os_time_now_us, os_thread_current_id, os_debug_print, os_exit
src/platform/win32/win32_platform.c   implémentation + entry_point + fenêtre noire (WndProc minimal)
tests/test_main.c              runner : TEST()/EXPECT(), comptage, exit code ≠ 0 si échec
tests/test_base.c              tests arènes, strings, UTF-8, hash, formatage
tests/bench_main.c             squelette de banc (__rdtsc + QueryPerformanceCounter), 1 banc memcpy
```

## Critères d'acceptation
- `build.bat release` → `build\minidisk.exe` < 8 KB, ouvre une fenêtre noire, `--selftest` renvoie 0.
- `build.bat debug` → exe avec CRT + `/fsanitize=address` + PDB.
- `build.bat test` → compile et exécute `tests/test_main.c` en debug ASan, ≥ 40 EXPECT, tout vert.
- `build.bat check` → échoue si `core/` ou `ui/` incluent `windows.h`, ou si `malloc`/`free`/`printf`
  apparaissent hors `platform/` et `third_party.c`.
- `build.bat analyze` → `cl /W4 /WX /analyze` propre + `clang-tidy` propre (chemin :
  `%VSINSTALLDIR%\VC\Tools\Llvm\x64\bin\clang-tidy.exe` ; sur ce poste VS est dans
  `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`, en CI `windows-latest` utiliser
  `vswhere` pour le localiser).
- `build.bat` localise `vcvars64.bat` via `vswhere` (`%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe`)
  et ne dépend d'aucune variable d'environnement préexistante.
- Aucun `malloc`, aucune fonction CRT en release ; `dumpbin /imports` ne montre que kernel32 et user32.

## Notes d'implémentation
- Flags exacts : ADR-002. Ne pas utiliser `/ALIGN:16`.
- `Assert` : `__debugbreak()` en debug ; `AssertAlways` : `__debugbreak()` puis `os_exit(3)` en release.
- Arena : `VirtualAlloc(MEM_RESERVE)` d'une grande plage (ex. 64 GB) + `MEM_COMMIT` par pages de 64 KB ;
  `arena_push` aligne à 8 par défaut ; `ArenaTemp` = position sauvegardée. Pas de free individuel.
- String8 non nul-terminée ; conversion UTF-16 uniquement à la frontière Win32 dans une scratch arena.
- Le formatage maison remplace `printf` partout, y compris dans les tests (via `os_debug_print`).
- Pas de programmation défensive (ADR-012) : `arena_push` ne renvoie jamais NULL, il `Assert`.
- Conventions : docs/CONVENTIONS.md (snake_case, préfixes de module, fichiers < 2000 lignes).

## Prompt d'implémentation
`docs/prompts/I-001-base-et-build.md`

## Livraison (2026-09-06)

Mesures sur le poste de dev (MSVC 14.44, Windows SDK 10.0.26100, VS BuildTools 2022) :

| Critère | Mesure |
|---------|--------|
| `build.bat release` | `build\minidisk.exe` = **7 168 octets** (budget 8 192), `minidisk.pdb` généré |
| Sections | `.text` 0xE44, `.data` 0x30, `.idata` 0x346, `.reloc` 0x10 (4 sections) |
| `dumpbin /imports` | **kernel32.dll** (12 fonctions) + **user32.dll** (9 fonctions), rien d'autre |
| Fenêtre | s'ouvre (titre « minidisk », fond noir), `WM_CLOSE`/Échap ferment, code de sortie 0 |
| `minidisk.exe --selftest` | affiche `selftest: ok`, **code de sortie 0** |
| `build.bat debug` | `minidisk_debug.exe` = 1 021 440 octets, CRT + `/fsanitize=address` + PDB (la DLL ASan est copiée dans `build\`) |
| `build.bat test` | 14 cas, **125 EXPECT**, 0 échec, debug + ASan |
| `build.bat check` | `OK` (et sort en erreur sur violation : vérifié avec un `core/` factice contenant `windows.h` + `malloc`) |
| `build.bat analyze` | `cl /W4 /WX /analyze` propre + `clang-tidy` propre |
| `build.bat bench` | `mem_copy 16 Mo x16 : 5 972 Mo/s, 3.0 o/cycle` |

Écarts et décisions prises en cours de route :
- **`/MERGE:.pdata=.text`** ajouté aux flags release (ADR-002 ne le mentionne pas) : une section de
  moins, −512 octets de fichier.
- **`_tls_index` non résolu sans CRT** : répertoire TLS écrit à la main dans `base_crt_stubs.c`
  (voir `docs/problems/P-003-tls-sans-crt.md`).
- **Le `--selftest` n'utilise pas `str8f`** : le formatage maison (~2.3 Ko de code) n'était tiré dans
  l'exe release que par le message du selftest, ce qui faisait passer l'exe à 9 216 octets. Le
  selftest imprime des littéraux ; `str8f` est couvert par les tests. Il reviendra dans l'exe dès que
  l'UI formatera quoi que ce soit (budget de phase : 100 Ko).
- `build.bat` localise VS via `vswhere` puis appelle `vcvars64.bat` lui-même ; aucune variable
  d'environnement préexistante n'est requise. Sur ce poste, `vcvars64.bat` écrit lui-même un
  `'vswhere.exe' n'est pas reconnu` sur stderr : c'est interne à VS, sans effet sur le build.
- `analyze` passe `/external:anglebrackets /external:W0 /analyze:external-` : sans ça, `/analyze`
  remonte des centaines de C28301 sur les en-têtes du SDK.
- `.clang-tidy` : `bugprone-suspicious-include` désactivé (le unity build inclut des `.c` par
  conception, ADR-002).

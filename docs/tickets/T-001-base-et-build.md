# T-001 — `base/`, `build.bat`, runner de tests, fenêtre noire, CI verte

Phase 1 · Statut : **todo** · Dépend de : ADR-001, ADR-002, ADR-012

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

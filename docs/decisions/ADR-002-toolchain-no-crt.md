# ADR-002 — Toolchain MSVC, release sans CRT, unity build

Statut : **accepté** (2026-09-06) — source : research/03 §1

## Décision
- Compilateur : `cl.exe` (MSVC 14.44), clang-cl toléré. Pas de CMake, pas de make : `build.bat debug|release|check`.
- Deux unités de compilation : `main.obj` (unity build de tout notre code) et `third_party.obj`
  (minimp3, dr_*, stb_*) pour isoler leurs macros.
- Release : `/O2 /Oi /GS- /Gs9999999 /GR- /EHa- /GL` + `link /NODEFAULTLIB /ENTRY:entry_point
  /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /LTCG /MERGE:.rdata=.text /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA`.
  PDB généré même en release.
- **Abandon de `/ALIGN:16 /FILEALIGN:16`** : gain 3-6 KB seulement, image copiée au lieu de mappée, et
  signature typique de packer pour les antivirus.
- Stubs CRT fournis par nous : `memset/memcpy/memmove/memcmp` (`__stosb`/`__movsb`), `_fltused`,
  math f32 SSE maison. Pas de `printf`, formatage maison.
- Debug : CRT autorisé, `/Zi /Od /fsanitize=address`.
- Manifest : DPI per-monitor v2, `activeCodePage` UTF-8, longPathAware.

## Conséquences
- Taille exe = KPI dans STATUS.md. Budget total estimé ~655 KB, jalons par phase (research/03 annexe A).
- Tout code tiers doit compiler sans CRT ou avec nos stubs (vérifié pour minimp3, dr_*, stb_*).

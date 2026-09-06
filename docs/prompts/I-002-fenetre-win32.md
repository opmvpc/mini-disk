# I-002 — Implémentation T-002 (Opus, 2026-09-06)

Prompt donné à l'agent (verbatim, anglais) :

---
HARD RULES: Do NOT spawn sub-agents. Do NOT use the Agent tool. Work alone. Budget: at most 120 tool calls; build and run locally after each milestone.

You are a systems programmer in the Casey Muratori / RAD tradition, implementing ticket T-002 of `minidisk` (pure C, Windows, no CRT in release, tiny exe). T-001 is merged: read the existing code first and build on it, do not rewrite it.

Read in order:
1. C:\Users\admin\repos\mini-disk\docs\tickets\T-002-fenetre-win32.md (deliverables, acceptance criteria)
2. C:\Users\admin\repos\mini-disk\docs\CONVENTIONS.md and docs\decisions\ADR-003-opengl-33-core-rendu-a-la-demande.md, ADR-012-qualite-tests-analyse-statique.md
3. Existing code: src\base\*, src\platform\platform.h, src\platform\win32\win32_platform.c, src\main.c, tests\*, build.bat
4. C:\Users\admin\repos\mini-disk\docs\research\03-tech-stack.md section 2 (window, WGL, DPI, input, on-demand loop, lines 506-1034) and the platform.h sketch in section 10 (lines 3457-3842) for the OsEvent / window API shapes.

Rules: keep the black-window path working; split window code into src\platform\win32\win32_window.c (included from main.c); dwmapi.dll and shell32.dll loaded dynamically (LoadLibraryW/GetProcAddress) so the import table stays kernel32+user32(+gdi32 if truly needed). Events go through a fixed ring buffer in an arena; DropFiles paths are UTF-8 String8 in a per-frame arena. No malloc, no CRT, no defensive checks in internal APIs, assert invariants. Keep the `--selftest` path and all build.bat targets green (test, check, analyze, release). Run `build.bat test`, `check`, `analyze`, `release` yourself and report the numbers: exe size, imports (`dumpbin /imports`), idle CPU measured with `Get-Counter '\Process(minidisk)\% Processor Time'` over ~10 s, test count.

Do not commit. Docs: append a "Livraison" section to the ticket with measurements; create docs/problems/P-00N-<slug>.md (French) only for a real obstacle. Style: English identifiers/comments, snake_case, module prefixes, files under 2000 lines, minimal and specific to the ticket.
---

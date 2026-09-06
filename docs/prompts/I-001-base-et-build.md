# I-001 — Implémentation T-001 (Opus, 2026-09-06)

Prompt donné à l'agent (verbatim, anglais) :

---
HARD RULES: Do NOT spawn sub-agents. Do NOT use the Agent tool. Work alone. Budget: at most 120 tool calls; build and run locally after each milestone rather than writing everything blind.

You are a systems programmer in the Casey Muratori / RAD Game Tools tradition, implementing ticket T-001 of the `minidisk` project (pure C, Windows, no CRT in release, tiny exe, platform-independent core).

Read in this order before writing any code:
1. C:\Users\admin\repos\mini-disk\docs\tickets\T-001-base-et-build.md (the ticket: deliverables, acceptance criteria, implementation notes)
2. C:\Users\admin\repos\mini-disk\docs\CONVENTIONS.md
3. C:\Users\admin\repos\mini-disk\docs\decisions\ADR-001-architecture-trois-couches.md, ADR-002-toolchain-no-crt.md, ADR-012-qualite-tests-analyse-statique.md
4. C:\Users\admin\repos\mini-disk\docs\research\03-tech-stack.md sections 1 (toolchain, lines 45-505) and 10 (code organisation, platform.h, build.bat, lines 3457-3842). Use its platform.h sketch and build.bat as the starting point; only implement the platform functions listed in the ticket.
5. C:\Users\admin\repos\mini-disk\tools\smoke\ (a working 1536-byte no-CRT window; reuse the approach, drop /ALIGN:16).
6. C:\Users\admin\repos\mini-disk\.github\workflows\ci.yml (the CI calls `build.bat check|analyze|test|release`; every target must exist and behave as described).

Environment: MSVC 14.44 at "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" (vcvars64.bat under VC\Auxiliary\Build; clang-tidy under VC\Tools\Llvm\x64\bin). Locate VS through vswhere in build.bat so it also works on GitHub's windows-latest runner. Run commands with PowerShell/cmd; call vcvars64.bat inside build.bat, never rely on the caller's environment.

Deliver every file listed in the ticket. Then verify each acceptance criterion yourself and report the measured numbers (exe size in bytes, test count, `dumpbin /imports` output for the release exe, output of `build.bat check` and `build.bat analyze`). Do not commit. Do not modify docs except: append a short "Livraison" section at the end of the ticket file with the measurements, and if you hit a real obstacle, create docs/problems/P-00N-<slug>.md (Symptôme / Cause / Solution / Leçon, in French).

Code style: English identifiers and comments, snake_case, module prefixes, String8 slices, arenas, no malloc outside platform/, no defensive checks inside internal APIs (assert invariants instead), files under 2000 lines. Keep it minimal: implement what the ticket asks, nothing speculative.
---

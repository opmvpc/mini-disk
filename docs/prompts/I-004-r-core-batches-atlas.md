# I-004 — Implémentation T-004 (Opus, 2026-09-06)

Prompt donné à l'agent (verbatim, anglais) :

---
HARD RULES: Do NOT spawn sub-agents. Do NOT use the Agent tool. Work alone. Budget: at most 120 tool calls; build and run locally after each milestone.

You are a systems programmer in the Casey Muratori / RAD tradition, implementing ticket T-004 of `minidisk` (pure C, Windows, no CRT in release, tiny exe, custom GL renderer). T-001..T-003 are merged: read the existing code and build on it, do not rewrite what works.

Read in order:
1. C:\Users\admin\repos\mini-disk\docs\tickets\T-004-r-core-batches-atlas.md
2. docs\CONVENTIONS.md, docs\decisions\ADR-005-renderer-primitive-unique-sdf.md, ADR-001, ADR-012
3. Existing code: src\ui\r_core.{h,c}, src\ui\r_gl.c, src\ui\gl_loader.{h,c}, src\app\app.c, src\platform\platform.h, tests\test_render.c, build.bat
4. docs\research\03-tech-stack.md section 4 (lines 1559-2013) for batching, persistent mapping, atlas.

Rules: ui/ includes no OS header; all GL through gl_loader; no malloc; frame render commands live in a frame arena passed to r_begin_frame; clip stack with intersection; batches split on (texture, scissor) and sorted stably by layer; persistent-mapped triple-buffered VBO with GL_ARB_buffer_storage when available, fallback orphaning; atlas R8 skyline bottom-left with dirty-region uploads; a small CPU polygon rasterizer with exact-area coverage for 8 test icons stored in the atlas; on-demand rendering and 0 % idle CPU must hold. Add benches to tests\bench_main.c for batch building (10k rects) and skyline packing. Keep every build.bat target green and report: exe size, imports, draw calls in the demo, test count, bench numbers, and a screenshot in build\demo.png (I will look at it).

Do not commit. Docs: append a "Livraison" section to the ticket; docs/problems/P-00N-<slug>.md (French) only for a real obstacle. Style: English identifiers/comments, snake_case, module prefixes, minimal, assert invariants instead of defensive checks.
---

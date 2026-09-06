# I-003 — Implémentation T-003 (Opus, 2026-09-06)

Prompt donné à l'agent (verbatim, anglais) :

---
HARD RULES: Do NOT spawn sub-agents. Do NOT use the Agent tool. Work alone. Budget: at most 120 tool calls; build and run locally after each milestone.

You are a systems programmer in the Casey Muratori / RAD tradition, implementing ticket T-003 of `minidisk` (pure C, Windows, no CRT in release, tiny exe). T-001 and T-002 are merged: read the existing code and build on it.

Read in order:
1. C:\Users\admin\repos\mini-disk\docs\tickets\T-003-wgl-gl33-quad-sdf.md
2. docs\CONVENTIONS.md, docs\decisions\ADR-003-opengl-33-core-rendu-a-la-demande.md, ADR-005-renderer-primitive-unique-sdf.md, ADR-001-architecture-trois-couches.md (ui/ must never include windows.h; GL function pointers come through platform.h `os_gl_get_proc`)
3. Existing code: src\platform\platform.h, src\platform\win32\win32_window.c (CS_OWNDC class already), src\platform\win32\win32_platform.c, src\app\app.c, src\main.c, build.bat, tests\
4. docs\research\03-tech-stack.md section 2.2-2.4 (WGL sequence, lines 506-700) and section 4 (renderer: vertex layout, GLSL for the SDF rect, lines 1559-2013). Use that GLSL as the base.

Rules: opengl32.dll loaded dynamically (LoadLibraryW) so imports stay kernel32+user32; GL 3.3 core forward-compatible, debug context + GL_DEBUG_OUTPUT in debug builds only; vsync on; on-demand rendering stays (only redraw on events / os_request_redraw), 0 % idle CPU must hold. Fragment shader: rounded rect SDF with border and softness, anti-aliased without MSAA; 1-px border must be crisp at 100 % and 150 % DPI (pixel-align in the vertex generation). If the context cannot be created (GL < 3.3), print a clear message via os_debug_print and exit code 2, no crash. Demo in app.c as described in the ticket. `os_window_fill_black` goes away (GL clear replaces it). Keep --selftest green and all build.bat targets green. Report: exe size, imports, idle CPU (`Get-Counter '\Processus(minidisk)\% temps processeur'` on this French Windows), test count, and a screenshot saved to build\demo.png if you can (PowerShell + System.Drawing CopyFromScreen of the window rect) — I will look at it.

Do not commit. Docs: append a "Livraison" section to the ticket; docs/problems/P-00N-<slug>.md (French) only for a real obstacle. Style: English identifiers/comments, snake_case, module prefixes, minimal, no defensive checks inside internal APIs.
---

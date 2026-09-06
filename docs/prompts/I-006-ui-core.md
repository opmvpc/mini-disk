# I-006 — Implémentation T-006 (Opus, 2026-09-06)

Résumé du prompt (règles habituelles : pas de sous-agents, ≤ 130 tool calls, build local à chaque jalon) :
implémenter T-006 (`ui_core` : UI_Box, clés hachées avec pile de seeds, table persistante, `UI_Size` avec
strictness, layout 5 passes, signaux hot/active/focus, animations exponentielles, `ui_animating()` pour le
timeout de la boucle, couches popup/tooltip, piles de style) sur la base de T-001..T-005, ui/ sans en-tête
OS, rendu via r_core/ui_text uniquement, tests tests/test_ui.c, bench layout 10k boxes < 1 ms, exe < 80 KB,
capture build/demo.png, CPU repos en delta exact, pas de commit, section Livraison dans le ticket.

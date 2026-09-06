# I-005 — Implémentation T-005 (Opus, 2026-09-06)

Prompt donné à l'agent (verbatim, anglais) : voir le lancement dans la session ; résumé :
T-005 (texte DirectWrite → atlas R8, cache de glyphes, mesure, ellipsis, fallback japonais/katakana,
4 styles, démo "Hello 世界 ﾃｽﾄ カタカナ" à 100 % et 150 %). Règles habituelles : pas de sous-agents,
≤ 120 tool calls, ui/ sans en-tête OS, dwrite.dll chargée dynamiquement, COM en C (COBJMACROS),
aucune police embarquée, exe < 60 KB, tests dans tests/test_text.c, capture build/demo.png, mesure
CPU repos en delta exact, pas de commit, section Livraison dans le ticket.

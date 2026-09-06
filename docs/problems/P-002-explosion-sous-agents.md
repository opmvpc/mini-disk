# P-002 — Explosion de sous-agents : quota 5h consommé en 10 minutes

## Symptôme
Lancement de 4 agents Opus `general-purpose` en parallèle pour la phase de recherche. En 10 minutes, 16 sous-agents
supplémentaires sont apparus (20 au total) et le quota d'usage de 5h a été entièrement consommé. Aucun rapport
final n'a été écrit dans `docs/research/` (seul un sous-rapport sur les design tokens a abouti).

## Cause racine
Le type d'agent `general-purpose` a accès à l'outil `Agent`. Chaque Opus, face à un prompt très large
("couvre 10 sections, lis les sources"), a lui-même délégué en parallèle à 3-5 sous-agents Opus, qui ont
eux-mêmes parfois délégué. Croissance exponentielle non contrôlée, et chaque agent recharge tout son contexte.

## Solution
1. **Interdiction de déléguer aux agents** : tout prompt d'agent commence par
   "Do NOT spawn sub-agents. Do NOT use the Agent tool. Work alone."
2. **Un agent à la fois**, jamais 4 en parallèle sur de la recherche web.
3. **Modèle** : Sonnet pour la recherche/lecture, Opus réservé à l'implémentation de tickets précis et bornés.
   Fable (le lead) fait lui-même les petites recherches ciblées.
4. **Prompts courts et bornés** : 3-4 questions précises par agent, pas "couvre tout le sujet".
5. **Budget explicite** dans le prompt : "au maximum 25 tool calls, puis écris ton rapport".
6. Le lead vérifie `ListAgents` après chaque lancement.

## Leçon
Le coût d'un agent n'est pas son prompt, c'est ce qu'il peut engendrer. Brider la récursion est non négociable.

## Statut
Résolu (procédure). Rapports R-01..R-04 à refaire en mode économe quand le quota sera revenu.

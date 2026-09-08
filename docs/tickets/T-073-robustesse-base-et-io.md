# T-073 — Robustesse base et E/S : P-010 (arènes et banc), P-009 (sauvegarde hors frame), journal d'erreurs

Phase 7 · Statut : **todo** · Dépend de : P-009, P-010, P-005

## Livrables
- **P-010** : `os_memory_commit` qui échoue dit ce qu'il sait (taille demandée, `GetLastError`, réservé /
  engagé de l'arène) via `os_debug_print` avant l'`AssertAlways` ; `bench_main.c` prend **une arène par
  groupe de bancs**, relâchée entre eux, au lieu d'un gigaoctet jamais rendu ; `jobs_init` sur un pool déjà
  vivant fait un `jobs_shutdown` explicite avec jointure de chaque worker, et un test enchaîne
  `jobs_init`/`jobs_shutdown` ×3 puis un parallel-for vérifié. Le banc complet passe 5 fois de suite
  pendant qu'une compilation tourne à côté.
- **P-009** : la sauvegarde durable du plan (`FlushFileBuffers`) quitte le thread de frame : écriture dans
  un tampon immuable (snapshot du document, pas de verrou sur le plan), puis job `plan_save_job` qui écrit,
  synchronise et renomme ; l'autosave et « Enregistrer » utilisent le même chemin ; un indicateur d'état
  (en cours / à jour / échec) dans l'en-tête du plan. Mesure : coût sur le thread de frame < 200 µs pour
  254 pistes (snapshot compris).
- **Journal d'erreurs** : `os_debug_print` gagne un miroir fichier `<cache>\logs\minidisk-<date>.txt`
  (ring en mémoire de 64 KB vidé par un job toutes les secondes ou à la sortie ; jamais d'E/S sur le thread
  de frame), rotation à 5 fichiers ; les `AssertAlways` écrivent leur dernière ligne avant de sortir.
- **P-005** : re-mesure du CPU résiduel au repos avec le protocole STATUS sur la version actuelle (le
  nombre de threads et de timers a changé depuis la phase 1) ; si > 0,5 %, identifier le réveil (timer du
  thread device ? job de purge ?) et l'éteindre. Consigner la mesure.
- Fermeture propre : ordre d'arrêt documenté (transfert refusé, jobs vidés, thread device joint, journal
  vidé, prefs écrites), testé par un `--selftest` étendu qui démarre et arrête tout.
- Tests : arène (échec d'engagement simulé par une réserve minuscule), pool de jobs réinitialisé, snapshot
  + save job (contenu identique, thread de frame libre), ring du journal (dépassement, rotation).

## Critères d'acceptation
- Bancs stables sous charge ; tests, check, analyze verts ; exe ± 8 KB ; CPU au repos consigné.

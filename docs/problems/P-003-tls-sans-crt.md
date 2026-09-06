# P-003 — `__declspec(thread)` sans CRT : `_tls_index` non résolu

Rencontré pendant T-001 (2026-09-06).

## Symptôme
Le build release échoue au link :

```
main.obj : error LNK2001: symbole externe non résolu _tls_index
build\minidisk.exe : fatal error LNK1120: 1 externes non résolus
```

Le seul usage de TLS statique est `thread_var Arena *tls_scratch[2]` dans `base_arena.c`
(les scratch arenas par thread, cf. research/03 §1.6).

## Cause
`__declspec(thread)` (TLS statique) ne dépend pas seulement du compilateur : il exige que l'image PE
contienne un **répertoire TLS** (`IMAGE_TLS_DIRECTORY`) plus le symbole `_tls_index` que le chargeur
Windows renseigne au démarrage de chaque thread. Ces objets sont normalement fournis par
`tlssup.obj`, qui vit dans le CRT. Avec `/NODEFAULTLIB`, personne ne les émet.

## Solution
On les écrit nous-mêmes dans `src/base/base_crt_stubs.c` (compilé uniquement quand `BUILD_NO_CRT=1`),
au même titre que `memset`/`memcpy`/`_fltused` : sections `.tls` et `.tls$ZZZ` encadrant les données,
`_tls_index`, et un `IMAGE_TLS_DIRECTORY64` placé dans `.rdata$T`, forcé dans l'image par
`#pragma comment(linker, "/INCLUDE:_tls_used")`. Coût mesuré : ~90 octets. Le chargeur alloue alors
le bloc TLS pour chaque thread comme pour un programme lié au CRT.

Deux détails :
- ces identifiants sont imposés par la toolchain, donc réservés au sens de clang-tidy : le fichier
  porte un `NOLINTBEGIN/NOLINTEND(bugprone-reserved-identifier)` justifié en commentaire ;
- en debug (CRT présent) le fichier n'est pas compilé du tout, le CRT fait le travail.

## Leçon
« Pas de CRT » ne veut pas dire seulement « pas de `printf` » : le CRT fournit aussi des morceaux de
**plomberie d'image** (TLS, cookies de pile, `_fltused`). Quand un symbole `_xxx` manque au link sans
CRT, la bonne question n'est pas « comment contourner la fonctionnalité » mais « quel objet du CRT
l'émettait, et combien d'octets coûte sa réimplémentation ». Ici : 90 octets pour garder les scratch
arenas par thread, contre l'abandon du TLS statique.

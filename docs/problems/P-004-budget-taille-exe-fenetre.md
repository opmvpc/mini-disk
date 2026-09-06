# P-004 — L'exe release passe de 7 à 23 KB avec la couche fenêtre

Statut : **ouvert** (décision lead attendue) · Ticket : T-002 · Date : 2026-09-06

## Symptôme
Le critère d'acceptation de T-002 dit « exe release < 16 KB ». Après la livraison de la fenêtre, des
événements et de la boucle à la demande, `build\minidisk.exe` fait **23 040 octets** (7 168 à la fin
de T-001). Le budget CI (`SIZE_BUDGET_KB` = 100) n'est pas dépassé, mais le critère du ticket l'est.

## Analyse
Répartition des sections (`dumpbin /headers`), en octets sur disque :

| Section | Taille |
|---------|--------|
| `.text` (code + `.rdata` + `.pdata` fusionnés) | 20 480 |
| `.data` | 512 |
| `.idata` | 512 |
| `.reloc` | 512 |
| `.rsrc` (manifest PerMonitorV2 embarqué) | 512 |
| en-têtes PE | 512 |

Les plus gros symboles (`/MAP`, tailles déduites des écarts d'adresses) :

| Symbole | Octets |
|---------|--------|
| bloc `.xdata`/`.pdata` (unwind de toutes les fonctions) | ~3 800 |
| `win32_window_proc` | 2 148 |
| `os_window_create` | 1 216 |
| `app_run` | 1 160 |
| `str8_format_buffer` | 1 104 |
| `win32_push_drop_files` | 784 |
| `minidisk_main` | 768 |
| `app_event_description` | 732 |
| `format_f64` | 720 |
| `win32_push_mouse_button` | 688 |

Rien d'aberrant : c'est le coût nominal d'une WndProc complète (une quinzaine de messages traduits),
de la création de fenêtre (DPI, DWM, curseurs, chargement dynamique) et de la boucle. Deux postes sont
en revanche du décor de démo ou de la mécanique de compilateur :

1. **Le formatage (`str8_format_buffer` + `format_f64` + `app_event_description`) ≈ 2,5 KB** n'existe
   que pour afficher le dernier événement dans le titre. Mesure faite : neutraliser
   `app_event_description` ne rend que 1 024 octets, parce que `app_run` utilise encore `str8f` pour
   composer le titre et tracer les drops. Le poste entier disparaîtra ou sera réutilisé en T-003.
2. **~3,8 KB de données d'unwind** (`.xdata`/`.pdata`) émises par MSVC pour chaque fonction, malgré
   `/GS- /GR- /EHa-`. Incompressible sans toucher aux options globales du projet.

## Options
1. **Relever le critère du ticket à 24 KB et le budget de phase 1 en conséquence.** C'est l'option
   honnête : le chiffre de 16 KB avait été posé avant d'écrire la WndProc, et 23 KB pour une fenêtre
   Win32 complète sans CRT reste très en dessous de tout ce qui existe.
2. **Passer la release en `/O1`** (favoriser la taille). Gain attendu 15-25 %, mais cela contredit
   `CONVENTIONS.md` qui fige `/O2`, et pénalisera le resampler et l'ATRAC3 en phase 5-6.
3. **Supprimer le titre de démo de `app.c`.** Rend ~2,5 KB une fois que plus rien n'appelle `str8f`,
   mais T-003 (overlay de debug) et l'UI le rappelleront de toute façon : gain temporaire.

## Recommandation
Option 1. Consigner 23 040 octets comme la nouvelle référence phase 1 dans `STATUS.md` et re-mesurer
à chaque jalon ; le vrai garde-fou reste `SIZE_BUDGET_KB` en CI.

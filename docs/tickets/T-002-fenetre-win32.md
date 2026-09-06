# T-002 — Fenêtre Win32 complète : événements, boucle à la demande, DPI v2, 0 % CPU

Phase 1 · Statut : **done** (review lead : accepté à 23 040 o, cible 16 KB relevée, cf. P-004) · Dépend de : T-001, ADR-003

## Objectif
La couche fenêtre de `platform/win32` telle que l'UI et le renderer l'utiliseront, sans OpenGL encore.

## Livrables
- `src/platform/platform.h` : partie fenêtre/événements/horloge : `os_window_create/destroy`,
  `os_window_get_size/dpi_scale`, `os_window_set_title`, `os_events_pump(blocking, timeout_us)`,
  `os_event_next(OsEvent*)`, `os_request_redraw()`, `os_clipboard_get/set`, `os_cursor_set(OsCursor)`,
  `os_time_now_us`, `os_sleep_us`. `OsEvent` (tagged union) : Key down/up (scancode + vk + modifiers + repeat),
  Char (UTF-32), MouseMove (px physiques, f32), MouseButton, Wheel (lignes et pixels, H/V),
  Resize, DpiChanged, Focus, DropFiles (liste String8 UTF-8 dans arène frame), Close, DeviceChange (stub).
- `src/platform/win32/win32_window.c` : classe, WndProc, DPI per-monitor v2 (manifest + `WM_DPICHANGED`
  → resize suggéré), dark title bar `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)`, `WM_DROPFILES`,
  `WM_MOUSEHWHEEL`, `WM_CHAR` avec paires de surrogates, dead keys OK, clipboard Unicode.
- Boucle **à la demande** : `os_events_pump` bloque sur `MsgWaitForMultipleObjectsEx` jusqu'à un message,
  un événement de réveil (`os_request_redraw` depuis un autre thread via `SetEvent`), ou le timeout des
  animations. Zéro `PeekMessage` en boucle.
- `src/app/app.c` : boucle de démo qui remplit la fenêtre via `PatchBlt`/GDI noir (sera remplacé par GL en
  T-003) et affiche dans le titre le dernier événement reçu.
- Tests : `tests/test_events.c` — encodage/décodage UTF-8 des DropFiles, table scancode→touche, ring
  buffer d'événements.

## Critères d'acceptation
- Fenêtre ouverte inactive : **0,0 % CPU** dans le Gestionnaire des tâches pendant 30 s (mesuré avec
  `Get-Counter '\Process(minidisk)\% Processor Time'`), et < 5 réveils/s.
- Déplacer la fenêtre entre deux écrans de DPI différents redimensionne correctement (ou simuler via
  `WM_DPICHANGED`).
- Glisser 3 fichiers depuis l'Explorateur produit un `DropFiles` avec 3 chemins UTF-8 corrects (accents).
- Exe release < 16 KB, imports : kernel32, user32, gdi32, dwmapi (chargé dynamiquement), shell32 (dynamique).
- `build.bat test/check/analyze` verts.

---

## Livraison (2026-09-06)

### Ce qui est livré
- `src/platform/platform.h` : `OsKey` (positionnel, dérivé du scancode), `OsMod`, `OsMouseButton`,
  `OsCursor`, `OsEventKind`, `OsEvent` (struct plate de taille fixe, `kind` dit quels champs sont
  porteurs), `OsWindow`, et les fonctions `os_window_create/destroy/get_size/dpi_scale/set_title`,
  `os_events_set_frame_arena`, `os_events_pump`, `os_event_next`, `os_request_redraw`,
  `os_redraw_requested`, `os_clipboard_get/set`, `os_cursor_set`, `os_sleep_us`.
- `src/platform/win32/win32_window.c` (~650 lignes) : classe `CS_OWNDC`, WndProc de pure traduction,
  ring buffer d'événements de 512 entrées en arène, DPI per-monitor v2 (manifest + `WM_DPICHANGED` →
  `SetWindowPos` sur le rect suggéré), dark title bar + coins arrondis + bordure DWM, `WM_DROPFILES`,
  `WM_MOUSEHWHEEL` (`SPI_GETWHEELSCROLLLINES`), `WM_CHAR` avec recombinaison des paires de
  substitution, multi-clic compté à la main jusqu'à 3, capture souris, `WM_ENTERSIZEMOVE` + timer,
  `WM_GETMINMAXINFO`, `WM_SETCURSOR`, clipboard Unicode.
- `src/app.manifest` : PerMonitorV2, longPathAware, activeCodePage UTF-8, asInvoker ; embarqué au link
  en debug comme en release.
- `src/app/app.c` : boucle à la demande, dernier événement affiché dans le titre, remplissage noir via
  `os_window_fill_black` (remplacé par le swap GL en T-003).
- `tests/test_events.c` : 3 cas (ring buffer, table scancode→touche, DropFiles UTF-8).
- `win32_platform.c` perd sa fenêtre noire jetable au profit de `app_run()` ; le chemin `--selftest`
  est inchangé et reste vert (release et debug).

### Mesures
| Métrique | Valeur |
|----------|--------|
| Exe release | **23 040 octets** (7 168 avant T-002) — voir P-004 |
| Exe debug (ASan) | 1 021 440 octets |
| Imports | **KERNEL32.dll** (19) + **USER32.dll** (31). Pas de gdi32. `dwmapi.dll` et `shell32.dll` chargées par `LoadLibraryW`/`GetProcAddress` |
| CPU au repos | **0,0 %** (`Get-Counter '\Processus(minidisk)\% temps processeur'`, 30 échantillons de 1 s, max = 0) |
| Réveils au repos | **≈ 0/s** (−2 changements de contexte cumulés sur 30 s, soit du bruit de mesure) |
| Temps CPU total | 0,094 s pour tout le cycle de vie du process (démarrage + session d'événements simulés) |
| Tests | `build.bat test` : **17 cas, 440 checks, 0 échec** (14 cas avant) |
| `build.bat check` | OK |
| `build.bat analyze` | OK (`cl /W4 /WX /analyze` + clang-tidy) |

### Vérifications fonctionnelles (messages injectés par `SendMessage` depuis PowerShell)
- `WM_KEYDOWN` scancode `0x30` → `key=2` (`OsKey_B`), `vk=66`, `sc=30` : table positionnelle correcte.
- `WM_KEYDOWN` étendu `0x14D` → `OsKey_Right`, `vk=39` : le flag étendu est bien pris en compte.
- `WM_CHAR` U+00E9 → `Char U+e9` ; paire `D83C DFB5` → un seul `Char U+1f3b5` (surrogates recombinés).
- `WM_MOUSEHWHEEL` +120 → `lines=3,0 px=75,0` (3 lignes système × 20 px × échelle 1,25).
- `WM_DPICHANGED` (192 dpi) → `DpiChanged 200%` et fenêtre redimensionnée au rect suggéré.
- Fenêtre créée à 1024×640 logiques sur un écran à 120 dpi → client physique **1280×800** : le manifest
  PerMonitorV2 est bien actif dès la création.

### Écarts et limites connues
- **Taille de l'exe : 23 040 octets contre les 16 KB visés.** Sous le budget CI (`SIZE_BUDGET_KB` = 100).
  Analyse et options dans **P-004**.
- Le drop de 3 fichiers depuis l'Explorateur n'est **pas automatisable** : un `HDROP` n'est pas
  partageable entre processus, `WM_DROPFILES` ne peut donc pas être simulé de l'extérieur. La moitié
  risquée (UTF-16 → UTF-8, accents, kana, paire de substitution, chemin vide) est couverte par
  `test_events.c::drop_path_utf8` ; le câblage `DragQueryFileW` reste à valider à la main.
- `os_clipboard_get/set` compile et est exporté par `platform.h`, mais aucun appelant ne l'utilise
  encore : `/OPT:REF` l'élimine du binaire, donc `OpenClipboard` n'apparaît pas dans les imports.
- `WM_MOUSELEAVE`, l'IME et `IDropTarget` ne sont pas traités : hors périmètre T-002 (phase 7).

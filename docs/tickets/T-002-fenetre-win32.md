# T-002 — Fenêtre Win32 complète : événements, boucle à la demande, DPI v2, 0 % CPU

Phase 1 · Statut : **todo** · Dépend de : T-001, ADR-003

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

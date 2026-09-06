# T-020 — WinUSB : énumération, ouverture, control transfers, hotplug, écran "driver manquant"

Phase 3 · Statut : **todo** · Dépend de : T-008, ADR-008, research/01 §1-2, problems/P-001

## Livrables
- `platform.h` : `os_usb_enumerate(arena) → OsUsbDeviceInfo[]` (VID, PID, chemin d'interface, état :
  `Ready` / `NoDriver` / `InUse`), `os_usb_open(path) → OsUsb`, `os_usb_close`,
  `os_usb_control(usb, request_type, request, value, index, buffer, length, timeout_ms) → i32 (octets ou -erreur)`,
  `os_usb_bulk_write(usb, endpoint, data, len, timeout_ms)`, `os_usb_bulk_read`, `os_usb_reset`.
- `src/platform/win32/win32_usb.c` : `winusb.dll` et `setupapi.dll` chargées dynamiquement ;
  énumération par `SetupDiGetClassDevsW` sur `GUID_DEVINTERFACE_USB_DEVICE` filtrée sur VID 054C et la
  table des PIDs NetMD (research/01 §1) ; pour un device présent **sans interface WinUSB** (ProblemCode 28,
  cf. P-001) renvoyer `NoDriver` avec le nom bus ("Net MD Walkman") ; timeouts via `WinUsb_SetPipePolicy`.
- Hotplug : `RegisterDeviceNotification(DBT_DEVTYP_DEVICEINTERFACE, all classes)` dans `win32_window.c`
  → `OsEvent_DeviceChange` (déjà prévu) ; le thread device réénumère sur cet événement.
- `src/core/netmd/netmd_transport.h` : `UsbTransport { control, bulk_write, bulk_read, user }` (ADR-008)
  + implémentation WinUSB (dans `platform/`) + **implémentation de rejeu** (dans `core/netmd/`) qui lit une
  transcription texte (`.trace` : lignes `> hex` envoyé / `< hex` reçu, `# commentaire`) et échoue si la
  requête émise diffère de la transcription.
- `src/core/netmd/netmd_device.c` : thread device dédié (`os_thread_create`) avec file de commandes
  (`NetmdCmd` : Enumerate, Open, Close, ReadDisc, …) et file de résultats vers l'UI (même modèle que
  `lib_events`), jamais d'appel USB depuis le thread principal.
- Vue Disque (`app.c`) : états `Aucun appareil` / `Appareil détecté, driver manquant` (instructions
  Zadig en 3 étapes + bouton "Ouvrir zadig.akeo.ie" via `ShellExecute` dynamique) / `Connecté : <modèle>`
  (nom depuis la table des PIDs, ex. "Sony MZ-N505").
- Tests : `tests/test_usb_replay.c` — le transport de rejeu détecte une requête divergente, rejoue une
  réponse, gère les timeouts ; table des PIDs (lookup, modèle, capacités).

## Critères d'acceptation
- Brancher/débrancher le MZ-N505 met à jour la vue Disque en < 500 ms sans action utilisateur.
- Sans driver : l'écran guidé s'affiche ; avec WinUSB (Zadig) : "Connecté : Sony MZ-N505".
- `core/netmd` sans `windows.h`. Exe < 270 KB (relever `SIZE_BUDGET_KB` à 300 en début de phase 3).
- Tests, check, analyze verts. **Action utilisateur préalable : Zadig → WinUSB sur "Net MD Walkman".**

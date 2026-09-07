# T-020 — WinUSB : énumération, ouverture, control transfers, hotplug, écran "driver manquant"

Phase 3 · Statut : **fait, validation device en attente (Zadig)** · Dépend de : T-008, ADR-008, research/01 §1-2, problems/P-001

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

---

## Livraison

Livré le 2026-09-07 sur la branche `t020`. Les six cibles `build.bat` sont vertes.

### Ce qui a été construit

**`platform.h` — le contrat USB** (~60 lignes, avant la section clipboard) : `OsUsbState { Ready,
NoDriver, InUse }`, `OsUsbError { Failed −1, Timeout −2, Disconnected −3, NotOpen −4,
Divergence −5 }`, `OsUsbDeviceInfo { vid, pid, state, problem_code, path, bus_name }`,
`OsUsbDeviceList`, puis `os_usb_enumerate(arena)`, `os_usb_open(path)`, `os_usb_close`,
`os_usb_is_open`, `os_usb_control(usb, request_type, request, value, index, buffer, length,
timeout_ms)`, `os_usb_bulk_write` / `os_usb_bulk_read(usb, endpoint, data, len, timeout_ms)`,
`os_usb_reset`. Ajout de `os_open_url(url)` pour la page Zadig — `ShellExecuteW` résolu
dynamiquement dans `win32_window.c`, qui charge déjà shell32.

**`src/platform/win32/win32_usb.c` (585 lignes)** — `winusb.dll`, `setupapi.dll`, `cfgmgr32.dll` et
`advapi32.dll` chargées par `LoadLibraryW` au premier appel : la table d'imports reste
kernel32 + user32 (vérifié au `dumpbin /imports`). Énumération des **nœuds** de l'énumérateur `USB`
(`SetupDiGetClassDevsW(0, L"USB", 0, DIGCF_PRESENT | DIGCF_ALLCLASSES)`), VID/PID lus dans l'instance
id, `CM_Get_DevNode_Status` pour le ProblemCode, `SPDRP_FRIENDLYNAME` (repli `SPDRP_DEVICEDESC`)
pour le nom bus. Pour un device dont un pilote est lié : lecture de `DeviceInterfaceGUIDs` (repli
`DeviceInterfaceGUID`) sous `Device Parameters` (`SetupDiOpenDevRegKey` + `RegQueryValueExW`),
parsing du GUID texte, puis `SetupDiGetClassDevsW(&guid, instance_id, …, DIGCF_DEVICEINTERFACE)` —
**aucun GUID en dur**, c'est celui que Zadig (ou un INF) a réellement enregistré. Ouverture :
`CreateFileW` (`FILE_FLAG_OVERLAPPED`) + `WinUsb_Initialize`, puis les politiques de research/01
§2.3 : `PIPE_TRANSFER_TIMEOUT` sur le pipe 0 et les deux bulk (10 s par défaut, re-réglé quand le
`timeout_ms` d'un appel change), `AUTO_CLEAR_STALL` on, `SHORT_PACKET_TERMINATE` off,
`IGNORE_SHORT_PACKETS` off, `AUTO_SUSPEND` off. Erreurs Win32 traduites (`ERROR_SEM_TIMEOUT` →
Timeout ; `ERROR_GEN_FAILURE`, `ERROR_DEVICE_NOT_CONNECTED`, `ERROR_NO_SUCH_DEVICE` →
Disconnected). Quatre slots statiques pour les handles : zéro `malloc`.

**`src/core/netmd/netmd_models.{h,c}`** — les 47 entrées de research/01 §1.2 (Sony, Sharp,
Panasonic, Buffalo, Kenwood, Aiwa), `NetmdCaps { Portable, Deck, TypeS, HiMD, Upload, MonoUpload }`,
`netmd_model_lookup / _known / _name / _count`. C'est **la** table de filtrage : un device USB absent
de la table n'est pas un NetMD.

**`src/core/netmd/netmd_transport.h`** — `UsbTransport { control, bulk_write, bulk_read, user }`
(ADR-008), les trois requêtes vendor (`0x01` poll, `0x80` send, `0x81` read), les `bmRequestType`
`0x41` / `0xC1`, `NETMD_REPLY_MAX 255` et les status bytes **AV/C corrects** (`0x0A` rejected,
`0x0F` interim — pas les valeurs fausses de netmd-js).

**`src/core/netmd/netmd_replay.{h,c}`** — le transport de rejeu : `# commentaire`, `> hex` émis,
`< hex` reçu, `! timeout`. La requête est sérialisée d'une seule façon (control : les 8 octets du
setup packet puis la charge utile d'un OUT ; bulk : l'endpoint puis les données) et comparée octet à
octet ; une divergence renvoie `OsUsbError_Divergence`, **conserve les deux trames** (`expected` /
`actual`, plus le numéro de ligne) et le rejeu reste en échec définitivement. `! timeout` vérifie
quand même la requête, puis renvoie `OsUsbError_Timeout` sans rien écrire dans le buffer.
`netmd_replay_done()` dit si toute la transcription a été jouée.

**`src/core/netmd/netmd_device.{h,c}`** — le thread device : file de commandes (`Enumerate`,
`DeviceChanged`, `Open`, `Ping`, `Close`, `Quit` ; `ReadDisc` arrive avec T-021) et file de
résultats (`Devices`, `Opened`, `Closed`, `Pong`, `Error`), deux anneaux à un producteur et un
consommateur (32 et 64 entrées) sur le modèle de `lib_events`, réveil par sémaphore (0 % CPU au
repos), `os_request_redraw()` à chaque événement publié — c'est ce qui met la vue à jour sans action
utilisateur. **Aucun appel USB hors de ce thread.** Le ping est la séquence de research/01
§2.5-2.7 : poll pré-envoi, drain d'une réponse orpheline, envoi de `getDiscFlags`
(`00 1806 01101000 ff00 0001000b`, §3.7.1), boucle de poll à backoff exponentiel plafonné à 200 ms
sous un budget de 3 s, lecture avec **`poll[1]`** comme bRequest (jamais `0x81` en dur), puis retour
du status byte AV/C.

**Hotplug** — `RegisterDeviceNotificationW(DBT_DEVTYP_DEVICEINTERFACE,
DEVICE_NOTIFY_ALL_INTERFACE_CLASSES)` à la création de la fenêtre,
`UnregisterDeviceNotification` à sa destruction. `WM_DEVICECHANGE` filtre `DBT_DEVICEARRIVAL`,
`DBT_DEVICEREMOVECOMPLETE` et `DBT_DEVNODES_CHANGED` (indispensable : un device sans pilote
n'expose **aucune** interface) → `OsEvent_DeviceChange` → `app_device_changed()` → commande
`DeviceChanged`. Le thread **débounce 200 ms**, avale les notifications restantes de la rafale, et
énumère **une seule fois**.

**UI — `src/app/view_device.c` (nouveau)** — les états du panneau Disque : « Aucun appareil »,
« Appareil détecté, pilote manquant » (nom du modèle, les trois étapes Zadig, bouton « Ouvrir
zadig.akeo.ie » → `os_open_url`), « Appareil utilisé par une autre application », « Connecté :
Sony MZ-N505 » (nom pris dans la table des PIDs) et « Appareil injoignable (%i) ». 13 chaînes FR/EN
dans `strings.h`. `view_plan.c` : deux endroits touchés — le sous-titre du panneau n'est plus le
littéral « MZ-N505 » mais `app_device_subtitle()`, et `app_device_status()` s'affiche en tête du
corps. `app.c` : quatre lignes (`app_device_init` / `_tick` / `_changed` / `_shutdown`).

**Tests — `tests/test_usb_replay.c`, 8 cas** : table des PIDs (lookup, nom, capacités, non-NetMD
rejetés) ; rejeu qui répond ; divergence détectée avec les deux trames conservées et échec
persistant ; `! timeout` ; transcription épuisée ; **aller-retour complet Enumerate → Open → Ping →
Close** dans le thread device sur le transport de rejeu (5 échanges, status `0x09` accepted) ; Open
hors bornes et Ping sans handle ; débounce du hotplug (4 notifications → 1 seule énumération, entre
200 et 500 ms) ; cohérence de `os_usb_enumerate` sur le vrai bus.

### Mesures

| Métrique | Valeur |
|---|---|
| Exe release | **327 168 o** (308 224 o avant T-020, soit **+18 944 o**), marge **20 992 o** sous les 340 KB du ticket et **184 832 o** sous le budget CI de 500 KB |
| Imports | kernel32 + user32 (`dumpbin /imports`) |
| Tests | **152 cas, 5 824 checks, 0 échec** sous ASan (144 cas / 5 731 checks avant) |
| Cibles `build.bat` | debug, release, test, check, analyze, bench — **toutes vertes** |
| `check` | vert : ni `windows.h` ni `malloc` dans `src/core/netmd` |
| Débounce hotplug | 200 ms mesurés par le test (4 notifications → 1 énumération, bout en bout < 500 ms) |
| Chaînes i18n | 13 FR + 13 EN, aucune littérale hors `strings.h` |
| Code ajouté | ~1 800 lignes (585 plateforme, ~640 `core/netmd`, ~200 UI, ~250 tests) |

### Validation sur le vrai device

Le MZ-N505 est resté branché pendant toute la livraison, **toujours sans pilote** : `Get-PnpDevice`
donne `USB\VID_054C&PID_0084\5&A8846EA&0&1`, Status `Error`, `CM_PROB_FAILED_INSTALL` (code 28).
Le chemin NoDriver est donc validé **sur le vrai matériel** : le cas
`usb_enumerate_is_coherent` imprime à chaque `build.bat test`

```
  netmd 054c:0084 Sony MZ-N505 state=1 problem=28 bus="Net MD Walkman"
```

soit : device trouvé par l'énumération des nœuds USB, VID/PID reconnus par la table (« Sony
MZ-N505 », là où le bus ne sait dire que « Net MD Walkman »), état `NoDriver`, ProblemCode 28,
aucun chemin d'interface. C'est exactement ce que le panneau transforme en écran guidé Zadig.

**Reste en attente** (action utilisateur, cf. P-001) : Zadig → WinUSB sur « Net MD Walkman », puis
`os_usb_open` + `os_usb_control` sur le vrai device (ping → status `0x09`) et le chronométrage d'un
vrai branchement/débranchement (< 500 ms). Le journal horodaté est déjà en place :
`app_device_set_state` écrit `device: panel updated %llu ms after hotplug` dans la sortie debug à
chaque changement d'état consécutif à un `WM_DEVICECHANGE`.

### Écarts assumés

1. **Le filtre VID/PID n'est pas dans `win32_usb.c` mais dans `core/netmd`.** Le ticket demandait le
   filtre VID 054C + table des PIDs dans la couche plateforme ; l'y mettre imposait à `platform/` de
   dépendre de `core/netmd/netmd_models.h`, c'est-à-dire d'inverser la direction des dépendances
   d'ADR-001. `os_usb_enumerate` rend donc **tous** les nœuds USB présents (VID, PID, état,
   ProblemCode, nom bus, chemin) et `netmd_device.c` applique la table. Coût mesuré : nul (même
   appel SetupAPI, ~30 nœuds sur cette machine). `netmd_transport.h` reste, lui, inclus par
   `win32_usb.c` : c'est un contrat pur, comme `platform.h`, et c'est ce que le ticket demandait
   pour la liaison WinUSB.
2. **`GUID_DEVINTERFACE_USB_DEVICE` n'est pas utilisé pour l'énumération.** Un device en code 28
   n'expose **aucune** interface : énumérer par ce GUID le rendrait invisible, alors qu'il est tout
   l'objet de l'écran guidé. On énumère les nœuds de l'énumérateur `USB` (`DIGCF_ALLCLASSES`), ce
   qui voit les deux populations ; le chemin d'interface est ensuite résolu par le GUID que
   l'installeur a réellement enregistré.
3. **`InUse` est décidé à l'énumération**, par une ouverture-test (`CreateFileW` puis fermeture
   immédiate) du seul chemin d'interface trouvé : `ERROR_ACCESS_DENIED` / `ERROR_SHARING_VIOLATION`
   → `InUse`. Seuls les devices ayant enregistré un `DeviceInterfaceGUIDs` (donc pilotés par WinUSB)
   sont touchés ; rien d'autre sur le bus n'est ouvert.
4. **`advapi32.dll` s'ajoute aux DLL chargées dynamiquement** (`RegQueryValueExW`, `RegCloseKey`) :
   `DeviceInterfaceGUIDs` vit sous `Device Parameters` et n'est pas une propriété devnode. Elle est
   chargée par `LoadLibraryW` comme les trois autres, donc hors table d'imports.
5. **Le budget de taille écrit dans le ticket (« exe < 270 KB, `SIZE_BUDGET_KB` à 300 »)** datait de
   la planification de la phase 3, avant les phases 3 et 4 ; la consigne de livraison en vigueur est
   < 340 KB avec `SIZE_BUDGET_KB` à 500 (deux tickets parallèles ajoutent décodeurs et DSP).
   327 168 o tient les deux plafonds actuels.

### Revue (lead, 2026-09-07)
- Grille ADR-012 dans le worktree `t020` : `check/test/release/analyze` verts, **327 168 o**, 152 cas / 5 824 checks.
  Le test d'énumération réelle voit le MZ-N505 en code 28 : le chemin « pilote manquant » est validé sur matériel.
- Choix approuvés : énumération de tous les nœuds USB (le device sans pilote n'a pas d'interface), GUID lu dans
  `DeviceInterfaceGUIDs`, filtre VID/PID dans `core/netmd` pour ne pas inverser la dépendance d'ADR-001.
- Reste : ouverture WinUSB + ping réel dès que Zadig aura été passé (P-001), chronométrage du branchement.

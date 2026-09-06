# R-01 — Protocole NetMD : spécification d'implémentation (C pur / WinUSB)

> Document de recherche produit le 2026-09-06 pour le projet `mini-disk`.
> Cible : application Windows native en **C pur**, réimplémentation **from scratch** du protocole
> NetMD au-dessus de **WinUSB**. Aucune ligne de code de `libnetmd` (LGPL-2.1) ou de `netmd-js`
> (GPL-2.0) n'est copiée : ce document décrit le **protocole sur le fil** (faits d'interopérabilité,
> non protégeables) et fournit du **pseudo-code original**.
>
> Périphérique de référence connecté : `USB\VID_054C&PID_0084` → **Sony MZ-N505**, aucun pilote lié.

---

## Table des matières

1. [Identification du device — PID 0084 et table complète des PIDs](#1-identification-du-device)
2. [Pilote Windows : pourquoi « Error », WinUSB/Zadig, dialogue brut](#2-pilote-windows--winusb)
3. [Protocole de commandes : trame AV/C-like, status bytes, commandes complètes](#3-protocole-de-commandes)
4. [Transfert sécurisé (download) : session, EKB, DES, wireformats, commit](#4-transfert-sécurisé--download-vers-le-disque)
5. [netmd-exploits / mode factory : périmètre v1](#5-netmd-exploits--mode-factory)
6. [Timings, débits réels, TOC edit, éjection](#6-timings-débits-toc-edit-éjection)
7. [Faits médium : capacité, granularité, budget de titres TOC](#7-faits-médium--capacité-et-budget-de-titres)
8. [Hi-MD : ce qui change, recommandation v1](#8-hi-md--ce-qui-change)
9. [Checklist d'implémentation priorisée + pièges connus](#9-checklist-dimplémentation-et-pièges)
10. [Notes de licence](#10-notes-de-licence)
11. [Sources / URLs](#11-sources--urls)

---

## 1. Identification du device

### 1.1 Le device connecté

| Champ | Valeur |
|---|---|
| VID | `0x054C` (Sony Corporation) |
| PID | `0x0084` |
| Modèle | **Sony MZ-N505** (portable enregistreur NetMD, 2001) |
| Chaîne USB `iProduct` | typiquement `"Net MD Walkman"` (générique, partagée par presque tous les portables NetMD Sony) |
| SoC ATRAC | **CXD2677** — famille **Type-R** |
| Versions firmware connues | `1.300`, `1.400` (nomenclature netmd-exploits : `R1.300`, `R1.400`) |
| Classe USB | Vendor-specific (`bInterfaceClass = 0xFF`) sur l'interface 0 |
| Endpoints | EP `0x01` BULK IN, EP `0x02` BULK OUT (+ EP0 control) |

**Point crucial** : la chaîne `iProduct` **ne permet pas** d'identifier le modèle. Presque tous les
baladeurs NetMD Sony s'annoncent `"Net MD Walkman"`. Le couple **VID/PID est la seule source
fiable** d'identification de modèle. Le nom précis du modèle ne s'obtient sinon qu'en mode factory
(lecture du « device code » descriptif, cf. §5).

### 1.2 Table des PIDs NetMD / Hi-MD

Table consolidée (source : table de devices de `netmd-js` et `known_devices[]` de `libnetmd`).
Colonne « Type » : `NetMD` = MiniDisc classique (SP/LP2/LP4) ; `Hi-MD` = supporte en plus le mode
Hi-MD (FAT/1 Go) ; `NetMD*` = platine/chaîne hi-fi (pas de baie portable).

| VID | PID | Modèle | Type | Remarques capacités |
|---|---|---|---|---|
| `04dd` | `7202` | Sharp IM-MT899H | NetMD | Vendor Sharp : chemin de renommage disque différent (cf. §3.9) |
| `04dd` | `9013` | Sharp IM-DR400 | NetMD | idem Sharp |
| `04dd` | `9014` | Sharp IM-DR80 | NetMD | `nativeMonoUpload` |
| `04da` | `23b3` | Panasonic SJ-MR250 | NetMD | `nativeMonoUpload` |
| `04da` | `23b6` | Panasonic SJ-MR270 | NetMD | `nativeMonoUpload` ; renvoie `0803` au lieu de `8003` dans la capacité, et `20 %?` au lieu de `20 00` sur l'échange de nonce |
| `0411` | `0083` | Buffalo MD-HUSB | NetMD | |
| `0b28` | `1004` | Kenwood MDX-J9 | NetMD | |
| `054c` | `0034` | Sony PCLK-XX | NetMD | |
| `054c` | `0036` | Sony (modèle inconnu) | NetMD | |
| `054c` | `0075` | Sony MZ-N1 | NetMD | Type-R (CXD2677), fw R1.200 |
| `054c` | `007c` | Sony (modèle inconnu) | NetMD | |
| `054c` | `0080` | Sony LAM-1 | NetMD* | `stop` peut échouer → ignorer l'erreur |
| `054c` | `0081` | Sony MDS-JB980 / MDS-NT1 / MDS-JE780 | NetMD* | `nativeMonoUpload` ; EKB « CorruptedDeck » si leafID = `FF..FF` |
| `054c` | **`0084`** | **Sony MZ-N505** | **NetMD** | **device cible**, Type-R CXD2677 |
| `054c` | `0085` | Sony MZ-S1 | NetMD | |
| `054c` | `0086` | Sony MZ-N707 | NetMD | |
| `054c` | `008e` | Sony CMT-C7NT | NetMD* | |
| `054c` | `0097` | Sony PCGA-MDN1 | NetMD | |
| `054c` | `00ad` | Sony CMT-L7HD | NetMD* | |
| `054c` | `00c6` | Sony MZ-N10 | NetMD | Type-S (CXD2678), fw S1.300 |
| `054c` | `00c7` | Sony MZ-N910 | NetMD | Type-S |
| `054c` | `00c8` | Sony MZ-N710 / MZ-NF810 | NetMD | Type-S (CXD2680), fw S1.600 |
| `054c` | `00c9` | Sony MZ-N510 / MZ-N610 | NetMD | Type-S (CXD2680), fw S1.600 |
| `054c` | `00ca` | Sony MZ-NE410 / MZ-NF520D | NetMD | Type-S |
| `054c` | `00e7` | Sony CMT-M333NT / M373NT | NetMD* | |
| `054c` | `00eb` | Sony MZ-NE810 / MZ-NE910 | NetMD | Type-S |
| `054c` | `0101` | Sony LAM (série) | NetMD* | Tous les LAM annoncent le même PID |
| `054c` | `011a` | Sony CMT-SE7 | NetMD* | |
| `054c` | `0113` | Aiwa AM-NX1 | NetMD | OEM Sony |
| `054c` | `013f` | Sony MDS-S500 | NetMD* | |
| `054c` | `0148` | Sony MDS-A1 | NetMD* | |
| `054c` | `014c` | Aiwa AM-NX9 | NetMD | OEM Sony |
| `054c` | `017e` | Sony MZ-NH1 | Hi-MD | CXD2681 gen1 (`Hn`) |
| `054c` | `0180` | Sony MZ-NH3D | Hi-MD | |
| `054c` | `0182` | Sony MZ-NH900 | Hi-MD | |
| `054c` | `0184` | Sony MZ-NH700 / NH800 | Hi-MD | |
| `054c` | `0186` | Sony MZ-NH600 | Hi-MD | CXD2681 gen1, fw Hn1.000 |
| `054c` | `0187` | Sony MZ-NH600D | Hi-MD | |
| `054c` | `0188` | Sony MZ-N920 | NetMD | Type-S |
| `054c` | `018a` | Sony LAM-3 | NetMD* | |
| `054c` | `01e9` | Sony MZ-DH10P | Hi-MD | avec appareil photo |
| `054c` | `0219` | Sony MZ-RH10 | Hi-MD | CXD2681 gen2, fw Hr1.000 |
| `054c` | `021b` | Sony MZ-RH710 / MZ-RH910 | Hi-MD | |
| `054c` | `021d` | Sony CMT-AH10 | Hi-MD* | |
| `054c` | `022c` | Sony CMT-AH10 (variante) | Hi-MD* | |
| `054c` | `023c` | Sony DS-HMD1 | Hi-MD | dictaphone pro |
| `054c` | `0286` | **Sony MZ-RH1 / M200** | Hi-MD | **seul appareil autorisant l'upload (disque → PC) natif**, CXD2687 (`Hx`) |

### 1.3 Capacités par famille (résumé opérationnel)

| Capacité | NetMD Type-R (dont MZ-N505) | NetMD Type-S | Hi-MD (Hn/Hr) | MZ-RH1 (Hx) |
|---|---|---|---|---|
| Download SP (PCM → ATRAC1 encodé par l'appareil) | oui | oui | oui | oui |
| Download LP2/LP4 (ATRAC3 pré-encodé côté hôte) | oui | oui | oui | oui |
| Encodage LP **dans** l'appareil (`nativeLPEncoding`) | non | non (sauf modèles marqués) | — | — |
| Upload « légal » (`0x30` secure recv track) | non | non | non | **oui** |
| Upload via exploit `CachedSectorNoRAMDownload` | oui (R1.x) | oui | — | — |
| Upload via exploit `CachedSectorControlDownload` | **non** (met le Type-R en état instable) | oui | — | — |
| Upload SP mono via exploit (`MonoSPUpload`) | oui | oui | non | non |
| `SPUpload` / `PCMFasterUpload` (exploits) | **non** (Type-S only) | oui | non | non |
| `ForcedTOCEdit` | oui | oui | non | non |
| Firmware dump | oui | oui | oui | oui |
| Groupes (`//` / `;` dans le titre disque) | oui | oui | oui | oui |

**Conséquence pour la v1** : sur MZ-N505, on cible **download + gestion TOC** ; l'upload est
hors périmètre (nécessite exploit + exécution de code sur l'appareil, cf. §5).

### 1.4 Descripteur USB attendu (à vérifier au runtime)

```
Device Descriptor:
  bDeviceClass        0x00   (par interface)
  idVendor            0x054C
  idProduct           0x0084
  bNumConfigurations  1
Configuration 1:
  bNumInterfaces      1
  Interface 0, AltSetting 0:
    bInterfaceClass     0xFF  (vendor specific)
    bInterfaceSubClass  0x00
    bInterfaceProtocol  0x00
    bNumEndpoints       2
      EP 0x01  BULK IN   wMaxPacketSize 0x0040 (Full-Speed)
      EP 0x02  BULK OUT  wMaxPacketSize 0x0040
```

Le NetMD est un périphérique **USB 1.1 Full-Speed** (12 Mbit/s théoriques) : c'est la borne
haute absolue du débit, jamais atteinte (cf. §6).

---

## 2. Pilote Windows : WinUSB

### 2.1 Pourquoi « Error » dans le Gestionnaire de périphériques

L'interface est `bInterfaceClass = 0xFF` (vendor-specific). Windows n'a **aucun pilote de classe**
pour ça. Historiquement le pilote fourni était `NetMD.sys` (livré avec SonicStage / OpenMG Jukebox),
signé pour Windows XP au plus. Sur Windows 10/11 x64 :

* Le pilote XP n'est pas signé WHQL en mode kernel moderne → refus de chargement
  (`Code 52 : Windows ne peut pas vérifier la signature numérique`).
* Sans pilote du tout → `Code 28 : Les pilotes de ce périphérique ne sont pas installés`,
  affiché « Autre périphérique / Net MD Walkman » avec un point d'exclamation.

Il n'y a **rien à réparer** : la solution moderne est de lier **WinUSB** (`winusb.sys`, pilote
générique in-box Microsoft, signé, présent depuis Vista) à l'interface, puis de piloter le device
entièrement depuis l'espace utilisateur via `winusb.dll`.

### 2.2 Trois façons de lier WinUSB

**(a) Zadig** (manuel, ce que fait toute la communauté MD) :
Zadig écrit un INF minimal + installe `winusb.sys` sur le device sélectionné. C'est la voie de
secours documentée pour l'utilisateur. Inconvénient : manuel, et il faut prévenir l'utilisateur
que SonicStage ne fonctionnera plus (le device n'est plus lié à `NetMD.sys`).

**(b) INF WinUSB signé, distribué avec l'application** (recommandé si signature possible) :

```inf
[Version]
Signature   = "$Windows NT$"
Class       = USBDevice
ClassGuid   = {88BAE032-5A81-49f0-BC3D-A4FF138216D6}
Provider    = %ProviderName%
CatalogFile = minidisk.cat
DriverVer   = 09/06/2026,1.0.0.0

[Manufacturer]
%ProviderName% = MiniDisk_WinUSB,NTamd64,NTarm64

[MiniDisk_WinUSB.NTamd64]
%DeviceName% = USB_Install, USB\VID_054C&PID_0084
; ... une ligne par PID supporté

[USB_Install]
Include = winusb.inf
Needs   = WINUSB.NT

[USB_Install.Services]
Include = winusb.inf
Needs   = WINUSB.NT.Services

[USB_Install.HW]
AddReg = Dev_AddReg

[Dev_AddReg]
HKR,,DeviceInterfaceGUIDs,0x10000,"{A5DCBF10-6530-11D2-901F-00C04FB951ED}"
```

Le `DeviceInterfaceGUID` est **obligatoire** : sans lui, `SetupDiGetClassDevs` ne trouvera aucune
interface exposée et l'ouverture du device sera impossible.
(Choisir un GUID **propre à l'application**, généré par `uuidgen` — l'exemple ci-dessus utilise le
GUID « USB device » historique, à ne pas réutiliser tel quel.)

**(c) libwdi / installation programmatique** : génère l'INF + le catalogue à la volée et appelle
`SetupCopyOEMInf`/`DiInstallDriver` avec élévation. C'est ce que fait Zadig en interne.
Attention : libwdi est LGPL-3.0 — cf. §10.

**Alternative sans pilote** : sur Windows 10 1803+, un device qui expose un descripteur BOS/MS OS
2.0 se lie automatiquement à WinUSB. Les NetMD, conçus en 2001, n'en ont évidemment pas. Il est
possible de forcer via une clé registre `WinUSB` par `Device Parameters`, mais c'est fragile ;
préférer (b) ou (a).

### 2.3 Ouverture du device en C (esquisse)

```c
// 1. Énumérer les interfaces exposant notre GUID
HDEVINFO set = SetupDiGetClassDevsW(&MINIDISK_IFACE_GUID, NULL, NULL,
                                    DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
SP_DEVICE_INTERFACE_DATA ifd = { .cbSize = sizeof ifd };
for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, NULL, &MINIDISK_IFACE_GUID, i, &ifd); ++i) {
    // SetupDiGetDeviceInterfaceDetailW deux fois (taille puis données)
    //   -> DevicePath : L"\\\\?\\usb#vid_054c&pid_0084#...#{guid}"
    // Filtrer sur "vid_054c&pid_" et la table de PIDs (§1.2), en insensible à la casse.
}

// 2. Ouvrir
HANDLE h = CreateFileW(devicePath,
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);

// 3. Initialiser WinUSB
WINUSB_INTERFACE_HANDLE wh;
WinUsb_Initialize(h, &wh);          // interface 0 = handle racine
```

Politiques de pipe à régler **avant** tout transfert :

```c
ULONG timeout = 10000;              // ms, par pipe
WinUsb_SetPipePolicy(wh, 0x01, PIPE_TRANSFER_TIMEOUT, sizeof timeout, &timeout); // BULK IN
WinUsb_SetPipePolicy(wh, 0x02, PIPE_TRANSFER_TIMEOUT, sizeof timeout, &timeout); // BULK OUT

UCHAR on = TRUE;
WinUsb_SetPipePolicy(wh, 0x01, AUTO_CLEAR_STALL, sizeof on, &on);
WinUsb_SetPipePolicy(wh, 0x02, AUTO_CLEAR_STALL, sizeof on, &on);

UCHAR off = FALSE;
// SHORT_PACKET_TERMINATE = FALSE : ne PAS ajouter de ZLP après un write multiple de wMaxPacketSize.
WinUsb_SetPipePolicy(wh, 0x02, SHORT_PACKET_TERMINATE, sizeof off, &off);
// IGNORE_SHORT_PACKETS = FALSE : un short packet doit terminer la lecture (essentiel en upload).
WinUsb_SetPipePolicy(wh, 0x01, IGNORE_SHORT_PACKETS, sizeof off, &off);
```

**Ne pas** activer `RAW_IO` sur le BULK OUT au début : `RAW_IO` impose que chaque buffer soit un
multiple de `wMaxPacketSize` et supprime le buffering du driver. Il devient intéressant plus tard
pour maximiser le débit du download (buffers alignés sur 64 octets), mais ajoute une contrainte
forte sur le découpage des paquets DES (voir §4.7 : les paquets font 0x00100000 - 24 octets pour
le premier, ce qui n'est pas un multiple de 64).

**Timeout du control transfer** : WinUSB n'expose pas de timeout par transfert sur EP0. Le timeout
est celui du pipe 0 (`WinUsb_SetPipePolicy(wh, 0, PIPE_TRANSFER_TIMEOUT, ...)`) ; en pratique, mieux
vaut faire des I/O **overlapped** et arbitrer soi-même avec `WaitForSingleObject` +
`CancelIoEx`.

### 2.4 Le canal de commande : tout passe par EP0 (control transfers vendor)

C'est la particularité la plus déroutante du NetMD : **les commandes et réponses ne passent pas par
les endpoints bulk**. Elles passent par des **control transfers vendor-specific, recipient =
interface**, sur EP0. Les endpoints bulk ne servent qu'au flux audio.

Trois requêtes vendor seulement :

| bRequest | Direction | Rôle |
|---|---|---|
| `0x01` | IN (`0xC1`) | **Poll** : demande l'état/longueur de la réponse en attente |
| `0x80` | OUT (`0x41`) | **Send command** : envoie la trame de commande |
| `0x81` | IN (`0xC1`) | **Read reply** : lit la trame de réponse |
| `0xFF` | IN/OUT | Jeu de commandes **factory** (hors périmètre v1, cf. §5) |

`bmRequestType` détaillé :

```
OUT : 0x41 = 0<<7 (Host→Device) | 0b10<<5 (Vendor) | 0b00001 (Interface)
IN  : 0xC1 = 1<<7 (Device→Host) | 0b10<<5 (Vendor) | 0b00001 (Interface)
wValue = 0x0000 ; wIndex = 0x0000 (numéro d'interface = 0)
```

### 2.5 Le poll : format et sémantique exacts

Le poll est un `IN` de **4 octets** :

```
bmRequestType=0xC1 bRequest=0x01 wValue=0x0000 wIndex=0x0000 wLength=0x0004
```

Interprétation du buffer de 4 octets renvoyé :

| Offset | Signification |
|---|---|
| `[0]` | **Flag de disponibilité**. `0x00` = rien à lire / device occupé. Non nul = une réponse est prête. |
| `[1]` | **bRequest à utiliser pour lire la réponse.** Vaut normalement `0x81` (réponse standard) ou `0xFF` (réponse factory). |
| `[2]` | **Longueur de la réponse en octets** (un seul octet → max 255). |
| `[3]` | Inutilisé / réservé (observé à `0x00`). |

Deux invariants tirés du comportement des implémentations de référence :

1. **Avant d'envoyer une commande**, il faut poller **une fois** et vérifier que `buf[2] == 0`.
   Si le device a encore une réponse en attente, envoyer une nouvelle commande la perd et
   désynchronise la machine à états. `libnetmd` fait exactement `netmd_poll(dev, buf, 1)` et
   retourne `NETMDERR_NOTREADY` si `len != 0`.
2. **Après avoir envoyé une commande**, il faut poller **en boucle** jusqu'à ce que `buf[0] != 0`,
   puis lire `buf[2]` octets avec `bRequest = buf[1]`.

C'est aussi pour ça que la séquence d'ouverture doit **drainer** une éventuelle réponse orpheline :
au tout début de la session, poller une fois ; si `len > 0`, lire et jeter.

### 2.6 Stratégies de retry / temporisation (comparaison des deux références)

| | `libnetmd` | `netmd-js` |
|---|---|---|
| Essais de poll en réception | 30 (`NETMD_RECV_TRIES`) | illimité |
| Attente entre polls | `msleep(1000)` à partir du 2ᵉ essai (1 s fixe) | backoff exponentiel : `10 * 2^i` ms (10, 20, 40, 80, 160, …) |
| Timeout control transfer | 1000 ms (`NETMD_POLL/SEND/RECV_TIMEOUT`) | timeout WebUSB par défaut |
| Timeout bulk write (download) | 80 000 ms | — |
| Timeout bulk read (upload) | 10 000 ms | — |

**Recommandation pour l'implémentation C** : backoff exponentiel plafonné, avec un budget total
explicite par commande.

```c
// Pseudo-code (original) — attente d'une réponse
static int md_wait_reply(md_dev *d, uint8_t *out_request, uint8_t *out_len,
                         uint32_t budget_ms)
{
    uint32_t waited = 0, delay = 5;
    uint8_t poll[4];
    for (;;) {
        if (md_ctrl_in(d, /*bRequest*/0x01, poll, 4) != 4) return MD_ERR_USB;
        if (poll[0] != 0) { *out_request = poll[1]; *out_len = poll[2]; return MD_OK; }
        if (waited >= budget_ms) return MD_ERR_TIMEOUT;
        md_sleep(delay);
        waited += delay;
        delay = (delay < 200) ? delay * 2 : 200;   // plafond 200 ms
    }
}
```

Budgets conseillés (empiriques, à ajuster) :

| Opération | Budget |
|---|---|
| Commandes d'interrogation (titres, flags, capacité) | 3 000 ms |
| `changeDescriptorState` | 3 000 ms |
| `play/pause/stop/goto` | 5 000 ms |
| `eraseTrack` / `moveTrack` (édition TOC) | 15 000 ms |
| `eraseDisc` | 30 000 ms |
| Fin de `sendTrack` (après le dernier paquet) | 60 000 ms |
| `commitTrack` (écriture TOC) | 30 000 ms |
| `ejectDisc` | 10 000 ms |

### 2.7 Séquence complète d'un aller-retour

```
   HOST                                             DEVICE
    |  CTRL IN  0xC1 0x01 wLen=4                       |
    |------------------------------------------------->|
    |<------------------------------------------------ |  00 81 00 00   (rien en attente : OK)
    |                                                  |
    |  CTRL OUT 0x41 0x80 data = <trame de commande>    |
    |------------------------------------------------->|
    |                                                  |
    |  CTRL IN  0xC1 0x01 wLen=4      (boucle)          |
    |------------------------------------------------->|
    |<------------------------------------------------ |  00 81 00 00   (pas encore prêt)
    |  ... attente, re-poll ...                         |
    |<------------------------------------------------ |  01 81 1a 00   (26 octets prêts, via 0x81)
    |                                                  |
    |  CTRL IN  0xC1 0x81 wLen=26                       |
    |------------------------------------------------->|
    |<------------------------------------------------ |  09 18 06 ...  (trame de réponse)
    |                                                  |
    |  CTRL IN  0xC1 0x01 wLen=4   (poll de clôture)    |   <- netmd-js le fait systématiquement
    |------------------------------------------------->|
```

Le **poll de clôture** après lecture (fait par `netmd-js` dans `_readReply`) n'est pas
strictement nécessaire mais aide à resynchroniser certains appareils. Le conserver est prudent.

### 2.8 Contraintes de taille

* La longueur de réponse tient sur **un seul octet** (`poll[2]`) → **255 octets maximum** par
  réponse. C'est pourquoi la lecture d'un titre disque long est **paginée** (cf. §3.8).
* Les commandes envoyées sur EP0 sont limitées en pratique à quelques centaines d'octets ;
  la plus grosse est `sendKeyData` (~ 90 octets avec la keychain à 2 clés).
* Les données audio (potentiellement des dizaines de Mo) passent **exclusivement** par les
  endpoints bulk, jamais par EP0.

### 2.9 Erreurs USB à gérer explicitement

| Situation | Symptôme WinUSB | Traitement |
|---|---|---|
| Device débranché | `ERROR_GEN_FAILURE` / `ERROR_DEVICE_NOT_CONNECTED` | Fermer le handle, notifier l'UI, réénumérer sur `WM_DEVICECHANGE` |
| Pipe bloqué (STALL) | `ERROR_GEN_FAILURE` sur le bulk | `WinUsb_ResetPipe` puis rejouer ; `AUTO_CLEAR_STALL` le fait déjà |
| Timeout | `ERROR_SEM_TIMEOUT` | Compter dans le budget, ne pas boucler indéfiniment |
| Suspension USB selective suspend | transferts très lents au réveil | Désactiver via `WinUsb_SetPowerPolicy(AUTO_SUSPEND, FALSE)` pendant un transfert |

```c
UCHAR autoSuspend = FALSE;
WinUsb_SetPowerPolicy(wh, AUTO_SUSPEND, sizeof autoSuspend, &autoSuspend);
```

---

## 3. Protocole de commandes

### 3.1 Modèle général : AV/C (IEC 61883-1 / 1394 TA) transposé sur USB

Le NetMD réutilise le **jeu de commandes AV/C** (le protocole des caméscopes DV FireWire),
transporté sur des control transfers USB. Une trame de commande est :

```
+--------+-----------------------------------------------+
| status |  ctype/opcode/operands (payload AV/C)          |
+--------+-----------------------------------------------+
   1 o                    n octets
```

Le premier octet est le **status byte** (appelé `ctype` en AV/C). Les implémentations de référence
n'utilisent en pratique que deux valeurs en émission :

| Valeur | Nom AV/C | Usage NetMD |
|---|---|---|
| `0x00` | CONTROL | quasiment toutes les commandes |
| `0x01` | STATUS | rarement |
| `0x02` | SPECIFIC INQUIRY | test « cette commande est-elle supportée ? » (ex. `canEjectDisc`) |
| `0x03` | NOTIFY | non utilisé |
| `0x04` | GENERAL INQUIRY | non utilisé |

> ⚠️ **Piège documenté** : `netmd-js` définit `specificInquiry = 0x01` dans son enum `Status`,
> alors que la constante AV/C (et `libnetmd`) donne `STATUS = 0x01` et `SPECIFIC INQUIRY = 0x02`.
> Les deux valeurs fonctionnent pour un test de support ; retenir **`0x01`** (comportement
> effectivement observé et utilisé par `netmd-js` pour `canEjectDisc`), et se souvenir que la
> littérature diverge ici.

### 3.2 Status bytes de **réponse** (premier octet renvoyé)

Valeurs AV/C canoniques (celles de `libnetmd`, qui sont les bonnes) :

| Valeur | Nom | Sens NetMD |
|---|---|---|
| `0x08` | NOT IMPLEMENTED | commande inconnue de l'appareil |
| `0x09` | ACCEPTED | succès (réponse à un CONTROL) |
| `0x0A` | **REJECTED** | refusée (mauvais état, piste inexistante, disque protégé…) |
| `0x0B` | IN TRANSITION | occupé |
| `0x0C` | IMPLEMENTED / STABLE | succès (réponse à un STATUS/INQUIRY) |
| `0x0D` | CHANGED | |
| `0x0F` | **INTERIM** | « j'ai compris, ça va prendre du temps, la vraie réponse suivra » |

> ⚠️ **Piège majeur** : `netmd-js` déclare `rejected = 0x0b` et `interim = 0x0d`, ce qui **ne
> correspond pas** à AV/C. Son code ne casse pas parce que sa branche « statut inconnu » est morte
> (`[...].indexOf(status) < -1` est toujours faux), donc tout ce qui n'est pas `0x08`/`0x0b` est
> accepté silencieusement. **Ne pas recopier cette table.** Utiliser les valeurs AV/C ci-dessus,
> et notamment traiter `0x0F` comme INTERIM.

Gestion recommandée de l'INTERIM :

* Pour les commandes normales : recevoir `0x0F`, puis **re-poller** et lire la réponse finale
  (`0x09`). `netmd-js` fait jusqu'à 4 tentatives espacées de `100 * (2^n - 1)` ms.
* Pour `sendTrack` (`0x28`) et `recvTrack` (`0x30`) : le `0x0F` est **attendu et significatif**
  — il autorise le début du transfert bulk. Il ne faut surtout pas boucler dessus.

```c
typedef enum {
    MD_ST_NOT_IMPLEMENTED = 0x08,
    MD_ST_ACCEPTED        = 0x09,
    MD_ST_REJECTED        = 0x0A,
    MD_ST_IN_TRANSITION   = 0x0B,
    MD_ST_IMPLEMENTED     = 0x0C,
    MD_ST_CHANGED         = 0x0D,
    MD_ST_INTERIM         = 0x0F
} md_status;
```

### 3.3 Le mini-langage de formatage (formatQuery / scanQuery)

Toutes les implémentations décrivent les trames par une chaîne de format hexadécimale avec des
placeholders. C'est un outil de lisibilité extrêmement utile — il vaut la peine de le réimplémenter
en C (30 lignes), car il rend le code de commande auto-documenté.

Grammaire :

| Token | En **émission** (`format`) | En **réception** (`scan`) |
|---|---|---|
| `xx` (2 chiffres hex) | octet littéral | octet attendu ; mismatch = erreur de parsing |
| espace | ignoré (lisibilité) | ignoré |
| `%b` | 1 octet, big-endian | lit 1 octet |
| `%w` | 2 octets, big-endian | lit 2 octets |
| `%d` | 4 octets, big-endian | lit 4 octets |
| `%q` | 8 octets, big-endian | lit 8 octets |
| `%<b/w/d/q` | idem mais **little-endian** | idem |
| `%B` | 1 octet **BCD** (`42` décimal → `0x42`) | lit 1 octet BCD → décimal |
| `%W` | 2 octets BCD | lit 2 octets BCD |
| `%x` | buffer précédé de 2 octets de longueur | lit longueur (2 o) puis buffer |
| `%z` | buffer précédé de 1 octet de longueur | lit longueur (1 o) puis buffer |
| `%s` | comme `%x` mais longueur+1 et NUL final | — |
| `%*` | buffer brut (tout le reste) | consomme tout le reste |
| `%#` | — | consomme tout le reste (alias de `%*`) |
| `%?` | — | **saute** 1 octet sans le vérifier ni le retourner |

Fait important : **le parsing doit consommer exactement toute la réponse** ; un reliquat d'octets
signale un modèle de réponse différent (variante constructeur) — c'est un excellent détecteur de
bug.

Esquisse C (originale) :

```c
/* Écriture : md_fmt(buf, cap, "1806 022018%b %w 3000 0a00 ff00 00000000", wchar, track) */
size_t md_fmt(uint8_t *out, size_t cap, const char *fmt, ...);

/* Lecture : renvoie le nombre de champs extraits, ou < 0 si mismatch.
   Les sorties sont passées par pointeurs variadiques typés. */
int md_scan(const uint8_t *in, size_t len, const char *fmt, ...);
```

Encodage BCD (nécessaire pour les temps) :

```c
static uint8_t bcd_from_u8(uint8_t v)  { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static uint8_t bcd_to_u8 (uint8_t b)   { return (uint8_t)(((b >> 4) * 10) + (b & 0x0F)); }
```

### 3.4 Descripteurs : ouvrir/fermer avant de lire ou d'écrire

Beaucoup de commandes d'interrogation ou d'édition doivent être encadrées par une ouverture et une
fermeture de « descripteur » (une base de données interne de l'appareil). Ne pas le faire provoque
des `REJECTED` erratiques selon les modèles.

Commande : `1808 <descriptor> <action> 00`

Descripteurs :

| Nom | Octets | Contenu |
|---|---|---|
| `discTitleTD` | `10 1801` | titre disque half-width |
| `audioUTOC1TD` | `10 1802` | titres de pistes half-width |
| `audioUTOC4TD` | `10 1803` | titres de pistes full-width (kanji) |
| `DSITD` | `10 1804` | Disc Subunit Information |
| `audioContentsTD` | `10 1001` | contenu audio (nb de pistes, longueurs, encodages) |
| `rootTD` | `10 1000` | racine (flags disque, capacité) |
| `discSubunitIdentifier` | `00` | identification de la sous-unité |
| `operatingStatusBlock` | `80 00` | état de fonctionnement / position de lecture |

Actions :

| Nom | Octet |
|---|---|
| `openRead` | `01` |
| `openWrite` | `03` |
| `close` | `00` |

Exemples hex complets (avec le status byte `00` en tête) :

```
Ouvrir audioContentsTD en lecture :  00 1808 10 1001 01 00
Fermer audioContentsTD            :  00 1808 10 1001 00 00
Ouvrir discTitleTD en écriture    :  00 1808 10 1801 03 00
Ouvrir operatingStatusBlock (RO)  :  00 1808 80 00   01 00
```

**Pratique recommandée** : ignorer les erreurs de `changeDescriptorState` (certains appareils
rejettent l'ouverture d'un descripteur déjà ouvert), mais **toujours** émettre le `close`
correspondant. Un descripteur laissé ouvert bloque des commandes ultérieures.

### 3.5 Acquisition / libération de l'appareil

Avant toute session d'écriture, il faut « acquérir » l'appareil (désactive les touches du
baladeur et signale une session hôte) :

```
acquire  :  00 ff 010c ffff ffff ffff ffff ffff ffff
réponse  :  09 ff 010c ffff ffff ffff ffff ffff ffff

release  :  00 ff 0100 ffff ffff ffff ffff ffff ffff
réponse  :  09 ff 0100 ffff ffff ffff ffff ffff ffff
```

⚠️ Un `acquire` sans `release` laisse l'appareil verrouillé (« PC --> MD » figé à l'écran) jusqu'au
débranchement. Prévoir un `release` dans **tous** les chemins d'erreur (pattern RAII manuel /
`goto cleanup`).

### 3.6 Identification et niveau NetMD

```
Ouvrir descripteur 00 en lecture :   00 1808 00 01 00
Query                            :   00 1809 00 ff00 0000 0000
Réponse (format de scan)         :   09 1809 00 1000 %?%? %?%? %w %b %b %b %b %w %*
```

Décodage du buffer final `%*` :

```
+0    listID[amtOfRootObjectLists]      (sizeOfListID octets chacun)
+..   subunitDependentLength   (2 octets BE)
+..   subunitFieldsLength      (2 octets BE)
+..   attributes               (1 octet)
+..   discSubunitVersion       (1 octet)
+..   amtSupportedMediaTypes   (1 octet)
      pour chaque type :
        supportedMediaType     (2 octets BE)   -- 0x0301 = 769 = MiniDisc audio
        implementationProfileID(1 octet)       -- <= NetMD level
        mediaTypeAttributes    (1 octet)
        typeDependentLength    (2 octets BE)
        mdAudioVersion         (1 octet)
        supportsMDClip         (1 octet)
+..   manufacturerDependentLength (2 octets BE)
+..   manufacturerDependentData
```

Le champ `implementationProfileID` du type `0x0301` donne le **niveau NetMD** :

| Valeur | Niveau | Sens |
|---|---|---|
| `0x20` | Level 1 | Network MD (download seulement) |
| `0x50` | Level 2 | Program Play MD |
| `0x70` | Level 3 | Editing MD (déplacement/effacement/titres) |

Si `0x0301` (769) est absent de la liste des types supportés, l'appareil **n'est pas un
enregistreur MiniDisc** → abandonner.

### 3.7 Interrogation du disque

#### 3.7.1 Flags du disque

```
Descripteur : rootTD (10 1000) openRead
Query       : 00 1806 01101000 ff00 0001000b
Scan        : 09 1806 01101000 1000 0001000b %b
Descripteur : rootTD close
```

| Bit | Constante | Sens |
|---|---|---|
| `0x10` | `DISC_WRITABLE` | disque enregistrable (non préenregistré) |
| `0x40` | `DISC_WRITE_PROTECTED` | onglet de protection fermé |

#### 3.7.2 Nombre de pistes

```
Descripteur : audioContentsTD (10 1001) openRead
Query       : 00 1806 02101001 3000 1000 ff00 00000000
Scan        : 09 1806 02101001 %?%? %?%? 1000 00%?0000 0006 0010000200 %b
Descripteur : audioContentsTD close
```

Le dernier `%b` est le nombre de pistes (0 à 254).

#### 3.7.3 Capacité du disque (utilisé / total / restant)

```
Descripteur : rootTD openRead
Query       : 00 1806 02101000 3080 0300 ff00 00000000
Scan        : 09 1806 02101000 3080 0300 1000 001d0000 001b %?03 0017 8000
                 0005 %W %B %B %B      <- used   (temps)
                 0005 %W %B %B %B      <- total  (temps)
                 0005 %W %B %B %B      <- left   (temps)
Descripteur : rootTD close
```

Chaque triplet renvoie **4 valeurs BCD** : `[heures(2 octets BCD), minutes, secondes, frames]`.

> ⚠️ Le `%?03` du scan est un contournement : la plupart des appareils renvoient `8003`, mais
> **Panasonic renvoie `0803`**. Ne jamais vérifier cet octet en dur.

Conversion en frames (unité de base : **512 frames = 1 seconde**) :

```c
static uint32_t md_time_to_frames(const uint8_t t[4]) /* t = {h, m, s, f} déjà décodés BCD */
{
    return (((uint32_t)t[0] * 60u + t[1]) * 60u + t[2]) * 512u + t[3];
}
```

Formatage inverse :

```c
void md_frames_to_hmsf(uint32_t v, int *h, int *m, int *s, int *f)
{
    *f = (int)(v % 512u); v /= 512u;
    *s = (int)(v % 60u);  v /= 60u;
    *m = (int)(v % 60u);  v /= 60u;
    *h = (int)v;
}
```

> ⚠️ **Correctif Sharp** : certains appareils rapportent la capacité **exprimée dans le mode
> d'enregistrement courant** (donc doublée en LP2, quadruplée en LP4). Correctif empirique
> appliqué par les implémentations de référence : tant que `framesTotal > 512 * 60 * 82`
> (c.-à-d. plus de 82 minutes en SP, physiquement impossible), diviser `used`, `total` et `left`
> par 2. À reproduire.

#### 3.7.4 Informations par piste

Interrogation générique :

```
Descripteur : audioContentsTD openRead
Query       : 00 1806 02201001 %w %w %w ff00 00000000     (track, p1, p2)
Scan        : 09 1806 02201001 %?%? %?%? %?%? 1000 00%?0000 %x
Descripteur : audioContentsTD close
```

Le `%x` renvoie un sous-buffer à re-parser :

**(a) Longueur de la piste** — `p1 = 0x3000`, `p2 = 0x0100`

```
sous-buffer : 0001 0006 0000 %B %B %B %B     ->  [heures, minutes, secondes, frames] BCD
```

Exemple pour la piste 0 (index 0-based) :

```
Requête :  00 1806 02201001 0000 3000 0100 ff00 00000000
Réponse :  09 1806 02201001 0000 3000 0100 1000 000b0000
           0009 0001 0006 0000 00 03 2f 01a
                                 ^^ ^^ ^^ ^^  = 00h 03m 47s 026f  (BCD)
```

**(b) Encodage et canaux** — `p1 = 0x3080`, `p2 = 0x0700`

```
sous-buffer : 8007 0004 0110 %b %b     ->  [encoding, channels]
```

| `encoding` | Mode |
|---|---|
| `0x90` | SP (ATRAC1) |
| `0x92` | LP2 (ATRAC3 132 kbit/s) |
| `0x93` | LP4 (ATRAC3 66 kbit/s, joint-stereo) |

| `channels` | |
|---|---|
| `0x00` | stéréo (2 canaux) |
| `0x01` | mono (1 canal) |

**(c) Flags de la piste** (protection SCMS/checkout)

```
Descripteur : audioContentsTD openRead
Query       : 00 1806 01201001 %w ff00 00010008          (track)
Scan        : 09 1806 01201001 %?%? 10 00 00010008 %b
```

| Valeur | Sens |
|---|---|
| `0x00` | non protégée (effaçable, déplaçable) |
| `0x03` | **protégée** (piste « checked out », l'effacer nécessite un check-in ou un TOC edit forcé) |

### 3.8 Titres : lecture

Deux espaces de titres coexistent sur un MiniDisc :

* **half-width** (« demi-chasse ») : jeu ASCII + katakana demi-chasse, encodé **Shift-JIS**,
  1 octet par caractère pour l'ASCII. C'est le titre « normal ».
* **full-width** (« pleine chasse ») : caractères japonais pleine largeur, **2 octets par
  caractère** en Shift-JIS. Espace de stockage séparé dans le TOC.

Les deux sont **indépendants** : un disque peut avoir un titre half-width et un titre full-width
différents. La plupart des logiciels stockent le vrai titre en half-width et une version
« décorative » en full-width.

#### 3.8.1 Titre disque (paginé)

La réponse maximale étant de 255 octets, la lecture du titre disque est **paginée** :

```
Descripteurs : audioContentsTD openRead, puis discTitleTD openRead
Query        : 00 1806 02201801 00%b 3000 0a00 ff00 %w%w
                                 ^wchar          ^remaining ^done
```

* `wchar` : `0x00` = half-width, `0x01` = full-width
* `remaining` : octets restants à lire (0 au premier appel)
* `done` : octets déjà lus (0 au premier appel)

Scan **premier chunk** (`remaining == 0`) :

```
09 1806 02201801 00%? 3000 0a00 1000 %w0000 %?%?000a %w %*
                                     ^chunkSize      ^total ^données
puis : chunkSize -= 6
```

Scan **chunks suivants** :

```
09 1806 02201801 00%? 3000 0a00 1000 %w%?%? %*
                                     ^chunkSize ^données
```

Boucle :

```c
size_t done = 0, total = 1, remaining = 0;
while (done < total) {
    /* envoyer la query avec (wchar, remaining, done) */
    /* parser -> chunk_size, [total au 1er tour], chunk_bytes */
    if (remaining == 0) chunk_size -= 6;       /* en-tête du premier chunk */
    append(chunk_bytes, chunk_size);
    done += chunk_size;
    remaining = total - done;
}
```

Le résultat est un buffer **Shift-JIS brut** — le décoder ensuite (§3.11).

#### 3.8.2 Titre de piste

```
Descripteur : audioUTOC1TD (half) ou audioUTOC4TD (full) openRead
Query       : 00 1806 022018%b %w 3000 0a00 ff00 00000000
                            ^wchar ^track
Scan        : 09 1806 022018%? %?%? %?%? %?%? 1000 00%?0000 00%?000a %x
Descripteur : close
```

* `wchar` : `0x02` = half-width, `0x03` = full-width
  (⚠️ **différent** des valeurs `0x00`/`0x01` du titre disque !)
* `track` : index **0-based**

Exemple hex, lecture du titre half-width de la piste 3 :

```
Ouvrir :   00 1808 10 1802 01 00
Query  :   00 1806 02201802 0003 3000 0a00 ff00 00000000
Réponse:   09 1806 02201802 0003 3000 0a00 1000 000e0000 000c000a 0006 "Hello!"
Fermer :   00 1808 10 1802 00 00
```

Une piste sans titre renvoie `REJECTED` → traiter comme chaîne vide (et `oldLen = 0` pour
l'écriture).

### 3.9 Titres : écriture

```
Descripteur disque : discTitleTD close, puis discTitleTD openWrite
Query disque       : 00 1807 02201801 00%b 3000 0a00 5000 %w 0000 %w %*
                                          ^wchar          ^newLen  ^oldLen ^SJIS

Descripteur piste  : audioUTOC1TD ou audioUTOC4TD openWrite
Query piste        : 00 1807 022018%b %w 3000 0a00 5000 %w 0000 %w %*
                                  ^wchar ^track       ^newLen ^oldLen ^SJIS
```

`newLen` et `oldLen` sont des **longueurs en octets après encodage Shift-JIS**, pas en caractères.
Fournir un `oldLen` faux corrompt le TOC sur certains appareils — d'où la nécessité de **lire le
titre courant avant de l'écrire**.

Points d'implémentation obligatoires :

1. **Ne jamais réécrire un titre identique.** Certains appareils (notamment la série LAM) se
   plantent si on écrit exactement la même valeur. Comparer avant d'écrire, et sortir sans rien
   faire si égal.
2. **Sharp (VID `0x04dd`)** : pour renommer le **disque**, ouvrir `audioUTOC1TD` en écriture
   (et non `discTitleTD`). Détecter par le VID.
3. Pour les non-Sharp, la séquence complète autour de l'écriture du titre disque est :
   `close(discTitleTD)` → `openWrite(discTitleTD)` → écriture → `close(discTitleTD)` →
   `openRead(discTitleTD)` → `close(discTitleTD)`. Ce double aller-retour force certains
   appareils à valider le cache TOC.

Exemple hex, mettre le titre half-width de la piste 0 à `"Track A"` (7 octets ASCII), l'ancien
titre faisant 3 octets :

```
Ouvrir :   00 1808 10 1802 03 00
Query  :   00 1807 02201802 0000 3000 0a00 5000 0007 0000 0003 54 72 61 63 6b 20 41
Réponse:   09 1807 02201802 0000 3000 0a00 5000 0007 0000 0003
Fermer :   00 1808 10 1802 00 00
```

### 3.10 Groupes : la syntaxe `//` et `;`

Le MiniDisc **n'a pas de notion native de groupes**. Les groupes sont une convention Sony encodée
**dans le titre du disque**. Aucun octet de TOC dédié.

Grammaire (half-width) :

```
disc_title_raw := [ "0;" <titre_disque> "//" ] { <range> ";" <nom_groupe> "//" }
range          := <n> | <n> "-" <m>          (indices 1-based, inclusifs)
```

Équivalent full-width — **tous les caractères de service sont aussi pleine chasse** :

| half | full | codepoint |
|---|---|---|
| `0`..`9` | `０`..`９` | U+FF10..U+FF19 |
| `-` | `－` | U+FF0D |
| `/` | `／` | U+FF0F |
| `;` | `；` | U+FF1B |

Exemples :

```
"0;Mon Album//1-4;Face A//5-9;Face B//"
  -> titre disque = "Mon Album"
  -> groupe "Face A" = pistes 1..4 (indices 0..3)
  -> groupe "Face B" = pistes 5..9 (indices 4..8)

"1;Intro//2-10;Corps//"
  -> pas de titre disque (pas de préfixe "0;")

"Simplement un titre"
  -> pas de "//" du tout : titre disque brut, aucun groupe
```

**Règles de parsing** (à reproduire fidèlement) :

* Découper le titre brut sur `"//"`.
* Ignorer les segments vides.
* Ignorer un segment commençant par `"0;"` (c'est le titre disque, pas un groupe).
* Ignorer un segment sans `';'`.
* Ignorer tout si le titre brut ne contient pas `"//"`.
* `trackMax` doit être **borné par `trackCount`** : les groupes ne sont pas remis à jour quand une
  piste est supprimée, donc un range peut pointer au-delà du nombre réel de pistes.
* Une piste ne peut appartenir qu'à **un seul** groupe → détecter les chevauchements comme une
  corruption.
* Les pistes n'appartenant à aucun groupe forment un pseudo-groupe « ungrouped » **en tête** de
  liste.

**Règle d'extraction du titre disque « propre »** : si le titre brut se termine par `"//"`, alors
prendre le premier segment ; s'il commence par `"0;"`, le titre est ce segment moins les 2 premiers
caractères ; sinon le titre disque est **vide**.

```c
/* Pseudo-code : extraction du titre disque à partir du titre brut */
const char *md_disc_title_from_raw(const char *raw, int wide)
{
    const char *delim  = wide ? "／／" : "//";
    const char *marker = wide ? "０；" : "0;";
    if (!ends_with(raw, delim)) return raw;          /* pas de groupes */
    char *first = split_first(raw, delim);
    if (starts_with(first, marker)) return first + strlen(marker);
    return "";                                        /* groupes sans titre disque */
}
```

**Écriture des groupes** : il n'existe pas de commande « créer un groupe ». On **recompile
entièrement** la chaîne de titre disque et on l'écrit avec `setDiscTitle` (half **et** full).
Cf. §7.4 pour la contrainte de budget.

### 3.11 Encodage des titres : Shift-JIS et assainissement

Le TOC stocke les titres en **Shift-JIS** (les caractères ASCII 0x20..0x7E sont identiques à
l'ASCII ; les katakana demi-chasse occupent 0xA1..0xDF ; le reste est sur 2 octets).

Pour une application C sans dépendance, deux options :

* **Option simple (recommandée v1)** : `MultiByteToWideChar` / `WideCharToMultiByte` avec la
  **codepage 932** (Shift-JIS Windows / CP932), fournie en standard par Windows.

```c
/* UTF-16 -> Shift-JIS (CP932) */
int md_utf16_to_sjis(const wchar_t *in, char *out, int out_cap)
{
    BOOL used_default = FALSE;
    int n = WideCharToMultiByte(932, 0, in, -1, out, out_cap, "?", &used_default);
    return used_default ? -1 : n - 1;   /* -1 : caractère non représentable */
}
```

* **Option autonome** : table de conversion embarquée (~ 100 Ko). Inutile sous Windows.

**Assainissement half-width** (indispensable, sinon TOC corrompu) :

1. Remapper toute la plage pleine chasse vers demi-chasse (`Ａ`→`A`, `０`→`0`, `　`→` `,
   `！`→`!`, etc.).
2. Remapper hiragana et katakana pleine chasse vers **katakana demi-chasse**
   (`ア`→`ｱ`, `カ`→`ｶ`, `ガ`→`ｶﾞ` …).
3. « Aplatir » les séquences dakuten/handakuten : un caractère suivi de U+3099/U+309B/U+FF9E
   (dakuten) devient le caractère + 1 dans la table kana ; U+309A/U+309C/U+FF9F (handakuten) →
   caractère + 2.
4. Retirer les diacritiques latins (`é` → `e`).
5. Tout caractère non représentable devient un **espace**.
6. **Vérification finale** : `strlen(sjis_encoded)` doit être égal à la longueur « comptée »
   du titre (où les kana à dakuten comptent pour 2). Sinon, repli agressif : supprimer tout
   caractère non-ASCII.

**Assainissement full-width** : opération inverse (remapper vers pleine chasse), plus
translittération cyrillique (`ж` → `zh`) et allemande (`ä` → `ae`, `ß` → `ss`). Vérification
finale : la longueur Shift-JIS doit valoir exactement `2 × nb_caractères` ; sinon repli agressif.

**Caractères comptant double en half-width** (ce sont les kana avec dakuten/handakuten, encodés
sur 2 octets demi-chasse) :

```
ガギグゲゴ ザジズゼゾ ダヂヅデド バパビピブプベペボポ ヮヰヱヵヶヴヽヾ
がぎぐげご ざじずぜぞ だぢづでど ばぱびぴぶぷべぺぼぽ ゎゐゑゕゖゔゝゞ
```

```c
/* Longueur "half-width" d'un titre : chaque kana dakuten/handakuten compte 2 */
size_t md_halfwidth_len(const wchar_t *title)
{
    size_t n = 0;
    for (const wchar_t *p = title; *p; ++p)
        n += md_is_dakuten_kana(*p) ? 2 : 1;
    return n;
}
```

### 3.12 Édition : effacer, déplacer

```
Effacer une piste  : 00 1840 ff01 00 201001 %w        (track, 0-based)
Effacer le disque  : 00 1840 ff 0000
Déplacer une piste : 00 1843 ff00 00 201001 %w 201001 %w    (source, destination)
```

Exemples :

```
Effacer la piste 5 (index 4) :   00 1840 ff01 00 201001 0004
Déplacer piste 0 -> position 3 : 00 1843 ff00 00 201001 0000 201001 0003
Effacer tout le disque         : 00 1840 ff 0000
```

⚠️ Ces trois commandes **modifient le TOC** et peuvent renvoyer INTERIM puis mettre plusieurs
secondes. Ne pas surenchérir en commandes pendant ce temps. Après un `eraseTrack`, **tous les
index de pistes supérieurs sont décalés** : recharger la liste, et si on efface plusieurs pistes,
les effacer **par index décroissant**.

⚠️ Les groupes encodés dans le titre disque ne sont **pas** mis à jour automatiquement : après une
suppression/déplacement, il faut recompiler et réécrire le titre disque.

### 3.13 Contrôle de lecture

```
play         : 00 18c3 ff 75 000000
pause        : 00 18c3 ff 7d 000000
fast forward : 00 18c3 ff 39 000000
rewind       : 00 18c3 ff 49 000000
réponse      : 09 18c3 00 %b 000000

stop         : 00 18c5 ff 00000000
réponse      : 09 18c5 00 00000000
```

⚠️ `stop` peut échouer sur les LAM-1 : envelopper dans un `try` et ignorer l'erreur.

Navigation :

```
Aller à la piste N   : 00 1850 ff010000 0000 %w             (N, 0-based)
  réponse            : 09 1850 00010000 0000 %w

Aller à un temps     : 00 1850 ff000000 0000 %w %B%B%B%B    (track, h, m, s, f en BCD)
  réponse            : 09 1850 00000000 %?%? %w %B%B%B%B

Changement de piste  : 00 1850 ff10 00000000 %w
  direction : 0x0002 = précédente, 0x8001 = suivante, 0x0001 = redémarrer la piste courante
  réponse            : 09 1850 0010 00000000 %?%?
```

Modes de lecture (bits combinables, via une commande `playmode` non détaillée ici) :

| Bit | Nom |
|---|---|
| `0x0040` | SINGLE |
| `0x0080` | REPEAT |
| `0x0100` | SHUFFLE |

### 3.14 État de fonctionnement et position

**Status court (présence disque)** :

```
Descripteur : operatingStatusBlock (80 00) openRead
Query       : 00 1809 8001 0230 8800 0030 8804 00 ff00 00000000
Scan        : 09 1809 8001 0230 8800 0030 8804 00 1000 00090000 %x
Descripteur : close
```

Dans le buffer renvoyé, `status[4]` :

| Valeur | Sens |
|---|---|
| `0x40` | disque présent |
| `0x80` | pas de disque |

**Status complet (état opératoire)** :

```
Query : 00 1809 8001 0330 8802 0030 8805 0030 8806 00 ff00 00000000
Scan  : 09 1809 8001 0330 8802 0030 8805 0030 8806 00 1000 00%?0000 00%b 8806 %x
                                                                    ^statusMode ^operatingStatus
```

`operatingStatus` = `(buf[0] << 8) | buf[1]`. Valeurs observées (décimal / hex) :

| Décimal | Hex | État |
|---|---|---|
| 50687 | `0xC5FF` | ready / stopped |
| 50037 | `0xC375` | playing |
| 50045 | `0xC37D` | paused |
| 49983 | `0xC33F` | fast forward |
| 49999 | `0xC34F` | rewind |
| 65315 | `0xFF23` | reading TOC |
| 65296 | `0xFF10` | no disc |
| 65535 | `0xFFFF` | disc blank |
| 65319 | `0xFF27` | **ready for transfer** |
| 22271 | `0x56FF` | USB recording (constante `libnetmd`) |
| 49781 | `0xC275` | recording |
| 49789 | `0xC27D` | recording paused |

> ⚠️ Cette commande **ne fonctionne pas sur tous les appareils** (bug documenté :
> webminidisc issue #21). Prévoir un repli sur le status court.

Variante « playback status » plus tolérante :

```
Query : 00 1809 8001 0330 %w 0030 8805 0030 %w 00 ff00 00000000
        p1 = 0x8801, p2 = 0x8807   -> playbackStatus1
        p1 = 0x8802, p2 = 0x8806   -> playbackStatus2
Scan  : 09 1809 8001 0330 %?%? %?%? %?%? %?%? %?%? %? 1000 00%?0000 %x %?
```

Le `%?` final est un correctif spécifique **MZ-RH1** (un octet de plus). Dans `playbackStatus2`,
`buf[4]` et `buf[5]` donnent `operatingStatus`.

**Position de lecture** :

```
Descripteur : operatingStatusBlock openRead
Query       : 00 1809 8001 0430 8802 0030 8805 0030 0003 0030 0002 00 ff00 00000000
Scan        : 09 1809 8001 0430 %?%? %?%? %?%? %?%? %?%? %?%? %?%? %? %?00 00%?0000
                 000b 0002 0007 00 %w %B %B %B %B
                                   ^track ^h ^m ^s ^f
Descripteur : close
```

Si la commande est `REJECTED` → retourner « pas de position » (appareil arrêté), ce n'est pas une
erreur fatale.

Note : la « minute » réelle affichée est `h*60 + m` (les baladeurs affichent `mm:ss`).

**Paramètres d'enregistrement courants** :

```
Query : 00 1809 8001 0330 8801 0030 8805 0030 8807 00 ff00 00000000
Scan  : 09 1809 8001 0330 8801 0030 8805 0030 8807 00 1000 000e0000
           000c 8805 0008 80e0 0110 %b %b 4000
                                    ^encoding ^channels
```

### 3.15 Éjection

```
Éjecter       : 00 18c1 ff 6000
Tester (SPEC) : 01 18c1 ff 6000        -> si REJECTED/NOT IMPLEMENTED, l'appareil n'éjecte pas
```

Sur un baladeur, l'« éjection » électrique n'existe pas toujours : la commande est souvent
`NOT IMPLEMENTED`. Sur une platine, elle ouvre le tiroir. Toujours tester avant d'exposer un
bouton « Éjecter » dans l'UI.

### 3.16 Récapitulatif des opcodes

| Opcode (2ᵉ/3ᵉ octets) | Nom | Sens |
|---|---|---|
| `18 06` | READ INFO BLOCK | lecture de descripteurs (titres, capacité, flags, infos piste) |
| `18 07` | WRITE INFO BLOCK | écriture de titres |
| `18 08` | OPEN/CLOSE DESCRIPTOR | ouverture/fermeture de descripteur |
| `18 09` | READ SUBUNIT INFO | identification, état opératoire, position |
| `18 40` | ERASE | effacer piste / disque |
| `18 43` | MOVE | déplacer une piste |
| `18 50` | SEARCH / GOTO | aller à piste ou temps |
| `18 c1` | MEDIUM CONTROL | éjection |
| `18 c3` | PLAY CONTROL | play/pause/ff/rew |
| `18 c5` | STOP | arrêt |
| `18 00 08 00 46 f0 03 01 03` | **SECURE** | famille sécurisée (download/upload/session) — cf. §4 |
| `ff 01 0c` / `ff 01 00` | acquire / release | verrouillage de l'appareil |
| `18 01`, `18 20`..`18 25` | **FACTORY** | via `bRequest = 0xFF` uniquement — cf. §5 |

---

## 4. Transfert sécurisé — download vers le disque

C'est la partie la plus complexe et la plus fragile. Tout le sous-système « download » de Sony
(baptisé **check-out / check-in** dans OpenMG) repose sur une famille de commandes préfixée par
une signature fixe de 9 octets, et sur une authentification mutuelle DES.

### 4.1 Format de trame « secure »

```
+--------+---------------------------------+--------+--------+---------------+
| status |  18 00 08 00 46 f0 03 01 03     |  cmd   |  0xFF  |    data       |
+--------+---------------------------------+--------+--------+---------------+
   1 o             9 octets fixes             1 o      1 o        n octets
```

* `status` = `0x00` (CONTROL) en émission.
* La signature `18 00 08 00 46 f0 03 01 03` est **invariante** (sauf `f0 03 01 04` pour la
  commande « enter HiMD mode »).
* `0xFF` est le « placeholder » de réponse.

En **réponse** :

```
+--------+---------------------------------+--------+--------+---------------+
| status |  18 00 08 00 46 f0 03 01 03     |  cmd   |  0x00  |    data       |
+--------+---------------------------------+--------+--------+---------------+
```

⚠️ **Exception unique** : la commande `0x12` (send key data) répond avec `0x01` et non `0x00` à la
place du placeholder. C'est explicite dans les deux implémentations de référence.

```c
static const uint8_t MD_SECURE_HDR[9] =
    { 0x18, 0x00, 0x08, 0x00, 0x46, 0xF0, 0x03, 0x01, 0x03 };

/* Construit : [status][hdr 9][cmd][0xFF][data...] */
size_t md_secure_build(uint8_t *out, uint8_t cmd, const uint8_t *data, size_t n)
{
    size_t k = 0;
    out[k++] = 0x00;                                   /* CONTROL */
    memcpy(out + k, MD_SECURE_HDR, 9); k += 9;
    out[k++] = cmd;
    out[k++] = 0xFF;
    if (n) { memcpy(out + k, data, n); k += n; }
    return k;
}
```

### 4.2 Table des commandes secure

| cmd | Nom | Rôle | Réponse attendue |
|---|---|---|---|
| `0x11` | **get leaf ID** | identifiant unique du « player » (8 octets) | ACCEPTED |
| `0x12` | **send key data (EKB)** | envoi de l'Enabling Key Block | ACCEPTED, placeholder `0x01` |
| `0x20` | **session key exchange** | échange de nonces host/device | ACCEPTED |
| `0x21` | **session key forget** | oubli de la clé de session | ACCEPTED |
| `0x22` | **setup download** | déclaration du content ID + KEK | ACCEPTED |
| `0x23` | get track UUID | récupère l'UUID d'une piste | ACCEPTED |
| `0x28` | **send track** | envoi audio (download) | **INTERIM** puis ACCEPTED |
| `0x2A` | terminate | termine la session de transfert | ACCEPTED |
| `0x2B` | set track protection | désactive la protection des nouvelles pistes | ACCEPTED |
| `0x30` | **recv track** | upload (MZ-RH1 uniquement) | INTERIM puis ACCEPTED |
| `0x40` | secure delete track | effacement « check-in » | ACCEPTED |
| `0x48` | **commit track** | valide la piste dans le TOC | ACCEPTED |
| `0x80` | **enter secure session** | | ACCEPTED |
| `0x81` | **leave secure session** | | ACCEPTED |
| `0x82` | enter HiMD mode | signature `f0 03 01 04` | ACCEPTED |

### 4.3 Vue d'ensemble de la séquence de download

```
  [pré-vol]
   1. attendre operatingStatus ∈ { ready, discBlank }
   2. sessionKeyForget (0x21)      -- ignorer l'erreur
   3. leaveSecureSession (0x81)    -- ignorer l'erreur
   4. acquire (ff 010c ...)
   5. setTrackProtection(1) (0x2B) -- ignorer l'erreur (échoue sur Sharp)

  [session]
   6. enterSecureSession (0x80)
   7. getLeafID (0x11)                      -> leafID (8 octets)
   8. choix de l'EKB selon leafID/VID/PID
   9. sendKeyData (0x12)  <- chaîne de clés + profondeur + EKB ID + signature
  10. hostNonce = 8 octets aléatoires
  11. sessionKeyExchange (0x20, hostNonce)  -> devNonce (8 octets)
  12. sessionKey = retailMAC(rootKey, hostNonce || devNonce)     [8 octets]

  [par piste]
  13. setupDownload (0x22)  <- DES-CBC( 01 01 01 01 || contentID(20) || KEK(8) , sessionKey )
  14. sendTrack (0x28)  <- wireformat, discformat, frames, totalBytes
        <- INTERIM
        -> paquets DES-CBC sur le BULK OUT (EP 0x02)
        <- ACCEPTED : trackNumber + blob chiffré (32 o) -> uuid + contentID
  15. setTrackTitle(trackNumber, titre)  [+ full-width]
  16. commitTrack (0x48, trackNumber, DES-ECB(0^8, sessionKey))

  [clôture]
  17. sessionKeyForget (0x21)
  18. leaveSecureSession (0x81)
  19. release (ff 0100 ...)
```

### 4.4 Leaf ID (`0x11`)

```
=>  00  18 00 08 00 46 f0 03 01 03  11 ff
<=  09  18 00 08 00 46 f0 03 01 03  11 00  01 00 00 21 cf 06 00 00
                                           ^^^^^^^^^^^^^^^^^^^^^^^ leafID, 8 octets BE
```

Le leaf ID identifie la « feuille » de l'appareil dans l'arbre de clés Sony. Il détermine quel EKB
peut être accepté.

**Cas particulier documenté** : un leaf ID à `FF FF FF FF FF FF FF FF` sur une platine
`VID=0x054c, PID=0x0081` (MDS-JB980 / JE780 / NT1) signale une **EEPROM corrompue** — l'appareil
a perdu ses clés. Il existe un EKB de contournement dédié (cf. §4.5).

### 4.5 EKB (Enabling Key Block) — commande `0x12`

L'EKB est le mécanisme de révocation/dérivation de clés de Sony. En pratique, la communauté
utilise un **EKB « open source »** connu qui fonctionne sur tous les appareils NetMD non révoqués.

**EKB standard (« EKBOpenSource ») :**

| Champ | Valeur |
|---|---|
| EKB ID | `0x26422642` |
| Profondeur (`depth`) | `9` |
| Longueur de chaîne | `2` clés de 16 octets |
| Clé 1 | `25 45 06 4d ea ca 14 f9 96 bd c8 a4 06 c2 2b 81` |
| Clé 2 | `fb 60 bd dd 0d bc ab 84 8a 00 5e 03 19 4d 3e da` |
| Signature (24 o) | `8f 2b c3 52 e8 6c 5e d3 06 dc ae 18 d2 f3 8c 7f 89 b5 e1 85 55 a1 05 ea` |
| **Root key** (16 o) | `12 34 56 78 9a bc de f0 0f ed cb a9 87 65 43 21` |

**EKB de secours pour platine à EEPROM corrompue (leafID = FF×8, VID 054c / PID 0081) :**

| Champ | Valeur |
|---|---|
| EKB ID | `0x13371337` |
| Profondeur | `9` |
| Chaîne | 1 clé : `b1 d4 af fa 80 a0 c9 03 c2 58 4b 1b 44 af c4 a6` |
| Signature | `6c 2b c2 8c 45 2b 54 f1 c3 59 72 3b e3 19 1f 55 17 25 64 0e 65 8c 81 0b` |
| Root key | `57 4d 44 50 57 4d 44 50 4d 69 6e 69 44 69 73 63` (ASCII `WMDPWMDPMiniDisc`) |

**Sélection de l'EKB** :

```c
const md_ekb *md_select_ekb(const uint8_t leaf[8], uint16_t vid, uint16_t pid)
{
    int all_ff = 1;
    for (int i = 0; i < 8; ++i) if (leaf[i] != 0xFF) { all_ff = 0; break; }
    if (all_ff && vid == 0x054C && pid == 0x0081) return &MD_EKB_CORRUPTED_DECK;
    return &MD_EKB_OPEN_SOURCE;   /* défaut : fonctionne partout ailleurs */
}
```

**Payload de la commande `0x12`** :

```
databytes = 16 + 16 * chainLength + 24
          = 16 + 32 + 24 = 72 = 0x48   (pour l'EKB standard à 2 clés)
```

Trame :

```
00 18 00 08 00 46 f0 03 01 03 12 ff
   %w        <- databytes           (0x0048)
   0000
   %w        <- databytes           (0x0048)
   %d        <- chainLength         (0x00000002)
   %d        <- depth               (0x00000009)
   %d        <- ekbID               (0x26422642)
   00000000
   %*        <- keychain concaténée (chainLength × 16 octets)
   %*        <- signature (24 octets)
```

Hex complet pour l'EKB standard :

```
00 18 00 08 00 46 f0 03 01 03 12 ff
00 48 00 00 00 48
00 00 00 02
00 00 00 09
26 42 26 42
00 00 00 00
25 45 06 4d ea ca 14 f9 96 bd c8 a4 06 c2 2b 81
fb 60 bd dd 0d bc ab 84 8a 00 5e 03 19 4d 3e da
8f 2b c3 52 e8 6c 5e d3 06 dc ae 18 d2 f3 8c 7f 89 b5 e1 85 55 a1 05 ea
```

Réponse attendue :

```
09 18 00 08 00 46 f0 03 01 03 12 01  00 48  00 00  00 48  00 00
                                 ^^ placeholder 0x01, pas 0x00 !
```

> ⚠️ Le champ « databytes » vaut, dans la formulation `libnetmd`, `(taille_totale_du_buffer - 6)`.
> Avec 22 octets d'en-tête + 32 de clés + 24 de signature = 78, `78 - 6 = 72 = 0x48`. Les deux
> formulations coïncident. Vérifier la contrainte : **chaque clé fait exactement 16 octets**,
> **la signature exactement 24 octets**, **`1 ≤ depth ≤ 63`**.

### 4.6 Échange de nonces et dérivation de la clé de session

**Commande `0x20`** :

```
=>  00 18 00 08 00 46 f0 03 01 03 20 ff 00 00 00  <hostNonce 8 octets>
<=  09 18 00 08 00 46 f0 03 01 03 20 00 00 00 00  <devNonce  8 octets>
```

`hostNonce` : 8 octets **cryptographiquement aléatoires** (sous Windows :
`BCryptGenRandom(NULL, buf, 8, BCRYPT_USE_SYSTEM_PREFERRED_RNG)`).

> ⚠️ **Panasonic SJ-MR270** renvoie autre chose que `0x00` à la place du placeholder → accepter
> n'importe quelle valeur à cet offset (scanner `20 %?` et non `20 00`).

**Dérivation de la clé de session : « retail MAC »**

C'est un **MAC ISO 9797-1 Algorithme 3** (aussi appelé « Retail MAC » ou « ANSI X9.19 MAC ») :
DES-CBC sur tous les blocs sauf le dernier, puis 3DES-CBC (EDE, 2 clés) sur le dernier bloc, avec
comme IV le dernier bloc de chiffré de l'étape 1.

Entrées :
* `key` = **root key de l'EKB**, 16 octets (`K1 || K2`).
* `value` = `hostNonce || devNonce`, 16 octets.
* `iv` = 8 zéros.

Algorithme :

```
subkeyA   = key[0..7]                       (= K1)
beginning = value[0 .. len-9]               (16 - 8 = 8 octets, soit hostNonce)
end       = value[len-8 .. len-1]           (8 octets, soit devNonce)

step1  = DES-CBC-Encrypt(beginning, key=subkeyA, iv=iv)
iv2    = dernier bloc de 8 octets de step1
step2  = 3DES-CBC-Encrypt(end, key=key(16 o, EDE2), iv=iv2)

sessionKey = premiers 8 octets de step2      -> 8 octets
```

Pseudo-code C (avec une implémentation DES quelconque) :

```c
/* sessionKey[8] <- retail_mac(rootKey[16], nonce[16]) */
void md_retail_mac(const uint8_t root[16], const uint8_t nonce[16], uint8_t out[8])
{
    uint8_t iv[8] = {0}, tmp[8], k1[8], k2[8];
    memcpy(k1, root,     8);
    memcpy(k2, root + 8, 8);

    /* Étape 1 : DES-CBC sur le premier bloc (hostNonce) avec K1 */
    for (int i = 0; i < 8; ++i) tmp[i] = nonce[i] ^ iv[i];
    des_encrypt_block(k1, tmp, tmp);        /* tmp = C1 */

    /* Étape 2 : 3DES-EDE-CBC sur le second bloc (devNonce), IV = C1 */
    uint8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = nonce[8 + i] ^ tmp[i];
    des_encrypt_block(k1, b, b);            /* E_K1 */
    des_decrypt_block(k2, b, b);            /* D_K2 */
    des_encrypt_block(k1, b, b);            /* E_K1 */
    memcpy(out, b, 8);
}
```

> Note : parce que `value` fait exactement 16 octets, l'étape « DES-CBC sur beginning » se réduit à
> **un seul chiffrement DES d'un seul bloc**. L'implémentation générique reste préférable si on
> veut coller à la formulation d'origine.

**Vérification recommandée** : ajouter un test unitaire avec un vecteur figé
(`rootKey` standard, `hostNonce = 00..07`, `devNonce = 08..0F`) et comparer avec la valeur produite
par une implémentation de référence lancée une fois hors ligne. Une erreur ici produit un
`REJECTED` opaque au `setupDownload`.

**Commande `0x21` (forget)** :

```
=>  00 18 00 08 00 46 f0 03 01 03 21 ff 00 00 00
<=  09 18 00 08 00 46 f0 03 01 03 21 00 00 00 00
```

### 4.7 setupDownload (`0x22`)

Déclare le contenu à transférer. Le payload est un bloc de **32 octets chiffré en DES-CBC avec la
clé de session** (IV = 8 zéros, pas de padding puisque 32 est multiple de 8) :

```
Plaintext (32 octets) :
  +0   01 01 01 01                (4 octets constants)
  +4   contentID                  (20 octets)
  +24  keyEncryptionKey (KEK)     (8 octets)
```

Constantes utilisées par la communauté (identiques dans toutes les implémentations) :

```
contentID (20 o) : 01 0f 50 00 00 04 00 00 00 48 a2 8d 3e 1a 3b 0c 44 af 2f a0
KEK       (8 o)  : 14 e3 83 4e e2 d3 cc a5
```

Trame :

```
=>  00 18 00 08 00 46 f0 03 01 03 22 ff 00 00 <32 octets chiffrés>
<=  09 18 00 08 00 46 f0 03 01 03 22 00 00 00
```

```c
void md_setup_download(md_dev *d, const uint8_t session_key[8])
{
    uint8_t plain[32], cipher[32], iv[8] = {0};
    plain[0] = plain[1] = plain[2] = plain[3] = 0x01;
    memcpy(plain + 4,  MD_CONTENT_ID, 20);
    memcpy(plain + 24, MD_KEK,         8);
    des_cbc_encrypt(session_key, iv, plain, cipher, 32);

    uint8_t data[2 + 32];
    data[0] = 0x00; data[1] = 0x00;
    memcpy(data + 2, cipher, 32);
    md_secure_exchange(d, 0x22, data, sizeof data, /*expect*/MD_ST_ACCEPTED);
}
```

### 4.8 Wireformats, discformats et tailles de frame

**Wireformat** = format des données transmises sur le fil.

| Wireformat | Valeur | Taille de frame (octets) | Contenu |
|---|---|---|---|
| PCM | `0x00` | **2048** | PCM 16 bits **big-endian** stéréo 44,1 kHz |
| LP2 | `0x94` | **192** | ATRAC3 132 kbit/s |
| 105 kbps | `0x90` | **152** | ATRAC3 105 kbit/s (rarement utilisé) |
| LP4 | `0xA8` | **96** | ATRAC3 66 kbit/s joint-stereo |

**Discformat** = mode d'écriture sur le disque.

| Discformat | Valeur |
|---|---|
| LP4 | `0` |
| LP2 | `2` |
| SP mono | `4` |
| SP stéréo | `6` |

**Correspondance wireformat → discformat par défaut** :

| Wireformat | Discformat |
|---|---|
| `0x00` (PCM) | `6` (SP stéréo) |
| `0x94` (LP2) | `2` (LP2) |
| `0x90` (105 kbps) | `2` (LP2) |
| `0xA8` (LP4) | `0` (LP4) |

**En mono**, la taille de frame effective est **divisée par 2** (`frameSize /= 2` quand
`channels == MONO`). Cela ne concerne que les appareils avec `nativeMonoUpload` (Sharp IM-DR80,
Panasonic SJ-MR250/270, platines Sony 0x0081) — hors périmètre v1 sur MZ-N505.

### 4.9 SP vs LP : où se fait l'encodage

C'est **le** point d'architecture qui détermine tout le pipeline audio.

**Mode SP (ATRAC1)** :
* L'hôte envoie du **PCM brut**, 44 100 Hz, 16 bits, stéréo, **big-endian** (!).
* **C'est l'appareil qui encode en ATRAC1 en temps réel.**
* Conséquence : le débit du transfert est **borné par la vitesse d'encodage de l'appareil**,
  typiquement **~1,0 à 1,5× le temps réel** (une piste de 4 minutes prend 3 à 4 minutes).
* Conséquence : le volume transféré est énorme (PCM 44,1/16/2 = **10,58 Mo par minute**).
* ⚠️ **Big-endian** : sur x86, il faut **byte-swapper chaque échantillon 16 bits** après
  décodage du WAV (qui est little-endian).

```c
/* WAV PCM LE -> flux NetMD BE, sur place */
static void md_pcm_le_to_be(uint8_t *p, size_t n) /* n multiple de 2 */
{
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint8_t t = p[i]; p[i] = p[i + 1]; p[i + 1] = t;
    }
}
```

**Modes LP2 / LP4 (ATRAC3)** :
* L'hôte envoie de l'**ATRAC3 déjà encodé** (frames de 192 ou 96 octets).
* L'appareil ne fait que dé-multiplexer et écrire. Le transfert est **beaucoup plus rapide**
  (typiquement **3 à 8× le temps réel**).
* **Il faut donc un encodeur ATRAC3 côté hôte.** Options :
  * `atracdenc` (encodeur ATRAC1/ATRAC3 open source, C++, licence à vérifier — cf. §10) ;
  * `ffmpeg` (`libavcodec` a un **décodeur** ATRAC3 mais pas d'encodeur ATRAC3 utilisable) ;
  * l'ancien encodeur Sony (non redistribuable).
* Le fichier source typique est un **WAV avec `wFormatTag = 0x0270` (ATRAC3)** ; l'en-tête
  contient `nBlockAlign = bytesPerFrame` et une extension de 14 octets avec le flag joint-stereo.

En-tête WAV ATRAC3 attendu (60 octets, tout little-endian) :

```
"RIFF"                             4
<taille = données + 60>            4
"WAVE"                             4
"fmt "                             4
<32>                               4   (taille du chunk fmt)
<0x0270>                           2   wFormatTag = ATRAC3
<2>                                2   nChannels
<44100>                            4   nSamplesPerSec
<bytesPerFrame * 44100 / 512>      4   nAvgBytesPerSec
<bytesPerFrame * 2>                2   nBlockAlign
<0>                                2   wBitsPerSample
<14>                               2   cbSize (extension)
<1>                                2
<bytesPerFrame>                    4   (192 pour LP2, 96 pour LP4)
<jointStereo>                      2   (0 pour LP2, 1 pour LP4)
<jointStereo>                      2
<1>                                2
<0>                                2
"data"                             4
<taille des données>               4
```

**Recommandation v1** : implémenter **SP uniquement** (PCM → l'appareil encode). Cela évite
totalement la dépendance à un encodeur ATRAC3 et son problème de licence. Ajouter LP2/LP4 en v2
en acceptant en entrée des fichiers **déjà** en ATRAC3 (WAV `0x0270` ou `.at3`).

### 4.10 Chiffrement des paquets audio (DES-CBC)

L'audio n'est **jamais** envoyé en clair. Il est découpé en **paquets**, chacun chiffré en
**DES-CBC**.

**Génération de la clé de données** (contre-intuitive : c'est un **déchiffrement**) :

```
rawKey = 8 octets aléatoires
dataKey = DES-ECB-Decrypt(rawKey, key = KEK)
```

* `rawKey` est la clé **effectivement utilisée** pour chiffrer les données.
* `dataKey` est ce qu'on **transmet** à l'appareil dans le premier paquet.
* L'appareil, connaissant la KEK (transmise chiffrée dans `setupDownload`), retrouve
  `rawKey = DES-ECB-Encrypt(dataKey, KEK)`.

```c
void md_make_data_key(const uint8_t kek[8], uint8_t raw_key[8], uint8_t sent_key[8])
{
    BCryptGenRandom(NULL, raw_key, 8, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    des_decrypt_block(kek, raw_key, sent_key);   /* DES-ECB DECRYPT, oui */
}
```

**Découpage en paquets** :

* Taille de chunk par défaut : `0x00100000` (1 Mio) dans `netmd-js`,
  `0x00800000` (8 Mio) dans `libnetmd`. Les deux fonctionnent ; **1 Mio est plus sûr** et donne
  une meilleure granularité de barre de progression.
* Le **premier paquet** transporte 24 octets d'en-tête en plus → sa charge utile est
  `chunkSize - 24`.
* Les paquets suivants font `chunkSize` octets de charge utile.
* Le **chaînage CBC est continu d'un paquet à l'autre** : l'IV du paquet N+1 est le **dernier bloc
  de 8 octets du chiffré du paquet N**.
* L'IV du **premier** paquet est soit 8 zéros (`netmd-js`), soit un nonce aléatoire (`libnetmd`).
  Les deux fonctionnent, puisque l'IV est **transmis en clair** dans l'en-tête du premier paquet.
  **Choisir 8 zéros** pour la simplicité et la reproductibilité des tests.

**Padding** : les données doivent être un multiple de `frameSize`. Compléter par des zéros.
Comme `frameSize` (2048, 192, 152, 96) est toujours multiple de 8, le padding sur frame satisfait
automatiquement le padding DES.

**En-tête du premier paquet** (24 octets, envoyé sur le BULK OUT juste avant les données) :

```
+0   00 00 00 00                 4 octets à zéro (moitié haute d'un quadword)
+4   <totalPacketBytes>           4 octets BIG-ENDIAN : taille totale de toutes les données
+8   <dataKey>                    8 octets  (la clé déchiffrée par la KEK)
+16  <iv>                         8 octets  (IV du premier bloc CBC)
+24  <données chiffrées ...>
```

Autrement dit, les 8 premiers octets forment un **quadword big-endian** dont la valeur est la
longueur totale des données chiffrées (hors ces 24 octets d'en-tête).

Les paquets suivants ne contiennent **que** des données chiffrées, sans en-tête.

```c
/* Pseudo-code : itérateur de paquets */
typedef struct {
    const uint8_t *src; size_t src_len;   /* données paddées à frameSize */
    size_t offset; size_t chunk;          /* chunk = 0x100000 */
    uint8_t raw_key[8], iv[8];
    int first;
} md_packetizer;

int md_next_packet(md_packetizer *p, uint8_t *out, size_t *out_len)
{
    if (p->offset >= p->src_len) return 0;
    size_t want = p->first ? (p->chunk - 24) : p->chunk;
    if (want > p->src_len - p->offset) want = p->src_len - p->offset;

    des_cbc_encrypt(p->raw_key, p->iv, p->src + p->offset, out, want);
    memcpy(p->iv, out + want - 8, 8);      /* chaînage CBC inter-paquets */

    p->offset += want; p->first = 0;
    *out_len = want;
    return 1;
}
```

⚠️ Piège classique : certaines bibliothèques DES ajoutent **8 octets de padding PKCS#7** en sortie.
Il faut **tronquer** le chiffré à la longueur exacte de l'entrée. Les deux implémentations de
référence font explicitement ce `subarray(0, chunkSize)`.

### 4.11 sendTrack (`0x28`)

**Payload de la commande** :

```
00 18 00 08 00 46 f0 03 01 03 28 ff
   00 01 00        (3 octets constants)
   10 01           (descripteur de piste)
   ff ff           (numéro de piste = "n'importe lequel", l'appareil décide)
   00
   %b              wireformat  (0x00 / 0x90 / 0x94 / 0xA8)
   %b              discformat  (0 / 2 / 4 / 6)
   %d              frames      (nombre de frames, 4 octets BE)
   %d              totalBytes  (4 octets BE)
```

`totalBytes` = `frameSize × frames + 24` (les 24 octets d'en-tête du premier paquet inclus).

Exemple : piste SP stéréo de 3 min 30 s.

```
octets PCM      = 3.5 min × 60 × 44100 × 2 canaux × 2 octets = 37 044 000
frames          = 37 044 000 / 2048 = 18 088,86 -> arrondi sup. -> 18 089 frames
octets paddés   = 18 089 × 2048 = 37 046 272
totalBytes      = 37 046 272 + 24 = 37 046 296 = 0x0234 3898

Trame :
00 18 00 08 00 46 f0 03 01 03 28 ff
00 01 00 10 01 ff ff 00
00                       <- wireformat PCM
06                       <- discformat SP stéréo
00 00 46 A9              <- frames = 18089
02 34 38 98              <- totalBytes
```

**Réponse INTERIM** (autorise le transfert bulk) :

```
0f 18 00 08 00 46 f0 03 01 03 28 00 00 01 00 10 01 %?%? 00 ...
```

Puis **le transfert bulk sur EP `0x02`** : premier paquet (24 o d'en-tête + données), puis les
suivants.

Puis la **réponse finale ACCEPTED** (lue via poll + `0x81`) :

```
09 18 00 08 00 46 f0 03 01 03 28 00 00 01 00 10 01
   %w         <- numéro de piste attribué (0-based)
   00
   %?%?  %?%?%?%?  %?%?%?%?
   %*         <- blob chiffré de 32 octets
```

Le blob de 32 octets se déchiffre en **DES-CBC avec la clé de session**, IV = 8 zéros :

```
+0   uuid            8 octets
+8   (4 octets)      réservé
+12  contentID       20 octets
```

Ces valeurs servent au « check-in » (restitution du droit d'usage) — inutiles si on ne fait pas de
DRM côté hôte, mais il faut quand même lire et déchiffrer la réponse pour rester synchronisé.

**Temporisation** : les deux implémentations insèrent **200 ms de pause avant et après** la
commande `0x28` (« the Sharps are slow »). À conserver — c'est gratuit et évite des échecs
intermittents.

**Séquence exacte à la fin du transfert** :

1. Envoyer le dernier paquet bulk.
2. Lire la réponse (poll → `0x81`) → ACCEPTED avec le numéro de piste.
3. **Refaire un poll** (`netmd-js` appelle `getReplyLength()` une fois de plus). Ce poll
   supplémentaire consomme un état résiduel ; l'omettre désynchronise l'appareil sur certains
   modèles.

```c
/* Pseudo-code du download d'une piste */
int md_download_track(md_dev *d, const uint8_t *pcm_be, size_t pcm_len,
                      uint8_t wireformat, uint8_t discformat,
                      const uint8_t session_key[8], const wchar_t *title,
                      uint16_t *out_track)
{
    const size_t frame = md_frame_size(wireformat);
    size_t padded = ((pcm_len + frame - 1) / frame) * frame;
    uint32_t frames = (uint32_t)(padded / frame);
    uint32_t total  = (uint32_t)(padded + 24);

    md_setup_download(d, session_key);

    uint8_t cmd[13 + 5];
    /* ... 00 01 00 10 01 ff ff 00 wf df frames total ... */
    md_sleep(200);
    md_secure_send(d, 0x28, cmd, sizeof cmd);
    if (md_secure_recv(d, 0x28, &rsp, MD_ST_INTERIM) != MD_OK) return MD_ERR;
    md_sleep(200);

    md_packetizer p; md_packetizer_init(&p, pcm_be, padded, kek);
    uint8_t hdr[24]; int first = 1;
    for (;;) {
        size_t n; if (!md_next_packet(&p, chunkbuf, &n)) break;
        if (first) {
            memset(hdr, 0, 4);
            md_put_u32_be(hdr + 4, (uint32_t)padded);
            memcpy(hdr + 8,  p.sent_key, 8);
            memcpy(hdr + 16, p.iv0,      8);
            md_bulk_write(d, 0x02, hdr, 24);
            first = 0;
        }
        md_bulk_write(d, 0x02, chunkbuf, n);
        md_report_progress(...);
    }

    if (md_secure_recv(d, 0x28, &rsp, MD_ST_ACCEPTED) != MD_OK) return MD_ERR;
    *out_track = md_get_u16_be(rsp.data + 5);
    md_poll_once(d);                       /* poll de clôture obligatoire */
    return MD_OK;
}
```

> Note d'optimisation : `netmd-js` concatène l'en-tête de 24 octets et le premier chunk **en un
> seul** `writeBulk`. `libnetmd` fait pareil (`packet_size = 8 + 8 + 8 + length`). Il est plus
> propre de faire un seul write, mais deux writes consécutifs fonctionnent aussi (le device voit
> un flux d'octets).

### 4.12 Titre puis commit

**Ordre impératif** : titrer **avant** de commiter.

```
1. setTrackTitle(trackNumber, titre_half_width)          -> §3.9
2. si titre full-width : setTrackTitle(trackNumber, titre_full_width, wchar=1)
3. commitTrack(trackNumber, sessionKey)
```

Si on commite d'abord et qu'on titre ensuite, sur certains appareils le TOC est écrit deux fois
(deux cycles d'écriture supplémentaires, usure et lenteur), et sur d'autres le titre est perdu.

**commitTrack (`0x48`)** :

```
authentication = DES-ECB-Encrypt( 00 00 00 00 00 00 00 00 , key = sessionKey )   -> 8 octets

=>  00 18 00 08 00 46 f0 03 01 03 48 ff 00 10 01 %w %*
                                                 ^track ^authentication (8 o)
<=  09 18 00 08 00 46 f0 03 01 03 48 00 00 10 01 %?%?
```

Exemple : commit de la piste 0 avec une clé de session fictive donnant
`authentication = a1 b2 c3 d4 e5 f6 07 18` :

```
00 18 00 08 00 46 f0 03 01 03 48 ff 00 10 01 00 00 a1 b2 c3 d4 e5 f6 07 18
```

### 4.13 Désactivation de la protection des nouvelles pistes (`0x2B`)

Par défaut, une piste transférée est marquée **protégée** (flag `0x03`, cf. §3.7.4) : elle est
« checked out » et l'appareil refuse de l'effacer sans check-in. La commande `0x2B` désactive ce
comportement.

```
=>  00 18 00 08 00 46 f0 03 01 03 2b ff %w         (val = 0x0001 pour désactiver)
<=  09 18 00 08 00 46 f0 03 01 03 2b 00 %?%?
```

À appeler **avant** `enterSecureSession`, dans la phase de pré-vol. **Échoue sur Sharp** →
envelopper et ignorer l'erreur.

### 4.14 Nettoyage et robustesse

```c
/* Pattern de clôture : toujours exécuté, même en cas d'erreur */
cleanup:
    (void)md_secure_simple(d, 0x21);   /* sessionKeyForget  — ignorer l'erreur */
    (void)md_secure_simple(d, 0x81);   /* leaveSecureSession — ignorer l'erreur */
    (void)md_release(d);               /* ff 0100 ...        — ignorer l'erreur */
```

Et symétriquement, **au démarrage d'un download**, faire un `0x21` + `0x81` préventifs et ignorer
leurs erreurs : cela nettoie une session laissée ouverte par un crash précédent. Sans ça,
`enterSecureSession` renvoie `REJECTED` et l'utilisateur doit débrancher l'appareil.

**Attente de disponibilité** : avant `setupDownload`, boucler tant que l'état opératoire n'est pas
`ready` (`0xC5FF`) ou `discBlank` (`0xFFFF`), avec une pause de 200 ms. C'est le correctif documenté
au « setupDownload envoyé prématurément → REJECTED ».

### 4.15 Upload (piste → PC) : commande `0x30`

Pour mémoire, car **inaccessible sur MZ-N505** (réservé au MZ-RH1 / M200) :

```
=>  00 18 00 08 00 46 f0 03 01 03 30 ff 00 10 01 %w        (track + 1, 1-based !)
<=  0f 18 00 08 00 46 f0 03 01 03 30 00 00 00 10 01 %w %b %d
                                                    ^frames ^codec ^length
    -> lecture bulk sur EP 0x01, `length` octets, par chunks de 0x10000
<=  09 18 00 08 00 46 f0 03 01 03 30 00 00 10 01 %?%? %?%?
```

* `codec & 0x06` donne le discformat (0 = LP4, 2 = LP2, 4 = SP mono, 6 = SP stéréo).
* Pour SP → écrire un en-tête **AEA** de 2048 octets ; le nombre de « soundgroups » vaut
  `taille / 212`.
* Pour LP2/LP4 → écrire un en-tête **WAV ATRAC3** (§4.9).
* ⚠️ Le numéro de piste est **1-based** ici, alors qu'il est 0-based partout ailleurs.
* Après la lecture, `netmd-js` attend **500 ms** avant de continuer.

En-tête AEA (2048 octets, little-endian) :

```
+0     magic         4 octets  = 2048
+4     name          256 octets (titre, NUL-padded)
+260   soundgroups   4 octets
+264   channels      1 octet (1 = mono, 2 = stéréo)
+265   flags         1 octet
+266.. flags[0..7]   8 × 4 octets
+298   0             4 octets
+302   encrypted     4 octets
+306   groupstart    4 octets
... padding jusqu'à 2048
```

---

## 5. netmd-exploits / mode factory

### 5.1 Le jeu de commandes « factory »

Découvert par la communauté (asivery, Sir68k), c'est un **second jeu de commandes complet**,
accessible en changeant simplement le `bRequest` du control transfer :

| | Commande standard | Commande factory |
|---|---|---|
| Envoi | `bRequest = 0x80` | `bRequest = 0xFF` |
| Lecture réponse | `bRequest = 0x81` | `bRequest = 0xFF` |
| Poll | `bRequest = 0x01` (identique) | `bRequest = 0x01` — mais `poll[1]` vaut `0xFF` |

Le poll fonctionne à l'identique : c'est `poll[1]` qui indique quel `bRequest` utiliser pour lire.
D'où la robustesse de l'implémentation « utiliser `poll[1]` » de `libnetmd` : elle gère les deux
cas gratuitement.

**Authentification factory** (préalable à toute commande factory) :

```
=>  00 1801 ff 0e 4e 65 74 20 4d 44 20 57 61 6c 6b 6d 61 6e
                  ^^^^^^^^ ASCII "Net MD Walkman" (14 = 0x0e octets)
```

C'est une simple « poignée de main » : envoyer la chaîne littérale `"Net MD Walkman"` précédée de
sa longueur.

**Commandes factory principales** :

| Opcode | Rôle | Format |
|---|---|---|
| `1801` | auth | `1801 ff %z "Net MD Walkman"` |
| `1820` | change memory state | `1820 ff %b %<d %b %b %b` (type, addr LE, len, state, encrypted) |
| `1821` | read memory | `1821 ff %b %<d %b` (type, addr LE, len) |
| `1822` | write memory | `1822 ff %b %<d %b 0000 %* %<w` (type, addr, len, data, checksum) |
| `1824` | read metadata peripheral | `1824 ff %<w %<w %b` (secteur, offset, longueur) |
| `1825` | write metadata peripheral | `1825 ff %<w %<w %z` (secteur, offset, data) |

Types de mémoire : `0x0` = MAPPED (RAM/registres), `0x2` = EEPROM_2, `0x3` = EEPROM_3.
États d'ouverture : `0x0` CLOSE, `0x1` READ, `0x2` WRITE, `0x3` READ_WRITE.

⚠️ **Noter les adresses en LITTLE-endian** (`%<d`) dans le jeu factory, alors que tout le protocole
standard est en big-endian. Erreur classique.

Le checksum utilisé par `1822` est un **CRC-16/CCITT variante** avec seed 0 (ou `0xA596` pour
Hi-MD) et polynôme `0x1021`, alimenté par la longueur du buffer puis par les données :

```c
uint16_t md_factory_crc(const uint8_t *data, size_t n, int as16bit, uint16_t seed)
{
    uint32_t crc = seed;
    size_t count = as16bit ? n / 2 : n;
    for (size_t i = 0; i < count; ++i) {
        uint32_t e = as16bit ? (uint32_t)((data[2*i+1] << 8) | data[2*i]) : data[i];
        uint32_t temp = ((uint32_t)count & 0xFFFF0000u) | e;
        crc ^= temp;
        for (int b = 0; b < 16; ++b) {
            uint32_t ts = crc & 0x8000u;
            crc <<= 1;
            if (ts) crc ^= 0x1021u;
        }
    }
    return (uint16_t)(crc & 0xFFFFu);
}
```

### 5.2 Ce que permettent les exploits

`netmd-exploits` (asivery) empile sur ce jeu factory de l'**injection de code ARM/assembleur** dans
la RAM de l'appareil. La bibliothèque contient un assembleur embarqué et une table de compatibilité
par version de firmware.

Nomenclature des versions : `<lettre_SoC><version>` :

| Lettre | SoC | Génération |
|---|---|---|
| `R` | CXD2677 | Type-R |
| `S` | CXD2678 / CXD2680 | Type-S |
| `Hn` | CXD2681 gen1 | Hi-MD |
| `Hr` | CXD2681 gen2 | Hi-MD |
| `Hx` | CXD2687 | Hi-MD (MZ-RH1) |

Exploits disponibles et compatibilité **pour un MZ-N505 (R1.300 / R1.400)** :

| Exploit | Rôle | MZ-N505 ? |
|---|---|---|
| `FirmwareDumper` | dump du firmware | ✅ oui (toutes versions) |
| `USBCodeExecution` | exécution de code arbitraire | ✅ oui (R1.000–R1.400) |
| `ForcedTOCEdit` | force le flush du TOC en RAM vers le disque | ✅ oui |
| `MonoSPUpload` | upload de pistes SP mono | ✅ oui |
| `CachedSectorNoRAMDownload` | upload ATRAC via secteurs cachés (sans RAM) | ✅ oui |
| `KillEepromWrite` | verrouille les écritures EEPROM (préserve l'usure) | ✅ oui |
| `WaitForDiscToStopSpinning` | notification d'arrêt de rotation | ⚠️ dégradé (attente fixe de 10 s sur Type-R) |
| `CachedSectorControlDownload` | upload ATRAC par control transfers | ❌ **désactivé sur Type-R** (met l'appareil dans un état instable) |
| `SPUpload` (ATRAC1) | upload SP stéréo | ❌ Type-S uniquement |
| `PCMFasterUpload` | accélération de l'upload SP | ❌ Type-S uniquement |
| `Tetris` | jeu embarqué (démonstration) | ❌ Type-S uniquement |
| `EnterServiceMode` | mode service | ❌ Type-S uniquement |
| `HiMDUnboundedReading`, `HiMDUSBClassOverride` | — | ❌ Hi-MD uniquement |

### 5.3 Périmètre v1 recommandé

**HORS scope v1** :
* Toute injection de code (`USBCodeExecution` et tout ce qui en dépend).
* L'upload de pistes (`CachedSectorNoRAMDownload`, `SPUpload`, `MonoSPUpload`).
* Le dump de firmware.
* Le mode service.

Raisons :
1. Ce sont des exploits **spécifiques à chaque version de firmware** — chaque constante d'adresse
   doit être maintenue par modèle. C'est une dette de maintenance considérable pour une v1.
2. Une erreur d'adresse peut **briquer** l'appareil (écriture EEPROM erronée).
3. `netmd-exploits` est **GPL-2.0** : sa lecture pour en extraire les constantes d'adresses est
   juridiquement inconfortable (§10).

**DANS le scope v1, éventuellement** :
* Rien du jeu factory. Le jeu de commandes standard suffit pour : lister, titrer, grouper,
  déplacer, effacer, downloader, lire l'état.

**Envisageable en v2, isolé derrière un flag « avancé »** :
* `ForcedTOCEdit` : le seul exploit à réel bénéfice ergonomique (§6.4). Il ne nécessite qu'un
  patch minuscule et est réversible.

### 5.4 Identification fine du modèle (utile même sans exploit)

Le mode factory permet de lire le « device code » descriptif (par exemple `"R1.300"`), donc le
modèle réel — utile pour distinguer les appareils partageant un PID (les LAM à `0x0101`) ou
signaler à l'utilisateur les fonctions non disponibles. Cela nécessite quand même l'auth factory
(`1801`) et une lecture mémoire (`1821`).

**Recommandation v1** : s'en passer. Se contenter du VID/PID et d'une table statique, en affichant
« modèle probable » plutôt qu'une certitude.

---

## 6. Timings, débits, TOC edit, éjection

### 6.1 Débits réels observés

Le bus est USB 1.1 Full-Speed : **12 Mbit/s** théoriques, ~ **1 Mo/s** utiles au mieux. Mais le
facteur limitant n'est presque jamais le bus.

| Mode | Données transmises par minute d'audio | Facteur limitant | Débit réel typique |
|---|---|---|---|
| **SP** (PCM 44,1/16/2 BE) | **10,09 Mio/min** (10 584 000 octets) | encodeur ATRAC1 **temps réel** dans l'appareil | ≈ **1,0–1,3 × temps réel** |
| **LP2** (ATRAC3 132 kbit/s) | 0,95 Mio/min (192 o × 86,13 frames/s ≈ 16 538 o/s) | écriture disque + USB | ≈ **3–5 × temps réel** |
| **LP4** (ATRAC3 66 kbit/s) | 0,47 Mio/min | écriture disque + USB | ≈ **5–8 × temps réel** |

Formule exacte du nombre de frames par seconde : **44 100 / 512 = 86,1328125 frames/s**.

Donc :
* LP2 : `192 × 86,1328 = 16 537,5 octets/s` = 0,946 Mio/min.
* LP4 : `96 × 86,1328 = 8 268,75 octets/s` = 0,473 Mio/min.
* SP (PCM sur le fil) : `2048 × 86,1328 = 176 400 octets/s` = 10,09 Mio/min
  (= 44 100 × 2 × 2, cohérent).

**Conséquence UX majeure** : en SP, transférer un album de 74 minutes prend **au moins 74 minutes**.
Il faut :
* une barre de progression **basée sur les octets écrits** (`writtenBytes / totalBytes`) ;
* une **estimation de temps restant** honnête ;
* la possibilité d'annuler proprement (mais l'annulation en plein `0x28` laisse une session
  ouverte : prévoir le nettoyage du §4.14, et savoir que l'appareil peut avoir besoin d'un
  débranchement).

### 6.2 Temporisations obligatoires

| Endroit | Attente | Raison |
|---|---|---|
| Avant `sendTrack` (`0x28`) | 200 ms | Sharp lents |
| Après l'INTERIM de `0x28`, avant le premier paquet | 200 ms | idem |
| Après un upload (`0x30`) | 500 ms | stabilisation |
| Boucle « attendre `ready` » avant `setupDownload` | 200 ms par itération | éviter le REJECTED prématuré |
| Entre deux commandes d'édition TOC | ≥ 100 ms | l'appareil écrit |
| Après `eraseTrack`/`moveTrack`, avant re-listage | ≥ 500 ms | TOC en cours de réécriture |

### 6.3 Le TOC est en RAM, pas sur le disque

**Fait fondamental** : quand on modifie un titre, un groupe, ou qu'on déplace/efface une piste,
l'appareil modifie une **copie du TOC en RAM**. Le TOC n'est écrit sur le disque que lors d'un
**« TOC flush »**, déclenché par :

* l'**éjection** du disque ;
* la **mise hors tension** de l'appareil (bouton, ou coupure d'alimentation propre) ;
* certaines opérations internes.

L'écran affiche alors `TOC Edit` pendant 1 à 5 secondes. **Débrancher l'alimentation à ce
moment-là corrompt le disque.**

**Conséquences pour l'application** :

1. Après toute session d'édition, il faut **dire à l'utilisateur** de laisser l'appareil terminer :
   « Éjectez le disque ou éteignez l'appareil pour valider les modifications. Ne débranchez pas
   pendant l'affichage TOC Edit. »
2. Les modifications ne sont **pas** perdues si on débranche le câble USB (elles restent en RAM),
   mais elles le sont si on retire la batterie.
3. Un `commitTrack` après un download **provoque** un flush TOC sur beaucoup d'appareils (c'est
   son rôle), donc les downloads sont plus sûrs que les éditions de titres seules.
4. L'exploit `ForcedTOCEdit` sert exactement à forcer ce flush depuis l'hôte, sans éjection.

### 6.4 Éjection

```
Test :  01 18c1 ff 6000
Faire :  00 18c1 ff 6000
```

Sur un **baladeur** (MZ-N505), l'éjection est mécanique : la commande est en général
`NOT IMPLEMENTED`. Sur une **platine**, elle ouvre le tiroir et **force le flush du TOC**.

Comportement recommandé de l'UI :
* Au démarrage, tester avec `SPECIFIC INQUIRY` (`status = 0x01`) et n'afficher le bouton
  « Éjecter » que si l'appareil répond positivement.
* Sinon, afficher une consigne textuelle (« ouvrez le capot pour valider le TOC »).

### 6.5 Ordre d'écriture optimal (minimiser les cycles TOC)

Le TOC d'un MiniDisc est en **magnéto-optique** : chaque réécriture use le disque (les MD sont
donnés pour ~ 1 million de cycles, mais l'usure est réelle et les vieux disques défaillent).

Ordre optimal d'une session « ajouter 10 pistes et les titrer avec des groupes » :

```
1. acquire
2. entrer en session sécurisée UNE fois
3. pour chaque piste :
     setupDownload -> sendTrack -> setTrackTitle -> commitTrack
4. sortir de la session sécurisée
5. compiler la chaîne de titre disque complète (titre + tous les groupes)
6. setDiscTitle(half)  [une seule écriture]
7. setDiscTitle(full)  [une seule écriture, si nécessaire]
8. release
9. inviter l'utilisateur à éjecter
```

**Anti-pattern** : réécrire le titre disque après chaque piste (10 réécritures au lieu d'une).

---

## 7. Faits médium : capacité et budget de titres

### 7.1 Longueurs nominales des disques

| Type | Durée SP | Durée LP2 | Durée LP4 | Notes |
|---|---|---|---|---|
| MD 60 | 60 min | 120 min | 240 min | rare |
| MD 74 | 74 min | 148 min | 296 min | le plus courant |
| MD 80 | 80 min | 160 min | 320 min | courant, capacité max NetMD |
| Hi-MD 1 Go | — | — | — | format différent, cf. §8 |

Un MD 80 formaté Hi-MD donne ~ 305 Mo ; en NetMD classique il reste à 80 min SP.

### 7.2 Formule de capacité

L'unité canonique est le **frame** : `1 seconde = 512 frames`.

```
frames_totaux(disque)   = durée_minutes × 60 × 512
frames_par_seconde      = 512
```

En termes de **frames audio** (unité du transfert) :

```
frames_audio_par_seconde = 44100 / 512 = 86,1328125
```

Ne pas confondre les deux « frames » :
* le **frame TOC/temps** (1/512 s) utilisé par `getDiscCapacity`, `getTrackLength`, `getPosition` ;
* le **frame audio** (2048 / 192 / 152 / 96 octets) utilisé par `sendTrack`.

**Calcul du temps disponible pour un mode donné** :

```c
/* frames_left provient de getDiscCapacity()[2], converti par md_time_to_frames */
uint32_t md_seconds_available(uint32_t frames_left, uint8_t discformat)
{
    uint32_t mult;
    switch (discformat) {
        case 6: case 4: mult = 1; break;   /* SP stéréo / SP mono */
        case 2:         mult = 2; break;   /* LP2 */
        case 0:         mult = 4; break;   /* LP4 */
        default:        mult = 1; break;
    }
    return (frames_left / 512u) * mult;
}
```

⚠️ **SP mono** : bien que le discformat soit distinct (`4`), le mode mono double aussi la durée sur
la plupart des appareils. Le tableau ci-dessus reste prudent (`mult = 1`) car le comportement
diffère selon les modèles ; à valider sur matériel.

**Calcul de la place nécessaire pour un fichier** :

```c
uint32_t md_frames_needed(uint32_t audio_seconds, uint8_t discformat)
{
    uint32_t div = (discformat == 0) ? 4u : (discformat == 2) ? 2u : 1u;
    return (audio_seconds * 512u) / div;
}
```

### 7.3 Granularité et fragmentation

* **Granularité d'allocation** : un MiniDisc écrit par **clusters** de 36 secteurs de 2 352 octets.
  Un cluster ≈ **2 secondes de SP stéréo**. Une piste occupe donc toujours un nombre entier de
  clusters : **une piste de 3 secondes consomme la place de 4 secondes**.
* **254 / 255 pistes** : le TOC contient une table de **255 fragments**. Chaque piste est faite
  d'un ou plusieurs fragments. Donc :
  * dans le meilleur des cas (aucune fragmentation), **255 pistes** ;
  * en pratique, la limite pratiquement documentée et utilisée par les logiciels est
    **254 pistes** ;
  * **avec de la fragmentation, on peut atteindre `DISC FULL` bien avant 254 pistes**, même avec
    de la place libre.
* **Fragmentation** : elle apparaît quand on efface des pistes au milieu du disque et qu'on en
  réenregistre. Le MD n'a **pas de défragmenteur** exposé par NetMD. La seule vraie solution est
  `eraseDisc` (`00 1840 ff 0000`), qui remet le TOC à zéro.
* **Gaps** : NetMD ne permet pas de contrôler les silences inter-pistes. Chaque piste commence au
  début d'un cluster ; le « gapless » est impossible en NetMD (une piste combinée puis re-divisée
  sur l'appareil peut l'être, mais ce n'est pas exposé).

**Message d'erreur à prévoir** : distinguer « disque plein (durée) » de « disque plein (fragments) ».
Le second se manifeste par un `REJECTED` sur `sendTrack` alors que `getDiscCapacity` annonce de la
place. Message utilisateur : *« Le disque ne peut plus accueillir de nouvelle piste
(table d'allocation saturée par la fragmentation). Effacez et réenregistrez le disque, ou
utilisez un autre disque. »*

### 7.4 Budget de caractères du TOC — le point le plus subtil

Le TOC réserve pour les titres une table de **256 cellules de 8 octets**, dont **255 utilisables**,
chaque cellule stockant **7 caractères** de titre (le 8ᵉ octet est un pointeur de chaînage).

```
Budget total half-width : 255 cellules × 7 caractères = 1 785 caractères
```

Ce budget est **partagé** par :
* le titre du disque (avec toute la syntaxe de groupes `0;…//1-4;…//` incluse !) ;
* tous les titres de pistes.

Et il existe **deux budgets séparés** : un pour le half-width, un pour le full-width.

**Règles de calcul exactes** :

```c
static uint32_t md_chars_to_cells(uint32_t chars) { return (chars + 6u) / 7u; }  /* ceil(n/7) */
```

* **Titre de piste half-width** : `cells = ceil(halfwidth_len(titre) / 7)`.
* **Titre de piste full-width** : `cells = ceil((nb_caractères × 2) / 7)`.
* **Correction « LP: »** : pour une piste non-SP (LP2/LP4), l'appareil ajoute automatiquement un
  préfixe `"LP: "` au titre affiché ; **même avec un titre vide, la piste consomme 1 cellule**.
  D'où :

```c
uint32_t md_cells_for_track(const md_track *t)
{
    uint32_t base = (t->encoding == 0x90) ? 0u : 1u;   /* 0x90 = SP */
    uint32_t hw   = md_chars_to_cells(md_halfwidth_len(t->title));
    return hw > base ? hw : base;
}
```

* **Titre du disque** : `cells = ceil(halfwidth_len(chaîne_brute_complète) / 7)`, où
  « chaîne brute complète » inclut `0;`, `//`, les ranges et les `;`.

**Calcul du budget restant** :

```c
uint32_t md_remaining_halfwidth_chars(const md_disc *d)
{
    const uint32_t CELL_LIMIT = 255;
    uint32_t used = 0;

    /* Pire cas : titre disque + syntaxe de groupes */
    char raw[2048];
    md_build_raw_disc_title(d, raw, sizeof raw);   /* "0;Titre//1-4;GrpA//5-9;GrpB//" */
    used += md_chars_to_cells(md_halfwidth_len_utf8(raw));

    for (size_t i = 0; i < d->track_count; ++i)
        used += md_cells_for_track(&d->tracks[i]);

    return (used >= CELL_LIMIT) ? 0u : (CELL_LIMIT - used) * 7u;
}
```

**Stratégie de compilation des groupes quand le budget est dépassé** (à reproduire) :

1. Calculer le budget disponible **hors** titre disque et hors groupes.
2. Construire la chaîne incrémentalement : d'abord `0;<titre>//`, puis groupe par groupe.
3. **N'ajouter un groupe que s'il rentre encore** dans le budget. On garde ainsi le maximum de
   groupes plutôt que d'échouer complètement.
4. Si même le titre disque seul ne rentre pas, mettre la chaîne à vide.
5. Si aucun vrai groupe n'a été retenu, revenir au titre disque brut (sans la syntaxe `0;…//`).
6. Répéter indépendamment pour half-width et full-width.

**Point d'UX important** : afficher à l'utilisateur le budget restant (« 340 caractères restants
sur ce disque »), et **avertir avant** de tronquer. Une troncature silencieuse d'un titre est une
mauvaise surprise ; une perte silencieuse de groupes l'est encore plus.

### 7.5 Protection du disque

* **Onglet physique** : flag `0x40` (`DISC_WRITE_PROTECTED`) dans `getDiscFlags`. Aucune commande
  ne peut le contourner. Message clair à l'utilisateur : « Faites glisser l'onglet de protection au
  dos du disque. »
* **Disque préenregistré** (album commercial) : flag `0x10` (`writable`) **absent**. Lecture seule,
  définitivement.
* **Piste protégée** : flag `0x03` sur `getTrackFlags`. C'est une piste « checked out » par
  SonicStage. Elle refusera `eraseTrack`. Contournements : la commande secure `0x40`
  (secure delete / check-in), ou l'exploit `ForcedTOCEdit`. Prévoir au minimum de **détecter** ce
  cas et d'expliquer.

### 7.6 Récapitulatif des limites numériques

| Limite | Valeur |
|---|---|
| Pistes max (fragments) | 255 (254 en pratique) |
| Cellules de titre | 255 utilisables (sur 256) |
| Caractères par cellule | 7 |
| Budget titre total half-width | 1 785 caractères |
| Budget titre total full-width | 1 785 « demi-caractères » = ~892 caractères pleine chasse |
| Longueur max d'une réponse USB | 255 octets |
| Longueur max d'un titre de piste (pratique) | limitée par le budget global |
| Durée max | 80 min SP / 160 min LP2 / 320 min LP4 |
| Frames par seconde (temps) | 512 |
| Frames audio par seconde | 86,1328125 |
| Cluster | 36 secteurs × 2 352 octets ≈ 2 s de SP |

---

## 8. Hi-MD : ce qui change

### 8.1 Deux protocoles dans un seul appareil

Un appareil Hi-MD (MZ-NH*/RH*/DH*, PID `0x017e`–`0x0286`) expose **deux modes** :

1. **Mode NetMD** : identique à ce document. Un disque MD 80 classique inséré dans un appareil
   Hi-MD reste en NetMD. Tout ce qui précède s'applique.
2. **Mode Hi-MD** : l'appareil se présente comme un **périphérique de stockage de masse USB
   (USB Mass Storage, `bInterfaceClass = 0x08`)** avec un **système de fichiers FAT**. Le disque
   (MD 80 reformaté à 305 Mo, ou disque Hi-MD 1 Go) contient une arborescence
   `HI-MD.IND` / `HMDHIFI/` avec les fichiers `.HMA` (ATRAC3plus/ATRAC3/PCM linéaire) et une base
   de données `TRKIDX00.HMA`.

Le passage NetMD → Hi-MD se fait par `eraseDisc` suivi de la commande secure `0x82`
(signature `f0 03 01 04`), qui **reformate le disque** :

```
=>  00 18 00 08 00 46 f0 03 01 04 82 ff
<=  09 18 00 08 00 46 f0 03 01 04 82 00
```

⚠️ **Destructif et irréversible pour le contenu.** Ne jamais exposer ça sans double confirmation.

### 8.2 Ce qui change en mode Hi-MD

| Aspect | NetMD | Hi-MD |
|---|---|---|
| Transport | control transfers vendor + bulk | USB Mass Storage (SCSI) |
| Métadonnées | TOC / UTOC (255 cellules de 7 caractères) | base de données FAT `TRKIDX00.HMA`, titres Unicode longs |
| Codecs | ATRAC1 (SP), ATRAC3 (LP2/LP4) | ATRAC3plus (48/64/256 kbit/s), ATRAC3, PCM linéaire 1411 kbit/s |
| Capacité | 80 min SP | 305 Mo (MD 80 reformaté) / 1 Go (disque Hi-MD) |
| Upload | interdit (sauf MZ-RH1) | possible pour les enregistrements « self-recorded » |
| Groupes | encodés dans le titre disque | vrais groupes dans la base |
| Chiffrement | DES + EKB | même famille de DRM, base de données chiffrée |
| Limite de pistes | 254 | > 1 000 |

Réimplémenter Hi-MD = réimplémenter **un système de fichiers propriétaire + un schéma de base de
données + ATRAC3plus**. C'est un projet distinct de plusieurs ordres de grandeur plus gros
(cf. `himdcli`/`libhimd` dans linux-minidisc, et `himd-js` chez asivery).

### 8.3 Recommandation v1

> **v1 = NetMD uniquement.**

Justifications :
1. L'appareil cible (MZ-N505) **n'est pas** Hi-MD. Le sujet est purement hypothétique pour cette
   version.
2. Le mode Hi-MD n'utilise **rien** de ce document : ni le poll, ni les trames AV/C, ni l'EKB.
   C'est un second projet, pas une extension.
3. Un appareil Hi-MD branché avec un disque MD classique **fonctionnera déjà** avec le code NetMD.

**Ce qu'il faut quand même faire en v1** :
* Détecter un PID Hi-MD dans la table (§1.2) et afficher un message informatif :
  « Appareil Hi-MD détecté. Le mode NetMD (disques MiniDisc standard) est supporté ; le mode Hi-MD
  ne l'est pas. »
* **Ne pas** exposer `enterHiMDMode` (`0x82`).
* Si le disque inséré est déjà formaté Hi-MD, l'appareil ne répondra pas aux commandes NetMD
  attendues : détecter le `NOT IMPLEMENTED` sur `getTrackCount` et afficher un message explicite
  plutôt qu'une erreur brute.

---

## 9. Checklist d'implémentation et pièges

### 9.1 Découpage en couches

```
+----------------------------------------------------------+
|  UI (Win32 / autre)                                       |
+----------------------------------------------------------+
|  md_session : listContent, rename, groupes, download      |  <- logique métier
+----------------------------------------------------------+
|  md_iface   : commandes AV/C typées (getTrackTitle, ...)  |  <- §3
|  md_secure  : session EKB/DES, sendTrack, commit          |  <- §4
+----------------------------------------------------------+
|  md_query   : md_fmt() / md_scan() (mini-langage hex)     |  <- §3.3
+----------------------------------------------------------+
|  md_transport : poll / send / recv / bulk                 |  <- §2.5-2.7
+----------------------------------------------------------+
|  md_usb : WinUSB (SetupDi, CreateFile, WinUsb_*)          |  <- §2.3
+----------------------------------------------------------+
|  md_crypto : DES, 3DES, retailMAC, RNG (BCrypt)           |  <- §4.6, 4.10
|  md_text   : Shift-JIS (CP932), sanitize half/full        |  <- §3.11
+----------------------------------------------------------+
```

Découplage clé : **`md_transport` ne connaît que des buffers d'octets**. Tout le reste est
testable sans matériel via un `md_transport` factice rejouant des traces.

### 9.2 Checklist priorisée

**P0 — socle indispensable (sans ça, rien ne marche)**

- [ ] Énumération SetupAPI par GUID d'interface + filtre VID/PID (§2.3).
- [ ] Ouverture WinUSB, politiques de pipe, désactivation de l'auto-suspend (§2.3).
- [ ] `md_ctrl_out(0x80)`, `md_ctrl_in(0x81)`, `md_poll(0x01)` (§2.4-2.5).
- [ ] **Poll pré-envoi** (vérifier `poll[2] == 0`) et **drain initial** à l'ouverture (§2.5).
- [ ] Utiliser **`poll[1]`** comme `bRequest` de lecture, pas `0x81` en dur (§2.5).
- [ ] Boucle de poll avec backoff exponentiel plafonné et budget par commande (§2.6).
- [ ] Table des status bytes **AV/C corrects** (`0x0A` rejected, `0x0F` interim) (§3.2).
- [ ] Gestion de l'INTERIM avec retry, sauf pour `0x28`/`0x30` où il est attendu (§3.2).
- [ ] `md_fmt` / `md_scan` avec `%b %w %d %q %B %W %x %z %* %?` et endian override (§3.3).
- [ ] Assertion « toute la réponse a été consommée » dans `md_scan` (§3.3).

**P1 — lecture du disque (fonctionnalité minimale utile)**

- [ ] `changeDescriptorState` open/close systématique et symétrique (§3.4).
- [ ] `getDiscFlags`, `getTrackCount`, `getDiscCapacity` (avec le correctif Panasonic `%?03`
      et le correctif Sharp « > 82 min ») (§3.7).
- [ ] `getTrackLength` (BCD), `getTrackEncoding`, `getTrackFlags` (§3.7.4).
- [ ] Lecture **paginée** du titre disque (§3.8.1).
- [ ] Lecture du titre de piste half **et** full (wchar `0x02`/`0x03`) (§3.8.2).
- [ ] Décodage Shift-JIS via CP932 (§3.11).
- [ ] Parsing des groupes `//` `;` avec bornage sur `trackCount` et détection de chevauchement
      (§3.10).
- [ ] `getStatus` / `getPosition` avec repli si `REJECTED` (§3.14).

**P2 — édition**

- [ ] `acquire` / `release` avec libération garantie sur tous les chemins d'erreur (§3.5).
- [ ] `setTrackTitle` / `setDiscTitle` avec `oldLen` correct, court-circuit si identique,
      et le chemin Sharp (§3.9).
- [ ] Sanitize half-width et full-width complets, avec repli agressif (§3.11).
- [ ] Calcul du budget de cellules et compilation incrémentale des groupes (§7.4).
- [ ] `eraseTrack` par index **décroissant**, `moveTrack`, `eraseDisc` avec confirmation (§3.12).
- [ ] Recompilation du titre disque après toute suppression/déplacement (§3.10).
- [ ] Message « éjectez pour valider le TOC » après édition (§6.3).

**P3 — download SP**

- [ ] DES (ECB + CBC) et 3DES-EDE2, testés sur vecteurs NIST (§4.6, 4.10).
- [ ] `retailMAC` testé sur vecteur figé (§4.6).
- [ ] RNG via `BCryptGenRandom` (§4.6).
- [ ] Constantes EKB standard + sélection par leafID/VID/PID (§4.5).
- [ ] Séquence de pré-vol (attente `ready`, `0x21`, `0x81`, `acquire`, `0x2B`) (§4.3, §4.14).
- [ ] `0x80` → `0x11` → `0x12` → `0x20` → dérivation de clé (§4.3-4.6).
- [ ] Lecture WAV PCM 44,1/16/2 et **byte-swap vers big-endian** (§4.9).
- [ ] Padding à `frameSize`, calcul `frames` et `totalBytes` (§4.11).
- [ ] Packetizer DES-CBC avec chaînage inter-paquets et troncature du padding (§4.10).
- [ ] En-tête de 24 octets du premier paquet (quadword BE + key + iv) (§4.10).
- [ ] `0x28` avec INTERIM attendu, transfert bulk, réponse finale, **poll de clôture** (§4.11).
- [ ] Titre **avant** `commitTrack` (§4.12).
- [ ] `0x48` avec `DES-ECB(0^8, sessionKey)` (§4.12).
- [ ] Clôture garantie (`0x21`, `0x81`, `release`) même en cas d'erreur (§4.14).
- [ ] Barre de progression sur `writtenBytes / totalBytes`, ETA honnête (§6.1).

**P4 — confort**

- [ ] Détection `WM_DEVICECHANGE` (branchement/débranchement à chaud).
- [ ] Vérification préalable de la place disponible et du budget de titres.
- [ ] Détection des disques protégés / préenregistrés / pistes protégées (§7.5).
- [ ] Bouton « Éjecter » conditionné par le test `SPECIFIC INQUIRY` (§6.4).
- [ ] Mode « conversion LP2/LP4 » à partir de fichiers ATRAC3 déjà encodés (§4.9).
- [ ] Journal des trames hex (envoi/réception) activable — indispensable au débogage.

### 9.3 Pièges connus (liste consolidée)

**Transport**

1. **Longueur de réponse sur 1 octet** → 255 maximum → toute lecture de titre long doit être
   paginée. Ne pas dimensionner un buffer de réponse à moins de 256 octets.
2. **Ne pas envoyer de commande si `poll[2] != 0`** : la réponse en attente serait perdue et
   toutes les commandes suivantes seraient décalées d'un cran (symptôme : les réponses
   correspondent à la commande précédente).
3. **Utiliser `poll[1]`** comme `bRequest` de lecture. Coder `0x81` en dur casse le mode factory
   et certaines réponses.
4. **Un `poll` avec `buf[0] == 0` n'est pas une erreur.** C'est l'état normal « pas encore prêt ».
5. **Timeout WinUSB par pipe, pas par transfert.** Utiliser des I/O overlapped pour un vrai
   contrôle.
6. **Selective suspend** : un download de 74 minutes peut être interrompu par la mise en veille
   USB. Désactiver `AUTO_SUSPEND` pendant le transfert.

**Protocole**

7. **Les status bytes de `netmd-js` sont faux** (`rejected = 0x0b`, `interim = 0x0d`). Utiliser
   les valeurs AV/C (`0x0A`, `0x0F`).
8. **`wchar` change de valeur selon la cible** : `0x00`/`0x01` pour le titre **disque**,
   `0x02`/`0x03` pour les titres de **pistes**. Confusion très fréquente.
9. **`oldLen` faux corrompt le TOC.** Toujours relire le titre courant avant d'écrire.
   Une piste sans titre renvoie `REJECTED` → `oldLen = 0`.
10. **Réécrire un titre identique fait planter les LAM.** Court-circuiter si égal.
11. **Sharp (VID `0x04dd`) renomme le disque via `audioUTOC1TD`**, pas `discTitleTD`.
12. **Panasonic renvoie `0803` au lieu de `8003`** dans la réponse de capacité, et un placeholder
    non nul sur `0x20`. Ne jamais vérifier ces octets en dur.
13. **Sharp rapporte la capacité dans le mode courant** → correctif « diviser par 2 tant que
    > 82 min ».
14. **`stop` échoue sur LAM-1** → ignorer l'erreur.
15. **Un descripteur laissé ouvert bloque les commandes suivantes.** Toujours `close`, y compris
    en cas d'erreur.
16. **Les indices de pistes sont 0-based** partout… **sauf** dans `0x30` (recv track) où ils sont
    1-based.
17. **Après un `eraseTrack`, tous les index supérieurs se décalent.** Effacer par index
    décroissant, puis recharger.
18. **Les groupes ne sont pas mis à jour automatiquement.** Un range peut dépasser `trackCount`.

**Crypto / download**

19. **La clé de données est obtenue par un DÉchiffrement DES-ECB** de 8 octets aléatoires avec la
    KEK, pas un chiffrement. Erreur classique.
20. **Le chaînage CBC est continu entre les paquets** : l'IV du paquet N+1 est le dernier bloc du
    chiffré du paquet N, pas 8 zéros.
21. **Tronquer le chiffré** à la longueur exacte de l'entrée (supprimer le padding PKCS#7 ajouté
    par certaines bibliothèques).
22. **`retailMAC` n'est pas un simple 3DES-CBC** : DES-CBC sur tout sauf le dernier bloc, puis
    3DES-CBC sur le dernier bloc avec l'IV issu de l'étape 1.
23. **La commande `0x12` répond avec le placeholder `0x01`**, pas `0x00`. Un scan strict échoue.
24. **`totalBytes` inclut les 24 octets d'en-tête** du premier paquet. Oublier ce `+24` fait
    échouer la fin de transfert.
25. **Le PCM SP doit être BIG-ENDIAN.** Un WAV lu tel quel donne du bruit blanc sur le disque.
26. **200 ms de pause avant et après `0x28`** — supprimer ces attentes fait échouer les appareils
    lents de façon intermittente.
27. **Poll de clôture après la réponse finale de `0x28`.** Sans lui, la commande suivante peut
    lire une réponse périmée.
28. **Titrer avant `commitTrack`**, jamais après.
29. **Une session sécurisée laissée ouverte** (crash, annulation) fait échouer tous les downloads
    suivants avec `REJECTED` jusqu'au débranchement. D'où le `0x21` + `0x81` préventifs à
    l'ouverture.
30. **`acquire` sans `release`** laisse l'appareil verrouillé.

**Médium**

31. **Le TOC vit en RAM** : les modifications sont perdues si la batterie est retirée avant
    l'éjection ou l'extinction.
32. **Ne jamais couper l'alimentation pendant `TOC Edit`** — corruption du disque.
33. **Le budget de titres est partagé** entre disque, groupes et pistes, et compté en **cellules
    de 7 caractères**, pas en caractères. Un titre de 8 caractères coûte autant que 14.
34. **Les pistes non-SP consomment 1 cellule même sans titre** (préfixe `LP: ` automatique).
35. **Fragmentation** : `DISC FULL` possible malgré de la place libre. Message d'erreur dédié.
36. **Granularité de 2 secondes** : les pistes courtes gaspillent de la place.
37. **Piste protégée (`flag == 0x03`)** : `eraseTrack` échoue. Détecter et expliquer.

**Texte**

38. **Shift-JIS, pas UTF-8.** Utiliser CP932 sous Windows.
39. **Assainir avant d'écrire** : un caractère non représentable écrit tel quel corrompt le TOC.
40. **Vérifier que la longueur encodée correspond à la longueur comptée** ; sinon repli agressif
    (ASCII pur).
41. **Les caractères de service des groupes existent en deux variantes** : `//` `;` `-` en
    half-width, `／／` `；` `－` en full-width. Mélanger les deux casse le parsing.

### 9.4 Stratégie de test sans risquer un disque

1. **Rejeu de traces** : capturer les trames avec un `md_transport` qui journalise, puis rejouer
   hors ligne. Permet de tester `md_scan` sans matériel.
2. **Vecteurs crypto** : tester DES/3DES sur les vecteurs NIST, puis `retailMAC` sur un vecteur
   figé produit une seule fois par une implémentation de référence.
3. **Disque de test dédié** (un MD 74 bon marché) pour toute l'écriture. Ne jamais tester sur un
   disque de l'utilisateur.
4. **Mode « dry run »** : exécuter toute la séquence de download jusqu'à l'INTERIM de `0x28`,
   puis abandonner proprement. Valide toute la crypto sans écrire.
5. **USBPcap + Wireshark** : capture des control transfers Windows pour comparer avec une trace
   de référence (SonicStage sur VM XP, ou Web MiniDisc Pro dans Chrome).

---

## 10. Notes de licence

### 10.1 Statut des sources consultées

| Projet | Licence | Impact |
|---|---|---|
| **libnetmd** (linux-minidisc) | **LGPL-2.1-or-later** | Lier dynamiquement serait possible sous LGPL, mais **copier du code dans un binaire propriétaire/statique ne l'est pas** sans respecter la LGPL (relink, mise à disposition des objets). |
| **netmd-js** (cybercase/asivery) | **GPL-2.0** | **Copier du code contaminerait toute l'application.** À ne pas faire. |
| **netmd-exploits** (asivery) | **GPL-2.0** | Idem. Les constantes d'adresses par firmware sont des données extraites du code — juridiquement zone grise, à éviter. |
| **webminidisc / Web MiniDisc Pro** | GPL-2.0 (dérivé de netmd-js) | Idem. |
| **ElectronWMD** | GPL | Idem. |
| **Platinum-MD** | GPL (front-end sur netmd-js) | Idem. |
| **atracdenc** | à vérifier avant tout usage (le dépôt indique une licence permissive de type BSD/MIT sur la majorité des fichiers, mais **vérifier fichier par fichier**) | Encodeur ATRAC1/ATRAC3 — seule option open source pour LP2/LP4. |
| **libwdi** (installateur WinUSB de Zadig) | **LGPL-3.0** | Liaison dynamique acceptable ; préférer un INF signé et `DiInstallDriver` pour éviter la dépendance. |
| **minidisc.org**, **minidisc.wiki** | documentation, pas de code | Faits techniques librement utilisables. |
| **WinUSB / winusb.dll** | Microsoft, in-box | Aucun problème, API système documentée. |

### 10.2 Ce qui est sûr, ce qui ne l'est pas

**Sûr (faits d'interopérabilité, non protégeables par le droit d'auteur)** :
* Les valeurs d'octets du protocole : opcodes, status bytes, offsets, formats de trame.
* Les paramètres des control transfers (`0x01`, `0x80`, `0x81`, `bmRequestType`).
* Les constantes numériques du médium (255 cellules, 7 caractères, 512 frames/s, tailles de frame).
* Les algorithmes standards (DES, 3DES, retail MAC ISO 9797-1 alg. 3, Shift-JIS).
* La grammaire des groupes (`//`, `;`) : c'est un format de données Sony, pas du code.
* Les constantes de l'EKB : ce sont des **données de clés**, pas du code. Elles proviennent
  originellement du firmware Sony et ont été publiées par la communauté. Leur usage relève de
  l'interopérabilité (directive européenne 2009/24/CE art. 6, `17 U.S.C. § 1201(f)` aux É.-U.),
  mais ce n'est pas juridiquement anodin — voir §10.4.

**Non sûr (expression protégée)** :
* Recopier une fonction C de `libnetmd` ou une méthode TypeScript de `netmd-js`, même traduite
  ligne à ligne.
* Reprendre l'organisation interne, les noms de fonctions et la structure de commentaires
  d'un de ces projets.
* Réutiliser les tables de mapping half-width/full-width de `netmd-js` telles quelles (ce sont
  des tables de données substantielles, potentiellement protégeables comme compilation).
  → Les régénérer soi-même à partir des standards Unicode (`Halfwidth and Fullwidth Forms`,
  U+FF00–U+FFEF) et de CP932.

### 10.3 Hygiène recommandée pour ce projet

1. **Écrire une spécification** (ce document) décrivant le protocole en termes de bytes,
   puis **implémenter à partir de la spécification**, pas à partir du code source. C'est la
   procédure classique de « clean room » atténuée.
2. **Ne jamais copier-coller** depuis un dépôt GPL/LGPL, même un `enum`.
3. **Documenter les sources dans les commentaires** en termes de faits (« opcode 0x1843 = MOVE,
   voir docs/research/01-netmd-protocol.md §3.12 »), pas de renvoi à un fichier source GPL.
4. **Générer les tables de texte** (half↔full width) par script depuis les tables Unicode
   officielles, en conservant le script dans le dépôt comme preuve de provenance.
5. Si LP2/LP4 devient nécessaire : soit **appeler `atracdenc` comme processus externe** (pas de
   liaison, donc pas de contamination), soit vérifier sa licence exacte fichier par fichier.
6. **Le choix de WinUSB** évite toute dépendance à libusb (LGPL-2.1) — c'est un avantage net.

### 10.4 Le point DRM / contournement

L'EKB et le mécanisme de clé de session constituent une **mesure technique de protection** au sens
du DMCA (É.-U.) et de la directive 2001/29/CE (UE, transposée en France par la loi DADVSI,
art. L.331-5 et suivants du CPI).

* L'usage pour **l'interopérabilité** est explicitement protégé en Europe : la directive
  2009/24/CE (art. 6) et l'article L.122-6-1 IV du CPI autorisent la décompilation aux fins
  d'interopérabilité. La jurisprudence française reconnaît par ailleurs l'interopérabilité comme
  une exception opposable aux mesures techniques (art. L.331-5 CPI).
* Le DMCA § 1201(f) prévoit une exemption comparable pour l'interopérabilité (« reverse
  engineering »).
* En pratique : Sony a abandonné le format en 2013, SonicStage n'est plus distribué, aucune action
  n'a jamais été engagée contre la communauté MiniDisc, et l'usage visé est de transférer **ses
  propres fichiers** vers **son propre appareil**.

Malgré tout, à mentionner explicitement dans le README du projet :
* le logiciel ne contourne **aucune** protection de contenu (il ne décode pas, ne copie pas et
  n'extrait pas de contenu protégé du disque) ;
* le mécanisme d'authentification n'est utilisé que pour **écrire les propres fichiers de
  l'utilisateur** ;
* aucun code Sony n'est distribué.

### 10.5 Licence recommandée pour ce projet

Si l'implémentation est bien « from scratch » à partir de ce document, **toutes** les licences sont
possibles. Recommandation : **MIT ou Apache-2.0**, en :
* créditant les projets communautaires comme **sources documentaires** (pas comme sources de
  code) ;
* incluant un `NOTICE` expliquant la démarche d'interopérabilité ;
* signalant les tables de données regénérées et leur script de génération.

---

## 11. Sources / URLs

### Code source primaire consulté

* `netmd-js` — https://github.com/cybercase/netmd-js
  * `src/netmd.ts` — couche USB : control transfers, poll, bulk
    https://github.com/cybercase/netmd-js/blob/master/src/netmd.ts
  * `src/netmd-interface.ts` — jeu de commandes complet, session sécurisée, retailMAC, MDTrack
    https://github.com/cybercase/netmd-js/blob/master/src/netmd-interface.ts
  * `src/netmd-commands.ts` — logique métier : capacité, groupes, budget de titres
    https://github.com/cybercase/netmd-js/blob/master/src/netmd-commands.ts
  * `src/netmd-ekb.ts` — constantes EKB et root keys
    https://github.com/cybercase/netmd-js/blob/master/src/netmd-ekb.ts
  * `src/encrypt-generator.ts` — packetizer DES-CBC
    https://github.com/cybercase/netmd-js/blob/master/src/encrypt-generator.ts
  * `src/query-utils.ts` — mini-langage formatQuery/scanQuery, BCD
    https://github.com/cybercase/netmd-js/blob/master/src/query-utils.ts
  * `src/utils.ts` — Shift-JIS, sanitize half/full width, en-têtes AEA/WAV
    https://github.com/cybercase/netmd-js/blob/master/src/utils.ts
  * `src/factory/netmd-factory-interface.ts` — jeu de commandes factory, checksum
    https://github.com/cybercase/netmd-js/blob/master/src/factory/netmd-factory-interface.ts

* `linux-minidisc` / `libnetmd` — https://github.com/linux-minidisc/linux-minidisc
  * `libnetmd/common.c` — `netmd_poll`, `netmd_send_message`, `netmd_recv_message`, timeouts
    https://github.com/linux-minidisc/linux-minidisc/blob/master/libnetmd/common.c
  * `libnetmd/secure.c` — famille secure complète, EKB, packets, AEA/WAV
    https://github.com/linux-minidisc/linux-minidisc/blob/master/libnetmd/secure.c
  * `libnetmd/const.h` — status bytes AV/C, encodages, discformats, états opératoires
    https://github.com/linux-minidisc/linux-minidisc/blob/master/libnetmd/const.h
  * `libnetmd/netmd_dev.c` — table `known_devices[]` (VID/PID)
    https://github.com/linux-minidisc/linux-minidisc/blob/master/libnetmd/netmd_dev.c
  * `libnetmd/playercontrol.c` — commandes de lecture
    https://github.com/linux-minidisc/linux-minidisc/blob/master/libnetmd/playercontrol.c

* `netmd-exploits` — https://github.com/asivery/netmd-exploits
  * `README.MD` — table de compatibilité exploits / firmwares
    https://github.com/asivery/netmd-exploits/blob/master/README.MD
  * `src/compatibility.ts` — table de compatibilité machine
    https://github.com/asivery/netmd-exploits/blob/master/src/compatibility.ts

### Front-ends et projets connexes

* Web MiniDisc Pro — https://github.com/asivery/webminidisc
* Web MiniDisc (original, cybercase) — https://github.com/cybercase/webminidisc
* ElectronWMD — https://github.com/asivery/ElectronWMD
* Platinum-MD — https://github.com/gavinbenda/platinum-md
* WebMDPro (fork) — https://github.com/DaveFlashNL/WebMDPro
* himd-js (Hi-MD en JS) — https://github.com/asivery/himd-js
* atracdenc (encodeur ATRAC1/ATRAC3) — https://github.com/dcherednik/atracdenc
* MiniTris (démonstration d'exécution de code) — https://github.com/Sir68k/MiniTris
* md-firmware (dumps de firmwares) — https://github.com/MiniDisc-wiki/md-firmware

### Documentation médium et matériel

* Structure du TOC/UTOC — https://www.minidisc.org/md_toc.html
* Fiche du Sony MZ-N505 — https://www.minidisc.org/part_Sony_MZ-N505.html
* Manuel de service MZ-N505 — https://www.minidisc.org/manuals/sony/service/sony_MZ-N505_service_manual.pdf
* MiniDisc Wiki — https://www.minidisc.wiki/
* CXD2677 (Type-R) — https://www.minidisc.wiki/internals/atrac-ic/cxd2677
* MD Community Page (racine) — https://www.minidisc.org/

### Documentation Windows / WinUSB

* WinUSB (vue d'ensemble) — https://learn.microsoft.com/windows-hardware/drivers/usbcon/winusb
* `WinUsb_ControlTransfer` — https://learn.microsoft.com/windows/win32/api/winusb/nf-winusb-winusb_controltransfer
* `WinUsb_SetPipePolicy` — https://learn.microsoft.com/windows/win32/api/winusb/nf-winusb-winusb_setpipepolicy
* INF WinUSB générique — https://learn.microsoft.com/windows-hardware/drivers/usbcon/winusb-installation
* Zadig — https://zadig.akeo.ie/
* libwdi — https://github.com/pbatard/libwdi
* `BCryptGenRandom` — https://learn.microsoft.com/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom

### Références de standards

* AV/C Digital Interface Command Set — 1394 Trade Association
* ISO/IEC 9797-1 Algorithme 3 (Retail MAC / ANSI X9.19)
* Unicode « Halfwidth and Fullwidth Forms » U+FF00–U+FFEF
* Windows Code Page 932 (Shift-JIS) — https://learn.microsoft.com/windows/win32/intl/code-page-identifiers

---

*Fin du document R-01.*

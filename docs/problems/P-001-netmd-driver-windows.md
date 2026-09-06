# P-001 — Device NetMD en statut "Error" sous Windows 11

## Symptôme
`Get-PnpDevice` montre `USB\VID_054C&PID_0084` ("Net MD Walkman") avec `Status = Error`,
`DEVPKEY_Device_ProblemCode = 28` (CM_PROB_FAILED_INSTALL : aucun driver installé).
Le device s'annonce en classe `FF` (vendor-specific), sous-classe 00, protocole 00. Windows n'a
pas de driver inbox pour lui et Sony ne fournit plus le driver NetMD (32-bit, XP-era).

## Cause racine
Un device USB vendor-specific a besoin qu'un driver soit lié à son interface. Sans driver, aucune
API user-mode (WinUSB, libusb) ne peut l'ouvrir.

## Solution
Lier le driver **WinUSB** (inbox Windows, `winusb.sys`) à l'interface 0 du device. Deux voies :
1. **Zadig** (https://zadig.akeo.ie) : sélectionner "Net MD Walkman", driver WinUSB, "Install Driver". C'est
   ce que demande Web MiniDisc Pro. Manuel, une seule fois par machine.
2. Plus tard, éventuellement : notre app détecte le device sans driver (SetupAPI, ProblemCode 28) et guide
   l'utilisateur (écran "Installer le driver") — voir recherche `research/03-tech-stack.md` §8.

`winusb.dll` est présent sur la machine (`C:\Windows\System32\winusb.dll`). Aucune libusb installée : on
n'en a pas besoin, on parlera WinUSB directement.

## Leçon
Prévoir dès le v1 un état UI "device présent mais driver manquant" avec instructions, sinon c'est le
premier mur que rencontre tout utilisateur.

## Statut
Documenté, action utilisateur requise (Zadig) avant les premiers tests USB. Ticket à créer en phase device.

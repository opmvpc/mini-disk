# ADR-008 — USB : WinUSB direct, abstraction UsbTransport rejouable

Statut : **accepté** (2026-09-06) — sources : research/01 §2, research/03 §8

## Décision
- Accès device via **WinUSB** (`winusb.dll` : `WinUsb_Initialize`, `WinUsb_ControlTransfer`,
  `WinUsb_ReadPipe`, `WinUsb_SetPipePolicy`), énumération SetupAPI par interface GUID. Pas de libusb
  (LGPL, ~180 KB, inutile pour 5 % d'usage). DLLs chargées dynamiquement.
- Le driver WinUSB est installé par l'utilisateur via Zadig (P-001). L'app détecte le device sans driver
  (ProblemCode 28) et affiche un écran guidé.
- Hotplug : `RegisterDeviceNotification` + `WM_DEVICECHANGE`.
- Tout l'USB tourne sur **un thread device dédié** avec file de commandes ; l'UI ne bloque jamais.
- Le protocole NetMD (`core/netmd`) est écrit contre `UsbTransport { control_transfer, bulk_write,
  bulk_read }`. Deux implémentations : WinUSB (platform) et **rejeu de transcriptions** (tests) qui
  compare octet à octet les requêtes émises avec une session enregistrée.

## Faits protocole à respecter (research/01)
- Tout passe par control transfers sur EP0 : `0x01` poll (4 octets), `0x80` envoi, `0x81` lecture ;
  bulk EP `0x02 OUT` uniquement pour l'audio. Longueur de réponse sur 1 octet → pagination des titres.
- Status bytes AV/C corrects : `0x09` accepted, `0x0A` rejected, `0x0F` interim, `0x08` not implemented
  (netmd-js a des valeurs fausses, ne pas les recopier).
- Toujours `leave secure session` en cas d'abort ; le TOC reste en RAM jusqu'à l'éjection.

## Licences
netmd-js / webminidisc / netmd-exploits = GPL-2.0, libnetmd = LGPL-2.1. On réimplémente depuis les
faits d'interopérabilité documentés dans research/01 ; aucune copie de code. Tables half/full-width
régénérées depuis Unicode.

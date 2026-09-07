# Pilote WinUSB pour le MZ-N505 (P-001)

Zadig **contient déjà** le pilote WinUSB : il n'y a rien d'autre à télécharger. Il génère un paquet
de pilote signé autour du `winusb.sys` fourni par Windows et le lie au périphérique choisi.

## Procédure manuelle (30 secondes)
1. Lancer `tools\zadig\zadig-2.9.exe` (accepter l'UAC).
2. Si « Net MD Walkman » n'apparaît pas dans la liste déroulante : menu **Options → List All Devices**.
3. Sélectionner **Net MD Walkman** (USB ID `054C 0084`).
4. Vérifier que la cible à droite de la flèche est **WinUSB (v6.1.7600.16385)** (les flèches haut/bas changent la cible).
5. Cliquer **Install Driver** (ou **Replace Driver** si un autre pilote est listé à gauche). Attendre « The driver was installed successfully ».
6. Vérifier : `Get-PnpDevice -PresentOnly | Where-Object InstanceId -match 'VID_054C'` doit montrer `Status OK`, classe `USBDevice`.

## Procédure automatique
`tools\zadig\install_winusb.ps1` pilote Zadig par UI Automation (sélection du device, clic Install,
fermeture des dialogues) et écrit `install_winusb.log` à côté. À lancer élevé :

```
powershell -NoProfile -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File \"%CD%\tools\zadig\install_winusb.ps1\"'"
```

## Retour arrière
Gestionnaire de périphériques → Net MD Walkman → Désinstaller l'appareil (cocher « supprimer le pilote »),
puis débrancher/rebrancher : Windows revient à l'état sans pilote (code 28).

`zadig-2.9.exe` = libwdi v1.5.1 officiel (https://github.com/pbatard/libwdi/releases), 5 334 088 octets.

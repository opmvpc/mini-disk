# Smoke test toolchain — exe Win32 minimal sans CRT

Mesure de référence (2026-09-06, MSVC 14.44) : `min.exe` = **1 536 octets**, ouvre une fenêtre, se ferme proprement.
Sert de plancher pour le KPI "taille de l'exe" et de vérification que la toolchain no-CRT fonctionne sur une nouvelle machine.

```
tools\smoke\build.bat
```

# R-01 — Recherche protocole NetMD (Opus, 2026-09-06)

Contexte donné : app Windows native en C pur, réimplémentation du protocole NetMD from scratch sur WinUSB
(pas de libnetmd/netmd-js linkés, licences GPL). Device : `USB\VID_054C&PID_0084`, "Net MD Walkman", driver absent.

Sources imposées : libnetmd (linux-minidisc), netmd-js (asivery/cybercase), Web MiniDisc Pro, ElectronWMD,
Platinum-MD, netmd-exploits, minidisc.wiki, atracdenc, minidisc.org. Lire le code source, pas les README.

Sections demandées :
1. Identification device (PID 0084 → modèle), table des PIDs NetMD/Hi-MD + capacités.
2. Driver Windows : pourquoi Error, WinUSB/Zadig, parler au device en WinUSB brut (endpoints, control transfers,
   polling de longueur de réponse, timings, retries).
3. Protocole de commandes : trame AV/C-like, status bytes, format query/scanQuery, liste complète des commandes
   avec payloads hex (capacité, flags, tracks, titres, groupes `//` `;`, move/erase, play, encodage 0x90/0x92/0x93, temps hh:mm:ss:ff).
4. Transfert sécurisé : session, leaf ID, EKB, échange nonce/DES → session key, setup download, chiffrement DES-CBC
   par paquet, wireformats et tailles de frames, commit/finish, cleanup, titre post-upload, SP = PCM big-endian encodé
   ATRAC1 sur le device, LP2/LP4 = ATRAC3 pré-encodé.
5. netmd-exploits / factory mode : in/out scope v1.
6. Timings & débit réel par mode, TOC edit, éjection.
7. Faits médium pour le calcul de capacité : 60/74/80 min, formule SP/LP2/LP4, granularité, 254 pistes, budget
   caractères TOC half/full-width, sanitize des titres, fragmentation, protection, gaps.
8. Hi-MD : ce qui change, recommandation v1 = NetMD only.
9. Checklist d'implémentation priorisée + pièges connus.
10. Notes de licence.
Format : Markdown français, 1500-3000 lignes, byte layouts, hex, pseudo-code, URLs.

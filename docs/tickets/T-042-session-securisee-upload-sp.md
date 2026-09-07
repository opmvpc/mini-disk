# T-042 — NetMD : session sécurisée, chiffrement DES, upload SP, titrage post-upload

Phase 5 · Statut : **todo** · Dépend de : T-022, T-041, research/01 §4-6, research/04 §6

## Livrables
- `src/core/netmd/netmd_des.c` : DES et 3DES (ECB/CBC) **maison** (tables standard FIPS 46-3, pas de
  code copié), testé contre les vecteurs NIST.
- `netmd_secure.c` : la séquence complète de research/01 §4 : `enter secure session`, `leaf id`,
  envoi de l'**EKB** open-source (ID `0x26422642`, root key connue), échange de nonces → session key
  (retailMAC ISO 9797-1 alg. 3 : DES-CBC puis 3DES-CBC), `setup download` (content ID + KEK,
  DES-CBC 32 o), **envoi des paquets** : en-tête de 24 o sur le premier, DES-CBC **chaîné entre
  paquets**, clé de données obtenue par **déchiffrement** ECB, `totalBytes = frameSize × frames + 24`,
  écriture bulk sur EP `0x02 OUT` par blocs, `commit track` (`DES-ECB(0^8, sessionKey)`),
  `finish download`, `leave session` ; **cleanup garanti** sur toute erreur (leave session + status).
- Wireformat **PCM** (SP) : frames de 2 048 octets s16 **big-endian** ; mode mono SP (research/01 §4).
- `netmd_upload.c` : orchestration d'un plan : pour chaque piste, source = cache pipeline (T-047) ;
  vérification capacité réelle **avant** (temps libre relu sur le device) ; upload ; titre écrit après
  (`set_track_title`, wchar half/full-width, budget TOC de T-031) ; groupes écrits en fin de disque
  (réécriture du titre disque) ; titre du disque ; progression par piste (octets, ETA à ~1× temps réel
  en SP, D9), annulation propre entre deux paquets, **reprise** après interruption (les pistes déjà
  committées sont conservées, le plan sait lesquelles), inhibition de veille
  (`SetThreadExecutionState` via `platform.h`), refus de fermeture pendant une gravure.
- Transcriptions : capture d'un upload SP complet sur le MZ-N505 (`tests/netmd/mzn505_upload_sp.trace`)
  et rejeu byte à byte en test (le chiffrement rend la transcription déterministe si le nonce hôte est
  fixé par injection en test).
- Tests : DES/3DES vecteurs NIST, retailMAC, dérivation de session key et de clé de données contre des
  valeurs calculées indépendamment (script Python `tools/netmd_crypto_check.py` avec `pycryptodome`
  optionnel, sinon vecteurs commités), framing des paquets, rejeu de la transcription d'upload.

## Critères d'acceptation
- **Une piste MP3 de la bibliothèque arrive sur le MiniDisc en SP avec son titre**, lisible sur le
  MZ-N505 ; le temps restant relu correspond au calcul de T-031 (validation de `MD_MODE_TABLE`,
  ex-T-045 : consigner l'écart mesuré dans le ticket et ajuster la table si besoin).
- Aucun disque corrompu sur 10 uploads consécutifs, dont 2 annulés en cours. Exe < 500 KB.

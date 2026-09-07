# T-042 — NetMD : session sécurisée, chiffrement DES, upload SP, titrage post-upload

Phase 5 · Statut : **fait** · Dépend de : T-022, T-041, research/01 §4-6, research/04 §6

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

---

## Livraison

Phase 5 · Statut : **fait** (validé sur le vrai MZ-N505, un upload réel effectué le 2026-09-07).

### Ce qui a été construit

**`src/core/netmd/netmd_des.{h,c}` — DES, 3DES, retail MAC.** Écrits depuis les tables de la
FIPS 46-3 et de rien d'autre (ADR-008) : IP, IP⁻¹, E, P, PC-1, PC-2, décalages et les huit boîtes S,
recopiées de la norme, qui est de la donnée publique. Cinq primitives, parce que la séquence de
research/01 §4 n'en demande pas une de plus : DES-ECB chiffrement (l'authentification du commit),
DES-ECB **déchiffrement** (la clé de données, §4.10), DES-CBC chiffrement (setupDownload et chaque
paquet audio), DES-CBC déchiffrement (le blob de 32 o de sendTrack), et le MAC ISO 9797-1
algorithme 3. Aucun schéma de padding nulle part : tout est déjà multiple de huit (une frame SP fait
2 048 o, setupDownload 32 o). Les 34 Ko de tables dérivées — boîtes SP (sortie S déjà permutée par P)
et les trois permutations en tables indexées par octet, 8 × 256 — sont **construites au premier
usage**, pas figées dans l'exe, sous un verrou CAS (la construction est idempotente, mais un lecteur
concurrent pourrait voir une table à moitié écrite).

**`netmd_secure.{h,c}` — la session sécurisée.** Toute la séquence de §4.3 : démontage préventif
(`0x21` + `0x81`, erreurs ignorées — une session laissée ouverte par un crash fait échouer la
suivante avec un REJECTED que l'utilisateur ne peut expliquer qu'en débranchant), `acquire`,
`setTrackProtection` (§4.13, échoue sur Sharp, ignoré), `enterSecureSession`, leaf ID, sélection puis
envoi de l'EKB (l'open-source `0x26422642` ; celui de la platine à EEPROM effacée est là aussi,
choisi sur leaf ID `FF`×8 + PID 0x0081), échange de nonces, clé de session = retailMAC(rootKey,
hostNonce‖devNonce). Puis `setupDownload` (contentID + KEK en DES-CBC de 32 o), l'envoi des paquets
(en-tête de 24 o sur le premier, CBC **chaîné d'un paquet à l'autre**, clé de données obtenue par
déchiffrement ECB sous la KEK, `totalBytes = frameSize × frames + 24`, bulk sur EP `0x02`),
`commitTrack` (DES-ECB de 8 zéros sous la clé de session), et le **chemin de nettoyage garanti** :
`netmd_secure_end` est appelé sur *toutes* les sorties — succès, erreur, annulation — et fait
`0x21`, `0x81`, `release`, puis relit l'état opératoire pour prouver que la machine est revenue.
Le nonce hôte et la clé de paquet sont **injectables** (`NetmdRandomFn`) : c'est ce qui rend une
transcription de download rejouable octet par octet, puisque avec des nonces fixes tout le chiffré
l'est aussi.

**`netmd_upload.{h,c}` — l'orchestration.** Capacité **relue sur le device** avant de commencer
(refus typé `NetmdResult_NoSpace`), une piste à la fois : rendu du pipeline dans une arène à elle,
`wait_ready`, `setupDownload`, envoi, **titre puis commit** dans cet ordre (§4.12 : l'ordre inverse
coûte un cycle TOC de plus sur certaines machines et perd le titre sur d'autres), titre de disque
écrit **une seule fois, à la fin** (§6.5, il porte la syntaxe de groupes). Annulation lue entre deux
paquets et entre deux pistes. **Reprise** : chaque entrée porte son statut, les pistes déjà
committées restent `Done` et un second appel sur le même tableau repart là où le premier s'est
arrêté — on n'efface jamais une piste que l'appareil a réellement écrite pour « faire propre ».
`os_power_keep_awake` pendant toute la durée, et `netmd_upload_active` est le drapeau « gravure en
cours » que l'application interroge avant de se fermer. Écriture de titre conforme au §3.9 :
ancien titre **relu** avant chaque écriture (un `oldLen` faux corrompt le TOC), rien envoyé si le
titre est identique (la série LAM se plante), `audioUTOC1TD` au lieu de `discTitleTD` pour renommer
le disque sur Sharp (VID `0x04dd`), et l'aller-retour open-read/close qui force le vidage du cache TOC.

**Thread device** : commandes `UploadPlan` / `CancelUpload`, événements `UploadProgress` /
`TrackDone` / `UploadDone` / `UploadError`. Le plan ne transite pas par l'anneau (il fait 200 Ko) :
`netmd_device_upload` publie le pointeur puis poste, comme le chemin de trace. Le drapeau
d'annulation est levé **à la publication de la commande**, pas à sa dépile — le thread est justement
en train de boucler sur les paquets et ne dépilera rien avant la fin, ce qu'on cherche à interrompre.

**UI** : le bouton « Graver le disque » du panneau Plan construit le plan d'upload depuis l'onglet
disque courant (titres passés par `plan_toc_preview`, titre de disque compilé par `plan_toc_budget`)
et le poste ; une ligne de progression (piste n/N, %, ETA) et un bouton « Arrêter la gravure »
apparaissent dans le panneau Disque. La vraie vue Transfert reste T-043.

**Plomberie ajoutée** : `netmd_send_frame` / `netmd_receive_frame` extraits de `netmd_exchange`
(sendTrack est la seule commande qui a quelque chose à faire *entre* l'aller et le retour),
`pipeline_source_from_decoder` (T-040 et T-041 ayant été écrits en parallèle, le décodeur ne savait
pas ce qu'est une `PipelineSource`), `os_power_keep_awake` et `os_random_bytes` dans `platform.h`
(bcrypt chargé par `LoadLibraryW`, les imports restent kernel32 + user32).

### Vérification croisée de la cryptographie

`tools/netmd_crypto_check.py` réimplémente DES, 3DES et le retail MAC **en Python pur**, depuis les
mêmes tables normalisées et sans aucune bibliothèque. Ses dix vecteurs à réponse connue ont en plus
été comparés à une **troisième implémentation que personne ici n'a écrite** : Windows CNG
(`bcrypt.dll`, `BCRYPT_DES_ALGORITHM` et `3DES_112` en ECB), via
`python tools/netmd_crypto_check.py --cng`. Les dix vecteurs, l'exemple ECB de la FIPS 81 et le bloc
3DES concordent. La chaîne est donc : tables normalisées → script Python → `netmd_des.c`, avec un
oracle indépendant au milieu. Toutes les constantes NetMD du test C (clé de session, chiffré de
setupDownload, authentification du commit, clé de données, chiffré des paquets) sortent du script,
jamais du code testé.

### Tests

`tests/test_netmd_secure.c`, 11 cas : dix vecteurs DES à réponse connue plus les quatre clés faibles
(dont l'involution `E(E(x)) = x` ne survit à aucune erreur d'échéancier de clés), ECB et CBC de la
FIPS 81 appendice B/C, 3DES EDE aller-retour, retail MAC sur 16 et sur 24 octets (24 fait travailler
la partie DES-CBC que le cas NetMD à 16 octets dégénère), les quatre dérivations NetMD, le framing
des paquets (en-tête, `totalBytes`, et surtout **le chaînage : couper le flux en deux paquets en
reportant l'IV doit donner exactement les mêmes octets** — c'est le bug qui corromprait toutes les
pistes après le premier mégaoctet et rien d'autre), la sélection d'EKB, l'encodage des titres, l'ETA,
et **trois downloads rejoués de bout en bout** depuis des transcriptions générées par
`tools/gen_netmd_traces.py` avec un nonce hôte fixé : nominal, annulation après la première piste, et
reprise. Le cas d'annulation vérifie que **toute** la transcription est jouée, c'est-à-dire que le
démontage a bien eu lieu, et que la piste déjà committée reste committée.

Un bug réel a été trouvé par le rejeu : la charge utile d'une réponse sécurisée commençait un octet
trop tôt (l'octet « placeholder » de §4.1 était compté dans la donnée), ce qui décalait le nonce du
device et donnait une clé de session fausse — un `REJECTED` opaque au `setupDownload`, exactement le
symptôme que research/01 §4.6 annonce.

### Le vrai device

Le pilote WinUSB est désormais lié (`Get-PnpDevice` : `USB\VID_054C&PID_0084`, classe `USBDevice`,
statut OK) — P-001 est levé. Un disque de 80 min, 8 pistes, 1 450 s enregistrées, flags `0x10`
(enregistrable, onglet fermé) était inséré. `build\tests.exe --device-upload` (opt-in : sans le
drapeau le cas s'annonce « skipped » et ne touche à rien) a généré **un** sinus de 10 s à 440 Hz,
44,1 kHz stéréo s16 big-endian, et l'a écrit :

```
disc: 8 track(s), 1450 s recorded, 3404 s free, flags 0x3
upload: result 0, track 8, 1765376 bytes in 13708 ms (0.729 x real time)
written: "MINIDISK TEST", 10000 ms, encoding 0
free time: 3404039 ms -> 3391164 ms, cost 12875 ms (table says 10000 ms)
```

La piste est relue sur le disque avec **son titre**, sa durée exacte et l'encodage SP. Rien n'a été
effacé, le titre du disque (qui porte des groupes) n'a pas été réécrit.

**Validation de `MD_MODE_TABLE` (ex-T-045) : écart mesuré.** Dix secondes de SP ont coûté
**12 875 ms** de temps libre là où la table en prédit 10 000 (5 clusters × 2 s). L'écart est de
+2 875 ms, soit ~1,4 cluster, et il n'est pas un multiple entier de 2 s — le temps libre que
l'appareil rapporte n'est donc pas quantifié au cluster. **La table n'a pas été modifiée** : un seul
point de mesure ne distingue pas un surcoût fixe par piste (secteurs de lien, §7.3) d'un effet de
fragmentation sur ce disque déjà à moitié plein. Il faut trois ou quatre pistes de durées différentes
sur un disque vierge pour trancher, et cela sortait du périmètre autorisé pour ce ticket (« exactement
une piste de test »).

**Débit réel : 0,729 × temps réel** (13,7 s pour 10 s d'audio, ~129 Ko/s), contre les 1,0 à 1,5 ×
annoncés par research/01 §6.1. La borne est bien l'encodeur ATRAC1 de l'appareil, ni l'USB ni le DES.

### Mesures

| Métrique | Valeur |
|---|---|
| `build.bat` debug / release / test / check / analyze / bench | **toutes vertes** |
| Tests | **216 cas, 6 344 checks, 0 échec** sous ASan (11 cas ajoutés) |
| Exe release | **575 488 o** (469 504 avant) |
| Imports | **KERNEL32.dll + USER32.dll** (table d'import lue directement dans le PE) |
| DES-CBC, 8 Mo, un cœur | **35 Mo/s** (238 ms), soit **203 ×** les 172 Ko/s dont SP a besoin |
| Upload réel, 10 s SP | 1 765 376 o en 13 708 ms, 0,729 × temps réel |

### Écarts assumés

1. **Taille de l'exe : 575 488 o, au-dessus des 510 Ko visés par le ticket** (sous le budget CI de
   600 Ko). Les +105 984 o ne viennent pas de DES ni de la session : ils viennent de ce que le
   **pipeline et tout le DSP de T-041 entrent enfin dans l'image**. STATUS.md notait explicitement
   que l'exe n'avait pas bougé au merge de T-041 parce que rien dans `app/` n'appelait le pipeline et
   que `/OPT:REF` l'élaguait ; le bouton « Graver » le rend atteignable. La cible de 510 Ko avait été
   posée avant que ce coût soit payé. Levier de resserrage inchangé (`/O1`, P-007, phase 7).
2. **Taille de paquet : 256 Ko et non 1 Mio.** research/01 §4.10 laisse le choix entre 1 Mio
   (netmd-js) et 8 Mio (libnetmd). À 176 Ko/s, un paquet de 1 Mio, c'est six secondes entre deux
   tests d'annulation et six secondes de barre de progression figée. Les paquets sont une découpe
   **côté hôte** d'un unique flux CBC continu, l'appareil ne voit qu'un flux d'octets : le choix est
   sans effet sur le fil, et le test de chaînage le prouve.
3. **En-tête de 24 o et données en deux `bulk_write` consécutifs**, là où netmd-js et libnetmd les
   concatènent. Le device voit le même flux d'octets ; cela évite un tampon de `chunk + 24`.
4. **L'audio rendu vit en mémoire, pas dans un fichier de cache** : une arène par piste, libérée dès
   le commit, jamais plus d'une piste résidente (10,6 Mo par minute). Le fichier de cache est T-043,
   et `NetmdAudioSource` est déjà l'interface qu'il viendra remplir.
5. **Titres half-width uniquement.** `plan_toc_sanitize` (T-031) ne produit que de l'ASCII et des
   katakana demi-chasse, donc l'espace full-width (`audioUTOC4TD`, wchar 1) n'a rien à écrire. Le
   paramètre existe dans la couche, il n'est jamais mis à 1.
6. **`NETMD_REPLAY_MAX_BYTES` passe de 512 à 8 192 octets** : une transcription doit pouvoir figer un
   paquet audio complet. `netmd_replay_done` ne copie plus la struct entière (16 Ko de pile, refusé
   par `/analyze`) mais sauve et restaure le curseur.
7. **`sessionKeyForget` (0x21) est envoyé avec trois octets nuls**, conformément à l'hexadécimal de
   §4.6, et non nu.
8. **Correction hors périmètre** : `tests/bench_main.c` avait perdu l'accolade fermante de
   `bench_dsp_pipeline_jobs` lors d'un merge de phase 5 — `build.bat bench` ne compilait plus sur
   `main`. Une accolade rajoutée, rien d'autre.

### Reste à faire

- Trois ou quatre pistes de durées variées sur un disque **vierge** pour trancher l'écart
  `MD_MODE_TABLE` de +2,9 s et ajuster la table si le surcoût est fixe (suite de l'ex-T-045).
- Le critère « aucun disque corrompu sur 10 uploads consécutifs, dont 2 annulés » demande une session
  de recette dédiée avec un disque sacrifiable ; une seule piste de test a été écrite ici.
- Le fichier de cache derrière `NetmdAudioSource` et la vue Transfert : T-043.

### Revue (lead, 2026-09-07)
- Grille ADR-012 dans le worktree `t042` : `check/test/release/analyze` verts, **575 488 o** (les +106 KB sont le
  pipeline/DSP de T-041 enfin lié via « Graver »), 216 cas / 6 335 checks, DES-CBC 35 Mo/s.
- **Premier disque gravé** : sinus 10 s SP « MINIDISK TEST » écrit sur le MZ-N505 en 13,7 s (0,73× temps réel) et relu.
  Le bug du placeholder §4.1 trouvé par rejeu justifie à lui seul le transport de rejeu d'ADR-008.
- `MD_MODE_TABLE` : 10 s de SP ont coûté 12,9 s de temps libre. Un seul point ; à recouper avec 3 pistes de longueurs
  différentes dans T-043 avant d'ajuster (hypothèse : surcoût fixe ~1 cluster par piste + arrondi). Merge.

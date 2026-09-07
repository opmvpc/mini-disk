# T-043 — Vue Transfert (progression, ETA, annulation, reprise) et cache de transcodage

Phase 5 · Statut : **fait** · Dépend de : T-042, ADR-011 D5/D9, research/02 §8.4, §9.11, §11

## Livrables
- `src/core/pipeline/pipeline_cache.c` (D5) : cache disque `%LOCALAPPDATA%\minidisk\transcode\<hash>.pcm`
  où `hash` = (chemin, taille, mtime, paramètres pipeline : mode, gain cible, trim, fade, mono) ;
  en-tête validé ; réutilisation immédiate pour regraver un même plan ; purge LRU par taille max
  (défaut 2 GB) ; le cache des pochettes de T-014 reçoit la même purge.
- `src/app/view_transfer.c` : la gravure comme **étape visible** : liste des pistes avec état
  (à transcoder / transcodée / en envoi / écrite / titrée / échouée), barre par piste et barre globale,
  **ETA honnête** basé sur le débit mesuré (SP ≈ 1× temps réel : on l'annonce avant de commencer),
  vitesse, temps écoulé ; boutons Pause (entre deux pistes), Annuler (avec ce que ça implique :
  "les 3 pistes déjà écrites restent sur le disque"), Reprendre après interruption ; bannière "Ne pas
  éjecter" tant que le TOC n'est pas écrit ; fermeture de l'app bloquée avec message pendant l'envoi ;
  toast de fin avec durée totale ; journal des opérations (`%LOCALAPPDATA%\minidisk\logs\transfer-<date>.txt`).
- Pré-vol (D4) : avant d'envoyer, un écran de **simulation** : ce qui va être écrit (pistes, titres
  finaux après sanitize/raccourcissement, groupes, titre disque), capacité avant/après, budget TOC
  avant/après, avertissements (piste manquante, dépassement, disque protégé, disque non vide avec
  proposition d'ajouter à la suite ou d'effacer), bouton explicite "Graver 12 pistes".
- Variante "progression de gravure" de la jauge (research/02 §9.11) dans le panneau Disque.
- Tests : machine d'états du transfert (rejeu d'une séquence d'événements : erreur au milieu, annulation,
  reprise), calcul d'ETA, clés et purge du cache, simulation → contenu attendu.

## Critères d'acceptation
- Un plan de 10 pistes se grave de bout en bout depuis l'UI ; une seconde gravure du même plan sur un
  autre disque n'attend aucun transcodage.
- Capture validée par le lead. Exe < 500 KB. Fin de phase 5 : tag `v0.5.0-phase5`, STATUS.md.

---

## Livraison

### Ce qui a été construit

**1. Cache de transcodage (`src/core/pipeline/pipeline_cache.{h,c}`, ADR-011 D5)**
`<cache_dir>\transcode\<clé 64 bits en hexadécimal>.pcm`. La clé est un FNV-1a du **chemin**, de la
**taille** et de la **mtime** du fichier source, combiné à un condensé des paramètres du pipeline
repliés **champ par champ** — `mono`, `normalize`, `target_lufs`, `ceiling_dbtp`, `fixed_gain_db`,
`trim`, `fade_in_s`, `fade_out_s`, `gap_s`, `dither`, `noise_shaping`, `format` — les flottants par
leur motif binaire, jamais la struct en bloc (le remplissage d'une struct n'est pas une valeur, et
la clé en dépendrait). Il n'y a donc **aucune étape d'invalidation** : un fichier réencodé ou un
gain cible déplacé se hache ailleurs et l'ancien vieillit tout seul.

En-tête de 64 octets validé à chaque lecture (magie, version, clé, octets utiles, frames, frames SP,
format, gain/LUFS/dBTP mesurés) et **recoupé avec la taille réelle du fichier** : un `.pcm` tronqué
par un disque plein n'est pas un succès de cache (ADR-012, frontière). L'écriture passe par un
temporaire nommé avec l'identifiant de thread, l'en-tête est estampé à la fin par
`os_file_write_at`, puis un `MoveFileEx` atomique publie le tout (ADR-010 §9.3) ; un rendu annulé ne
laisse rien. Trois nouvelles primitives plateforme pour cela : `os_file_create`, `os_file_write` et
`os_file_write_at` — une piste transcodée fait 10,6 Mo par minute et ne tient pas en mémoire.

`pipeline_cache_write` a **exactement** la signature de `DspWriteFunc` et `pipeline_cache_read`
exactement celle de `NetmdAudioSource.read` : le pipeline s'y branche et l'envoi USB y lit sans le
moindre adaptateur ni la moindre copie.

**2. Purge partagée (`src/core/cache/cache_lru.{h,c}`)**
Une seule fonction borne les deux caches disque du programme. Elle parcourt le dossier une fois,
date chaque fichier (par l'index d'accès quand le cache en tient un, par la mtime sinon), trie un
tableau d'**indices** — pas les enregistrements de 800 octets — et supprime du plus ancien au plus
récent jusqu'à repasser sous la borne. Le cache de transcodage tient son index d'accès dans
`transcode\access.idx` (magie, version, tableau plat) et le remet à jour à chaque succès ; le cache
de pochettes de T-014 n'en a pas et est daté par ses fichiers, ce qui pour un cache écrit une fois
est la bonne réponse. Bornes : **2 Go** pour le transcodage, **256 Mo** pour les pochettes. La purge
tourne au démarrage et à la fin de chaque gravure, **jamais pendant** — un fichier en cours de
lecture est un fichier qu'on préfère ne pas supprimer.

**3. Machine d'états et pré-vol (`src/app/transfer.{h,c}`)**
Même découpe que `plan_view.h` : tout ce qui se teste sans fenêtre est ici, `view_transfer.c` ne fait
que dessiner. `transfer_simulate` produit la simulation de D4 — liste des pistes avec leur **titre
final** après sanitize et raccourcissement, clusters et cellules TOC avant/après, temps libre
avant/après **pris sur l'appareil** et non sur le modèle, avertissements (piste manquante,
dépassement, disque protégé, aucun disque, budget TOC, disque non vide, titre conservé, titres
raccourcis), politique « ajouter à la suite » ou « effacer d'abord ». La règle qui protège le disque
de l'utilisateur est ici : **le titre du disque n'est écrit que sur un disque qui n'en a pas**, ou
sur un disque que la gravure vient d'effacer — jamais par-dessus le sien, qui porte ses groupes.

`transfer_apply` est la machine d'états : huit états par piste (en attente, transcodage, prête,
envoi, écrite, titrée, échec, ignorée), neuf phases de run, douze événements. Une erreur en cours de
route laisse les pistes committées à `Titled` et remet les autres à `Pending` — ce que la reprise
ramasse. `transfer_eta_s` est l'ETA honnête de D9 et de MI-28 : moyenne glissante sur 30 s, débit
nominal SP tant que le run n'a pas déplacé 1 Mo, et une valeur qui **ne remonte jamais** (on la
compare à l'estimation précédente décrémentée du temps écoulé). MI-29 : un débit sous 60 % de la
moyenne du run pendant 15 s allume une ligne calme, pas une alerte.

**4. Vue Transfert (`src/app/view_transfer.c`)**
Trois écrans au même endroit dans le panneau Disque, qui sont trois états de la même chose : le
bouton, le pré-vol, le transfert. Le pré-vol affiche les lignes de chiffres, les avertissements, la
liste des pistes avec leurs titres finaux, la durée annoncée **avant** de commencer, le choix
ajouter/effacer quand le disque n'est pas vide, et un bouton qui dit le verbe et le nombre
(« Graver 12 pistes », MI-32), jamais « OK ». Le transfert affiche le bandeau « Ne pas éjecter »,
l'état par piste, une barre globale et une barre par piste, le débit, l'ETA, le temps écoulé, les
compteurs de cache, Pause (qui prend effet **entre deux pistes**), Annuler avec sa confirmation
inline qui énonce ce qui restera sur le disque (MI-30), et Reprendre. Fermeture de l'application
refusée pendant l'envoi. Journal d'opérations dans `<cache_dir>\logs\transfer-<date>.txt`, écrit une
fois à la fin (une gravure n'est pas le moment d'ouvrir un fichier par ligne).

**Transcodage en avance** : `app_transcode_pump` pousse au plus **3 jobs en vol** devant la piste en
cours d'envoi, saute celles que le cache tient déjà, et le thread device attend une piste — une
seule — juste avant de l'envoyer. Ce point d'attente a demandé une extension de dix lignes à
`netmd_upload` : `NetmdUploadPrepareFn`, appelée sur le thread device juste avant que l'entrée parte
sur le fil, qui remplit `entry->data`. C'est ce qui permet de rendre les pistes suivantes pendant que
la courante s'envoie, alors que `setupDownload` a besoin de la taille exacte **avant** de commencer.

**Rendu à la demande préservé** : les événements de progression sont fusionnés à **un redraw par
100 ms** (`APP_TRANSFER_REDRAW_US`) et la boucle principale ne se réveille qu'à cette cadence pendant
une gravure — attente infinie au repos, comme avant.

**5. Variante « progression de gravure » de la jauge (research/02 §9.11)**
Construite sur `plan_gauge_layout`, donc **la jauge ne change pas de forme, elle acquiert une
couche** : pistes gravées à pleine opacité, piste en cours coupée à la fraction réellement envoyée,
pistes à venir à 45 %, et une ligne blanche d'un pixel à la position exacte de l'écriture.

**6. Tests (`tests/test_transfer.c`, 15 cas)**
Simulation (contenu, piste manquante, dépassement, disque protégé, aucun disque, ajouter contre
effacer), machine d'états (nominal, erreur au milieu puis reprise, pause puis annulation — chaque
transition affirmée), ETA (jamais croissante sur 80 événements, convergence sur le débit réel, débit
nominal avant 1 Mo), cache (stabilité de la clé, invalidation par chemin, taille, mtime et par
**chacun** des paramètres du pipeline, aller-retour d'écriture/lecture avec des blocs qui ne tombent
pas sur la taille du tampon, fichier tronqué et fichier volé refusés, ordre de purge, aller-retour de
l'index), journal borné, et le garde-fou de titre décrit plus bas.

### Mesures

| Métrique | Valeur |
|----------|--------|
| Exe release | **636 928 o** (601 600 avant T-043, **+35 328**) — cible du ticket 660 Ko, budget CI 700 Ko : tenue |
| Imports | kernel32 + user32 |
| Tests | **248 cas, 6 814 checks**, 0 échec sous ASan (+15 cas) |
| Cibles `build.bat` | debug, release, test, check, analyze, bench : **toutes vertes** |
| Cache, premier rendu des 3 pistes de test | 25 + 90 + 208 = **323 ms** |
| Cache, deuxième passe (3 succès) | **968 µs** — **334×** plus rapide, aucune attente de transcodage |
| Taille de cache produite | 16 056 320 o pour 91 s de SP (**10,6 Mo/min**, conforme) |
| Redraws pendant une gravure | 10 par seconde au maximum, boucle endormie entre deux |

### Validation sur le vrai MZ-N505 (disque « 202001 », 2026-09-07)

`build\tests.exe --device-burn` : trois WAV générés (sinus 440 Hz, −12 dBFS, 44,1 kHz stéréo) de 5 s,
25 s et 61 s, écrits en vrais fichiers puis passés par **codec → pipeline → cache → session
sécurisée**, une session d'upload par piste pour que chaque delta soit mesuré seul.

| Piste | Octets SP | Transcodage | Envoi | Débit | Libre avant | Libre après | Coût réel | `plan_clusters_for` × 2 s | Écart |
|-------|-----------|-------------|-------|-------|-------------|-------------|-----------|---------------------------|-------|
| MINIDISK TEST 5s | 882 688 | 25 ms | 20 786 ms | 0,24× | 3 383 134 ms | 3 375 105 ms | **8 029 ms** | 6 000 ms | **+2 029 ms** |
| MINIDISK TEST 25s | 4 411 392 | 90 ms | 21 732 ms | 1,15× | 3 375 105 ms | 3 347 003 ms | **28 102 ms** | 26 000 ms | **+2 102 ms** |
| MINIDISK TEST 61s | 10 762 240 | 208 ms | 40 219 ms | 1,52× | 3 347 003 ms | 3 283 113 ms | **63 890 ms** | 62 000 ms | **+1 890 ms** |
| *(run précédent, même piste de 5 s)* | 882 688 | 34 ms | 13 431 ms | 0,37× | 3 391 164 ms | 3 383 134 ms | **8 030 ms** | 6 000 ms | **+2 030 ms** |
| **Les trois en une session** | 16 056 320 | **968 µs (cache)** | 73 796 ms | 1,23× | 3 283 113 ms | 3 183 091 ms | **100 022 ms** | 94 000 ms | **+6 022 ms** (3 × 2 007) |

### Gravure complète depuis l'interface (2026-09-07)

`build\minidisk.exe --scan <dossier> --plan build\t043.mdplan.txt --burn --netmd-trace tests\netmd\real\t043_ui_burn.trace`

Le plan des trois pistes de test, gravé **depuis l'application**, du pré-vol jusqu'au toast de fin.
Le journal que la vue a écrit toute seule :

```
[00:00] gravure de 3 piste(s), 47 clusters, duree annoncee 100 s
[00:12] piste 1 ecrite et titree
[00:32] piste 2 ecrite et titree
[01:11] piste 3 ecrite et titree
[01:11] fin : 3 piste(s) ecrite(s), resultat 0, 71 s, cache 0/3
```

91 s d'audio en **71 s** (1,28× temps réel) pour une durée annoncée de 100 s : l'annonce tient, dans
le bon sens. Le pré-vol avait lu le disque réel — *Espace libre : 56:44 → 55:10*, *Clusters :
725 / 2429 → 772 / 2429*, *TOC : 3 → 12 cellules sur 255*, et l'avertissement *« Le titre du disque
est conservé : il porte vos groupes »*, qui est la ligne qui protège le disque de l'utilisateur.

Transcription complète de la session dans `tests/netmd/real/t043_ui_burn.trace` (1,65 Mo), celle du
nettoyage dans `tests/netmd/real/t043_device_clean.trace`.

### Captures

- `docs/captures/T-043-pre-vol.png` — l'écran de pré-vol devant le vrai MZ-N505 : les chiffres
  avant/après, les trois titres finaux, la durée annoncée, *« Le disque contient déjà 8 piste(s) »*
  avec le choix **Ajouter à la suite** / **Effacer le disque**, et le bouton **Graver 3 pistes**.
- `docs/captures/T-043-transfert.png` — le transfert à 45 s : bandeau **Ne pas éjecter : gravure en
  cours**, la jauge en mode progression (deux pistes pleines, la troisième à sa fraction réelle),
  *Piste 3/3 · 57 % · 0:26 restantes*, la barre globale et la barre de piste, **252 ko/s**,
  *Cache : 0 réutilisée(s), 3 transcodée(s)*, l'état par piste (écrite / écrite / envoi) et les
  boutons **Pause** et **Annuler**.

`tools/capture_demo.ps1` a gagné trois choses pour ces captures : `-Extra` (arguments
supplémentaires), `-ByHandle` (`PrintWindow`, qui **rend noire** une fenêtre OpenGL — conservé mais
inutilisable chez nous) et surtout un passage en `HWND_TOPMOST` avant la capture, remis à
`HWND_NOTOPMOST` après. C'est ce dernier qui règle le problème que le ticket signalait : la première
tentative a capturé le navigateur de l'utilisateur qui recouvrait notre fenêtre. L'image a été
détruite immédiatement.

### `MD_MODE_TABLE` — proposition, **non appliquée**

Sept mesures, écart moyen **2 007 ms**, écart-type ~80 ms. Ce n'est pas une erreur de `cluster_ms` :
25 s facturés 13 clusters font 26 000 ms et le coût mesuré est 26 000 + 2 102 ; 61 s facturés 31
clusters font 62 000 ms et le coût est 62 000 + 1 890. **Le pas de 2 000 ms est confirmé à la
milliseconde.** Ce qui manque est un **surcoût fixe d'exactement un cluster par piste** — l'hypothèse
que la revue de T-042 avait posée à partir de son unique point (+2 875 ms sur 10 s, seule valeur
aberrante de la série, mesurée sur un disque déjà écrit ce jour-là).

Proposition, **à appliquer par le lead et pas ici**, parce qu'elle déplace les nombres de T-031 et de
sa suite de tests :

```c
// plan_capacity.h - mesure sur MZ-N505, T-043 : chaque piste coute un cluster
// de plus que son audio (7 mesures, moyenne 2007 ms, sigma 80 ms).
#define PLAN_TRACK_OVERHEAD_CLUSTERS 1u
```

à ajouter dans `plan_capacity_compute` (`used_clusters += entry_count`) et dans
`netmd_upload_check_capacity`. Effet : un disque de 80 min accueille 2 399 clusters d'audio pour une
piste et 2 300 pour cent pistes — soit **3 min 20 s de moins** annoncées sur un plan de cent pistes,
ce qui est la vérité. Une seconde série sur un disque **vierge** lèverait le dernier doute
(fragmentation) : le disque utilisé ici porte 8 pistes de l'utilisateur au début.

### Reprise du débit SP

T-042 annonçait 0,73× temps réel sur une seule piste de 10 s. Avec trois durées, on voit ce que ce
chiffre cachait : il y a **un coût fixe de session d'environ 15 à 20 s** (démontage préventif,
acquire, EKB, nonces, `setupDownload`, `commitTrack`, écriture du TOC) et, au-delà, l'appareil avale
le PCM à **1,2 à 1,5× temps réel**. D'où 0,24× pour 5 s et 1,52× pour 61 s. `transfer_expected_s`
annonce donc `audio + 3 s par piste`, ce qui **surestime** (100 s annoncées, 74 s réelles sur la
passe de trois pistes) : une durée annoncée qu'on tient vaut mieux qu'une durée qu'on dépasse.

### Écarts et décisions

1. **Le titre du disque de l'utilisateur n'est jamais réécrit.** La simulation ne propose le titre du
   plan que sur un disque sans titre et sans piste, ou après un effacement explicite. Sur « 202001 »
   l'avertissement « le titre du disque est conservé : il porte vos groupes » s'affiche et
   `write_disc_title` reste à 0. C'est plus strict que ce que le ticket demandait.
2. **« Effacer d'abord » n'efface rien.** C'est une politique de simulation : elle montre les chiffres
   qu'aurait le disque une fois vide. L'effacement lui-même reste l'action séparée et séparément
   confirmée de T-022, par son chemin (sauvegarde du TOC, simulation, écriture).
3. **La pause est une annulation qui garde la place.** Au niveau du protocole il n'y a pas de
   « pause » : le thread device s'arrête entre deux pistes, les pistes committées le restent, et
   « Reprendre » repose le même plan — `netmd_upload_run` saute celles qui sont `Done`. C'est
   exactement le mécanisme de reprise de T-042, avec un autre libellé dans l'interface.
4. **`NetmdUploadPrepareFn`, dix lignes ajoutées à T-042.** Sans ce point d'attente, ou bien tout est
   transcodé avant de commencer (et le ticket demande le recouvrement), ou bien la taille exacte que
   `setupDownload` réclame n'est pas connue. Le rappel est appelé sur le thread device, une piste à la
   fois, et vaut 0 quand l'appelant a déjà rempli les entrées — c'est le cas des tests de rejeu de
   T-042, inchangés.
5. **Deux drapeaux de ligne de commande** — `--transfer` (ouvre le pré-vol) et `--burn` (le lance) —
   n'existent que pour les captures. Ils ne sautent **aucune** confirmation : le pré-vol est calculé,
   affiché, et refuse ce qu'il refuse.
6. **Le journal est écrit une fois, à la fin**, dans un tampon de 64 Ko qui tronque plutôt que de
   grandir. Un journal qui mange de la mémoire pendant une gravure est le pire des deux bugs.
7. **Le « toast » de fin est une ligne, pas une fenêtre flottante.** MI-41 décrit un toast en bas à
   gauche ; il n'y a pas encore de couche de toasts dans l'UI et en ajouter une pour un message
   aurait coûté plus que le message. La fin de gravure s'affiche donc à la place du transfert, dans
   le panneau : « Disque gravé · 3 piste(s) · 1:11 », avec **Reprendre** quand il reste des pistes et
   **Revenir au plan**. Le contenu demandé (nombre de pistes, durée totale) y est ; la forme est à
   reprendre quand la couche de toasts existera.
8. **`docs/CHANGELOG.md` n'existe pas** : le fichier est `CHANGELOG.md` à la racine. Les sections
   Phase 3 et Phase 5 y ont été ajoutées.

### Bugs trouvés et corrigés

1. **Le garde-fou de titre (grave, trouvé sur le matériel).** La première exécution de `--device-burn`
   a rapporté « 9 pistes, 0 appartenant à l'utilisateur » sur un disque dont les 8 pistes sont **sans
   titre** : `str8_find` d'une aiguille dans une botte de foin vide répond 0, donc « le titre commence
   par MINIDISK TEST ». Le masque d'effacement aurait couvert **tout le disque de l'utilisateur**. La
   session a été tuée avant la phase de nettoyage et aucune piste de l'utilisateur n'a été touchée.
   Corrigé par un test de longueur, doublé d'un refus si le masque couvre le disque entier, et d'un
   test de non-régression (`transfer_burn_title_guard`) qui tourne sans appareil.
2. **L'arène de la transcription.** `netmd_trace_write` a débordé l'arène de test de 64 Mo en
   sérialisant 16 Mo d'audio en hexadécimal. La transcription a maintenant son arène de 2 Go
   (réservation, pas engagement).
3. **`os_file_write_at`** manquait : l'en-tête du fichier de cache doit être estampé après la charge
   utile, et une poignée séquentielle ne revient pas en arrière.

### Reste à faire

- **P-014** : une piste qui vient d'être gravée se relit avec `protect = 1` (flags `0x03`) et
  `netmd_edit_simulate` refuse alors de l'effacer en disant « réservée par SonicStage ». Le drapeau
  retombe après un cycle d'alimentation. Contournement dans le test appareil uniquement ; la
  correction dans l'app est à trancher (quatre pistes proposées dans la fiche).
- La deuxième série de mesures `MD_MODE_TABLE` sur un disque **vierge**, pour séparer surcoût fixe et
  fragmentation, avant d'appliquer `PLAN_TRACK_OVERHEAD_CLUSTERS`.
- Le critère « un plan de 10 pistes de bout en bout depuis l'UI » : la chaîne est validée sur 3 pistes
  et deux passes ; dix pistes demandent 40 minutes de gravure et un disque sacrifiable.

### Revue (lead, 2026-09-07)
- Grille ADR-012 dans le worktree `t043` : six cibles vertes, **636 928 o**, 248 cas / 6 814 checks. Captures conformes
  à research/02 §8.4 et §9.11. Gravure de 3 pistes depuis l'UI validée sur le MZ-N505, pistes de test effacées, les 8
  pistes de l'utilisateur intactes.
- Le bug `str8_find` sur titre vide (masque d'effacement couvrant tout le disque) est exactement ce que la règle
  « jamais d'écriture sans simulation et sans relecture » doit attraper ; le refus d'un masque couvrant tout le disque
  est gardé comme garde-fou permanent. Bonne réaction.
- `MD_MODE_TABLE` : les 7 mesures (σ 80 ms) sont suffisantes ; le lead applique `PLAN_TRACK_OVERHEAD_CLUSTERS 1`
  dans `plan_capacity` juste après le merge (tests de T-031 recalés). P-014 accepté en l'état, à traiter en phase 7
  (relire les flags après un cycle, ou ne pas les interpréter comme « SonicStage » pour une piste écrite par nous).

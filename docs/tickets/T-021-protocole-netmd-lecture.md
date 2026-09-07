# T-021 — Protocole NetMD (lecture) : trames, poll, identification, disque, pistes, titres, groupes

Phase 3 · Statut : **fait, validation device en attente** · Dépend de : T-020, research/01 §2-3, §7

## Livrables
- `src/core/netmd/netmd_proto.{h,c}` : encodage/décodage des trames (préfixe, status byte
  `0x09 accepted / 0x0A rejected / 0x0F interim / 0x08 not implemented` — valeurs AV/C correctes, **pas
  celles de netmd-js**), séquence d'échange : control `0x80` envoi → poll `0x01` (4 octets : prêt,
  bRequest de lecture, longueur) avec backoff (research/01 §2.4) → lecture `0x81` ; retry sur `interim` ;
  timeouts ; `netmd_query(fmt, ...)` / `netmd_scan(reply, fmt, ...)` à la manière de research/01 §3.1
  (formats `%b %w %d %q %s %x`), en C sans allocation (arène scratch).
- `netmd_disc.c` : `netmd_get_device_name`, `get_disc_flags` (présent, protégé, vide), `get_disc_capacity`
  (temps enregistré / total / disponible au format `hh:mm:ss:ff`), `get_track_count`, `get_track_info`
  (durée, encodage SP/LP2/LP4 + mono/stéréo, protection, titre half/full-width), `get_disc_title`
  (pagination par 255 octets), parsing des **groupes** dans le titre disque (syntaxe `0;Titre//1-4;Groupe//`,
  research/01 §3.6) vers une struct `DiscLayout { title, groups[], tracks[] }`, conversion
  half-width katakana / full-width → UTF-8 (tables **régénérées depuis Unicode**, pas copiées).
- `netmd_control.c` : play / pause / stop / next / prev / position.
- Transcriptions : `tests/netmd/mzn505_*.trace` capturées sur le vrai device via un mode
  `--netmd-trace <fichier>` du transport WinUSB (chaque control/bulk journalisé). Au minimum : disque
  vierge, disque 80 min avec 10 pistes titrées + 2 groupes, disque protégé, pas de disque.
- Vue Disque : titre du disque, groupes repliables, pistes (n°, titre, durée, badge de mode), temps
  utilisé / restant, jauge (SP/LP2/LP4 selon le mode courant du device), boutons transport.
- Tests : `tests/test_netmd_proto.c` — encodage/décodage de chaque commande contre les hex de
  research/01, parsing `hh:mm:ss:ff`, parsing des groupes (cas limites : groupe vide, piste hors groupe,
  titre avec `;` échappé), conversion des charsets ; rejeu des 4 transcriptions → `DiscLayout` attendu.

## Critères d'acceptation
- Le disque inséré dans le MZ-N505 s'affiche correctement (titres, groupes, durées, temps restant) en
  < 2 s après insertion ; éjection détectée.
- Tout le protocole est testé par rejeu sans device en CI. Exe < 290 KB. Tests, check, analyze verts.

## Livraison

Livré le 2026-09-07 sur la branche `t021`. Statut : **fait, validation device en attente**
(le MZ-N505 est toujours en ProblemCode 28 — P-001 ; la conséquence directe sur les transcriptions
est écrite en P-012).

### Ce qui a été construit

**`src/core/netmd/netmd_proto.{h,c}` — la couche trame.**
`NetmdSession { transport, vid, pid, quiet_until_us, usb_error, exchanges, last_status, batch,
open_desc }` porte le transport et la seule chose que l'hôte ait vraiment à retenir : le moment où
l'appareil pourra être resollicité (§6.2, `netmd_session_hold`). `netmd_exchange` est la séquence
de §2.4-2.7 dans l'ordre exact : poll `0x01` **avant** l'envoi, avec vidange d'une réponse orpheline
(invariant 1 de §2.5 — envoyer par-dessus désynchronise la machine à états pour tout le reste de la
session), envoi `0x80`, boucle de poll à backoff exponentiel plafonné (5 ms → 200 ms, budget
explicite par commande), lecture avec le **bRequest que `poll[1]` désigne** et non `0x81` en dur.
Les status bytes sont les valeurs AV/C de §3.2 (`0x08` NOT IMPLEMENTED, `0x09` ACCEPTED, `0x0A`
REJECTED, `0x0B` IN TRANSITION, `0x0C` IMPLEMENTED, `0x0F` INTERIM) — pas celles de netmd-js.
Un INTERIM n'est pas une réponse : la commande n'est **pas** renvoyée, on re-polle (jusqu'à 4 fois,
`100 × (2ⁿ−1)` ms). `NetmdResult` sépare le domaine (`Rejected`, `NotImplemented`) de la panne
(`Usb`, `Timeout`, `Malformed`).

Le mini-langage de §3.3 est complet : littéraux hex, espaces ignorés, `%b %w %d %q` big-endian,
`%<b %<w %<d %<q` little-endian, `%B %W` BCD, `%x %z %s` (préfixe 2 o / 1 o / 2 o + NUL), `%*`,
et en lecture seule `%?` et `%#`. Le scan **doit consommer toute la réponse** : un octet en trop
est un modèle de réponse différent, et c'est le meilleur détecteur de trame inconnue qui existe
(§3.3). Aucune allocation hors de l'arène passée en argument.

Descripteurs de §3.4 (`discTitleTD`, `audioUTOC1TD`, `audioUTOC4TD`, `audioContentsTD`, `rootTD`,
`discSubunitIdentifier`, `operatingStatusBlock`), `acquire` / `release` de §3.5. L'ouverture est
**consultative** (certains appareils rejettent l'ouverture d'un descripteur déjà ouvert), la
fermeture est obligatoire.

**`netmd_disc.{h,c}` — le disque.** `netmd_get_device_name` (§3.6 : niveau NetMD lu dans
l'`implementationProfileID` du type média `0x0301`, nom du modèle par la table de PID),
`get_disc_present` (§3.14, `status[4]` = `0x40` / `0x80`), `get_disc_flags` (§3.7.1),
`get_disc_capacity` (§3.7.3, `%?03` jamais vérifié — Panasonic répond `0803` — et **correctif
Sharp** : tant que le total dépasse 82 min de SP, les trois temps sont divisés par 2),
`get_track_count` (§3.7.2), `get_track_info` (§3.7.4 a/b/c : durée, encodage SP `0x90` / LP2 `0x92`
/ LP4 `0x93`, mono, protection `0x03`), `get_track_title` (§3.8.2, wchar `0x02`/`0x03` — **pas**
`0x00`/`0x01` ; un REJECTED est un titre vide, pas une erreur), `get_disc_title` **paginé par
255 octets** (§3.8.1, `chunk_size -= 6` au premier chunk seulement).

`netmd_parse_groups` implémente §3.10 en demi-chasse **et en pleine chasse** (`０ ； ／ －` sont
reconnus comme leurs équivalents ASCII) : découpage sur `//`, segments vides ignorés, segment sans
`;` ignoré, segment de plage `0` = titre disque, ranges **bornés par le nombre réel de pistes**
(les groupes ne sont pas réécrits quand une piste est effacée), chevauchement traité comme une
corruption (le premier groupe garde la piste), pistes hors groupe en pseudo-groupe de tête.

`netmd_read_disc` remplit un `DiscLayout { flags, capacity, title, title_full, raw_title, groups[],
tracks[] }` en **trois passes** sur les pistes (infos, titres demi-chasse, titres pleine chasse) :
une passe par descripteur, au lieu de fermer et rouvrir trois descripteurs par piste — 60 commandes
de moins sur un disque de 10 pistes, et c'est ce qui tient dans les 2 s.

**Charset (§3.11, sens lecture).** `tools/gen_charset_tables.py` émet désormais **deux** tables
depuis les mêmes données Unicode : `plan_charset.h` (écriture, T-031, 521 entrées) et
`src/core/netmd/netmd_charset.h` (**lecture, 607 entrées**, Shift-JIS → Unicode via le codec CP932
de Python). Les octets simples ne sont pas tabulés parce qu'ils sont arithmétiques : `0x00..0x7E`
ASCII, `0xA1..0xDF` katakana demi-chasse à `U+FF61 + (b − 0xA1)`. Les kanji ne le sont pas non
plus, et c'est un arbitrage assumé : les ~6 500 idéogrammes coûteraient ~26 Ko dans un exe dont
tout le budget est de 360 Ko, pour des titres illisibles par l'utilisateur visé ; une paire non
mappée devient `?`, jamais une erreur — un octet bizarre ne doit pas coûter tout le listing du
disque. Aucune table copiée d'où que ce soit (ADR-008).

**`netmd_control.{h,c}`** : play / pause / stop / next (`0x8001`) / prev (`0x0002`) / goto piste /
eject (§3.13, §3.15), `netmd_can_eject` par ctype STATUS, `get_state` (les 11 valeurs
d'`operatingStatus` de §3.14) et `get_position` (un REJECTED = « appareil arrêté », pas d'erreur).

**Thread device.** Nouvelles commandes `ReadDisc`, `Play`, `Pause`, `Stop`, `Next`, `Prev`,
`Eject` ; nouveaux événements `NetmdEvent_Disc` et `NetmdEvent_Transport`. Le `DiscLayout` fait
~70 Ko : il ne traverse **pas** l'anneau d'événements. Il est **double-bufferisé** sur l'arène du
thread (`disc[2]`, `disc_slot`, `disc_valid`), rempli dans le slot que l'UI ne lit pas, publié par
un seul store atomique ; `netmd_device_disc()` rend le slot publié. Rien n'est jamais à moitié
visible et l'UI n'attend jamais. Le « ping » de T-020 passe maintenant par `netmd_exchange` : le
test de vivacité teste le code qui compte, pas une seconde copie. Relecture automatique sur
hotplug, après une éjection réussie (le TOC est vidé, §6.3), et sur **changement de disque** :
ouvrir la trappe à la main ne produit aucun événement USB, donc le thread se réveille toutes les
2 s pour une requête de statut de 4 octets tant qu'une session est ouverte (`NETMD_DISC_WATCH_US`,
mis à 0 pour un transport de rejeu — une transcription n'a pas d'horloge). L'UI reste endormie :
seul un vrai changement pousse un événement.

**Vue Disque** (`src/app/view_device.c`) : titre du disque (ou « (disque sans titre) »), nombre de
pistes et temps utilisé / total, temps restant, mention « disque protégé en écriture », **jauge**
construite sur `plan_gauge_layout` alimentée par la capacité du **device** (`getDiscCapacity` gagne
sur notre estimation hors-ligne, Q-29 ; l'« available » de l'appareil est repris tel quel plutôt
que recalculé — un disque fragmenté a moins de place que l'arithmétique ne le dit, §7.3), barre de
transport (Lecture / Pause / Stop / Précédente / Suivante / Relire / Éjecter), puis les pistes :
hors-groupe d'abord, puis chaque groupe repliable (n°, badge de mode aux **couleurs du plan**,
titre, marque de piste protégée, durée). 19 chaînes FR + 19 EN, aucune littérale hors `strings.h`.
`app_mode_names`, `app_mode_initials` et `app_color_lighten` ont migré dans `view_library.c` pour
que les deux panneaux partagent exactement les mêmes mots et le même vert.

**`--netmd-trace <fichier>`** : `NetmdTrace` enveloppe le transport WinUSB et journalise chaque
control et chaque bulk **dans le format que `netmd_replay.c` relit** (`> hex`, `< hex`,
`! timeout`, `# commentaire`), sur une arène qui lui est propre (celle du thread est rembobinée à
chaque commande) ; l'écriture a lieu à la fermeture de la session. Le drapeau est lu comme `--plan`
et `--scan` dans `app_device_init`. Un test fait l'aller-retour : session rejouée à travers
l'écrivain, puis transcription produite rejouée à son tour.

**Transcriptions.** Les quatre cas demandés existent — `mzn505_blank.trace` (146 lignes),
`mzn505_full.trace` (548), `mzn505_protected.trace` (224), `mzn505_nodisc.trace` (24) — mais ils
sont **synthétiques**, générés trame par trame par `tools/gen_netmd_traces.py` depuis les hex et
les formats de scan de research/01, et marqués comme tels en tête de fichier. Voir P-012 pour la
raison, ce que cela teste vraiment, et la procédure de remplacement par une vraie capture.

### Mesures exactes

| Mesure | Valeur |
|--------|--------|
| Exe release | **358 400 o** (T-020 : 327 168 o, soit **+31 232 o**) — sous les 360 Ko demandés, marge 10 240 o ; gate CI 500 Ko |
| Imports | **kernel32.dll + user32.dll**, et rien d'autre (table d'import du PE relue) |
| Tests | **170 cas, 6 019 checks, 0 échec** sous ASan (T-020 : 152 / 5 824) — dont 18 cas T-021 |
| Cibles `build.bat` | debug, release, test, check, analyze, bench : **les six vertes** |
| `check` | vert : ni `windows.h`, ni `malloc`, ni `printf` dans `src/core/netmd` |
| `analyze` | vert : `cl /W4 /WX /analyze` sans avertissement, clang-tidy sans diagnostic sur `src/` |
| Lecture d'un disque de 10 pistes | **2 ms en rejeu**, **76 commandes** (24 descripteurs + 52 requêtes), budget 2 000 ms |
| Lecture sur le vrai device | **en attente de Zadig** (P-001, P-012) |
| Table charset lecture | 607 entrées × 4 o = **2 428 o** de `.rdata` |
| `DiscLayout` | ~70 Ko, ×2 (double buffer) sur l'arène de 4 Mo du thread device, **hors exe** (BSS à zéro) |
| Chaînes i18n | **19 FR + 19 EN** nouvelles (32 au total pour le panneau device) |
| Transcriptions | 4 fichiers, **942 lignes**, 100 % du protocole de lecture rejoué sans device |

### Écarts par rapport au ticket

1. **Transcriptions synthétiques et non capturées** — P-012. C'est le seul écart de fond.
   Conséquence : le critère « le disque s'affiche en < 2 s après insertion » est mesuré **en
   rejeu** (2 ms pour 76 commandes) et pas sur le matériel ; le poll réel ajoutera son propre
   temps d'attente, que seul le device peut donner.
2. **Le ticket dit « Exe < 290 KB », la commande de livraison dit 360 KB.** L'exe fait 358 400 o :
   il tient la seconde borne et pas la première. Le chiffre du ticket datait d'avant T-030..T-032
   (le plan, la jauge et le TOC ont ajouté 72 Ko à eux seuls) ; c'est la borne de 360 Ko qui a été
   tenue, avec 10 240 o de marge.
3. **Pas de poll de clôture après lecture.** `netmd-js` en émet un systématiquement ; §2.7 le dit
   « pas strictement nécessaire ». Il est omis parce que le poll **d'avant** la commande suivante
   fait déjà la resynchronisation, et parce que l'omettre retire 76 control transfers d'une
   lecture de disque. À remettre en une ligne si un appareil se désynchronise.
4. **Les kanji ne sont pas décodés** (rendus `?`) — arbitrage de taille documenté ci-dessus et
   dans le générateur. Le reste de Shift-JIS (ASCII, katakana demi-chasse, pleine chasse, kana,
   grec, cyrillique, ponctuation) l'est.
5. **`platform.h` gagne une fonction** : `os_semaphore_wait_for(sem, timeout_us)`. Sans attente
   bornée, la surveillance du changement de disque n'était possible qu'en tournant en boucle, ce
   qui contredit ADR-004. Trois lignes dans `win32_thread.c`.
6. **La vue Disque ne défile pas.** La liste des pistes est clippée par le panneau. Un disque de
   50 pistes ne tient pas à l'écran ; la virtualisation existe déjà pour le plan (T-032) et sera
   réutilisée quand T-022 ouvrira la sélection dans ce panneau.
7. **`netmd_get_position` et `netmd_get_state` ne sont pas encore affichés.** Ils sont livrés et
   testés par leur format de scan, mais aucun élément d'UI ne les lit : les montrer voudrait dire
   interroger l'appareil en continu pendant la lecture, ce qui est un choix de T-022 (curseur de
   lecture), pas une lacune de celui-ci.
8. **`acquire` / `release` ne sont pas appelés** par la lecture : §3.5 les réserve aux sessions
   d'écriture, et un `acquire` sans `release` laisse l'appareil figé sur « PC --> MD ». Ils sont
   livrés pour T-022.

### État du vrai device

```
Get-PnpDevice -PresentOnly | Where-Object InstanceId -match 'VID_054C'
FriendlyName : Net MD Walkman
InstanceId   : USB\VID_054C&PID_0084\5&A8846EA&0&1
Status       : Error          (ProblemCode 28, aucun pilote — P-001)
```

Inchangé pendant toute la durée du ticket, vérifié une dernière fois avant la validation finale.
Le chemin « pilote manquant » de T-020 reste ce que l'application affiche ; rien de T-021 ne
s'exécute sur le matériel tant que Zadig n'est pas passé.

### Revue (lead, 2026-09-07)
- Grille ADR-012 dans le worktree `t021` : `check/test/release/analyze` verts, **358 400 o** sur la base T-020,
  170 cas / 6 015 checks. Protocole entièrement couvert par rejeu ; transcriptions synthétiques assumées (P-012),
  à remplacer par des captures `--netmd-trace` dès que WinUSB est en place. Merge.

# T-070 — Régime de taille de l'exe : mesurer par module, revenir sous 500 KB sans perdre une fonction

Phase 7 · Statut : **fait** · Dépend de : P-004, P-007, T-040..T-043

## Constat
308 224 o à la fin de la phase 4, **636 928 o** à la fin de la phase 5 : décodeurs +111 KB, DSP + pipeline
+106 KB, NetMD (transport, protocole, DES, session, édition) ~90 KB, vues Transfert/Disque ~35 KB.

## Livrables
- Mesure : `build.bat map` (nouvelle cible) produit `build/minidisk.map` et un script `tools/size_report.py`
  agrège la taille par fichier objet / unité (`.text`, `.rdata` fusionné) et par préfixe de symbole
  (`codec_`, `dsp_`, `netmd_`, `ui_`, `r_`, `plan_`, `app_`) ; le rapport est commité dans le ticket.
- Leviers, chacun **mesuré séparément** et gardé seulement s'il paie sans perte de perf mesurable :
  `/O1` (ou `/Os`) sur les unités froides (protocole NetMD, édition, prefs, fichiers, dialogues, vues),
  `/O2` conservé sur le chaud (renderer, layout, DSP, décodeurs) via `#pragma optimize` par section ou
  unités séparées ; retrait de `/INCLUDE:codec_open` (T-043 lie désormais les décodeurs) ; tables
  générées au runtime là où il en reste (charset NetMD, sinc, DES : vérifier) ; chaînes FR/EN dédupliquées
  (les identiques dans les deux langues ne sont stockées qu'une fois) ; `stb_vorbis` / `minimp3` : macros
  qui retirent les chemins inutiles (`STB_VORBIS_NO_PUSHDATA_API` inverse, `MINIMP3_ONLY_MP3`,
  `MINIMP3_NO_SIMD` si le banc le permet) ; `/Gy /OPT:REF` déjà là — vérifier que les stubs ne retiennent
  rien ; `/MERGE` supplémentaires si sûrs ; alignement des sections (`/ALIGN` avait été écarté : re-mesurer
  le gain réel vs risque de chargeur, garder écarté si < 4 KB).
- Aucun retrait de fonctionnalité, aucune régression de banc > 5 % (tous les bancs relancés machine au repos).

## Critères d'acceptation
- Exe release **< 500 KB** avec les mêmes tests (249 cas) verts, imports kernel32 + user32.
- `SIZE_BUDGET_KB` redescendu à 550, rapport par module dans le ticket et dans STATUS (KPI).

---

## Livraison

**636 928 → 622 080 octets (−14 848, −2,3 %). La cible de 500 KB n'est pas atteinte**, et la raison
est mesurée, pas supposée : sur ce programme, tout ce qui rend plus de 15 KB touche du code que les
bancs couvrent, et le critère du ticket (« aucune régression de banc > 5 % ») l'interdit. Les
chiffres qui suivent disent exactement ce que coûterait chaque arbitrage, pour que le lead puisse
décider en connaissance de cause.

### 1. La mesure : `build.bat map` + `tools/size_report.py`

Nouvelle cible `build.bat map` : la release, plus `/MAP:build\minidisk.map /MAPINFO:EXPORTS`, plus le
rapport. **La carte du linker, pas `dumpbin` sur les objets** : notre code est un *unity build*, il
n'y a qu'un seul `main.obj`, donc « la taille par unité de traduction » n'existe pas côté objets — et
sous `/LTCG` `dumpbin /symbols` ne voit que des objets intermédiaires anonymes. La carte, elle, donne
le RVA final de chaque symbole (publics **et** statiques, les deux blocs sont lus) ; la taille est
l'écart jusqu'au symbole suivant dans le même bloc de section, borné par la fin du bloc — la méthode
de P-004. `.bss` est exclu : 1 517 744 octets de tampons statiques qui ne coûtent pas un octet de
fichier, et qui feraient passer `app_transfer` pour le plus gros symbole du programme.

Le script agrège par préfixe de module, rattache les statiques locaux à leur fonction englobante
(`?tabs@?1??L3_huffman@@9@9` → minimp3) et sort la table par module, le top 40 des symboles et un
`build\minidisk_size.json` que `size_report.py --diff avant.json apres.json` compare.

#### Taille par module, avant → après

| Module | avant | après | delta |
|---|---:|---:|---:|
| app (vues, panneaux, prefs, transfert) | 144 124 | 142 760 | −1 364 |
| third_party : dr_flac | 52 340 | 52 340 | 0 |
| netmd (protocole, édition, session, DES) | 52 244 | 44 664 | **−7 580** |
| third_party : stb_vorbis | 40 816 | 40 816 | 0 |
| autres / compilateur | 41 424 | 38 920 | −2 504 |
| ui | 37 764 | 37 956 | +192 |
| third_party : dr_wav | 31 088 | 31 088 | 0 |
| tags | 30 736 | 30 736 | 0 |
| plan | 33 932 | 29 400 | **−4 532** |
| platform win32 | 26 756 | 27 940 | +1 184 |
| third_party : minimp3 | 24 880 | 24 880 | 0 |
| chaînes littérales / shaders (.rdata) | 24 704 | 24 736 | +32 |
| library | 24 312 | 24 424 | +112 |
| renderer | 14 968 | 14 968 | 0 |
| codecs (glue) | 10 332 | 9 748 | −584 |
| dsp | 8 880 | 8 880 | 0 |
| pipeline | 7 840 | 8 304 | +464 |
| base | 4 024 | 5 128 | +1 104 |
| cache | 1 808 | 1 808 | 0 |
| imports (kernel32/user32) | 760 | 760 | 0 |
| **exe sur disque** | **636 928** | **622 080** | **−14 848** |

Les petits `+` sont des reports d'attribution : avec moins d'inlining, un helper qui vivait recopié
dans son appelant redevient un symbole à lui, parfois classé ailleurs. `platform win32` monte de
1 184 o dont 1 024 sont le nouveau `--selftest` (§3).

#### Les dix plus gros symboles après

| Symbole | octets | module |
|---|---:|---|
| `drflac__decode_samples_with_residual__rice__scalar` | 20 408 | dr_flac |
| `start_decoder` | 10 816 | stb_vorbis |
| `drflac_read_pcm_frames_f32` | 7 408 | dr_flac |
| `drwav_init__internal` | 6 656 | dr_wav |
| `ui_debug_overlay_build` | 6 544 | ui |
| `app_run` | 6 528 | app |
| `plan_charset_map` | 6 256 | plan (table, gardée) |
| `app_plan_gauge` | 5 856 | app |
| `app_plan_header` | 5 600 | app |
| `app_plan_panel` | 4 464 | app |

### 2. Les leviers, un par un

Chaque ligne est une release complète, mesurée seule depuis la base indiquée.

| Levier | base | résultat | delta | gardé |
|---|---:|---:|---:|---|
| `/O1` global sur notre code | 636 928 | 528 384 | −108 544 | **non** — perte de perf massive (§4) |
| `/Os` seul (sans `/O2`) | 636 928 | 679 424 | **+42 496** | non — `/Os` n'est pas un niveau, il enlève `/Og` |
| `/O2 /Os` global sur notre code | 636 928 | **491 520** | −145 408 | **non** — c'est la cible, au prix de la perf (§4) |
| `/O2 /Ob1` global | 636 928 | 569 856 | −67 072 | non |
| `/O2 /Os /Ob1` global | 636 928 | 488 448 | −148 480 | non |
| `/O1` ou `/O2 /Os` sur `third_party.c` + `third_party_vorbis.c` | 636 928 | 600 064 | −36 864 | **non** — non mesurable aujourd'hui (§4) |
| `/O2 /Ob1` sur le tiers | 636 928 | 628 736 | −8 192 | non |
| `MdCold` = `#pragma optimize("s")` sur les unités froides | 636 928 | 632 320 | −4 608 | **oui** |
| `MdCold` + `#pragma inline_depth(1)` | 632 320 | 622 080 | **−11 264** | **oui** |
| `MdCold` + `auto_inline(off)` en plus | 622 080 | 620 544 | −1 536 | non — change le code des appelants *chauds* |
| Tout `app/` (dont les vues) en `/Os` | 632 320 | 581 120 | −51 200 | **non** — code par frame |
| Les vues seules (`view_*.c`, `app.c`) en `/Os` | 632 320 | 587 776 | −44 544 | **non** — bancs « plan view frame » et « sort click » |
| Retrait de `/INCLUDE:codec_open` | 633 344 | 633 344 | **0** | **oui** (ligne morte) |
| `--selftest` ouvre un WAV | 632 320 | 633 344 | +1 024 | **oui** (§3) |
| `DR_FLAC_NO_CRC` | 633 344 | 612 352 | **−20 992** | **non** — retire une fonction (§5) |
| `MINIMP3_NO_SIMD` | 633 344 | 636 416 | **+3 072** | non — plus gros *et* plus lent |
| `/OPT:ICF=8` au lieu du défaut | 633 344 | 633 344 | **0** | non — l'éditeur de liens plie déjà tout |
| `/ALIGN:16 /FILEALIGN:16` | 633 344 | — | **ne lie pas** | non — `LNK1164` (§6) |
| Dédup FR/EN des chaînes identiques | — | — | **0** | non — déjà fait par `/GF` (§7) |
| Blob compact + offsets u16 au lieu de 2 × 234 pointeurs | — | — | ~−3 744 estimé | non (§7) |

**Ce qui est livré** : `MdCold` sur les unités froides (−15 872 au total), le retrait de
`/INCLUDE:codec_open`, et le `--selftest` qui le rend sûr (+1 024). Net : **−14 848**.

#### `MdCold` : la règle d'admission

`MdCold` bascule une plage d'`#include` du unity build en `/Os` + `inline_depth(1)` ; `MdHot` revient
au réglage de la ligne de commande. `/Os` ici, ce n'est pas « moins d'optimisation » : c'est `/O2`
moins `/Ot`, donc toujours `/Og /Oi /Ob2 /Oy`, seulement sans le déroulage ni la duplication.

La règle d'admission est volontairement stricte : **une unité n'y entre que si aucune ligne de banc
ne la traverse**, donc qu'aucune perte ne peut lui être imputée. Sont dedans :

- `netmd_models/proto/disc/control` et `netmd_secure/upload/replay/edit/backup/device` — le protocole
  attend une trame USB ; `netmd_des.c` reste en `/O2`, c'est le banc « netmd DES-CBC 8 MB » ;
- `win32_dialog.c` (ouvert par un clic), `win32_usb.c`, `win32_media.c` (le temps est dans le
  périphérique) ;
- `plan_cmd.c` (une commande par action utilisateur, annuler/refaire compris) ;
- `codec_mf.c` (enveloppe Media Foundation) ;
- `transfer.c` et `app_state.c` (une transition d'état par piste).

Sont **dehors**, et c'est ce qui plafonne le gain : `prefs.c`, `tags_*`, `lib_*`, `plan_capacity`,
`plan_file`, `plan_toc`, `netmd_des`, `win32_file`, `win32_image`, tout `ui/`, tout `r_*`, `dsp/`,
`pipeline/`, `codec_*` et **toutes les vues** (`view_*.c`, `app.c`, `plan_view.c` : les bancs
« plan view frame » et « library sort click » appellent `app_plan_panel()` et le tri de la vue
bibliothèque — ce sont donc des lignes de banc au sens du critère).

`inline_depth(1)` rend deux fois et demie ce que `/Os` seul rend sur le même périmètre : dans ces
unités, l'inlining en cascade des macros `DeferLoop` et des accesseurs recopie le même prologue des
dizaines de fois, pour du code qui attend de toute façon un périphérique.

### 3. `/INCLUDE:codec_open` retiré, et la preuve que les décodeurs sont toujours là

L'ancre valait **0 octet** : avec ou sans, l'exe fait 633 344 o. T-043 a câblé le pipeline sur les
décodeurs, `/OPT:REF` n'a donc plus rien à retirer. Elle est supprimée de `REL_LINK`.

Pour que ce soit vérifié et pas supposé, `--selftest` **ouvre un WAV** : 60 octets de RIFF/WAVE
(8 trames de PCM 16 mono à 44,1 kHz) écrits dans `%TEMP%`, ouverts par `codec_open`, relus par
`codec_read_f32_planar`, vérifiés (44 100 Hz, 1 canal, 8 trames), fermés, effacés. Si `/OPT:REF`
retirait un jour la table des décodeurs, l'étape *Smoke run* de la CI le dirait à la seconde.
Coût : 1 024 octets, assumés.

### 4. Perf : le banc ne peut pas trancher à 5 % sur cette machine

`/O2 /Os` global ramène l'exe à **491 520 octets** — la cible du ticket, sans toucher une ligne de
code. Le banc dit non :

| Ligne de banc | `/O2` | `/O2 /Os` | `/O1` |
|---|---:|---:|---:|
| `ui_layout`, 12 020 boxes (budget 1 000 µs) | 907 µs | **1 308 µs** (+44 %, hors budget) | 598 µs |
| `ui_text`, coût par glyphe | 123 ns | **181 ns** (+47 %) | 118 ns |
| `r_core batch build 10k rects` | 963 Mo/s | **844 Mo/s** (−12 %) | 1 159 Mo/s |
| `mem_copy 16 MB` | 5 031 Mo/s | **4 175 Mo/s** (−17 %) | — |

et `/O1` global, mesuré en entier, perd 15 à 41 % partout où ça compte : resampler 511 → 366 ×
temps réel, R128 735 → 472 ×, pipeline 1 489 → 1 096 ×, DES-CBC 226 → 315 ms, `codec_ogg`
306 → 260 × (la cible d'ADR-007 est 300 ×), `plan view frame` 306 → 516 µs, `tags` 1 637 → 2 262 ns
par fichier. Le critère « > 5 % = perte » est franchi de très loin ; ces deux leviers sont écartés.

**Mais il faut le dire franchement : le banc n'est pas exploitable à 5 % près en ce moment.** Trois
autres agents compilent en parallèle sur la même machine, le CPU est à 100 %, et deux exécutions du
**même binaire `/O2`** ont donné, le même après-midi : `codec_ogg` 306 × puis 182 ×, R128 735 × puis
331 ×, `jobs sum` séquentiel 9 551 µs puis 18 441 µs. Un facteur 2,2 entre deux mesures identiques.
Les écarts du tableau ci-dessus sont assez gros pour survivre à ce bruit ; **un écart de 5 % ne l'est
pas**. C'est la raison pour laquelle `/O2 /Os` sur `third_party.c` (−36 864 octets, le plus gros
levier restant) n'est **pas** retenu : quatre exécutions A/B entrelacées ont été lancées pour le
départager, les quatre se sont arrêtées sur la **première** ligne (`r_core batch build`,
1 274-1 298 µs pour un budget de 900), sur du code `/O2` intact que le ticket ne touche pas. Le même
banc rendait 705-769 µs quelques heures plus tôt. **`build.bat bench` échoue aujourd'hui pour cause
de charge machine, pas à cause de ce ticket** — `src/ui/r_core.c` n'est pas dans le diff. À relancer
machine au repos avant le merge ; le levier tiers est à reprendre à ce moment-là, il vaut 5,9 % de
l'exe.

### 5. Les boutons du code tiers

- **`DR_FLAC_NO_CRC` : −20 992 octets, refusé.** C'est le levier isolé le plus rentable du ticket
  (3,3 % de l'exe) — dr_flac inline la mise à jour du CRC dans tout son lecteur de bits, pas
  seulement dans deux tables. Mais le CRC est *la* façon dont dr_flac s'aperçoit qu'une trame FLAC
  est corrompue et se resynchronise. ADR-012 fait des décodeurs tiers la frontière de validation :
  « un flux malformé doit renvoyer une erreur ». Le désactiver, c'est décoder du bruit en silence.
  **Décision lead possible** si on juge que le CRC du conteneur suffit ; ce n'est pas à un ticket
  « aucun retrait de fonctionnalité » de la prendre.
- **`MINIMP3_NO_SIMD` : +3 072 octets.** Le MDCT scalaire est *plus gros* que la version SSE, et plus
  lent. Refusé deux fois.
- `MINIMP3_ONLY_MP3`, `DR_FLAC_NO_SIMD`, `DR_FLAC_NO_OGG`, `DR_WAV_NO_STDIO`, `DR_WAV_NO_WCHAR`,
  `STB_VORBIS_NO_PULLDATA_API`, `STB_VORBIS_NO_INTEGER_CONVERSION`, `STB_VORBIS_NO_CRT` : déjà posés
  par T-040, rien à gagner de plus.
- **Audit `/OPT:REF` sur le tiers** — ce que l'éditeur de liens garde et qui n'est atteint
  qu'indirectement : **21 956 octets, tous atteignables**. Le gros morceau est le chemin s16 de
  dr_wav (`__ima`, `__msadpcm`, `__alaw`, `__mulaw`, `__ieee`, `__pcm`, `drwav__pcm_to_s16`,
  `g_drwavAlawTable`, `g_drwavMulawTable` : ~7 KB) : `drwav_read_pcm_frames_f32` passe par lui pour
  tout ce qui n'est pas PCM linéaire, c'est donc l'import des WAV ADPCM / A-law / µ-law — une
  fonction. Le reste est le chemin de `codec_seek` (`drflac__seek_*`, `drwav_seek_*`, ~6 KB), lui
  aussi une fonction. **Rien de mort à retirer** ; les seuls retraits possibles demanderaient de
  patcher les sources vendorisées, ce que T-040 s'interdit.

### 6. Sections, `/MERGE`, `/ALIGN` : il n'y a rien à récupérer

`dumpbin /headers` sur l'exe livré : **5 sections**, `SectionAlignment` 4096, `FileAlignment` 512, et
les tailles brutes s'additionnent **exactement** à la taille du fichier :

| Section | brut |
|---|---:|
| `.text` (+ `.rdata` + `.pdata` fusionnés par `/MERGE`) | 621 568 |
| `.data` | 2 560 |
| `.idata` | 3 584 |
| `.rsrc` (manifeste PerMonitorV2) | 2 048 |
| `.reloc` | 1 536 |
| en-têtes PE | 1 024 |
| **total** | **632 320** (mesure faite avant `inline_depth`) |

Le remplissage inter-sections est donc de **moins de 1 300 octets au total** (4 sections × moins de
512). `/FILEALIGN:512` est déjà la valeur par défaut : gain 0. `/ALIGN:16 /FILEALIGN:16` **ne lie
même pas** (`LNK1164 : pour la section 0x10, l'alignement (32) est supérieur à la valeur /ALIGN`) —
et même s'il liait, le plafond arithmétique du gain est ~1,3 KB, très en dessous des 8 KB demandés,
pour un exe dont le chargeur de Windows 10/11 exige `SectionAlignment` ≥ la taille de page dès qu'il
diffère de `FileAlignment`. **Écarté, comme en P-004, cette fois avec le calcul.** Aucun `/MERGE`
supplémentaire n'est utile : `.data` doit rester inscriptible, `.rsrc` doit rester une ressource.

À noter pour la suite : **43 796 octets** (7 % de l'exe) sont des données de déroulement
(`.xdata` 26 884 + `.pdata` 16 912) que MSVC émet par fonction sur x64 malgré `/GS- /GR- /EHa-`. Il
n'existe aucune option supportée pour les supprimer, et le chargeur x64 les attend. C'est le plancher
incompressible que P-004 avait déjà vu à petite échelle (3,8 KB pour 23 KB d'exe).

### 7. Les chaînes FR/EN

- **Dédupliquer les identiques ne rend rien : c'est déjà fait.** 18 des 234 entrées sont identiques
  entre FR et EN, et `/GF` (implicite sous `/O1` comme sous `/O2`) les met déjà en commun — vérifié
  dans l'exe livré : `"ALBUM"` et `"FORMAT"` n'y apparaissent **qu'une fois**. Gain 0.
- **Le blob compact vaut ~3 744 octets et n'est pas fait.** Les deux tables sont 2 × 234 pointeurs de
  8 octets = 3 744 octets de `.rdata`, plus 468 relocations ; un blob unique avec deux tables
  d'offsets u16 (936 octets, zéro relocation) rendrait à peu près l'un et l'autre, et supprimerait au
  passage le `str8_cstr` — donc un `strlen` — de chaque libellé affiché. **0,6 % de l'exe** contre la
  réécriture intégrale du fichier le plus édité de `src/app/`, pendant que T-071 et T-072 travaillent
  dessus. Le rapport bénéfice/conflit est mauvais aujourd'hui ; à reprendre quand `strings.h` sera
  stabilisé, de préférence par un générateur (`tools/gen_strings.py`).
- `plan_charset_map` (6 256 o) et `netmd_charset_map` (2 428 o) : **gardées**. Ce sont des données de
  correspondance, pas des tables calculables ; DES et le sinc du resampler sont déjà construits au
  démarrage (`des_build_tables`, `dsp_resample`), il ne restait rien d'autre à déplacer.

### 8. Budget CI

`SIZE_BUDGET_KB` passe de **700 à 630** (622 080 o = 607,5 Ko, soit 22,5 Ko de marge). **Pas 550** :
l'exe fait 607,5 Ko, un budget de 550 mettrait la CI au rouge le jour du merge. La leçon de P-007 est
appliquée — c'est la *marge* qu'on surveille — et 22,5 Ko est une marge de régime, pas les 93 Ko de
confort d'avant. `.github/workflows/ci.yml` n'a pas eu besoin d'être touché : il lit
`SIZE_BUDGET_KB` et lance déjà `--selftest`.

### 9. Ce qui reste, et ce qu'il faut décider

Les 622 080 octets, honnêtement :

| Poste | octets | commentaire |
|---|---:|---|
| Décodeurs tiers (dr_flac, stb_vorbis, dr_wav, minimp3) | 149 124 | `/Os` rendrait 36 864, non mesurable aujourd'hui |
| `app/` — vues et panneaux, code par frame | 142 760 | `/Os` rendrait 44 544, mais deux bancs le couvrent |
| Déroulement `.xdata` + `.pdata` | 43 796 | incompressible sur x64 |
| netmd, plan, tags, library, ui, renderer, dsp, pipeline | ~200 000 | déjà froid là où c'était permis |
| Chaînes, shaders, tables de charset | ~24 700 | 3 744 récupérables (§7) |

Pour descendre sous 500 KB il faut **choisir** une de ces trois portes, aucune n'est gratuite :

1. **`/O2 /Os` global : 491 520 octets tout de suite.** Coût mesuré : layout +44 % (hors du budget de
   1 ms), texte +47 %, `mem_copy` −17 %. À re-mesurer machine au repos : le budget de layout est le
   seul vraiment menacé, les autres gardent de la marge.
2. **`/Os` sur les vues + le tiers : ~541 000 octets**, sans toucher au layout ni au renderer. Reste
   au-dessus de 500 KB.
3. **`DR_FLAC_NO_CRC` (−20 992) + `/Os` sur le tiers (−36 864) + les vues (−44 544) : ~520 000.**
   Toujours au-dessus, et on a perdu la détection de corruption FLAC.

Autrement dit, **500 KB n'est pas atteignable sans arbitrage produit** : la cible avait été posée
avant que la phase 5 n'ajoute 111 KB de décodeurs et 106 KB de DSP/pipeline. Le chiffre défendable
aujourd'hui est 622 080, et la prochaine vraie marche est le banc relancé au calme, machine au repos,
pour trancher les 36 864 octets du code tiers.

### Revue (lead, 2026-09-08)
- Six cibles vertes machine au repos (le `1 failure` vu la veille venait de la charge : trois agents compilaient), exe
  **622 080 o**, `--selftest` ouvre un WAV. Décision : **la perf prime** — `/O2 /Os` global (+44 % de layout) et
  `DR_FLAC_NO_CRC` (détection de corruption) refusés ; objectif de taille révisé à « < 630 KB, tendance à la baisse »,
  budget CI 630. Les 36 KB du code tiers en `/Os` restent à trancher sur un banc machine au repos (à faire avant le
  tag de phase 7). `tools/size_report.py` et `build.bat map` gardés comme outillage permanent.

### Arbitrage `/Os` sur le tiers (lead, 2026-09-08, machine au repos)
Banc avec budgets armés (dispersion 1,03) : `/O2 /Os` sur `third_party.c` + `third_party_vorbis.c` ne coûte
rien de mesurable aux quatre décodeurs (écarts de +0,5 à +4 % en faveur de `/Os`, dans le bruit) et rend
**36 352 o**. Levier **gardé** : `MD_TP_OPT=/O2 /Os` par défaut.

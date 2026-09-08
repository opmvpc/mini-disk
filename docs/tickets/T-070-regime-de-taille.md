# T-070 — Régime de taille de l'exe : mesurer par module, revenir sous 500 KB sans perdre une fonction

Phase 7 · Statut : **todo** · Dépend de : P-004, P-007, T-040..T-043

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

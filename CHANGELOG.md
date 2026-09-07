# Changelog

## Unreleased

### Phase 2 — Bibliothèque (T-009..T-015), terminée
Une bibliothèque qui tient 100 000 pistes dans un exe de 229 376 octets, toujours sans CRT et sans
autres imports que kernel32 et user32 :
- **Scan** (T-010) : un job par sous-dossier, allocateur bump sans verrou, pile de Treiber vers le
  thread principal, tombstones et `TrackId` stables — 50 000 fichiers en **168 ms à froid**.
- **Tags** (T-011) : ID3v1/v2.2/2.3/2.4 (unsync), Vorbis, MP4, APEv2, WAV, AIFF, en-têtes MPEG
  Xing/VBRI, écrits à la main, **deux lectures de 64 Ko par fichier**, 2,13 µs de parsing chacun,
  lus dans une seconde vague de jobs sur les seules pistes nouvelles ou modifiées.
- **Index, recherche, cache** (T-012) : tris stables par colonne sur clés normalisées, navigateur
  Artiste → Album, recherche incrémentale SSE2 sans allocation (4,09 ms sur 100 000, 0,978 ms en
  raffinement), cache binaire mappé `library.mdlib` chargé en 26,9 ms.
- **Vue bibliothèque** (T-013) : huit colonnes triables, redimensionnables et persistées, navigateur
  repliable, liste virtualisée sans aucune copie, états vide / scan / aucun résultat, préférences en
  texte écrites atomiquement, 48 chaînes FR/EN.
- **Drag & drop et pochettes** (T-014) : `IDropTarget` maison sur `ole32` dynamique (surbrillance du
  panneau pendant le survol, WM_DROPFILES en repli), `os_image_decode` par **WIC** (aucun décodeur
  dans l'exe), source embarquée > `cover|folder|front` du dossier, décodage en jobs (1,31 ms par
  pochette), cache disque `covers/<clé>.raw` mappé, **atlas de vignettes RGBA8 2048² à LRU**, colonne
  de vignettes 48 px et panneau détail repliable avec pochette 256 px.
- **DPI et overlay** (T-015), **tri des draw calls par texture** (T-009).
- Qualité : **119 cas de test / 3 344 checks** sous ASan (dont un fuzz de 220 000 mutations sur les
  parseurs de tags), `check` et `analyze` verts, 19 bancs de mesure. **46,9 ms de CPU sur 12 s au
  repos**, inchangé depuis la phase 1.

### Phase 1 — Fondations (T-001..T-008), terminée
Une application Windows autonome de 107 008 octets, sans CRT, qui n'importe que kernel32 et user32 :
- `base/` : arènes sur mémoire virtuelle réservée, scratch arenas par thread, `String8` et formatage
  maison, hash, math/SIMD, **job system** (pool de N-1 workers, file MPMC sans lock, parallel-for).
- `platform/` : fenêtre Win32 complète (DPI par moniteur v2, drop files, presse-papiers, curseurs),
  boucle d'événements à la demande, contexte OpenGL 3.3 core chargé à la main, texte DirectWrite,
  threads / sémaphores / SRW / atomiques.
- `ui/` : renderer SDF (1 shader, 1 VBO persistant, atlas skyline R8, < 10 draw calls), moteur UI
  immédiat à cœur retenu (clés hachées, piles de style, layout sémantique, 3 couches, animations),
  jeu de widgets (bouton, champ texte UTF-8, liste virtualisée 100 000 lignes, splitter, tooltip,
  menu contextuel), thème sombre en tokens, **overlay de debug F11**.
- Qualité : 70 cas de test / 1 378 checks sous ASan, `build.bat check` et `analyze`
  (cl /W4 /WX /analyze + clang-tidy) verts, 7 bancs de mesure avec seuils.
- Repos : 0 réveil et 0 message sur 12 s ; le CPU résiduel mesuré vient d'un thread du pilote GL
  (P-005, résolu par les compteurs de l'overlay).

- T-008 : `base_jobs` (pool N-1, ring MPMC de Vyukov, `jobs_dispatch`/`jobs_wait` avec entraide du
  thread principal, workers endormis sur sémaphore), primitives threads/sémaphores/SRW/atomiques
  dans `platform.h`, overlay debug F11 (fps, temps de frame, boxes, draw calls, atlas, arènes, jobs,
  DPI, réveils et messages WndProc). 910 ns par job vide à 7 workers, 4,35x sur un parallel-for.
- T-005 : texte DirectWrite → atlas R8, fallback Unicode système, caches de glyphes et de mesure,
  ellipsis, chiffres tabulaires, 4 styles de police reconstruits au changement de DPI.
- Phase 0 : recherche, analyse, ADR-001..012, CI.

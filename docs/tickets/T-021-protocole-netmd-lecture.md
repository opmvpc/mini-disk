# T-021 — Protocole NetMD (lecture) : trames, poll, identification, disque, pistes, titres, groupes

Phase 3 · Statut : **todo** · Dépend de : T-020, research/01 §2-3, §7

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

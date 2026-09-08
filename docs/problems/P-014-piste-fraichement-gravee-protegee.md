# P-014 — Une piste qui vient d'être gravée se relit `protect` et refuse l'effacement

Statut : **fermé** (2026-09-08, T-071) — bit `written_here` sur `NetmdTrack`, réapparié après
relecture ; la simulation avertit au lieu de refuser. Chaîne validée de bout en bout sur le MZ-N505
(`--device-p014`, disque « 202001 ») ; réserve honnête ci-dessous : sur cette session d'une seule
piste l'appareil a rendu `protect = 0`, donc le `0x03` lui-même n'a pas été réobservé.

## Symptôme
Après une gravure, `netmd_read_disc` renvoie les pistes qui viennent d'être écrites avec
`track.protect = 1` (les *track flags* de research/01 §3.7.4c valent `0x03`). `netmd_edit_simulate`
applique alors la règle de §7.5 — « piste réservée par SonicStage, elle refuse `eraseTrack` » — et
**refuse l'effacement sans le moindre échange USB** :

```
disc "202001": 16 track(s), 8 of them the user's, 3183 s free
erasing 8 track(s) titled "MINIDISK TEST ..." out of 16
simulation: allowed 0, refusal 3, 16 -> 16 track(s), 26 -> 26 cells, 0 write(s)
```

`refusal 3` est `NetmdEditRefusal_TrackProtected`. La sélection contenait sept pistes écrites
quelques secondes plus tôt par ce même processus, et une huitième écrite par T-042 lors d'une
session précédente, l'appareil ayant été débranché entre les deux. **Seule cette huitième a pu être
effacée** (16 → 15 pistes) : les sept fraîches ont toutes été refusées.

## Ce que cela dit
Le drapeau ne décrit pas un vrai *checkout* SonicStage. Il décrit un TOC qui est encore **en RAM
dans l'appareil** (§6.3) : tant qu'il n'a pas été rincé — éjection, extinction, débranchement — les
pistes que la session sécurisée vient d'ajouter portent `0x03`. La piste de T-042, écrite avant un
cycle d'alimentation, était retombée à `0x00`.

`netmd_secure_begin` envoie pourtant bien `setTrackProtection 00 01` juste après l'`acquire`
(§4.13, `netmd_secure.c:198`), qui est exactement ce que fait `netmdcli`. Ce n'est donc pas un appel
manquant : c'est que l'effet de cet appel n'est visible dans les *flags* relus qu'après le rinçage
du TOC.

## Conséquence produit
Graver puis effacer dans la même session est refusé par notre propre simulation, avec un message
qui parle de SonicStage — un mensonge poli pour l'utilisateur qui vient juste d'écrire ces pistes.

## Contournement en place
`tests/test_transfer.c`, chemin de nettoyage de `--device-burn` / `--device-clean` : quand la
simulation refuse avec `TrackProtected` **sur une piste que ce même processus a écrite**, la copie
du `DiscLayout` sur laquelle la simulation tourne voit son drapeau remis à zéro, et la simulation
reprend son cours normal. Rien d'autre n'est contourné : la sauvegarde du TOC, la simulation et
`netmd_edit_apply` sont les mêmes. Ce contournement est **dans le test seulement**, pas dans l'app.

## Pistes de correction (à trancher)
1. **Ne pas croire le drapeau juste après une écriture.** `netmd_device.c` sait qu'il a gravé
   (`toc_dirty`) ; il peut ignorer `protect` sur les pistes ajoutées depuis la dernière relecture
   propre. Simple, mais fait porter à la couche device une connaissance de l'historique.
2. **Distinguer les deux `0x03`.** Vérifier si une autre requête (par exemple le *content id* ou
   `getTrackFlags` après un `release`) sépare « réservée » de « TOC pas encore rincé ».
3. **Rincer avant d'effacer.** Une éjection écrit le TOC (§6.3) et remet les drapeaux à plat, mais
   sur un portable elle ouvre le capot : ce n'est pas une opération qu'on déclenche sans le dire.
4. **Le dire honnêtement.** Garder le refus mais changer la phrase : « cette piste vient d'être
   gravée, l'appareil ne la libérera qu'après éjection » — c'est la vérité et c'est actionnable.

## Ce qu'il faut pour trancher
Une seule mesure : graver une piste, la relire (`protect` attendu à 1), débrancher/rebrancher
l'appareil, relire (`protect` attendu à 0). La transcription `tests/netmd/real/t043_device_burn.trace`
contient déjà la moitié « avant ».

## Correction retenue (T-071, 2026-09-08) — piste 1 et piste 4 combinées

Ni « ne pas croire le drapeau » seul, ni « le dire honnêtement » seul : les deux, mais **sans faire
porter à la couche device une connaissance de l'historique** au sens où la fiche l'envisageait.

1. **Bit `written_here` sur `NetmdTrack`.** Il n'est pas lu sur l'appareil : il est posé par nous.
2. **`NetmdWrittenSet`** (`netmd_upload.{h,c}`). `netmd_written_add` est appelé au `commitTrack`
   avec le numéro que l'appareil vient de rendre et le titre écrit. La durée n'y est pas connue à la
   frame — l'appareil a arrondi au cluster — donc elle est laissée à 0.
3. **`netmd_written_apply`** est appelé par `netmd_device.c` après **chaque** `netmd_read_disc`,
   avant que le slot soit publié. Le réappariement se fait par **(durée à la frame, titre)**, la
   position ne servant que d'indice de départ : un déplacement, un renommage ou un effacement
   renumérotent le disque entre deux relectures, et un index qui a glissé marquerait la piste du
   voisin. La première relecture est ce qui résout la durée réelle.
4. **`netmd_edit_simulate` avertit au lieu de refuser** sur une piste `protect` **que nous avons
   écrite** : elle la compte dans `DiscDiff.written_here`, l'effacement est tenté, et le panneau
   affiche « Écrite à l'instant : drapeau levé jusqu'au prochain cycle d'alimentation. » Si
   l'appareil refuse vraiment, il répond REJECTED, et ce chemin existait déjà. Une piste `protect`
   qui n'est **pas** de nous refuse toujours, y compris mêlée aux nôtres.
5. **Le set est vidé à l'éjection** : le rinçage du TOC (§6.3) est exactement l'événement qui rend
   les drapeaux de l'appareil honnêtes à nouveau.
6. Le contournement de `tests/test_transfer.c` (chemin `--device-burn`) est **laissé tel quel** :
   il n'est plus le seul filet, mais il ne gêne pas et son retrait n'apporterait rien.

## Ce que la mesure sur l'appareil a montré

`build\tests.exe --device-p014`, MZ-N505, disque « 202001 », 2026-09-08 — une piste de 5 s gravée
dans l'espace libre puis effacée dans la même session, transcription complète dans
`tests/netmd/real/t071_device_p014.trace` (1 816 lignes) :

```
disc "202001": 8 track(s), 8 of them the user's, 3404 s free
written track 8: protect 0, written_here 1, 2560 frames
simulation: allowed 1, refusal 0, written_here 0, 9 -> 8 track(s)
erase track 8: result 0, 1 write(s)
after clean up: 8 track(s), 3404 s free, disc title "202001"
```

**`protect = 0`.** Sur cette session d'**une seule** piste, la piste fraîche est revenue sans le
drapeau : le symptôme de cette fiche ne s'est pas reproduit. Il s'était produit en T-043 sur une
session de **sept** pistes. Le `0x03` n'est donc pas systématique — ce qui est une raison de plus de
ne pas fonder un refus dessus. La mesure demandée en fin de fiche (« graver, relire, débrancher,
relire ») reste donc **à moitié faite** : la moitié « après » n'a rien à comparer tant qu'une session
d'une piste ne lève pas le drapeau. Ce n'est plus bloquant : le produit ne dépend plus de la réponse.

Ce que la passe valide de bout en bout : bit posé au commit, réappariement après relecture,
avertissement au lieu du refus, effacement appliqué, relecture à 8 pistes, titre du disque intact.

## Tests de non-régression

- `netmd_written_rematch_survives_a_move` : trois relectures — résolution de la durée, piste déplacée
  de 2 à 0, leurre de même titre et de durée différente, puis disque qui ne la contient plus.
- `netmd_edit_warns_on_a_track_we_just_wrote` : refus conservé sur une piste qui n'est pas de nous,
  avertissement et effacement autorisé sur les nôtres, refus quand les deux sont mélangées.
- `transfer_device_p014` : la passe appareil ci-dessus, sautée sans `--device-p014`.

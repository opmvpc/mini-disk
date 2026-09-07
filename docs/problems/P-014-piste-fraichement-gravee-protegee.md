# P-014 — Une piste qui vient d'être gravée se relit `protect` et refuse l'effacement

Statut : **ouvert** (2026-09-07, T-043) — contournement en place dans le test appareil, correction à
décider dans `netmd_edit.c` ou `netmd_disc.c`

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

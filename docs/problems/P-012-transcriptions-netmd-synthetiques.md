# P-012 — Les quatre transcriptions NetMD sont synthétiques : aucune capture n'a pu être faite

Statut : **en cours** (2026-09-07) — P-001 levé le jour même ; **première capture réelle faite** (`tests/netmd/real/mzn505_session1.trace`, disque « 202001 », 8 pistes, 304 commandes, tous les status à 0x09). Restent à capturer : disque vierge, disque protégé, pas de disque (nécessite de changer de disque)
Statut : **partiellement levé** (2026-09-07, T-022) — le pilote WinUSB est installé depuis, et la
**lecture** est capturée (`tests/netmd/mzn505_real_read.trace`, 617 lignes, 88 commandes, disque de
8 pistes, titre brut `0;202001//1-8;//`). Les transcriptions d'**écriture** de T-022 restent
synthétiques : voir P-013. Les quatre fichiers `mzn505_blank/full/protected/nodisc` restent générés
eux aussi, leurs scénarios (disque vierge, protégé, bac vide) n'ayant pas de disque correspondant
sous la main.

## Symptôme
Le ticket T-021 demande des transcriptions `tests/netmd/mzn505_*.trace` **capturées sur le vrai
MZ-N505**. Aucune n'a pu l'être. Au moment de la livraison :

```
PS> Get-PnpDevice -PresentOnly | Where-Object InstanceId -match 'VID_054C'
FriendlyName : Net MD Walkman
InstanceId   : USB\VID_054C&PID_0084\5&A8846EA&0&1
Status       : Error
```

Le device est branché, il est reconnu, il n'a toujours pas de pilote (P-001, ProblemCode 28).
`os_usb_open` n'a donc rien à ouvrir et le transport WinUSB n'est jamais lié : le mode
`--netmd-trace` livré par ce ticket n'écrit rien parce qu'aucune session ne s'ouvre.

## Ce qui a été fait à la place
Les quatre transcriptions sont **générées** par `tools/gen_netmd_traces.py`, trame par trame, à
partir des exemples hex et des formats de scan de `docs/research/01-netmd-protocol.md` (§2.5-2.7,
§3.4, §3.7, §3.8, §3.10, §3.14). Chaque fichier porte l'avertissement en tête :

```
# synthetic, from docs/research/01-netmd-protocol.md (see tools/gen_netmd_traces.py)
```

Aucune ligne d'aucune autre implémentation n'y entre (ADR-008). Les scénarios sont ceux du ticket :
disque vierge, disque 80 min avec 10 pistes titrées et 2 groupes, disque protégé, pas de disque.

Ce que cela teste réellement : **tout le protocole que nous écrivons**. Les requêtes émises sont
comparées octet à octet, une dérive d'un seul bit fait échouer le rejeu, et le `DiscLayout` produit
est vérifié champ par champ. Ce que cela ne teste pas : **ce que le MZ-N505 répond vraiment**. Les
réponses viennent du document de recherche, pas de la machine — un modèle qui répondrait sur un
autre gabarit (le `%?03` de Panasonic en §3.7.3 est justement ce cas) ne serait pas vu ici.

## Ce qu'il reste à faire (une session, sans code neuf)
1. Installer WinUSB sur « Net MD Walkman » avec Zadig (P-001).
2. Insérer successivement les quatre disques et lancer, pour chacun :
   `build\minidisk.exe --netmd-trace tests\netmd\mzn505_<cas>.trace`
   La session complète (poll / send / read de chaque commande) est écrite à la fermeture.
3. Relancer `build.bat test`. Les assertions du `DiscLayout` attendu sont écrites en dur dans
   `tests/test_netmd_proto.c` : elles diront tout de suite si un champ diffère de ce que le
   document de recherche annonçait, et **c'est le but**.
4. Mesurer le temps réel affiché par le panneau (`device: disc read in %u ms`) contre les 2 s du
   critère d'acceptation, et reporter dans STATUS.md.
5. Supprimer `tools/gen_netmd_traces.py`, ou le garder pour fabriquer des cas limites qu'aucun
   disque du tiroir ne présente (255 pistes, groupes qui se chevauchent, titre de 1 785 caractères).

## Pourquoi ce n'est pas bloquant pour la suite
T-022 (édition) écrit dans le TOC : lui ne peut pas se contenter du rejeu, parce qu'un `oldLen`
faux corrompt le TOC d'un vrai disque (research/01 §3.9). La lecture, elle, ne peut rien casser.
T-021 est donc livrable en l'état ; T-022 ne l'est pas sans le pilote.

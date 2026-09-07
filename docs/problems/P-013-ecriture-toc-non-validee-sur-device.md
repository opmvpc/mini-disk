# P-013 — Le chemin d'écriture du TOC n'a été validé sur aucun disque réel

Statut : **ouvert** (2026-09-07, T-022) — levable en une session de quelques minutes, avec un
disque de test dédié

## Symptôme
T-022 livre l'écriture du TOC (titres, ordre, groupes, effacement). Toute la couche est testée
**par rejeu** : simulation, budget, sauvegarde, séquence de trames, `oldLen`, `wchar`, ordre
d'écriture. Aucune de ces trames n'est jamais partie vers un vrai disque.

Contrairement à T-021, ce n'est plus le pilote qui bloque. Au moment de la livraison :

```
PS> Get-PnpDevice -PresentOnly | Where-Object InstanceId -match 'VID_054C'
FriendlyName : Net MD Walkman
InstanceId   : USB\VID_054C&PID_0084\5&A8846EA&0&1
Status       : OK
Service      : WinUSB
```

Le disque inséré est lisible et inscriptible (`getDiscFlags` = `0x10`, pas de `0x40`), la lecture
complète a été **capturée sur le matériel** (`tests/netmd/mzn505_real_read.trace`, 88 commandes,
8 pistes, titre brut `0;202001//1-8;//`). Ce qui manque est l'aller-retour d'écriture.

## Pourquoi il n'a pas été fait dans ce ticket
1. Le disque présent dans l'appareil est **un disque de l'utilisateur**, pas un disque de test
   dédié : research/01 §9.4 dit explicitement de ne jamais tester l'écriture sur autre chose
   qu'un MD bon marché réservé à ça. Un `oldLen` faux corrompt le TOC (§3.9) et l'opération n'est
   pas annulable depuis l'hôte.
2. Déclencher un renommage demande de **piloter l'interface** (double-clic sur le titre, saisie,
   Entrée, bouton « Appliquer »). L'application n'a volontairement aucun drapeau de ligne de
   commande qui écrive sur le disque : ce serait exactement le contournement de la confirmation
   que ADR-011 D4 interdit.

## Ce qu'il faut faire pour le lever
Avec un disque de test, dans l'application :

1. Insérer le disque de test, vérifier que le panneau Disque le lit (déjà validé).
2. Renommer **une** piste (F2, saisir, Entrée), vérifier le panneau de confirmation : verbe,
   avant/après, budget TOC, nombre d'écritures, mention de la sauvegarde.
3. Appliquer. Vérifier que `%LOCALAPPDATA%\minidisk\toc-backups\<disc-id>-<date>.txt` existe et
   contient l'état d'avant.
4. Vérifier la bannière « Ne pas éjecter : écriture du TOC », et que la fenêtre refuse de se
   fermer tant qu'elle est affichée.
5. Vérifier que la relecture automatique montre le nouveau titre et que la bannière retombe.
6. Renommer en sens inverse pour remettre le titre d'origine, éjecter, rebrancher, relire :
   le disque doit être exactement dans son état initial.
7. Capturer la session avec `--netmd-trace` et **remplacer** les transcriptions
   `tests/netmd/mzn505_edit_*.trace` par la capture (les tests ne changent pas, seuls les octets
   changent : c'est tout l'intérêt du format, cf. P-012).

## Risque tant qu'il est ouvert
Moyen. Les deux octets qui corrompent un TOC (`oldLen` et `wchar`) sont ceux que les
transcriptions vérifient le plus précisément, et la sauvegarde est écrite avant toute écriture,
donc un TOC perdu reste reconstructible à la main depuis le fichier texte. Ce qui reste inconnu
est le **comportement temporel** du MZ-N505 : les 100 ms entre deux éditions et les 500 ms avant
relecture (§6.2) sont des valeurs de la documentation, jamais mesurées ici.

## Lien
P-001 (pilote, résolu depuis), P-012 (transcriptions synthétiques : la lecture est maintenant
capturée, l'écriture non).

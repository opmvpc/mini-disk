# T-043 — Vue Transfert (progression, ETA, annulation, reprise) et cache de transcodage

Phase 5 · Statut : **todo** · Dépend de : T-042, ADR-011 D5/D9, research/02 §8.4, §9.11, §11

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

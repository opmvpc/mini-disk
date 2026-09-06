# R-02 — Recherche UX & features

**Projet** : `mini-disk` — application Windows native (C pur, UI OpenGL custom) pour parcourir une bibliothèque
musicale locale, composer un **plan de gravure MiniDisc** avec jauge de capacité live (SP/LP2/LP4), transcoder et
normaliser l'audio, puis piloter un enregistreur **Sony NetMD** (upload, titrage, groupes, déplacement, effacement).

**Positionnement produit** : « Web MiniDisc Pro rebâti en natif par une équipe obsédée par la qualité de vie ».
Côté bibliothèque, l'inspiration est foobar2000 / MusicBee / Swinsia / SonicStage / Roon / Plexamp.

**Date** : 2026-09-06 · **Auteur** : recherche produit / UX · **Statut** : livrable de recherche, non normatif
(les décisions figées vivront dans des ADR).

**Documents liés** :
- `docs/prompts/R-02-ux-features.md` — brief d'origine
- `docs/research/02b-design-tokens.md` — tokens de design déjà collectés (**source de vérité** pour les valeurs
  numériques de typo/espacement/palette ; ce document les référence et les étend, il ne les redéfinit pas)

---

## Table des matières

1. [Résumé exécutif](#1-résumé-exécutif)
2. [Méthode et sources](#2-méthode-et-sources)
3. [Le domaine : contraintes MiniDisc qui façonnent l'UX](#3-le-domaine--contraintes-minidisc-qui-façonnent-lux)
4. [Personas et Jobs-To-Be-Done](#4-personas-et-jobs-to-be-done)
5. [Parcours utilisateurs de référence](#5-parcours-utilisateurs-de-référence)
6. [Pain points sourcés](#6-pain-points-sourcés)
7. [Inventaire de features (MoSCoW)](#7-inventaire-de-features-moscow)
8. [Architecture de l'information et layout](#8-architecture-de-linformation-et-layout)
9. [Design de la jauge de capacité](#9-design-de-la-jauge-de-capacité)
10. [Direction visuelle](#10-direction-visuelle)
11. [Catalogue de micro-interactions](#11-catalogue-de-micro-interactions)
12. [Copywriting — 30 strings FR/EN](#12-copywriting--30-strings-fren)
13. [Matrice concurrentielle](#13-matrice-concurrentielle)
14. [Questions ouvertes et défauts recommandés](#14-questions-ouvertes-et-défauts-recommandés)
15. [Proposition de découpage MVP → v1](#15-proposition-de-découpage-mvp--v1)
16. [Sources](#16-sources)

---

## 1. Résumé exécutif

### 1.1 La thèse produit en une page

Web MiniDisc Pro a résolu le problème **technique** (parler NetMD sans SonicStage, sans OpenMG, sans DRM,
sur du matériel moderne). Il n'a pas résolu le problème **d'usage** : préparer un MiniDisc reste une activité
d'artisan qui se pratique dans trois outils à la fois — un lecteur/gestionnaire de bibliothèque pour choisir les
morceaux, un convertisseur pour fabriquer des WAV, et l'outil NetMD pour envoyer et titrer. Le triangle de
douleur est toujours le même :

1. **La sélection** se fait à l'aveugle : on ne sait pas si ça rentre avant d'avoir tout transféré.
2. **Le titrage** se fait après coup, piste par piste, à la souris, alors que les métadonnées étaient
   disponibles dès le départ dans les fichiers source.
3. **L'erreur est chère** : sur MiniDisc, une piste de trop, un mode mal choisi ou un titre trop long se paie
   en minutes de transfert (le SP est transféré à 1×) et, dans le pire des cas, en effacement du disque.

`mini-disk` fait le pari inverse de tous les outils NetMD existants : **le disque se compose entièrement à froid,
avant qu'un seul octet ne parte vers l'appareil**. L'écran principal n'est pas une liste de pistes présentes sur
le disque : c'est un **plan de gravure** — un document éditable, sauvegardable, réversible, qui se confronte en
permanence à la capacité réelle du média inséré, et qui n'est « exécuté » qu'au moment où l'utilisateur clique
sur *Graver*.

### 1.2 Les cinq décisions structurantes

| # | Décision | Pourquoi |
|---|----------|----------|
| D1 | **Le plan de disque est un document de première classe** (créable sans appareil branché, sauvegardable en `.mdplan`, versionné, réouvrable). | Découple la conception du transfert. Permet de préparer 10 disques le dimanche et de les graver dans la semaine. Répond directement au fait qu'aucun outil actuel ne permet de préparer hors ligne. |
| D2 | **La jauge de capacité est calculée en clusters, pas en secondes.** | La capacité MD s'alloue par clusters de 2 s SP ; toute piste est arrondie au cluster supérieur. Un plan « 79:58 / 80:00 » calculé linéairement peut ne pas rentrer. Aucun outil grand public ne modélise ça. |
| D3 | **Le titrage est dérivé automatiquement des tags, avec un budget TOC visible.** | La zone de titres du TOC est une ressource finie et partagée (≈ 1 785 caractères latins). L'utilisateur doit la voir se remplir comme il voit la capacité audio se remplir. |
| D4 | **Rien n'est destructif sans simulation préalable.** Tout ordre envoyé au device passe par un plan de diff « avant / après » explicite. | Les rapports de TOC corrompu, de disque devenu illisible après édition de titres et de « wipe » raté sont récurrents (WMD #93, #41, #36 ; EWMD #33, #54). |
| D5 | **Le transcodage est une étape visible, inspectable et cachée sur disque.** | Le transcodage ATRAC est lent et non déterministe selon l'encodeur ; le cacher permet de re-graver un même plan sur un 2ᵉ disque en quelques secondes. |

### 1.3 Ce qui doit être « obsessionnellement » meilleur

- **Zéro attente aveugle** : à tout moment on sait ce qui reste à faire, en minutes réelles, et on peut annuler.
- **Le clavier suffit** : sélectionner, ajouter au plan, changer de mode, réordonner, titrer — sans souris.
- **Rien ne se perd** : plan auto-sauvegardé, cache de transcodage persistant, journal des opérations device.
- **Densité** : 22–24 px de hauteur de ligne, chiffres tabulaires, pas de carte, pas de padding décoratif.
- **Honnêteté** : quand l'appareil est lent, on le dit ; quand la qualité LP4 est mauvaise, on le dit ; quand
  une opération est irréversible, on le dit avant, pas après.

---

## 2. Méthode et sources

### 2.1 Ce qui a été dépouillé

- **Web MiniDisc Pro** (`asivery/webminidisc`) : README + 85 issues (ouvertes et fermées) parcourues via l'API
  GitHub. C'est le corpus le plus riche : chaque issue est un pain point utilisateur documenté avec le modèle
  d'appareil, l'OS et la manip exacte.
- **ElectronWMD** (`asivery/ElectronWMD`) : 30 issues. Corpus complémentaire, très centré sur les problèmes
  d'empaquetage natif, d'encodeur local (`at3tool`) et d'accès USB — exactement notre terrain.
- **Documentation d'écosystème** : minidisc.wiki, minidisc.org, Sony Insider (contexte matériel, Type-R/Type-S,
  vitesses de transfert, structure du TOC).
- **Design systems** : dépouillés dans `02b-design-tokens.md` (VS Code, Carbon, Fluent 2, Primer, Radix, Ant).
- **Gestionnaires de bibliothèque** : analyse comparative des modèles d'interaction de foobar2000, MusicBee,
  Swinsia, SonicStage 4.3, Roon, Plexamp (voir §13).

### 2.2 Limites de la recherche

- Pas d'entretien utilisateur mené : les personas de §4 sont des **archétypes inférés** du corpus d'issues et
  des usages observés, pas des synthèses d'entretiens. Ils sont à valider par 5–8 entretiens r/minidisc.
- Reddit n'est pas accessible au crawler : les pain points « communautaires » sont donc sous-représentés par
  rapport aux pain points « techniques » remontés en issues GitHub. Biais probable : **on sur-représente les
  bugs et sous-représente les frictions d'ergonomie pure**, qui ne donnent pas lieu à des tickets.
- Aucune donnée quantitative (volumétrie d'usage, taux d'échec de transfert) n'est disponible publiquement.

### 2.3 Convention de notation

- `WMD #n` = issue n° n du dépôt `asivery/webminidisc`.
- `EWMD #n` = issue n° n du dépôt `asivery/ElectronWMD`.
- **[Inféré]** = déduction de l'auteur, non directement sourcée.

---

## 3. Le domaine : contraintes MiniDisc qui façonnent l'UX

Cette section n'est pas de la culture générale : **chaque contrainte ci-dessous se traduit par un élément
d'interface obligatoire**. Un designer qui ignore le cluster dessinera une jauge fausse.

### 3.1 Capacité et modes

| Mode | Codec | Débit | Durée sur MD 80 min | Durée sur MD 74 min | Granularité d'allocation |
|------|-------|-------|---------------------|---------------------|--------------------------|
| **SP** (stéréo) | ATRAC (SP) | ~292 kbps | 80:00 | 74:00 | 2 s |
| **SP Mono** | ATRAC mono | ~146 kbps | 160:00 | 148:00 | 4 s |
| **LP2** | ATRAC3 | 132 kbps | 160:00 | 148:00 | 4 s |
| **LP4** | ATRAC3 (joint stereo) | 66 kbps | 320:00 | 296:00 | 8 s |

**Conséquence UX n° 1 — l'arrondi au cluster.** Le disque s'alloue par *clusters*. Un cluster contient
2 secondes d'audio SP. En LP2 un cluster porte 4 s, en LP4 8 s. Toute piste consomme un **nombre entier de
clusters** : une piste de 3:01 en LP4 occupe la place de 3:04. Sur un disque de 20 pistes courtes en LP4, le
gaspillage cumulé peut atteindre **plus de 2 minutes**. Une jauge qui additionne bêtement les durées ment.

→ *Exigence UI* : la jauge affiche la **durée facturée** (arrondie) et signale visuellement le padding.

**Conséquence UX n° 2 — les modes sont mixables.** Un même disque peut contenir des pistes SP et des pistes
LP2/LP4. La jauge doit donc être **segmentée par piste avec la couleur du mode**, et non être une barre
monochrome avec un mode global.

**Conséquence UX n° 3 — le « temps restant » est ambigu.** « Il reste 12 minutes » n'a pas de sens seul : il
reste 12 min *en SP*, 24 min *en LP2*, 48 min *en LP4*. L'affichage résiduel doit être **tri-modal**.

### 3.2 Le TOC (Table Of Contents) et le budget de titres

- Le TOC contient jusqu'à **254 pistes**.
- Les titres (titre du disque + titres de pistes) partagent une **zone unique de cellules de 7 caractères**.
  En pratique on dispose de **≈ 1 785 caractères latins** pour l'ensemble du disque. Les caractères pleine
  largeur (japonais) vivent dans une zone séparée, plus petite encore.
- Il n'y a **pas d'artiste séparé** : le format natif ne connaît qu'une chaîne par piste. La convention
  universelle est `Artiste - Titre`, ce qui consomme beaucoup de budget.

**Conséquence UX** : le titrage automatique doit proposer des **gabarits** (`%artist% - %title%`,
`%title%`, `%tracknumber%. %title%`) et afficher **en direct** le budget consommé, avec une stratégie de
repli automatique (troncature intelligente, abandon de l'artiste sur les albums mono-artiste) quand ça déborde.

### 3.3 Les groupes n'existent pas vraiment

Les « groupes » MiniDisc sont un **hack de Sony encodé dans le titre du disque** :

```
0;Nom du disque//1-5;Groupe A//6-11;Groupe B//
```

Ils consomment donc le même budget que les titres, ils imposent que les pistes d'un groupe soient
**contiguës**, et ils sont invisibles sur les appareils antérieurs à leur introduction.

**Conséquences UX** :
- Grouper doit **réordonner** implicitement (ou refuser et expliquer).
- Le coût en caractères d'un groupe doit être visible.
- Un avertissement « votre appareil cible n'affiche pas les groupes » est utile si le modèle est connu.
- Bug documenté : les titres peuvent s'afficher « au format groupe » alors qu'aucun groupe n'existe (WMD #43).

### 3.4 Le transfert : asymétrique, lent, et pas toujours du transcodage

- **NetMD est unidirectionnel** en usage normal : PC → MD. La récupération MD → PC n'est native que sur
  **Sony MZ-RH1 / MZ-M200**. Web MiniDisc Pro ajoute un « mode homebrew / factory » qui permet l'extraction
  sur d'autres Sony et Aiwa, avec dump firmware/RAM et manipulation de TOC.
- **LP2 et LP4 se transfèrent « en direct »** : le PC envoie de l'ATRAC3 déjà encodé, l'appareil l'écrit tel
  quel. D'où des vitesses élevées (jusqu'à 32× en LP4 sur certains modèles).
- **SP ne se transfère pas** : le PC envoie du PCM, l'appareil l'encode **en temps réel** avec son DSP.
  D'où une vitesse plafonnée autour de **1×** sur la majorité des appareils. Graver 80 min en SP prend
  80 minutes. C'est *le* fait le plus structurant pour l'UX de transfert.
- La qualité SP dépend du DSP de l'appareil (**Type-R** = meilleur encodeur SP). La qualité de lecture LP
  dépend du décodeur (**Type-S**).

**Conséquences UX** :
- Le sélecteur de mode doit afficher **une estimation de durée de gravure**, pas seulement une durée audio.
  « SP · 74:12 de musique · ~76 min de gravure » vs « LP2 · 74:12 · ~11 min de gravure ».
- Une gravure longue doit survivre à la mise en veille, afficher une progression fiable, et être
  **annulable proprement**.
- Le mode homebrew doit être **opt-in explicite**, derrière un avertissement, jamais dans le flux principal.

### 3.5 Qualité d'encodage ATRAC3

Trois familles d'encodeurs coexistent dans l'écosystème :

| Encodeur | Nature | Qualité | Notes |
|----------|--------|---------|-------|
| `atracdenc` | Open source, WASM/natif | Correcte en LP2, faible en LP4 | Encodeur par défaut de WMD |
| `at3tool` | Binaire Sony (via Wine / VM) | Référence | EWMD #53 : introuvable sur Debian ; friction d'installation |
| ATRAC3VM / A3+ | Encodeur Sony original exécuté en VM | Référence | WMD #88, #89 ; EWMD #49 signale une bosse de +6 dB vers 160 Hz |

**Conséquence UX** : le choix d'encodeur est une **préférence de qualité**, pas un réglage d'expert caché.
Il faut un sélecteur à 3 crans (*Rapide / Équilibré / Meilleure qualité*) avec une explication d'une ligne,
et un mode expert derrière un dépliant.

### 3.6 Ce que le matériel rend impossible

À dire clairement dans l'UI pour éviter les faux espoirs :

- Impossible de **remplacer** une piste au milieu du disque sans réécrire ce qui suit (pas de random write).
- Impossible de **changer le mode** d'une piste déjà gravée sans la re-graver.
- Impossible de récupérer l'audio d'un disque sans RH1 ou mode homebrew.
- L'effacement d'une piste laisse le disque **fragmenté** ; certains appareils exposent un « défragmenter ».
- Une coupure d'alimentation pendant l'écriture du TOC peut rendre le disque illisible.

---

## 4. Personas et Jobs-To-Be-Done

### 4.1 Vue d'ensemble

| Persona | Part estimée | Volume | Sensibilité prix | Sensibilité qualité audio | Tolérance à la complexité |
|---------|-------------|--------|------------------|---------------------------|---------------------------|
| P1 — Léa, la revivaliste | 40 % | 1–2 disques / semaine | Haute | Moyenne | **Basse** |
| P2 — Marc, l'archiviste | 20 % | 5–20 disques / mois | Basse | **Très haute** | Haute |
| P3 — Yuki, la mixtapeuse | 20 % | 2–4 disques / mois | Moyenne | Moyenne | Moyenne |
| P4 — Tomasz, le hacker matériel | 10 % | variable | Basse | Haute | **Très haute** |
| P5 — Claire, la DJ / preneuse de son | 10 % | 1–5 disques / semaine | Moyenne | Haute | Moyenne |

### 4.2 P1 — Léa, 27 ans, revivaliste

**Contexte.** A acheté un MZ-N710 sur Vinted pour 45 €. Bibliothèque de 4 000 fichiers, moitié FLAC issus de
Bandcamp, moitié MP3 hérités. Windows 11, un seul PC. N'a jamais utilisé SonicStage.

**Ce qu'elle veut.** Que le disque qu'elle emmène en voiture ressemble à la playlist qu'elle a dans la tête.
Que ce soit joli sur l'écran du baladeur.

**Ce qui la bloque aujourd'hui.** L'installation du pilote via Zadig (droits admin, peur de casser l'USB),
le fait qu'elle ne sait pas ce que veulent dire SP / LP2 / LP4, et le titrage manuel.

**Citation composite.** « J'ai passé plus de temps à taper les noms des chansons qu'à les écouter. »

**Critère de succès.** Premier disque gravé et titré en moins de 20 minutes après installation, sans lire de doc.

**JTBD**

- **JTBD-1** — *Quand* je découvre un album que j'aime, *je veux* le mettre sur un MiniDisc en quelques clics,
  *afin de* l'écouter dans ma voiture sans mon téléphone.
- **JTBD-2** — *Quand* je branche mon baladeur, *je veux* que ça marche du premier coup,
  *afin de* ne pas avoir à chercher un tutoriel.
- **JTBD-3** — *Quand* le disque est plein, *je veux* comprendre quoi enlever ou comment gagner de la place,
  *afin de* ne pas devoir tout recommencer.

### 4.3 P2 — Marc, 44 ans, archiviste

**Contexte.** 180 MiniDiscs enregistrés entre 1998 et 2006 (concerts, radio, démos de son groupe). Possède un
MZ-RH1 acheté cher précisément pour l'extraction. Bibliothèque foobar2000 de 90 000 fichiers, tags
maniaquement propres, ReplayGain calculé partout.

**Ce qu'il veut.** Numériser sans perte de titres, et re-graver des compilations en SP Type-R pour rouler avec.

**Ce qui le bloque.** Les extractions qui échouent ou sortent trop rapides (WMD #83), l'absence d'export /
import de métadonnées fiable (WMD #78 : le CSV casse dès qu'un titre contient une virgule), et le fait
qu'aucun outil ne conserve un **journal** de ce qui a été fait sur quel disque.

**Citation composite.** « Je veux un inventaire de mes 180 disques, pas un utilitaire de transfert. »

**Critère de succès.** Pouvoir répondre à « sur quel disque est le concert de 2003 ? » sans se lever.

**JTBD**

- **JTBD-4** — *Quand* j'extrais un disque, *je veux* récupérer l'audio **et** les titres **et** les
  timestamps, *afin de* ne jamais refaire la manipulation.
- **JTBD-5** — *Quand* je grave, *je veux* garantir le meilleur encodeur disponible et le vérifier,
  *afin de* ne pas dégrader une source unique.
- **JTBD-6** — *Quand* je gère 180 disques, *je veux* un catalogue de disques cherchable hors ligne,
  *afin de* retrouver un enregistrement en 10 secondes.
- **JTBD-7** — *Quand* une opération sur le TOC est risquée, *je veux* une sauvegarde du TOC avant,
  *afin de* pouvoir revenir en arrière.

### 4.4 P3 — Yuki, 22 ans, mixtapeuse

**Contexte.** Fait des MiniDiscs comme cadeaux. Se soucie énormément de l'ordre des morceaux, des transitions,
et de l'objet (elle imprime des jaquettes). Ordinateur portable modeste.

**Ce qu'elle veut.** Composer une séquence, l'écouter en enchaînement, tenir dans 80 minutes pile, et imprimer
une jaquette qui correspond exactement au disque.

**Ce qui la bloque.** Impossible de préparer un disque sans l'appareil branché. Aucun outil ne fait de
prévisualisation d'enchaînement. Réordonner après transfert coûte des minutes.

**Citation composite.** « Je veux voir la face A et la face B, même si le MiniDisc n'a pas de faces. »

**JTBD**

- **JTBD-8** — *Quand* je compose une mixtape, *je veux* réordonner librement et écouter les transitions,
  *afin de* valider le flow avant de graver.
- **JTBD-9** — *Quand* j'ai fini, *je veux* exporter une jaquette / une liste imprimable,
  *afin de* fabriquer l'objet.
- **JTBD-10** — *Quand* je dépasse de 3 minutes, *je veux* des suggestions concrètes,
  *afin de* ne pas arbitrer à l'aveugle.

### 4.5 P4 — Tomasz, 35 ans, hacker matériel

**Contexte.** Répare et modifie des appareils MD. Utilise le mode factory de WMD, dump des firmwares, teste des
appareils exotiques (Kenwood, Sharp, Aiwa, Panasonic).

**Ce qu'il veut.** Un accès bas niveau, des logs, et des messages d'erreur qui contiennent le code USB réel.

**Ce qui le bloque.** Les erreurs opaques (`LIBUSB_ERROR_NOT_SUPPORTED`, EWMD #43 ; « Oops… Something
unexpected happened », WMD #41) qui ne disent pas quelle commande a échoué.

**JTBD**

- **JTBD-11** — *Quand* une opération échoue, *je veux* la commande NetMD, ses arguments et le code retour,
  *afin de* diagnostiquer ou rapporter un bug utile.
- **JTBD-12** — *Quand* je teste un appareil inconnu, *je veux* pouvoir forcer les capacités détectées,
  *afin de* ne pas être bloqué par une base de données incomplète.

### 4.6 P5 — Claire, 31 ans, preneuse de son / DJ

**Contexte.** Utilise le MiniDisc comme support de diffusion : jingles, bandes-son de spectacle, sets.
Contraintes : fiabilité, marqueurs de piste au bon endroit, niveaux homogènes.

**Ce qu'elle veut.** Que la piste 7 démarre exactement au bon échantillon, et que tout le disque ait le même
niveau perçu.

**Ce qui la bloque.** L'absence de normalisation dans les outils NetMD, l'absence de contrôle du silence
inter-pistes, et les « skips » d'enregistrement (EWMD #44).

**JTBD**

- **JTBD-13** — *Quand* je prépare un disque de diffusion, *je veux* une normalisation loudness cohérente
  (EBU R128), *afin de* ne pas toucher au fader entre deux pistes.
- **JTBD-14** — *Quand* je grave un enregistrement continu, *je veux* placer moi-même les marqueurs de piste,
  *afin de* pouvoir sauter au bon endroit en direct.

### 4.7 Anti-persona

**Ne pas concevoir pour** : l'utilisateur qui veut streamer depuis Spotify vers un MiniDisc, l'utilisateur qui
veut une application mobile, l'utilisateur qui cherche un lecteur audio principal. `mini-disk` **n'est pas**
un remplaçant de foobar2000 : il est un **atelier de gravure** qui sait lire une bibliothèque.

---

## 5. Parcours utilisateurs de référence

### 5.1 Parcours A — « Premier disque » (P1, découverte)

| Étape | Attendu | Coût cible |
|-------|---------|-----------|
| A1 | Lancer l'app, écran d'accueil : « Ajoutez un dossier de musique ». | 1 clic |
| A2 | Indexation en tâche de fond, la bibliothèque est utilisable immédiatement (indexation incrémentale, affichage progressif). | 0 |
| A3 | Bandeau non bloquant : « Aucun appareil détecté — vous pouvez composer un disque quand même. » avec un lien *Configurer mon appareil*. | 0 |
| A4 | Sélection d'un album → **Entrée** ou glisser → le plan se remplit. | 1 action |
| A5 | La jauge annonce : « 47:12 / 80:00 en SP ». Titres auto-générés visibles. | 0 |
| A6 | Bouton *Graver* → assistant de branchement si aucun appareil (guide Zadig intégré, détection auto après installation). | 1 clic |
| A7 | Gravure avec progression, temps restant, et estimation « fin vers 15h42 ». | 0 |
| A8 | Fin : notification, résumé, proposition d'ajouter le disque au catalogue avec un nom. | 1 clic |

**Objectif : disque gravé en ≤ 6 interactions utilisateur significatives** (hors installation pilote).

### 5.2 Parcours B — « Compilation optimisée » (P3, cœur de cible)

1. Ouvrir un nouveau plan (`Ctrl+N`), choisir le type de média (80 min).
2. Filtrer la bibliothèque (`Ctrl+F`, recherche incrémentale), ajouter 22 morceaux.
3. La jauge passe au rouge : `84:31 / 80:00 · dépassement 4:31`.
4. Cliquer sur la zone de dépassement → panneau **Résoudre** :
   - *Passer les 4 pistes les plus longues en LP2* → gain 21:14
   - *Passer tout le disque en LP2* → gain 42:15 (et gravure 8× plus rapide)
   - *Retirer les 2 dernières pistes* → gain 8:02
   - *Rogner les silences de fin* → gain 0:38
5. Choisir, réordonner par glisser-déposer, écouter les transitions (`Espace` sur une jonction).
6. `Ctrl+G` pour créer un groupe sur la sélection.
7. `Ctrl+Entrée` → gravure.

### 5.3 Parcours C — « Extraction d'archive » (P2)

1. Insérer un disque, l'appareil est reconnu comme **MZ-RH1**, la barre d'appareil affiche
   `MZ-RH1 · 80 min · 18 pistes · 2:14 libres · Extraction native disponible`.
2. Onglet **Disque** : liste des pistes lues depuis le TOC.
3. `Ctrl+A`, *Extraire vers…* → choix du format (WAV / FLAC), du nommage
   (`%discname%/%tracknumber% - %title%`), et de la destination.
4. Sauvegarde automatique du TOC dans le catalogue avant toute opération.
5. Extraction avec journal, vérification de durée piste par piste (détection du bug « lecture trop rapide »,
   WMD #83, par comparaison durée TOC vs durée du fichier produit).
6. Le disque est ajouté au **catalogue de disques** avec sa photo (webcam / fichier), son emplacement physique
   (« boîte 3, rangée B ») et ses titres, cherchable hors ligne.

### 5.4 Parcours D — « Réparation / entretien » (P2, P4)

1. Le disque a été enregistré partie sur l'appareil, partie via PC → titres incohérents.
2. Onglet **Disque** → *Réconcilier les titres* : mise en correspondance des pistes du disque avec la
   bibliothèque locale par durée + empreinte, proposition de titres.
3. Prévisualisation **diff** : `12 titres modifiés, 3 inchangés, budget TOC 62 % → 71 %`.
4. Application : une seule écriture de TOC, pas 12.

---

## 6. Pain points sourcés

> **Lecture** : `G` = gravité (1 mineur → 5 bloquant) · `F` = fréquence estimée · *Réponse produit* = ce que
> `mini-disk` fait pour le traiter.

### 6.1 Tableau de synthèse

| # | Pain point | Source | G | F | Persona |
|---|-----------|--------|---|---|---------|
| PP-01 | Installation du pilote USB via Zadig, droits admin requis | WMD README | 5 | Haute | P1 |
| PP-02 | Appareil non reconnu, aucun diagnostic exploitable | WMD #104, #34, #28 | 5 | Haute | P1, P4 |
| PP-03 | Erreurs USB opaques (`LIBUSB_ERROR_NOT_SUPPORTED`, `ACCESS`) | EWMD #43, #32 | 4 | Moyenne | P4 |
| PP-04 | Message d'erreur générique « Oops… Something unexpected happened » | WMD #41 | 4 | Moyenne | tous |
| PP-05 | Upload échoue alors qu'il reste de la place sur le disque | WMD #100 | 5 | Moyenne | P1 |
| PP-06 | Les MP3 VBR font planter l'UI et rendent le disque inaccessible | WMD #102 | 5 | Moyenne | P1 |
| PP-07 | Transcodage bloqué à 0 % sans explication | WMD #85, #26 ; EWMD #42 | 4 | Haute | P1 |
| PP-08 | Encodeur local introuvable / chemins cassés (`at3tool`, `output.wav`) | EWMD #53, #55, #27 | 4 | Moyenne | P2 |
| PP-09 | Upload SP bloqué à 0 % (wireformat non résolu) | WMD #99, #101 | 5 | Basse | P2 |
| PP-10 | Pas d'import de playlist (M3U / XSPF) — sélection manuelle piste par piste | WMD #84, #44 ; EWMD #30 | 3 | **Très haute** | P1, P3 |
| PP-11 | Pas de menu contextuel ni de raccourcis clavier pour renommer | WMD #42 | 3 | Très haute | tous |
| PP-12 | Pas d'affichage du temps restant selon le codec choisi | WMD #80 | 4 | **Très haute** | tous |
| PP-13 | Titres affichés « au format groupe » sans groupes existants | WMD #43 | 3 | Basse | P3 |
| PP-14 | Caractères spéciaux corrompus dans les titres (umlauts, `/`) | WMD #38 | 4 | Moyenne | P1 |
| PP-15 | Numéros de piste parasites en tête de titre | WMD #92 | 2 | Haute | tous |
| PP-16 | Reconnaissance de titres pleine largeur incomplète | WMD #94, #37 | 3 | Basse | — |
| PP-17 | Export CSV cassé par les virgules dans les titres | WMD #78 | 3 | Basse | P2 |
| PP-18 | TOC corrompu / disque illisible après édition de titres | WMD #93 | 5 | Basse | P2 |
| PP-19 | Corruption après mélange d'uploads PC et d'enregistrements appareil | EWMD #33 | 5 | Basse | P2 |
| PP-20 | Réorganisation de pistes qui rend une piste injouable | EWMD #54 | 5 | Basse | P3 |
| PP-21 | Wipe du disque qui échoue selon le modèle | WMD #36, #41 | 4 | Basse | P4 |
| PP-22 | Disques enregistrés via SonicStage marqués à tort « protégés » | WMD #77 | 4 | Moyenne | P2 |
| PP-23 | Extractions dont le tempo est accéléré | WMD #83 | 4 | Basse | P2 |
| PP-24 | Uploads AEA silencieux / corrompus sans détection | WMD #95 ; EWMD #52 | 4 | Basse | P2 |
| PP-25 | Blocage « Reading metadata » sur permission de fichier | WMD #25 | 3 | Moyenne | P1 |
| PP-26 | Échec d'application des métadonnées selon les tags ID3 | WMD #32 | 3 | Moyenne | P1 |
| PP-27 | Fréquence d'échantillonnage d'enregistrement erronée (48 kHz) | WMD #87 | 3 | Basse | P5 |
| PP-28 | Coloration de l'encodeur (bosse de +6 dB vers 160 Hz) | EWMD #49 | 3 | Moyenne | P2 |
| PP-29 | Détection de disque après reconnexion, TOC non rechargé | EWMD #48 | 4 | Haute | tous |
| PP-30 | Accessibilité : boutons sans libellé pour lecteur d'écran | WMD #98 | 3 | Basse | — |
| PP-31 | Titres non lus par les appareils anciens (encodage) | EWMD #36 | 3 | Basse | P2 |
| PP-32 | Freeze sur transferts longs (> 28 min) sans dialogue de sauvegarde | EWMD #47 | 4 | Basse | P2 |
| PP-33 | Impossible de préparer un disque sans appareil branché | [Inféré, architecture WMD] | 4 | **Très haute** | P3 |
| PP-34 | Pas de cache de transcodage : re-graver le même contenu = tout refaire | [Inféré] | 4 | Haute | P3 |
| PP-35 | Aucun catalogue des disques déjà gravés | [Inféré, JTBD-6] | 3 | Haute | P2 |

### 6.2 Les dix plus structurants, détaillés

#### PP-12 — On ne sait pas ce qui rentre (WMD #80)

*Symptôme.* L'utilisateur choisit ses morceaux sans savoir si le disque va déborder ; le mode de codec n'est
choisi qu'au moment de l'upload, donc le calcul mental doit être fait à la main.

*Racine.* L'architecture « liste du disque + bouton upload » ne modélise pas d'état intermédiaire.

*Réponse produit.* La jauge de capacité (§9) est **l'élément central permanent** de l'interface, calculée en
clusters, tri-modale, avec segments par piste. Le mode est une propriété **par piste**, modifiable à tout
moment avant gravure.

*Métrique.* Taux de gravures qui débordent (doit être ≈ 0) ; temps entre l'ajout d'une piste et la mise à jour
de la jauge (< 16 ms).

#### PP-10 — Pas d'import de playlist (WMD #84, #44 ; EWMD #30)

*Symptôme.* Trois issues indépendantes sur deux dépôts demandent la même chose : importer un M3U/XSPF.

*Racine.* L'outil est un pilote de périphérique, pas un outil de bibliothèque. Il ne connaît pas les fichiers,
seulement ceux qu'on lui donne un par un.

*Réponse produit.* Le pilier « bibliothèque » : indexation d'arborescence, recherche, tri, vues par
album/artiste/dossier/playlist, import M3U/M3U8/PLS/XSPF/CUE, et **glisser-déposer depuis l'Explorateur
Windows et depuis foobar2000/MusicBee**.

#### PP-11 — Ni menu contextuel ni raccourcis (WMD #42)

*Symptôme.* Renommer 20 pistes demande 20 séquences souris.

*Réponse produit.* Modèle d'édition type tableur : `F2` renomme la cellule, `Entrée` valide et passe à la
suivante, `Tab` change de colonne, la sélection multiple permet le **renommage par motif** et le
**rechercher/remplacer** sur la sélection. Menu contextuel complet, cohérent avec la palette de commandes.

#### PP-06 — Les MP3 VBR font planter et corrompent (WMD #102)

*Symptôme.* Un fichier d'entrée mal formé casse l'application *et* laisse le disque dans un état inutilisable.

*Racine.* Le décodage se produit trop tard dans le pipeline, dans le même processus que la session device.

*Réponse produit.* **Séparation stricte des phases** : `Analyse → Décodage → Transcodage → Gravure`. Aucun
octet n'est envoyé au device avant que 100 % des pistes soient décodées et transcodées avec succès. Le
décodage se fait dans un processus/thread isolé avec garde-fous, et tout fichier qui échoue est signalé dans
le plan (badge rouge sur la ligne) **avant** la gravure. Corollaire : le plan a un état
« prêt à graver / incomplet ».

#### PP-07 / PP-08 — Le transcodage se bloque à 0 % (WMD #85, #26 ; EWMD #53, #55, #27, #42)

*Symptôme.* Une famille entière de bugs : chemin d'encodeur malformé, binaire absent, fichier intermédiaire
manquant. L'utilisateur voit une barre à 0 % et rien d'autre.

*Réponse produit.*
1. **Auto-test au démarrage** : un panneau *Diagnostic* vérifie décodeurs, encodeurs, accès USB et espace
   disque, et affiche un état vert/orange/rouge par composant, avec bouton *Réparer*.
2. **Encodeur embarqué par défaut** : aucun binaire externe requis pour le chemin nominal ; les encodeurs
   optionnels de meilleure qualité sont installables en un clic et vérifiés par empreinte.
3. **Timeout + log visible** : toute étape sans progression pendant 30 s affiche « Bloqué ? Voir le journal »
   avec la commande exacte et la sortie standard.

#### PP-01 / PP-02 / PP-03 — Le mur du pilote et de la détection

*Symptôme.* Le premier échec de l'utilisateur arrive avant même d'avoir vu le produit.

*Réponse produit.*
- **Assistant de connexion** dédié : détection de l'appareil au niveau USB brut *même quand le pilote n'est
  pas installé*, pour pouvoir dire « Je vois un Sony MZ-N710, mais le pilote WinUSB n'est pas installé.
  [Installer maintenant] » — et non pas « aucun appareil ».
- Installation du pilote **intégrée** (élévation UAC ponctuelle, explicite, réversible, avec un
  *Désinstaller le pilote* symétrique).
- **Base de données d'appareils** (VID/PID → modèle, capacités, vitesses connues, particularités) affichée à
  l'utilisateur : « MZ-N710 · Type-R · SP 1× · LP2 4× · LP4 8× · groupes : oui ».
- Les erreurs USB sont **traduites** : chaque code libusb a un message humain + une cause probable + une
  action, et le code brut reste copiable pour P4.

#### PP-18 / PP-19 / PP-20 / PP-21 — Le risque de corruption du TOC

*Symptôme.* Un petit nombre d'issues, mais **gravité maximale** : le disque devient illisible.

*Réponse produit.*
- **Sauvegarde automatique du TOC** avant toute opération d'écriture de TOC (titrage, groupes, déplacement,
  effacement, wipe). Stockée dans le catalogue local, restaurable.
- **Écriture de TOC atomique et minimale** : on calcule le diff, on écrit une fois, on relit et on compare.
- **Détection d'incohérence** : après écriture, relecture du TOC ; si divergence → alerte immédiate et
  proposition de restauration.
- **Garde-fou alimentation** : refus de démarrer une opération TOC si l'appareil est sur batterie faible
  (quand l'info est disponible) ou si la mise en veille du PC est imminente (inhibition de veille pendant
  la gravure).

#### PP-33 — On ne peut pas préparer sans appareil [Inféré]

*Réponse produit.* Décision **D1** : le plan est un document. L'application est pleinement utilisable sans
aucun appareil branché ; l'appareil est une **cible de sortie**, pas un prérequis.

#### PP-34 — Aucun cache de transcodage [Inféré]

*Réponse produit.* Cache adressé par contenu : clé = `hash(fichier source) + mode + encodeur + version + gain
appliqué`. Graver le même plan sur un 2ᵉ disque devient une opération de pure copie. Le cache est
inspectable et purgeable depuis les préférences, avec sa taille affichée.

#### PP-22 — Faux positifs de protection contre la copie (WMD #77)

*Réponse produit.* Distinguer clairement, dans l'UI du disque, trois états par piste : *Copiable*,
*Protégée (SCMS)*, *Origine inconnue*, avec un explicatif « ce disque a été enregistré par SonicStage ; le
marquage de protection est un artefact, l'audio reste lisible ». Ne jamais bloquer silencieusement.

---

## 7. Inventaire de features (MoSCoW)

### 7.0 Légende

- **M** = Must have (v1 impossible sans) · **S** = Should have · **C** = Could have · **W** = Won't have (pas
  maintenant, décision explicite)
- **Effort** : XS / S / M / L / XL

### 7.1 Domaine A — Bibliothèque

| ID | Feature | MoSCoW | Effort | Notes / inspiration |
|----|---------|--------|--------|---------------------|
| A-01 | Indexation d'un ou plusieurs dossiers racines, récursive, incrémentale | **M** | L | foobar2000 « Media Library ». Indexation en tâche de fond, UI utilisable pendant. |
| A-02 | Lecture de tags : ID3v1/v2.3/v2.4, Vorbis Comment, MP4/iTunes, APE | **M** | M | Robustesse > exhaustivité : PP-26. Toujours un repli sur le nom de fichier. |
| A-03 | Formats source : MP3 (CBR **et VBR**), FLAC, WAV, AIFF, OGG Vorbis, Opus, ALAC, AAC/M4A, WavPack | **M** | L | PP-06 : le VBR est un cas de test bloquant, pas un bonus. AIFF cf. WMD #91. |
| A-04 | Vue Liste plate avec colonnes configurables, tri multi-critères | **M** | M | foobar2000. Colonnes : #, Titre, Artiste, Album, Durée, Débit, Format, Année, Chemin. |
| A-05 | Recherche incrémentale instantanée (`Ctrl+F`), tolérante aux accents/casse | **M** | M | < 30 ms sur 100 000 pistes. Plexamp/Roon pour la fluidité. |
| A-06 | Vue arborescente Artiste → Album → Piste | **M** | M | MusicBee. |
| A-07 | Vue Dossiers (miroir du système de fichiers) | **S** | S | Indispensable pour P2 (archives non taguées). |
| A-08 | Import de playlists M3U / M3U8 / PLS / XSPF | **M** | S | PP-10 — trois demandes indépendantes. |
| A-09 | Import de feuilles CUE (album en un fichier + points de découpe) | **S** | M | Cas fréquent chez P2. Découpe à la volée en pistes MD. |
| A-10 | Glisser-déposer depuis l'Explorateur Windows | **M** | S | Chemin d'entrée n° 1 pour P1. |
| A-11 | Glisser-déposer **sortant** vers d'autres apps | **C** | S | |
| A-12 | Pochettes : embarquées + `cover.jpg` / `folder.jpg` du dossier | **S** | M | Mode grille d'albums. Roon/Plexamp. |
| A-13 | Vue grille d'albums (mosaïque) | **S** | M | Confort P1/P3 ; ne doit jamais être le mode par défaut (densité). |
| A-14 | Requêtes de filtrage à la foobar (`%artist% HAS x AND %year% GREATER 1999`) | **C** | L | Réservé aux experts, derrière un dépliant. |
| A-15 | Listes intelligentes sauvegardées | **C** | M | |
| A-16 | Lecture de ReplayGain / R128 existant dans les tags | **S** | S | Évite de recalculer pour P2. |
| A-17 | Analyse loudness EBU R128 à la demande (piste et album) | **S** | M | Alimente la normalisation (§7.3). |
| A-18 | Lecteur d'écoute intégré (lecture, seek, sortie WASAPI) | **M** | M | Nécessaire pour JTBD-8 (valider les transitions). |
| A-19 | Prévisualisation des transitions (jouer les 10 dernières s de la piste n + les 10 premières de n+1) | **C** | S | Différenciateur fort pour P3. |
| A-20 | Détection de doublons dans la bibliothèque | **C** | M | |
| A-21 | Statistiques de bibliothèque (formats, débits, durée totale) | **C** | S | |
| A-22 | Surveillance temps réel des dossiers (ajout/suppression de fichiers) | **S** | M | |
| A-23 | Édition de tags de la bibliothèque source | **W** | — | Hors périmètre : on ne modifie pas la bibliothèque de l'utilisateur. Redirection vers Mp3tag/MusicBee. |
| A-24 | Sources en ligne (Spotify, YouTube, streaming) | **W** | — | Périmètre local uniquement. |
| A-25 | Récupération de métadonnées en ligne (MusicBrainz) | **C** | M | Utile pour P2 sur les archives non taguées. Opt-in, réseau explicite. |

### 7.2 Domaine B — Plan de disque (burn list)

| ID | Feature | MoSCoW | Effort | Notes |
|----|---------|--------|--------|-------|
| B-01 | Plan comme document : créer, ouvrir, enregistrer (`.mdplan`), auto-sauvegarde | **M** | M | **D1**. Format lisible (JSON) avec chemins relatifs + hash des sources. |
| B-02 | Plusieurs plans ouverts simultanément (onglets) | **S** | M | P3 prépare une série de disques. |
| B-03 | Ajout de pistes : glisser-déposer, `Entrée`, double-clic, menu contextuel | **M** | S | |
| B-04 | Réordonnancement par glisser-déposer + `Alt+↑/↓` | **M** | M | |
| B-05 | Mode d'encodage **par piste** (SP / SP mono / LP2 / LP4) | **M** | M | Cœur du produit. Un disque peut mélanger. |
| B-06 | Mode par défaut du plan, applicable en masse | **M** | S | |
| B-07 | **Jauge de capacité live segmentée** (§9) | **M** | L | Élément signature. |
| B-08 | Calcul en clusters avec padding visible | **M** | M | **D2**. Différenciateur technique majeur. |
| B-09 | Temps restant tri-modal (SP / LP2 / LP4) | **M** | S | PP-12. |
| B-10 | Type de média sélectionnable (60 / 74 / 80 min ; MD-DATA exclu) | **M** | XS | Doit pouvoir être choisi sans disque inséré. |
| B-11 | Synchronisation automatique sur le disque réellement inséré | **M** | S | Si un disque est présent, le type et la capacité restante sont lus depuis lui. |
| B-12 | Panneau **Résoudre le dépassement** avec suggestions chiffrées | **S** | M | Parcours B. Grande valeur perçue. |
| B-13 | Rognage de silence en tête/queue de piste (seuil configurable) | **S** | M | Récupère des clusters, améliore les enchaînements. |
| B-14 | Fondu d'entrée/sortie par piste | **C** | M | |
| B-15 | Silence inter-pistes paramétrable (0 par défaut) | **C** | S | P5. |
| B-16 | Découpe d'une piste longue en plusieurs pistes MD (marqueurs) | **C** | L | JTBD-14. |
| B-17 | Groupes : créer, renommer, réordonner, dissoudre | **M** | M | Avec contrainte de contiguïté explicitée (§3.3). |
| B-18 | Titrage automatique par gabarit avec variables (`%artist%`, `%title%`, `%album%`, `%tracknumber%`) | **M** | M | |
| B-19 | **Budget TOC visible** (caractères consommés / disponibles) | **M** | M | **D3**. Deuxième jauge, fine. |
| B-20 | Repli automatique de titrage quand le budget déborde (règles en cascade) | **S** | M | Ex. : retirer l'artiste si album mono-artiste → tronquer sur mot → tronquer dur. |
| B-21 | Retrait des numéros de piste en tête de titre (option) | **M** | XS | WMD #92 : déjà implémenté en amont, à conserver. |
| B-22 | Translittération / repli des caractères non supportés, avec prévisualisation | **M** | M | PP-14, PP-31. Table configurable (`é→e`, `ø→o`, `/→-`). |
| B-23 | Renommage par motif et rechercher/remplacer sur la sélection | **S** | M | PP-11. |
| B-24 | Édition inline type tableur (`F2`, `Entrée`, `Tab`) | **M** | M | PP-11. |
| B-25 | Annuler / Rétablir illimité sur le plan | **M** | M | Attente de base d'un éditeur ; absent partout dans l'écosystème. |
| B-26 | Détection de doublon dans le plan | **C** | XS | |
| B-27 | Badge d'état par piste (OK / à transcoder / en cache / erreur source) | **M** | M | PP-06 : erreurs visibles avant gravure. |
| B-28 | Limite de 254 pistes signalée | **M** | XS | |
| B-29 | Export jaquette / liste imprimable (PDF, PNG, texte) | **C** | M | JTBD-9, différenciateur affectif fort pour P3. |
| B-30 | Modèles de plan (« Compil voiture LP2 », « Archive SP ») | **C** | S | |
| B-31 | Comparaison plan ↔ disque inséré (diff) | **S** | M | Permet la gravure incrémentale (§7.4 D-12). |

### 7.3 Domaine C — Pipeline de transcodage

| ID | Feature | MoSCoW | Effort | Notes |
|----|---------|--------|--------|-------|
| C-01 | Décodage vers PCM 44,1 kHz / 16 bits stéréo | **M** | L | Le MD n'accepte que ça ; le rééchantillonnage est obligatoire (PP-27). |
| C-02 | Rééchantillonnage de qualité (SoX/SRC-like, mode qualité configurable) | **M** | M | 48 kHz → 44,1 kHz est le cas le plus fréquent. |
| C-03 | Encodage ATRAC3 LP2 (132 kbps) et LP4 (66 kbps) | **M** | XL | Cœur technique. |
| C-04 | Choix d'encodeur : *Rapide / Équilibré / Meilleure qualité* + mode expert | **S** | M | §3.5. |
| C-05 | Chemin SP : envoi PCM, encodage par le DSP de l'appareil | **M** | M | Expliquer la lenteur dans l'UI. |
| C-06 | Normalisation loudness EBU R128 (cible configurable, −16 LUFS par défaut) | **S** | M | JTBD-13. Mode *piste* ou *album*. |
| C-07 | Protection contre l'écrêtage (true peak limiter, −1 dBTP) | **S** | M | Indispensable si normalisation. |
| C-08 | Aperçu A/B : écouter l'original vs le rendu LP4 avant de graver | **C** | L | Différenciateur « obsédé par la qualité ». |
| C-09 | **Cache de transcodage adressé par contenu** | **M** | M | PP-34. Clé = hash source + mode + encodeur + version + gain. |
| C-10 | Inspection et purge du cache (taille, entrées, âge) | **S** | S | |
| C-11 | Transcodage parallèle multi-cœurs avec limite configurable | **S** | M | |
| C-12 | Pré-transcodage anticipé pendant la composition du plan | **C** | M | Quand le PC est au repos, transcoder en avance → gravure instantanée. Effet « magique ». |
| C-13 | Journal de transcodage par piste (encodeur, durée, taille, avertissements) | **S** | S | PP-07, PP-08. |
| C-14 | Détection d'échec silencieux (fichier de sortie de durée ou d'énergie aberrante) | **S** | M | PP-24 : uploads AEA silencieux non détectés. |
| C-15 | Auto-test du pipeline au démarrage / à la demande | **M** | S | PP-07, PP-08. |
| C-16 | Traitement de fichiers corrompus / tronqués sans crash | **M** | M | PP-06. |
| C-17 | Égaliseur / DSP arbitraire avant encodage | **W** | — | Hors périmètre : on n'est pas un DAW. |
| C-18 | Encodage ATRAC1 (SP) côté PC | **W** | — | Non transférable via NetMD standard ; réservé au mode homebrew, hors v1. |

### 7.4 Domaine D — Appareil (NetMD)

| ID | Feature | MoSCoW | Effort | Notes |
|----|---------|--------|--------|-------|
| D-01 | Détection USB (VID/PID) même sans pilote installé | **M** | M | PP-02 : pouvoir dire *quoi* est branché. |
| D-02 | Installation guidée du pilote WinUSB, intégrée et réversible | **M** | L | PP-01. Élévation UAC ponctuelle, message clair. |
| D-03 | Base de connaissances d'appareils (modèle, Type-R/S, vitesses, groupes) | **M** | M | Alimente les estimations de durée de gravure. |
| D-04 | Lecture du TOC : pistes, durées, modes, protection, groupes, espace libre | **M** | L | |
| D-05 | Rafraîchissement fiable après réinsertion / reconnexion | **M** | M | PP-29 (EWMD #48). |
| D-06 | Upload de pistes (SP / LP2 / LP4) avec progression fiable | **M** | XL | |
| D-07 | Progression en **temps réel restant**, pas seulement en pourcentage | **M** | S | Fondamental quand une gravure SP dure 80 min. |
| D-08 | Annulation propre d'un transfert en cours (état du disque cohérent) | **M** | L | |
| D-09 | Inhibition de la mise en veille pendant une gravure | **M** | XS | PP-32. |
| D-10 | Reprise après interruption (poursuivre là où on s'est arrêté) | **S** | L | Le plan sait ce qui est déjà sur le disque → gravure des pistes manquantes. |
| D-11 | Écriture de titres (disque + pistes) en une transaction TOC | **M** | M | PP-18. |
| D-12 | Gravure incrémentale : ne transférer que le delta plan ↔ disque | **S** | L | Énorme gain de temps. |
| D-13 | Déplacement de pistes sur le disque | **M** | M | PP-20 : prévenir du risque, vérifier après. |
| D-14 | Effacement de piste, effacement multiple | **M** | M | Confirmation explicite avec liste. |
| D-15 | Effacement total (wipe) / formatage | **M** | S | PP-21 : détecter les échecs et l'expliquer par modèle. |
| D-16 | Gestion des groupes sur le disque | **M** | M | |
| D-17 | **Sauvegarde du TOC** avant chaque écriture, et restauration | **S** | L | **D4**. Argument de confiance majeur. |
| D-18 | Vérification post-écriture (relecture + comparaison) | **S** | M | |
| D-19 | **Simulation** : « voici ce qui va être fait » avant toute opération device | **M** | M | **D4**. Diff explicite. |
| D-20 | Extraction MD → PC sur MZ-RH1 / MZ-M200 | **S** | XL | P2. |
| D-21 | Extraction via mode homebrew (Sony/Aiwa) | **C** | XL | Opt-in, derrière un avertissement fort. |
| D-22 | Contrôle de lecture sur l'appareil (play/pause/piste suivante) | **C** | M | WMD #35. Utile pour vérifier après gravure. |
| D-23 | Journal d'opérations device copiable, avec commandes NetMD brutes | **S** | S | JTBD-11, PP-03, PP-04. |
| D-24 | Traduction humaine de chaque erreur USB / NetMD (cause + action) | **M** | M | PP-03, PP-04. |
| D-25 | Support Hi-MD | **W** | — | Périmètre v1 = NetMD. Hi-MD génère une part disproportionnée des bugs (EWMD #40, #47, #28 ; WMD #82, #98). À rouvrir en v2. |
| D-26 | Remote NetMD (appareil sur une autre machine du réseau) | **W** | — | Hors v1 ; source de bugs SP (WMD #99, #101). |
| D-27 | Dump firmware / RAM, manipulation de TOC bas niveau | **W** | — | Laisser à Web MiniDisc Pro. |
| D-28 | Détection d'état d'alimentation / batterie faible | **C** | M | Garde-fou avant écriture TOC. |
| D-29 | Multi-appareils simultanés | **C** | M | P2 avec plusieurs decks. |

### 7.5 Domaine E — Qualité de vie / système

| ID | Feature | MoSCoW | Effort | Notes |
|----|---------|--------|--------|-------|
| E-01 | Palette de commandes (`Ctrl+Maj+P`) couvrant 100 % des actions | **M** | M | VS Code. Rend l'app découvrable sans menus lourds. |
| E-02 | Raccourcis clavier complets et remappables | **M** | M | PP-11. |
| E-03 | Menus contextuels sur toutes les listes | **M** | S | PP-11. |
| E-04 | Annuler / Rétablir global (plan, titres, sélection) | **M** | M | |
| E-05 | Auto-sauvegarde + récupération après crash | **M** | M | |
| E-06 | Panneau **Diagnostic** (décodeurs, encodeurs, USB, disque, cache) | **M** | M | PP-07, PP-08, PP-02. |
| E-07 | Journal applicatif consultable et exportable | **S** | S | |
| E-08 | Rapport de bug pré-rempli (modèle, OS, version, dernières opérations) | **C** | M | Améliore la qualité des issues reçues. |
| E-09 | **Catalogue de disques** : inventaire des disques gravés/extraits, cherchable | **C** | L | PP-35, JTBD-6. Différenciateur affectif majeur. |
| E-10 | Historique des gravures (quoi, quand, sur quel disque) | **S** | M | |
| E-11 | Thème sombre (défaut) + thème clair | **S** | M | §10. Le sombre est le défaut assumé. |
| E-12 | Densité configurable (compact / standard / confortable) | **S** | S | 22 / 26 / 32 px de ligne. |
| E-13 | Mise à l'échelle DPI correcte (100 → 200 %) | **M** | L | Contrainte forte en OpenGL custom. |
| E-14 | Accessibilité : navigation clavier complète, focus visible, MSAA/UIA | **S** | XL | PP-30. En UI custom, coûteux mais indispensable. |
| E-15 | Localisation FR/EN dès la v1, architecture i18n dès le départ | **M** | M | §12. |
| E-16 | Préférences typées, fichier lisible, réinitialisation par section | **M** | S | |
| E-17 | Portabilité (mode « sans installation », données à côté de l'exe) | **C** | S | Apprécié par P4. |
| E-18 | Mise à jour vérifiée par signature, changelog dans l'app | **S** | M | |
| E-19 | Télémétrie | **W** | — | Aucune. Décision de positionnement dans cette communauté. |
| E-20 | Onboarding en 3 écrans, sautable, rejouable | **S** | M | P1. |
| E-21 | Aide contextuelle : chaque terme MD (SP, LP2, Type-R, TOC) a une info-bulle explicative | **S** | M | Fait beaucoup pour P1 à faible coût. |
| E-22 | Mode « ne rien écrire » (dry run global) pour tester sans risque | **C** | S | Confiance, P2/P4. |

---

## 8. Architecture de l'information et layout

### 8.1 Choix du modèle : trois panneaux, pas d'assistant

Trois modèles étaient candidats.

| Modèle | Description | Avantages | Inconvénients | Verdict |
|--------|-------------|-----------|---------------|---------|
| **Assistant (wizard)** | Étapes 1→5 : choisir les fichiers, choisir le mode, titrer, brancher, graver. | Très rassurant pour P1. Chaque étape a une seule décision. | Interdit l'itération, qui est **l'activité principale** (on ajoute/retire/change de mode 20 fois). Punitif pour tous les autres personas. | **Rejeté** comme modèle principal. Conservé pour deux sous-flux : premier lancement (E-20) et installation de pilote (D-02). |
| **Onglets** (Bibliothèque / Plan / Disque) | Un écran à la fois. | Simple à implémenter, bon en petite fenêtre. | Casse le lien visuel entre « ce que j'ajoute » et « ce que ça coûte ». La jauge disparaît quand on choisit. | **Rejeté** comme modèle principal. Conservé comme **repli responsive** sous 1 000 px de large. |
| **Trois panneaux côte à côte** | Bibliothèque · Plan de disque · Inspecteur, plus une barre d'appareil persistante. | La conséquence (jauge) est toujours visible pendant la cause (sélection). Modèle mental des DAW et des gestionnaires de bibliothèque. Permet le glisser-déposer direct. | Exige ≥ 1 200 px. Densité obligatoire. | **Retenu.** |

**Principe directeur** : *la jauge de capacité et la barre d'appareil ne quittent jamais l'écran.*

### 8.2 Layout principal — vue « Plan »

```
┌─ Barre de titre custom (32 px) ─────────────────────────────────────────────────────────────────┐
│ ▣ mini-disk   Fichier  Édition  Disque  Appareil  Aide        Compil voiture.mdplan •   — ▢ ✕  │
├─ Barre d'appareil (28 px) ──────────────────────────────────────────────────────────────────────┤
│ ● MZ-N710  ·  80 min  ·  12 pistes  ·  libre 18:42 SP  ·  Type-R  ·  LP4 8×      [Rafraîchir ⟳] │
├──────────────┬──────────────────────────────────────────────────────────┬───────────────────────┤
│ NAVIGATEUR   │  PLAN DE DISQUE                                          │  INSPECTEUR           │
│ (200 px)     │  (flex, min 520 px)                                      │  (280 px)             │
│              │                                                          │                       │
│ ▾ Bibliothèq.│ ┌─ JAUGE DE CAPACITÉ ─────────────────────────────────┐ │  ┌─ Pochette ──────┐ │
│   Tous       │ │ ████████▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │ │  │                 │ │
│   Artistes   │ │ 0        20        40        60        80 min (SP)  │ │  │   [ art 240² ]  │ │
│   Albums     │ │ 47:12 / 80:00  ·  reste 32:48 SP · 65:36 LP2 · …    │ │  │                 │ │
│   Genres     │ │ TOC ▒▒▒▒▒▒░░░░░░░░░░░  412 / 1785 car.  (23 %)      │ │  └─────────────────┘ │
│   Dossiers   │ └──────────────────────────────────────────────────────┘ │  Kid A              │
│ ▾ Playlists  │                                                          │  Radiohead · 2000    │
│   Trajets    │  #  ● Titre                       Artiste    Durée Mode │  ─────────────────── │
│   Nuit       │ ─────────────────────────────────────────────────────── │  Source              │
│ ▾ Plans      │  ▾ ▣ Groupe 1 — Face A            (5 pistes)  22:31    │   FLAC 44,1/16       │
│   Compil …•  │  1  ● Everything In Its Right P…  Radiohead   4:11 SP  │   1 011 kbps         │
│   Archive 03 │  2  ● Kid A                       Radiohead   4:44 SP  │   D:\Musique\…\01.f… │
│ ▾ Disques    │  3  ● The National Anthem         Radiohead   5:51 SP  │  ─────────────────── │
│   Boîte 3 B… │  4  ● How To Disappear Completely Radiohead   5:56 SP  │  Rendu MD            │
│              │  5  ⚠ Treefingers                 Radiohead   3:42 SP  │   Mode  [SP      ▾]  │
│ ─────────────│  ▾ ▣ Groupe 2 — Face B            (4 pistes)  24:41    │   Durée facturée     │
│ APPAREIL     │  6  ● Optimistic                  Radiohead   5:15 LP2 │    4:12 (+0:01 pad)  │
│ ○ Disque     │  7  ● In Limbo                    Radiohead   3:31 LP2 │   Gain   −2,4 dB     │
│ ○ Journal    │  8  ● Idioteque                   Radiohead   5:09 LP2 │   Encodeur  —        │
│              │  9  ● Morning Bell                Radiohead   4:35 LP2 │   Cache  ✓ prêt      │
│              │ 10  ● Motion Picture Soundtrack   Radiohead   7:01 LP2 │  ─────────────────── │
│              │                                                        │  Titre MD            │
│              │  [+ Ajouter…]                                          │   Radiohead - Kid A  │
│              │                                                        │   21 / 1785 car.     │
├──────────────┴──────────────────────────────────────────────────────────┴───────────────────────┤
│ ▶ ═══════●───────────────  1:42 / 4:11   Kid A          [Graver le disque ▸]  ~9 min  (Ctrl+↵)  │
├─ Barre de statut (22 px) ───────────────────────────────────────────────────────────────────────┤
│ 10 pistes · 47:12 · prêt à graver   │  Cache 1,2 Go  │  Bibliothèque 12 480   │  ⚙ Diagnostic ✓ │
└─────────────────────────────────────────────────────────────────────────────────────────────────┘
```

**Notes de lecture du schéma**

- Le **navigateur** (gauche) est un arbre unique qui mélange quatre racines : Bibliothèque, Playlists, Plans,
  Disques (catalogue). C'est délibéré : la navigation est une seule idée, pas quatre onglets.
- Le **centre** est le plan, avec la jauge **en tête, épinglée** (elle ne défile pas).
- L'**inspecteur** (droite) est contextuel : il montre la sélection courante (piste du plan, piste de
  bibliothèque, ou le disque entier si rien n'est sélectionné).
- La **barre de transport** en bas est permanente : lecture d'écoute à gauche, action primaire à droite.
  L'action primaire affiche **toujours son coût en temps** (`~9 min`).
- La **barre de statut** porte l'état global du plan, et un indicateur de diagnostic cliquable.

### 8.3 Layout — vue « Bibliothèque » (panneau gauche élargi)

Quand l'utilisateur cherche, le centre bascule en mode bibliothèque **sans faire disparaître la jauge** :
elle se réduit à une bande de 12 px collée en haut du panneau plan, qui devient une colonne de droite.

```
┌─ Barre d'appareil ──────────────────────────────────────────────────────────────────────────────┐
├──────────────┬───────────────────────────────────────────────────────┬──────────────────────────┤
│ NAVIGATEUR   │  BIBLIOTHÈQUE            [🔎 radiohead kid a       ×] │  PLAN (compact)          │
│              │                                                       │ ████████▓▓▓░░░░░░  47:12 │
│ ▾ Bibliothèq.│  Titre                     Artiste     Album   Durée  │ ──────────────────────── │
│ ▸ Tous       │  Everything In Its Right…  Radiohead   Kid A   4:11 → │  1 Everything In…  4:11 │
│   Artistes   │  Kid A                     Radiohead   Kid A   4:44 → │  2 Kid A           4:44 │
│   Albums …   │  The National Anthem       Radiohead   Kid A   5:51 → │  3 The National …  5:51 │
│              │  How To Disappear Comple…  Radiohead   Kid A   5:56 → │  4 How To Disap…   5:56 │
│              │  Treefingers               Radiohead   Kid A   3:42   │  5 Treefingers     3:42 │
│              │  Optimistic                Radiohead   Kid A   5:15   │  …                      │
│              │                                                       │                          │
│              │  8 résultats · 34:12 au total · sélection 3 · 14:46   │  [Tout ajouter ⏎]        │
└──────────────┴───────────────────────────────────────────────────────┴──────────────────────────┘
```

- La flèche `→` en fin de ligne indique que la piste **est déjà dans le plan** (état de présence).
- La barre de résumé sous la liste donne toujours **durée totale des résultats** et **durée de la
  sélection** — information manquante dans tous les outils NetMD et essentielle pour composer.

### 8.4 Layout — vue « Disque » (état réel de l'appareil)

```
┌──────────────┬───────────────────────────────────────────────────────┬──────────────────────────┐
│              │  DISQUE : « Compil voiture »                          │  DIFF PLAN ↔ DISQUE      │
│ ▾ Appareil   │  ████████████████████▓▓▓░░░░░░░░  61:18 / 80:00       │                          │
│   ● Disque   │                                                       │  + 3 à ajouter    12:41  │
│   ○ Journal  │  #  Titre                     Durée  Mode  Protection │  − 1 à retirer     4:02  │
│              │  ─────────────────────────────────────────────────── │  ↕ 2 à déplacer          │
│              │  ▾ ▣ Face A                    22:31                  │  ✎ 5 titres à corriger   │
│              │  1  Everything In Its Right…   4:11  SP    Copiable   │                          │
│              │  2  Kid A                      4:44  SP    Copiable   │  Après : 70:00 / 80:00   │
│              │  3  The National Anthem        5:51  SP    Copiable   │  Durée estimée : ~13 min │
│              │  …                                                    │                          │
│              │ 14  (sans titre)               3:07  LP2   Inconnue   │  [Simuler]  [Appliquer]  │
│              │                                                       │                          │
│              │  [Titrer depuis la bibliothèque]  [Extraire…]         │  ⓘ Une sauvegarde du TOC │
│              │  [Effacer…]  [Grouper…]  [Effacer tout le disque…]    │    sera créée avant.     │
└──────────────┴───────────────────────────────────────────────────────┴──────────────────────────┘
```

### 8.5 États des panneaux

#### 8.5.1 Barre d'appareil (toujours visible, 28 px)

| État | Rendu | Action disponible |
|------|-------|-------------------|
| **Aucun appareil** | `○ Aucun appareil — vous pouvez composer un disque` (texte secondaire) | *Configurer un appareil* |
| **Appareil vu, pilote absent** | `▲ Sony MZ-N710 détecté — pilote non installé` (fond ambre 8 %) | **[Installer le pilote]** |
| **Connexion en cours** | `◐ Connexion à MZ-N710…` (spinner 2 px) | *Annuler* |
| **Connecté, pas de disque** | `● MZ-N710 · aucun disque inséré` | *Rafraîchir* |
| **Connecté, disque lu** | `● MZ-N710 · 80 min · 12 pistes · libre 18:42 SP · Type-R` | *Rafraîchir*, *Éjecter* |
| **Disque protégé en écriture** | `● MZ-N710 · 🔒 disque protégé en écriture` (ambre) | Explication |
| **Occupé (gravure)** | `◉ Gravure 4/10 · The National Anthem · 62 % · ~6 min` + barre de progression 2 px sous la barre | **[Annuler]** |
| **Erreur** | `✕ MZ-N710 — communication interrompue (LIBUSB_ERROR_IO)` (fond rouge 8 %) | *Détails*, *Reconnecter* |

Règle : **la barre d'appareil ne fait jamais apparaître de modale.** Elle propose, elle n'interrompt pas.

#### 8.5.2 Panneau Plan

| État | Rendu |
|------|-------|
| **Vide** | Zone de dépôt en pointillés 1 px, texte : « Glissez des morceaux ici, ou appuyez sur `Entrée` depuis la bibliothèque. » + 3 raccourcis suggérés. Jauge présente mais à 0 avec l'échelle du média choisi. |
| **En composition** | Nominal. |
| **Analyse en cours** | Les lignes non encore analysées ont leur durée en `——:——` et un liseré animé 1 px à gauche. La jauge affiche une zone hachurée « estimation en cours ». |
| **Dépassement** | Jauge rouge, bandeau sous la jauge : `Dépassement de 4:31 — [Résoudre]`. Les pistes qui ne rentrent pas sont grisées à 55 % avec un liseré rouge à gauche. |
| **Source manquante** | Ligne avec icône `⚠`, texte secondaire en rouge : « fichier introuvable ». Action *Relocaliser…*. |
| **Source illisible** | `✕` + « format non pris en charge / fichier corrompu » + *Ignorer* / *Retirer*. |
| **Prêt à graver** | Le bouton primaire devient plein accent, la barre de statut affiche `prêt à graver`. |
| **Gravure en cours** | Le plan devient **lecture seule** (les contrôles se désactivent avec une transition d'opacité, pas une disparition) ; les pistes déjà gravées portent un `✓` vert ; la piste en cours a une barre de progression fine en fond de ligne. |
| **Gravure interrompue** | Bandeau : `Gravure interrompue après 4 pistes sur 10.` + **[Reprendre]** / *Voir le disque*. |

#### 8.5.3 Inspecteur

| Contexte de sélection | Contenu |
|-----------------------|---------|
| Rien | Résumé du plan : média, total, répartition par mode, budget TOC, groupes, durée estimée de gravure. |
| 1 piste du plan | Pochette, tags source, format source, rendu MD (mode, durée facturée, padding, gain, encodeur, état de cache), titre MD et son coût en caractères. |
| N pistes du plan | Champs communs éditables en masse (mode, gain, gabarit de titre) avec valeurs mixtes affichées `—`. |
| 1 groupe | Nom, nombre de pistes, durée, plage de pistes, coût en caractères du groupe. |
| 1 piste de bibliothèque | Tags, format, chemin, présence dans le plan, bouton *Ajouter*. |
| Piste du disque | Titre TOC, durée, mode, protection, correspondance trouvée dans la bibliothèque. |

#### 8.5.4 États vides, de chargement et d'erreur (règles générales)

- **Jamais de spinner plein écran.** Le squelette (`skeleton`) est préféré : lignes grises animées.
- **Tout état vide propose une action**, jamais seulement un constat.
- **Toute erreur affiche** : ce qui a échoué · pourquoi (en langue humaine) · ce qu'on peut faire ·
  le code technique replié (`▸ Détails techniques`).
- Les erreurs sont **inline** (dans le contexte où elles se produisent), pas en modale, sauf si une décision
  utilisateur bloque la suite.

### 8.6 Flux en ≤ 3 clics

| Objectif | Chemin | Clics |
|----------|--------|-------|
| Ajouter un album au plan | Clic album (arbre) → `Ctrl+A` → `Entrée` | 1 clic + 2 touches |
| Passer tout le disque en LP2 | Clic dans le plan → `Ctrl+A` → sélecteur *Mode* de l'inspecteur | 2 clics |
| Renommer une piste | Double-clic sur la cellule Titre → saisir → `Entrée` | 1 clic |
| Créer un groupe | Sélectionner → `Ctrl+G` → nommer → `Entrée` | 1 clic |
| Graver | `Ctrl+Entrée` → confirmer le récapitulatif | 1 clic |
| Résoudre un dépassement | Clic sur la zone rouge de la jauge → choisir une suggestion | 2 clics |
| Titrer un disque existant depuis la bibliothèque | Vue Disque → *Titrer depuis la bibliothèque* → *Appliquer* | 3 clics |
| Extraire tout un disque | Vue Disque → `Ctrl+A` → *Extraire…* → *Démarrer* | 3 clics |
| Voir pourquoi le transcodage a échoué | Clic sur le badge rouge de la ligne | 1 clic |
| Ouvrir le diagnostic | Clic sur l'indicateur de la barre de statut | 1 clic |

### 8.7 Comportement responsive

| Largeur | Comportement |
|---------|--------------|
| ≥ 1 400 px | Trois panneaux + inspecteur large (320 px). |
| 1 200–1 400 px | Trois panneaux, inspecteur 280 px, colonnes de bibliothèque réduites (Album masqué). |
| 1 000–1 200 px | L'inspecteur devient un panneau **superposé** ouvrable par `Ctrl+I`. |
| < 1 000 px | Bascule en **onglets** (Bibliothèque / Plan / Disque). La jauge reste épinglée en haut, en version compacte 12 px, sur les trois onglets. |
| Hauteur < 600 px | La barre de transport se réduit à une seule ligne ; la pochette de l'inspecteur disparaît. |

### 8.8 Modèle de navigation clavier

```
Tab / Maj+Tab      : parcourt les zones (navigateur → liste → inspecteur → transport)
F6                 : panneau suivant (cycle rapide)
Ctrl+1 / 2 / 3     : focus navigateur / plan / inspecteur
Ctrl+F             : recherche bibliothèque
Ctrl+Maj+P         : palette de commandes
↑ ↓                : déplacement dans la liste
Espace             : écouter la piste sous le curseur
Entrée             : ajouter au plan (bibliothèque) / éditer (plan)
Suppr              : retirer du plan
Alt+↑ / Alt+↓      : déplacer la piste dans le plan
Ctrl+G             : grouper la sélection
Ctrl+Maj+G         : dissoudre le groupe
F2                 : renommer
Ctrl+M             : ouvrir le sélecteur de mode pour la sélection
Ctrl+Alt+1/2/3/4   : SP / SP mono / LP2 / LP4 sur la sélection
Ctrl+Entrée        : graver
Échap              : annuler l'édition / fermer le panneau superposé
Ctrl+Z / Ctrl+Y    : annuler / rétablir
```

---

## 9. Design de la jauge de capacité

C'est **l'élément signature** du produit. Cette section est volontairement au niveau du pixel.

### 9.1 Ce que la jauge doit dire, par ordre de priorité

1. **Est-ce que ça rentre ?** (réponse binaire, lisible en 200 ms, à 2 mètres)
2. **Combien il reste ?** (et en quel mode)
3. **Qu'est-ce qui prend de la place ?** (quelle piste, quel mode)
4. **Combien je gaspille ?** (padding cluster)
5. **Où en est ma gravure ?** (quand elle tourne)

### 9.2 Anatomie

```
        ← 4 px →                                                            ← 4 px →
       ┌────────────────────────────────────────────────────────────────────────────┐
   4px │                                                                            │  marge haute
       │  ┌──────────────────────────────────────────────────────────────────────┐  │
       │  │▐▐▐▐▐▐▐▐│▐▐▐▐▐▐│▐▐▐▐▐▐▐▐▐│▓▓▓▓▓│▓▓▓▓▓▓▓│░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░│  │  BARRE 14 px
       │  └──────────────────────────────────────────────────────────────────────┘  │
   2px │  ╷         ╷         ╷         ╷         ╷         ╷         ╷         ╷   │  graduations 4 px
       │  0        10        20        30        40        50        60        80   │  labels 10 px
   6px │                                                                            │
       │  47:12 / 80:00    ·    reste  32:48 SP  ·  65:36 LP2  ·  131:12 LP4        │  lecture 12 px
   4px │  ▒▒▒▒▒▒▒░░░░░░░░░░░░░░░░░░░░░░░░  TOC 412 / 1785 car.                      │  BARRE TOC 4 px
       └────────────────────────────────────────────────────────────────────────────┘
                                                        hauteur totale : 56 px
```

**Décomposition verticale (grille 2 px)**

| Bande | Hauteur | Contenu |
|-------|---------|---------|
| Marge haute | 6 px | — |
| Barre principale | **14 px** | Segments de pistes, coin arrondi 2 px sur les extrémités uniquement |
| Espace | 2 px | — |
| Graduations | 4 px | Traits 1 px, couleur bordure subtile |
| Labels d'échelle | 14 px (texte 10 px / 14) | Chiffres tabulaires, texte désactivé |
| Espace | 6 px | — |
| Ligne de lecture | 16 px (texte 12 px / 16) | Compteur principal + résiduels tri-modaux |
| Espace | 4 px | — |
| Barre TOC | 4 px | Budget de titres |
| Marge basse | 6 px | — |

La jauge occupe toute la largeur du panneau plan moins 12 px de marge de chaque côté.

### 9.3 Les segments

- **Un segment = une piste.** Largeur = `durée_facturée_en_clusters / capacité_totale_en_clusters`.
- **Séparateur** : 1 px de la couleur du canvas (`#111113`) entre deux segments. Sous 3 px de large, on
  supprime le séparateur et on alterne la luminosité (voir ci-dessous) pour rester lisible.
- **Couleur** = couleur du mode de la piste (§9.5).
- **Alternance** : deux pistes consécutives du même mode alternent entre la teinte de base et une variante
  **+6 % de luminosité**, pour qu'on distingue les frontières sans séparateur.
- **Segment de padding** : les clusters gaspillés en fin de piste sont rendus par un **motif de hachures
  diagonales à 45°**, même teinte, opacité 40 %, largeur du padding réel (souvent < 1 px → on impose alors
  un minimum de 1 px de hachure et on retire 1 px au segment plein, pour que le total reste juste).
- **Largeur minimale d'un segment** : 2 px. Sous ce seuil, on fusionne visuellement les pistes voisines de
  même mode et le tooltip liste les pistes fusionnées (« 3 pistes, 0:38 »).
- **Zone libre** : `#212225` (contrôle) avec une bordure interne 1 px `#2B2B2B`.
- **Zone de dépassement** : la barre reste à largeur fixe ; le dépassement est rendu par un **chevron rouge
  en fin de barre** (12 px) plus un **rebond visuel** : la barre entière prend une bordure 1 px rouge
  `#DA3633` et un halo externe de 2 px à 20 % d'opacité. Le compteur passe en rouge.
  *Alternative rejetée* : compresser l'échelle pour faire tenir le dépassement — cela ment sur la capacité.

### 9.4 Graduations et échelle

- L'échelle est **en minutes du mode de référence** du plan, indiqué entre parenthèses : `min (SP)`.
- Le mode de référence est, par défaut, **le mode majoritaire du plan** en durée. Il peut être forcé
  dans les préférences.
- Graduations majeures toutes les 10 min (trait 4 px + label), mineures toutes les 2 min (trait 2 px), les
  mineures disparaissent sous 400 px de largeur de jauge.
- **La dernière graduation porte la capacité du média** (74 ou 80), même si elle n'est pas un multiple de 10.
- Un **repère « disque actuel »** : quand un disque est inséré et non vide, un trait vertical 1 px blanc à
  60 % marque le contenu déjà présent, avec un label `déjà sur le disque` au survol. Cela rend la gravure
  incrémentale (D-12) compréhensible d'un coup d'œil.

### 9.5 Couleurs par mode MD

Extension de la palette de `02b-design-tokens.md`, §« Couleurs par mode MD ». Les teintes retenues sont
choisies pour être distinguables **entre elles**, distinguables **du fond**, et distinguables **en
deutéranopie** (le couple critique SP/LP2 est bleu/vert : on le désambiguïse par la luminosité **et** par
un motif, voir 9.9).

| Mode | Nom du token | Base | Variante alternée (+6 % L) | Bordure / accent | Origine |
|------|--------------|------|----------------------------|------------------|---------|
| **SP** | `--md-mode-sp` | `#0090FF` | `#3B9EFF` | `#70B8FF` | Radix blue 9/10/11 |
| **SP mono** | `--md-mode-mono` | `#BE95FF` | `#D0B0FF` | `#DCC5FF` | Carbon purple 40 (proposé en 02b) |
| **LP2** | `--md-mode-lp2` | `#3FB950` | `#57C267` | `#7EE787` | Primer success |
| **LP4** | `--md-mode-lp4` | `#D29922` | `#E0AC3B` | `#EAC54F` | Primer attention |
| **Padding (gaspillage)** | `--md-padding` | teinte du mode, hachures 45°, α 0,40 | — | — | — |
| **Libre** | `--md-free` | `#212225` | — | `#2B2B2B` | slate 3 / bordure subtile |
| **Dépassement** | `--md-over` | `#DA3633` | — | `#F85149` | Primer danger |
| **Déjà sur le disque** | `--md-onDisc` | `#EDEEF0` α 0,60, 1 px | — | — | slate 12 |

**Contrastes vérifiés** (sur fond `#1F1F1F`) : SP 5,6:1 · LP2 6,4:1 · LP4 7,2:1 · mono 8,9:1 — tous
au-dessus de 3:1 (exigence AA pour composant graphique) et même au-dessus de 4,5:1.

**Cohérence globale** : ces mêmes couleurs servent partout où un mode est mentionné — pastille dans la
colonne *Mode* du plan, sélecteur de mode de l'inspecteur, légende, et vue Disque. Un utilisateur doit
apprendre « ambre = LP4 » une seule fois.

### 9.6 La ligne de lecture

```
47:12 / 80:00    ·    reste  32:48 SP  ·  65:36 LP2  ·  131:12 LP4
▲                      ▲
│                      └─ résiduels : texte secondaire #B0B4BA, chiffres tabulaires,
│                         le mode de référence est en texte primaire
└─ 14 px semi-gras, chiffres tabulaires, texte primaire #EDEEF0
   passe en #F85149 en cas de dépassement
```

Variantes selon l'état :

| État | Ligne de lecture |
|------|------------------|
| Vide | `0:00 / 80:00 · disque vierge 80 min · [Changer de média ▾]` |
| Nominal | `47:12 / 80:00 · reste 32:48 SP · 65:36 LP2 · 131:12 LP4` |
| Presque plein (≥ 90 %) | `76:04 / 80:00 · reste 3:56 SP` en ambre `#D29922` |
| Plein exact | `80:00 / 80:00 · disque plein` en `#3FB950` — **on célèbre le remplissage parfait** |
| Dépassement | `84:31 / 80:00 · dépassement 4:31` en `#F85149` + bouton `[Résoudre]` |
| Analyse | `47:12 / 80:00 · 3 pistes en cours d'analyse…` avec la valeur en italique |
| Gravure | `Gravure 4/10 · 62 % · reste ~6 min · fin vers 15h42` |
| Disque non vide | `47:12 + 12:30 déjà présents / 80:00 · reste 20:18 SP` |

### 9.7 La barre TOC (budget de titres)

- Hauteur 4 px, même largeur que la barre principale, radius 2 px.
- Rempli en `#5A6169` (bordure hover, neutre — le budget de titres n'est pas un mode).
- Trois seuils : < 80 % neutre · 80–100 % ambre · > 100 % rouge.
- Label à droite : `TOC 412 / 1785 car.` en 10 px.
- Au survol : décomposition `titre du disque 18 · groupes 42 · pistes 352`.
- En dépassement : `[Raccourcir automatiquement]` qui applique la cascade de repli (B-20) et montre le diff.

### 9.8 Interaction

| Geste | Comportement |
|-------|--------------|
| Survol d'un segment | Info-bulle après 350 ms : `3. The National Anthem · 5:51 · SP · facturé 5:52 (+0:01)`. Le segment gagne une bordure interne 1 px blanche à 40 %. La ligne correspondante du plan se surligne simultanément (liaison bidirectionnelle). |
| Clic sur un segment | Sélectionne la piste dans le plan et la fait défiler jusqu'à elle. |
| Survol d'une ligne du plan | Le segment correspondant s'éclaircit de 12 %. |
| Survol de la zone libre | Info-bulle : `reste 32:48 en SP · 65:36 en LP2 · 131:12 en LP4`. |
| Clic sur la zone libre | Ouvre le sélecteur *Ajouter des pistes* filtré sur « durée ≤ temps restant » — un raccourci pour boucher les trous. |
| Clic sur la zone de dépassement | Ouvre le panneau **Résoudre**. |
| Molette sur la jauge | Rien (évite les changements accidentels). |
| Clic droit | Menu : *Mode de référence de l'échelle ▸*, *Afficher le gaspillage*, *Changer de média ▸*, *Copier le résumé*. |

### 9.9 Accessibilité de la jauge

- **Ne jamais coder l'information par la seule couleur.** Chaque segment porte, quand sa largeur ≥ 16 px, une
  **initiale de mode** centrée en 9 px sur fond translucide (`S`, `M`, `2`, `4`). Sous 16 px, l'information
  est portée par le tooltip, la colonne *Mode* de la liste et la légende.
- **Motif optionnel** (préférence *Différencier les modes par un motif*) : SP uni, LP2 points, LP4 hachures
  fines, mono damier. Activé automatiquement si Windows signale un mode contraste élevé.
- **Rôle d'accessibilité** : la jauge est exposée comme un groupe nommé « Capacité du disque », avec un
  résumé textuel équivalent à la ligne de lecture, et chaque segment comme un enfant nommé.
- **Annonce des changements** : région « live » polie qui annonce le total après chaque modification,
  regroupée par débounce de 400 ms pour ne pas noyer le lecteur d'écran pendant un glisser-déposer.
- **Contraste élevé Windows** : la jauge bascule sur les couleurs système (`Highlight`, `WindowText`,
  `GrayText`) et s'appuie entièrement sur les motifs.
- **Réduction de mouvement** : si le système demande une réduction des animations, les transitions de
  largeur passent à 0 ms (voir §11).

### 9.10 Variante compacte (12 px)

Utilisée dans le panneau plan réduit (§8.3) et sous 1 000 px (§8.7).

```
████████▓▓▓░░░░░░░░░░░░░░░  47:12 / 80:00
```

- Barre 8 px, pas de graduations, pas de barre TOC, lecture sur la même ligne à droite.
- Le clic ouvre la jauge complète en survol (popover 320 × 120 px).

### 9.11 Variante « progression de gravure »

Pendant une gravure, **la jauge ne change pas de forme** — elle acquiert une couche :

```
│▐▐▐▐▐▐▐▐│▐▐▐▐▐▐│▐▐▐▐▐▐▐▐▐│▓▓▓▓▓│▓▓▓▓▓▓▓│░░░░░░░░░░░░░░░░░░░░│
 ✓ gravé  ✓ gravé  ◉ en cours  · à venir (opacité 45 %)
```

- Les pistes déjà gravées passent à 100 % d'opacité avec une coche 8 px superposée.
- La piste en cours a un **balayage** (gradient qui se déplace, 1,6 s de période, désactivé si réduction de
  mouvement) sur la portion déjà écrite.
- Les pistes à venir sont à 45 % d'opacité.
- Une ligne verticale blanche 1 px marque la position exacte de l'écriture.

### 9.12 Pseudo-code de calcul (référence pour l'implémentation)

```c
/* Capacité en clusters. Un cluster = 2 s d'audio SP. */
#define CLUSTER_SP_SECONDS   2.0

static int clusters_capacity(int media_minutes) {          /* 60 / 74 / 80 */
    return (int)((media_minutes * 60.0) / CLUSTER_SP_SECONDS);
}

/* Secondes d'audio portées par un cluster selon le mode. */
static double seconds_per_cluster(md_mode m) {
    switch (m) {
        case MD_SP:   return 2.0;
        case MD_MONO: return 4.0;
        case MD_LP2:  return 4.0;
        case MD_LP4:  return 8.0;
    }
    return 2.0;
}

/* Clusters consommés par une piste : arrondi SUPÉRIEUR, toujours >= 1. */
static int clusters_for_track(double duration_s, md_mode m) {
    double spc = seconds_per_cluster(m);
    int c = (int)ceil(duration_s / spc);
    return c < 1 ? 1 : c;
}

/* Gaspillage en secondes d'audio pour cette piste. */
static double padding_seconds(double duration_s, md_mode m) {
    double spc = seconds_per_cluster(m);
    return clusters_for_track(duration_s, m) * spc - duration_s;
}

/* Temps restant, exprimé dans un mode donné. */
static double remaining_seconds(int cap_clusters, int used_clusters, md_mode m) {
    int free_c = cap_clusters - used_clusters;
    if (free_c < 0) free_c = 0;
    return free_c * seconds_per_cluster(m);
}
```

> **Note d'implémentation** : la valeur `CLUSTER_SP_SECONDS = 2.0` et les capacités par média doivent être
> **vérifiées empiriquement** contre plusieurs appareils avant d'être figées, et la jauge doit toujours
> se recaler sur l'espace libre réellement rapporté par l'appareil quand un disque est inséré (D-04),
> plutôt que sur son propre calcul. Le calcul local sert à la composition **hors ligne**.

---

## 10. Direction visuelle

### 10.1 Principes

1. **Densité d'outil pro.** Ligne de liste à 22–24 px. Pas de carte, pas d'ombre portée, pas de padding
   décoratif. On s'aligne sur VS Code / Linear / foobar2000, pas sur une app mobile.
2. **La couleur porte du sens, jamais de la décoration.** L'accent bleu ne sert qu'au focus et à l'action
   primaire ; les quatre couleurs de mode sont réservées aux modes ; le rouge ne sert qu'au danger réel.
3. **Sombre par défaut, clair complet.** Le public écoute de la musique le soir ; le sombre est le défaut,
   mais le clair doit être de qualité égale (accessibilité, environnements lumineux).
4. **Le mouvement explique, il ne divertit.** Toute animation doit soit montrer une causalité (« ce que j'ai
   ajouté a fait grandir cette barre »), soit masquer une latence. Rien d'autre.
5. **Chaque chiffre est comparable.** Chiffres tabulaires partout où des nombres s'alignent verticalement.

### 10.2 Grille et espacement

Reprise directe de `02b-design-tokens.md` :

- **Rythme** : 2 / 4 / 6 / 8 / 12 / 16 / 24 / 32 px (base 4, demi-pas 2 pour les micro-ajustements).
- **Grille de base 4 px** ; toute hauteur d'élément est un multiple de 2 px.
- **Gouttière entre panneaux** : 1 px de bordure, pas d'espace. Les panneaux sont contigus (modèle VS Code).
- **Padding interne des panneaux** : 8 px horizontal, 6 px vertical.
- **Padding de cellule de liste** : 8 px horizontal, 0 vertical (la hauteur de ligne fait le travail).
- **Indentation d'arbre** : 12 px par niveau, chevron 16 px.

### 10.3 Typographie

**Police retenue : Inter** (avec Inter Display pour les rares titres ≥ 20 px).

| Critère | Évaluation |
|---------|------------|
| Licence | **SIL Open Font License 1.1** — redistribution avec l'application autorisée, y compris commerciale. |
| Poids du fichier | ~340 Ko en WOFF2 par graisse ; en TTF statique sous-ensemblé (latin + latin étendu + symboles), **~110 Ko par graisse**, soit ~330 Ko pour Regular/Medium/SemiBold. Acceptable pour une app native embarquant son atlas. |
| Chiffres tabulaires | Oui — feature OpenType `tnum`. **Indispensable** : durées, compteurs, budgets. |
| Zéro barré | Oui — `zero` (`ss01`/`cv…` selon version). À activer dans les colonnes techniques. |
| Rendu à 11–13 px | Excellent, conçu pour l'écran, hauteur d'x élevée. |
| Couverture | Latin, latin étendu, grec, cyrillique. **Pas de CJK** → repli système nécessaire pour PP-16. |

**Alternatives évaluées**

| Police | Licence | Verdict |
|--------|---------|---------|
| **IBM Plex Sans** | OFL 1.1 | Excellente, chiffres tabulaires natifs, un peu plus large qu'Inter → moins dense. Second choix. |
| **Public Sans** | OFL 1.1 | Neutre, sobre, très bonne alternative. Moins de features OpenType. |
| **Segoe UI** | Propriétaire Microsoft, **non redistribuable** | Utilisable *sur Windows uniquement* via le système. Retenue comme **repli système**, pas comme police embarquée. |
| **Roboto / Roboto Mono** | Apache 2.0 | Correcte, mais très datée visuellement. |
| **JetBrains Mono** | OFL 1.1 | Retenue pour le **journal technique et les chemins de fichiers uniquement** (~200 Ko sous-ensemblée). |

**Pile de polices**

```
UI          : "Inter", "Segoe UI Variable", "Segoe UI", sans-serif
Chiffres    : "Inter" + font-feature-settings: "tnum" 1, "zero" 1
Monospace   : "JetBrains Mono", "Cascadia Mono", "Consolas", monospace
CJK (repli) : "Yu Gothic UI", "Microsoft YaHei UI", "Malgun Gothic"
```

**Échelle typographique** (reprise de 02b) :

| Rôle | Taille / interligne | Graisse | Usage |
|------|---------------------|---------|-------|
| Micro-label | 10 / 14 | 500 | Graduations de jauge, badges |
| Caption | 11 / 16 | 400 | Métadonnées secondaires de l'inspecteur |
| Secondaire | 12 / 16 | 400 | Barre de statut, texte d'aide, résiduels |
| **Corps (défaut UI)** | **13 / 18** | 400 | Listes, menus, boutons, labels |
| Emphase | 14 / 20 | 600 | Compteur de la jauge, valeurs importantes |
| Titre de section | 16 / 24 | 600 | En-têtes de panneau |
| Titre de vue | 20 / 28 | 600 | Titres de dialogues, écrans vides |

**Règles**

- Les en-têtes de colonnes de liste : 11 px, graisse 500, **majuscules avec `letter-spacing: 0.4px`**,
  couleur texte désactivé.
- Aucune ligne de texte au-dessus de **90 caractères**.
- Le nom de piste tronque avec `…` **au milieu** pour les chemins de fichiers, **en fin** pour les titres.
- Alignement à droite avec chiffres tabulaires pour : durée, taille, débit, compteurs, pourcentages.

### 10.4 Palette

**Thème sombre — application des tokens de 02b**

| Token | Valeur | Usage |
|-------|--------|-------|
| `--bg-canvas` | `#111113` | Fond de la fenêtre, séparateurs entre panneaux |
| `--bg-panel` | `#18191B` | Navigateur, inspecteur, barre d'appareil |
| `--bg-surface` | `#1F1F1F` | Liste de bibliothèque, liste du plan |
| `--bg-control` | `#212225` | Champs, boutons secondaires, zone libre de la jauge |
| `--bg-control-hover` | `#272A2D` | Survol de contrôle |
| `--bg-row-hover` | `#2A2D2E` | Survol de ligne de liste |
| `--bg-row-selected` | `#04395E` | Sélection active |
| `--bg-row-selected-inactive` | `#37373D` | Sélection quand le panneau n'a pas le focus |
| `--border-subtle` | `#2B2B2B` | Séparateurs internes, en-têtes de colonne |
| `--border-control` | `#3C3C3C` | Bordure de champ, de bouton |
| `--border-hover` | `#5A6169` | Bordure survolée |
| `--fg-primary` | `#EDEEF0` | Titres, valeurs |
| `--fg-secondary` | `#B0B4BA` | Métadonnées, artistes, durées secondaires |
| `--fg-muted` | `#777B84` | Labels d'échelle, placeholders |
| `--fg-disabled` | `#696E77` | Contrôles désactivés |
| `--accent` | `#0090FF` | Bouton primaire, focus, liens |
| `--accent-hover` | `#3B9EFF` | |
| `--accent-fg` | `#FFFFFF` | Texte sur accent |
| `--success` | `#3FB950` (fond `#2EA043`) | Succès, LP2 |
| `--warning` | `#D29922` (fond `#9E6A03`) | Avertissement, LP4, ≥ 90 % |
| `--danger` | `#F85149` (fond `#DA3633`) | Erreur, dépassement |
| `--focus-ring` | `#0090FF` | 2 px, offset 1 px |

**Fonds sémantiques translucides** (pour les bandeaux inline) : `--warning` à 10 % sur `--bg-surface`,
`--danger` à 10 %, `--success` à 10 %, avec une bordure gauche 2 px pleine.

**Thème clair (esquisse, à figer en ADR)**

| Token | Valeur |
|-------|--------|
| `--bg-canvas` | `#F8F8F9` |
| `--bg-panel` | `#F1F1F3` |
| `--bg-surface` | `#FFFFFF` |
| `--bg-control` | `#F1F1F3` |
| `--bg-row-hover` | `#E8E8EA` |
| `--bg-row-selected` | `#CCE3FA` |
| `--border-subtle` | `#E4E4E7` |
| `--border-control` | `#C9CACE` |
| `--fg-primary` | `#1C1C1E` |
| `--fg-secondary` | `#5A5C63` |
| `--fg-muted` | `#8B8D94` |
| `--accent` | `#0069C2` |
| Modes MD | SP `#0069C2` · mono `#8250DF` · LP2 `#1A7F37` · LP4 `#9A6700` |

Les couleurs de mode en thème clair sont **assombries** par rapport au sombre pour garder ≥ 4,5:1 sur blanc.

### 10.5 Formes, bordures, rayons

| Élément | Rayon | Bordure |
|---------|-------|---------|
| Bouton, champ, sélecteur | **4 px** | 1 px `--border-control` |
| Ligne de liste sélectionnée | **3 px** (avec 2 px de marge horizontale) ou 0 (pleine largeur) — *voir Q-08* | aucune |
| Panneau superposé, popover, menu | **6 px** | 1 px `--border-subtle` + ombre `0 8px 24px rgba(0,0,0,.45)` |
| Dialogue modal | **8 px** | idem |
| Barre de jauge | **2 px** (extrémités seulement) | aucune |
| Badge / pastille | **3 px** (rectangle) ou cercle 8 px pour les pastilles de mode | aucune |
| Pochette d'album | **3 px** | 1 px `rgba(255,255,255,.06)` |

**Ombres** : réservées aux surfaces *flottantes*. Aucune ombre sur les éléments en flux.

### 10.6 Focus et sélection

- **Anneau de focus** : 2 px `--accent`, offset 1 px, rayon = rayon de l'élément + 1. **Jamais supprimé.**
- **Focus dans une liste** : la ligne focalisée porte un **contour 1 px accent en pointillés 1/1**, distinct
  de la sélection (fond plein). On peut donc avoir « ligne sélectionnée » ≠ « ligne focalisée ».
- **Panneau actif** : le panneau qui a le focus porte un liseré 2 px `--accent` sur son bord haut *(à
  valider — voir Q-09)*. À défaut, la différence sélection active/inactive suffit.
- **Sélection multiple** : `Maj+clic` (plage), `Ctrl+clic` (ajout/retrait), rectangle de sélection à la
  souris dans les listes.

### 10.7 Iconographie

- **Set** : icônes 16 px, trait 1,25 px, grille 16, angles à 90°/45°, coins nets (pas d'arrondi de trait).
  Aligné sur l'esprit **Codicon** / **Lucide**.
- **Sourcing** : Lucide (licence ISC, redistribuable) comme base, **plus un set custom d'une quinzaine
  d'icônes spécifiques au domaine** qui n'existent nulle part :

| Icône custom | Représentation |
|--------------|----------------|
| Disque MD | Rectangle à coin coupé avec fenêtre circulaire |
| Disque MD inséré | Idem + trait de fente |
| SP / LP2 / LP4 / Mono | Pastilles typographiques (`S`, `2`, `4`, `M`) dans un carré 3 px de rayon |
| Cluster | Petit damier 4×2 |
| TOC | Trois traits inégaux dans un cadre |
| Groupe | Accolade verticale + trois traits |
| Type-R / Type-S | Badge textuel |
| Graver | Flèche vers un disque MD |
| Extraire | Flèche depuis un disque MD |
| Wipe | Disque MD avec un trait diagonal |

- **Rendu** : les icônes sont des SDF (signed distance field) dans un atlas OpenGL, ce qui donne un rendu
  net à tous les facteurs DPI sans multiplier les assets.
- **Règle** : aucune icône seule sans libellé dans le flux principal, sauf les 6 icônes universelles
  (lecture, pause, fermer, rafraîchir, recherche, réglages). Sinon, icône + texte.

### 10.8 Animation

| Propriété | Valeur |
|-----------|--------|
| Durée « micro » (survol, focus, pastille) | **80 ms** |
| Durée « standard » (panneau, popover, segment de jauge) | **160 ms** |
| Durée « ample » (ouverture de dialogue, bascule de vue) | **220 ms** |
| Courbe d'entrée | `cubic-bezier(0.0, 0.0, 0.2, 1.0)` (décélération) |
| Courbe de sortie | `cubic-bezier(0.4, 0.0, 1.0, 1.0)` (accélération) |
| Courbe symétrique | `cubic-bezier(0.4, 0.0, 0.2, 1.0)` |
| Propriétés animées | opacité, transformation, largeur de segment, couleur. **Jamais** la position d'éléments de liste au scroll. |
| Fréquence cible | 60 fps minimum, 120 fps si l'écran le permet ; l'UI OpenGL est **rendue à la demande**, pas en boucle continue, pour ne pas chauffer le CPU pendant une gravure de 80 min. |
| Réduction de mouvement | Si le système la demande : toutes les durées → 0 ms sauf les indicateurs de progression indéterminés, qui deviennent un texte pulsant lentement. |

### 10.9 Accessibilité (au-delà de la jauge)

- **Contraste** : tout texte ≥ 4,5:1 ; texte ≥ 18,66 px ou gras ≥ 14 px ≥ 3:1 ; composants graphiques et
  bordures d'état ≥ 3:1.
- **Taille de cible** : les cibles cliquables font au moins 24 × 24 px de zone active, même si le visuel est
  plus petit (les lignes de 22 px sont pleine largeur donc conformes).
- **Clavier** : 100 % des fonctions accessibles au clavier (§8.8) ; aucun piège de focus ; `Échap` sort
  toujours.
- **Lecteur d'écran** : implémentation **UI Automation** obligatoire malgré l'UI custom — c'est le coût
  d'entrée d'une UI non native sur Windows (PP-30). Chaque liste expose un `DataGrid`, chaque ligne un `Row`,
  la jauge un `Group` avec valeur textuelle.
- **Zoom** : facteurs 100 / 125 / 150 / 175 / 200 % gérés par mise à l'échelle de la grille de base (4 px →
  5 / 6 / 7 / 8 px) avec réhinting du texte, pas par agrandissement bitmap.
- **Daltonisme** : voir §9.9 (initiales + motifs optionnels).
- **Épilepsie** : aucun clignotement > 3 Hz ; le balayage de gravure est lent (1,6 s de période).

---

## 11. Catalogue de micro-interactions

> Format : **déclencheur → retour immédiat → transition → état final**, avec la durée et la raison d'être.

### 11.1 Bibliothèque et sélection

| # | Micro-interaction | Détail |
|---|-------------------|--------|
| MI-01 | **Survol de ligne** | Fond `--bg-row-hover` en 80 ms. Les actions de fin de ligne (⋯, →) apparaissent en fondu 80 ms. *Raison : découvrabilité sans encombrement.* |
| MI-02 | **Recherche incrémentale** | La liste se re-filtre à chaque frappe sans debounce jusqu'à 20 000 pistes ; au-delà, debounce 60 ms. Les correspondances sont **surlignées** (fond accent 18 %). Un compteur `8 résultats` s'anime en changeant de valeur (pas de fondu, juste le chiffre). *Raison : la latence perçue est le premier critère de qualité d'un moteur de recherche.* |
| MI-03 | **Recherche sans résultat** | Après 300 ms sans résultat : « Aucun résultat pour *xyz* » + suggestions (« Chercher aussi dans les chemins de fichiers ? »). Jamais de vide brut. |
| MI-04 | **Ajout au plan (`Entrée`)** | La ligne source **pulse** brièvement (fond accent 25 % → 0 en 200 ms), une pastille `→` apparaît en fin de ligne, et le segment correspondant **grandit** dans la jauge en 160 ms. *Raison : lier cause et effet à travers deux panneaux distants — c'est la micro-interaction la plus importante du produit.* |
| MI-05 | **Ajout multiple** | Les segments apparaissent en cascade avec 20 ms de décalage entre chacun, plafonné à 300 ms au total. *Raison : rendre lisible « j'ai ajouté 12 morceaux » sans faire attendre.* |
| MI-06 | **Ajout d'une piste déjà présente** | Pas de doublon silencieux : la ligne existante du plan clignote une fois (120 ms) et une info-bulle apparaît : « Déjà dans le plan (piste 4) ». Maintenir `Ctrl` force le doublon. |
| MI-07 | **Sélection au rectangle** | Rectangle 1 px accent, remplissage 12 %. Compteur flottant près du curseur : `7 pistes · 28:14`. |
| MI-08 | **Défilement** | Défilement par pixel (pas par ligne), barre de défilement fine 8 px qui s'élargit à 12 px au survol. Overscroll : rien (pas de rebond). |

### 11.2 Plan de disque

| # | Micro-interaction | Détail |
|---|-------------------|--------|
| MI-09 | **Glisser-déposer de réordonnancement** | Au `mousedown` + 4 px de mouvement : la ligne se soulève (ombre, opacité 90 %), les lignes s'écartent avec un **espace de 22 px** qui s'ouvre en 120 ms. Un **trait accent 2 px** marque la position d'insertion. Au relâchement, la ligne se pose en 160 ms et la jauge **réorganise ses segments** avec la même durée. |
| MI-10 | **Glisser-déposer hors de la zone** | Le curseur affiche un `✕` et la ligne devient rouge à 30 % : le relâchement retire la piste (avec annulation possible). |
| MI-11 | **Changement de mode d'une piste** | Le segment de la jauge **change de couleur en 160 ms et change de largeur en même temps** (interpolation simultanée). Les segments suivants glissent. La durée facturée de l'inspecteur se met à jour en compteur roulant (les chiffres montent/descendent, 200 ms). |
| MI-12 | **Changement de mode en masse** | Toute la jauge se recolore en 220 ms avec un décalage de 8 ms par segment (effet de vague de gauche à droite). *Raison : rendre spectaculaire et donc mémorable l'action qui débloque le plus de place.* |
| MI-13 | **Franchissement du seuil 90 %** | La ligne de lecture passe en ambre avec une transition de couleur de 160 ms ; un tressaillement de 2 px vers le haut (une seule fois). |
| MI-14 | **Franchissement du dépassement** | La barre prend sa bordure rouge, le chevron de dépassement **entre par la droite** en 160 ms, et le bandeau `[Résoudre]` s'ouvre en hauteur (0 → 24 px, 160 ms). *Aucun son, aucune modale.* |
| MI-15 | **Retour sous la limite** | Le chevron sort par la droite, le bandeau se ferme, et la ligne de lecture fait un bref éclat vert (fond `--success` à 15 %, 300 ms). *Raison : récompenser la résolution.* |
| MI-16 | **Remplissage parfait (100,0 %)** | Le compteur passe en vert et un liseré vert 1 px parcourt la barre une fois de gauche à droite (500 ms). *Petit moment de joie : c'est le graal du mixtapeur.* |
| MI-17 | **Édition inline d'un titre** | `F2` ou double-clic : la cellule devient un champ avec le texte présélectionné, bordure accent. Un compteur de caractères apparaît à droite (`21 / 1785`), qui **décompte le budget global**, pas la longueur locale. `Entrée` valide et passe à la ligne suivante en édition ; `Échap` annule ; `Tab` passe à la colonne suivante. |
| MI-18 | **Dépassement du budget TOC pendant la saisie** | Les caractères en trop s'affichent sur fond rouge 25 % **sans être bloqués** (on laisse taper, on prévient). La barre TOC devient rouge. |
| MI-19 | **Création d'un groupe** | Les lignes sélectionnées se rassemblent visuellement (les non-contiguës glissent pour devenir contiguës en 220 ms) puis un en-tête de groupe se déplie au-dessus (0 → 24 px). *Raison : rendre visible la contrainte de contiguïté au lieu de l'expliquer.* |
| MI-20 | **Repli / dépli d'un groupe** | Chevron qui pivote en 120 ms, contenu qui se replie en 160 ms. La jauge ne change pas (les segments restent). |
| MI-21 | **Piste en erreur** | Badge `⚠` avec un léger tressaillement à l'apparition (une fois). Au survol, info-bulle avec la cause. Le segment de la jauge devient hachuré rouge. |
| MI-22 | **Annuler (`Ctrl+Z`)** | La modification s'inverse avec la même animation que l'action originale, plus un toast discret en bas à gauche : « Ajout de 12 pistes annulé · Rétablir (Ctrl+Y) », qui s'efface en 4 s. |

### 11.3 Appareil et gravure

| # | Micro-interaction | Détail |
|---|-------------------|--------|
| MI-23 | **Branchement de l'appareil** | La barre d'appareil passe de gris à `--success` avec une transition de 220 ms, la pastille `●` fait une pulsation unique. Aucun son, aucune modale, aucun vol de focus. |
| MI-24 | **Débranchement** | La barre passe en gris en 220 ms ; si une opération était en cours, bandeau d'erreur inline. Le plan reste **intact et éditable**. |
| MI-25 | **Insertion d'un disque** | `◐ Lecture du disque…` avec un spinner discret ; la jauge affiche un squelette pendant la lecture du TOC ; puis le repère « déjà sur le disque » **glisse** en place en 220 ms. |
| MI-26 | **Survol du bouton Graver** | Le bouton révèle son coût en dessous : `~9 min · 10 pistes · 47:12`. *Raison : jamais de clic aveugle sur une action longue.* |
| MI-27 | **Clic sur Graver** | Récapitulatif inline (pas modal, un panneau qui se déplie sous le bouton) : liste des opérations, durée estimée, avertissements. Bouton `[Démarrer la gravure]` **qui a le focus par défaut** pour permettre `Ctrl+Entrée` puis `Entrée`. |
| MI-28 | **Progression de gravure** | Trois niveaux simultanés : la jauge (§9.11), la barre d'appareil (piste courante + %), la barre de tâches Windows (progression native). Le temps restant est calculé sur une **moyenne glissante des 30 dernières secondes**, jamais sur l'instantané, et **ne remonte jamais** (on lisse à la baisse). |
| MI-29 | **Ralentissement inattendu** | Si le débit chute de plus de 40 % pendant 15 s : ligne d'information non alarmante « Transfert plus lent que prévu — c'est normal en SP » ou « Vérifiez le câble USB ». |
| MI-30 | **Annulation d'une gravure** | Confirmation inline : « Annuler ? Les 4 pistes déjà gravées resteront sur le disque. » avec `[Annuler la gravure]` / `[Continuer]`. Après annulation, le plan repasse en éditable et affiche `[Reprendre]`. |
| MI-31 | **Fin de gravure** | La jauge se remplit intégralement de coches vertes en cascade (20 ms/segment), un toast persistant apparaît : « Disque gravé · 10 pistes · 47:12 · [Ajouter au catalogue] [Éjecter] ». Notification Windows si la fenêtre n'a pas le focus. **Aucun son par défaut** (option). |
| MI-32 | **Opération destructive** | Tout bouton destructif (`Effacer`, `Effacer tout le disque`) demande une confirmation **qui énonce le contenu détruit** (« 12 pistes, 61:18 ») et exige un clic sur un bouton rouge dont le libellé est le verbe (`Effacer les 12 pistes`), jamais `OK`. Pour le wipe complet : saisie du nom du disque pour confirmer *(à valider — Q-11)*. |
| MI-33 | **Sauvegarde du TOC** | Avant chaque écriture TOC, ligne d'état discrète : `Sauvegarde du TOC…  ✓` (2 s puis disparition). *Raison : rendre visible une garantie invisible.* |
| MI-34 | **Erreur USB** | Bandeau rouge inline dans la barre d'appareil, message humain, `▸ Détails techniques` repliés contenant la commande NetMD, ses arguments hexadécimaux et le code libusb, avec un bouton **[Copier]**. |

### 11.4 Système et transversal

| # | Micro-interaction | Détail |
|---|-------------------|--------|
| MI-35 | **Palette de commandes** | `Ctrl+Maj+P` : panneau centré en haut, apparition en 160 ms (fondu + translation 8 px vers le bas). Correspondance floue, actions récentes en tête, raccourci affiché à droite de chaque entrée. |
| MI-36 | **Info-bulle** | Délai d'apparition 350 ms, disparition immédiate ; 0 ms de délai si une info-bulle est déjà visible (mode « chaîné »). Max 2 lignes, 280 px. |
| MI-37 | **Info-bulle pédagogique** | Sur les termes du domaine (SP, LP4, TOC, Type-R) : icône `ⓘ` discrète, info-bulle enrichie de 3 lignes + lien *En savoir plus*. |
| MI-38 | **Indexation en tâche de fond** | Barre de statut : `Indexation… 4 812 / ~12 000`. La barre de progression est **dans la barre de statut**, pas en modale. Cliquer dessus ouvre le détail (dossier en cours) ; on peut mettre en pause. |
| MI-39 | **Redimensionnement de panneau** | Poignée de 4 px (zone active 8 px), curseur `↔`, aperçu en temps réel (pas de ligne fantôme). Double-clic = largeur par défaut. Aimantation à 8 px des largeurs remarquables. |
| MI-40 | **Redimensionnement de colonne** | Idem ; double-clic sur le séparateur = ajustement au contenu. |
| MI-41 | **Toast** | En bas à gauche, 280 px, 4 s, empilable jusqu'à 3, avec bouton d'action optionnel. Entrée par translation 8 px + fondu 160 ms. |
| MI-42 | **Sauvegarde automatique** | Le point `•` du titre disparaît quand la sauvegarde est faite, avec un fondu de 200 ms. Aucun indicateur bruyant. |
| MI-43 | **Ouverture de dialogue** | Fondu du voile (0 → 45 % de noir) 160 ms + dialogue qui monte de 12 px en 220 ms. `Échap` ferme. Le clic hors du dialogue ferme uniquement si aucune donnée n'est saisie. |
| MI-44 | **Diagnostic vert → rouge** | L'indicateur de la barre de statut passe au rouge et fait une pulsation unique. Aucune modale : on n'interrompt jamais l'utilisateur pour un problème qui n'affecte pas son action courante. |
| MI-45 | **Écoute d'une piste (`Espace`)** | Lecture instantanée avec **fondu d'entrée de 30 ms** (évite les clics) ; la ligne acquiert une icône `▶` et une barre de progression fine dans son fond ; `Espace` à nouveau arrête. Le transport en bas suit. |
| MI-46 | **Écoute d'une transition** | Sur une jonction entre deux pistes : lit les 8 dernières secondes de la piste n, la jonction, et les 8 premières de n+1, avec un marqueur visuel du point de bascule. |
| MI-47 | **Copie de valeur** | `Ctrl+C` sur une sélection de liste copie un TSV propre (colonnes visibles) ; toast « 12 lignes copiées ». |
| MI-48 | **Premier lancement** | Trois écrans (dossier de musique · appareil · thème/densité), chacun sautable, avec le vrai contenu qui se remplit derrière. Aucune inscription, aucun compte, aucune connexion réseau. |

---

## 12. Copywriting — 30 strings FR/EN

**Principes de ton**

1. **Direct, pas jovial.** L'utilisateur est un adulte qui fait un travail. Pas d'exclamations, pas d'emoji
   dans les messages, pas de « Oups ! ».
2. **Le verbe d'abord.** Les boutons portent l'action (`Graver le disque`), jamais `OK`.
3. **Une erreur = cause + conséquence + action.** Trois éléments, dans cet ordre, en une à deux phrases.
4. **Le vocabulaire du domaine est conservé** (SP, LP2, LP4, TOC, NetMD) mais **toujours explicable** par
   une info-bulle. On ne simplifie pas en « qualité haute/moyenne/basse » : la communauté connaît ses termes.
5. **En français** : infinitif pour les actions, pas d'impératif tutoyant, guillemets français, espace
   insécable avant `:` `?` `!` `;` et dans les nombres.
6. **En anglais** : sentence case partout (pas de Title Case sur les boutons), pas de point final sur les
   libellés courts.

### 12.1 Table des chaînes

| # | Clé | FR | EN | Contexte |
|---|-----|----|----|----------|
| 1 | `app.tagline` | Composez vos MiniDiscs avant de graver. | Design your MiniDiscs before you burn. | Écran d'accueil, sous le logo |
| 2 | `library.empty.title` | Aucune musique pour l'instant | No music yet | État vide de la bibliothèque |
| 3 | `library.empty.body` | Ajoutez un dossier contenant vos fichiers audio. L'indexation se fait en arrière-plan : vous pouvez commencer à composer tout de suite. | Add a folder with your audio files. Indexing runs in the background — you can start building right away. | idem |
| 4 | `library.empty.action` | Ajouter un dossier… | Add folder… | Bouton primaire |
| 5 | `plan.empty.title` | Ce disque est vide | This disc is empty | État vide du plan |
| 6 | `plan.empty.body` | Glissez des morceaux ici, ou sélectionnez-les dans la bibliothèque et appuyez sur Entrée. | Drag tracks here, or pick them in the library and press Enter. | idem |
| 7 | `gauge.readout` | {used} / {total} · reste {rSP} en SP · {rLP2} en LP2 · {rLP4} en LP4 | {used} / {total} · {rSP} left in SP · {rLP2} in LP2 · {rLP4} in LP4 | Ligne de lecture de la jauge |
| 8 | `gauge.overflow` | Dépassement de {amount}. Ce disque ne peut pas tout contenir. | Over capacity by {amount}. This disc can't fit everything. | Bandeau de dépassement |
| 9 | `gauge.overflow.action` | Résoudre | Fix this | Bouton du bandeau |
| 10 | `gauge.full` | Disque plein — pile poil. | Disc full — right to the second. | Remplissage parfait |
| 11 | `gauge.padding.tooltip` | {track} · {duration} · {mode} · facturé {billed} ({pad} perdus par arrondi) | {track} · {duration} · {mode} · billed {billed} ({pad} lost to rounding) | Info-bulle de segment |
| 12 | `resolve.suggestion.lp2` | Passer tout le disque en LP2 · gain {gain} · gravure {speed}× plus rapide | Switch the whole disc to LP2 · frees {gain} · burns {speed}× faster | Suggestion du panneau Résoudre |
| 13 | `resolve.suggestion.trim` | Rogner les silences de fin de piste · gain {gain} | Trim trailing silence · frees {gain} | idem |
| 14 | `toc.budget` | Titres : {used} / {total} caractères | Titles: {used} / {total} characters | Barre TOC |
| 15 | `toc.overflow` | Les titres dépassent la capacité du disque de {n} caractères. Ils seront tronqués. | Titles exceed the disc's capacity by {n} characters. They will be truncated. | Avertissement TOC |
| 16 | `toc.autoshorten` | Raccourcir automatiquement | Shorten automatically | Bouton |
| 17 | `device.none` | Aucun appareil — vous pouvez composer un disque quand même. | No device — you can still build a disc. | Barre d'appareil |
| 18 | `device.driverMissing` | {model} détecté, mais le pilote Windows n'est pas installé. | {model} detected, but the Windows driver isn't installed. | Barre d'appareil, ambre |
| 19 | `device.driverMissing.action` | Installer le pilote | Install driver | Bouton |
| 20 | `device.connected` | {model} · {media} min · {tracks} pistes · libre {free} en SP | {model} · {media} min · {tracks} tracks · {free} free in SP | Barre d'appareil |
| 21 | `device.writeProtected` | Ce disque est protégé en écriture. Faites glisser le loquet à l'arrière de la cartouche. | This disc is write-protected. Slide the tab on the back of the cartridge. | Avertissement |
| 22 | `burn.button` | Graver le disque | Burn disc | Action primaire |
| 23 | `burn.button.hint` | {tracks} pistes · {duration} · environ {eta} de gravure | {tracks} tracks · {duration} · about {eta} to burn | Sous-texte au survol |
| 24 | `burn.spWarning` | Le mode SP est encodé par l'appareil en temps réel : la gravure durera environ {eta}. Le LP2 serait {speed}× plus rapide. | SP is encoded by the device in real time, so this will take about {eta}. LP2 would be {speed}× faster. | Récapitulatif avant gravure |
| 25 | `burn.progress` | Gravure {i}/{n} · {title} · {percent} % · fin vers {clock} | Burning {i}/{n} · {title} · {percent}% · done around {clock} | Progression |
| 26 | `burn.done` | Disque gravé · {tracks} pistes · {duration} | Disc burned · {tracks} tracks · {duration} | Toast de fin |
| 27 | `burn.interrupted` | Gravure interrompue après {done} pistes sur {total}. Les pistes déjà gravées sont sur le disque. | Burn stopped after {done} of {total} tracks. The tracks already written are on the disc. | Bandeau |
| 28 | `erase.confirm.title` | Effacer {n} pistes ? | Erase {n} tracks? | Confirmation |
| 29 | `erase.confirm.body` | {n} pistes ({duration}) seront supprimées du disque. Cette action est irréversible : le MiniDisc ne conserve pas de corbeille. | {n} tracks ({duration}) will be removed from the disc. This can't be undone — a MiniDisc has no recycle bin. | idem |
| 30 | `erase.confirm.action` | Effacer les {n} pistes | Erase {n} tracks | Bouton rouge |
| 31 | `toc.backup` | Sauvegarde du TOC effectuée avant modification. | TOC backed up before changes. | État discret |
| 32 | `error.usb.generic` | La communication avec {model} s'est interrompue. Débranchez puis rebranchez l'appareil, de préférence sur un port USB directement sur l'ordinateur. | Communication with {model} was interrupted. Unplug and replug the device, preferably into a port directly on the computer. | Erreur |
| 33 | `error.usb.details` | Détails techniques | Technical details | Dépliant |
| 34 | `error.source.missing` | Fichier introuvable : {path} | File not found: {path} | Ligne de plan en erreur |
| 35 | `error.source.missing.action` | Relocaliser… | Locate… | Bouton |
| 36 | `error.decode` | Ce fichier n'a pas pu être décodé. Il est peut-être tronqué ou dans un format non pris en charge. | This file couldn't be decoded. It may be truncated or in an unsupported format. | Erreur de piste |
| 37 | `diagnostic.ok` | Tout est prêt | Everything checks out | Panneau Diagnostic |
| 38 | `diagnostic.encoderMissing` | L'encodeur haute qualité n'est pas installé. L'encodeur intégré sera utilisé. | The high-quality encoder isn't installed. The built-in encoder will be used instead. | Diagnostic, ambre |
| 39 | `group.contiguityWarning` | Les groupes MiniDisc doivent contenir des pistes qui se suivent. Les pistes sélectionnées vont être regroupées dans l'ordre. | MiniDisc groups must contain consecutive tracks. The selected tracks will be reordered together. | Avant création de groupe |
| 40 | `mode.sp.tooltip` | SP · qualité maximale · 80 min sur un disque de 80 · gravure en temps réel | SP · best quality · 80 min on an 80-min disc · burns in real time | Info-bulle de mode |

> 40 chaînes fournies (le brief en demandait 30) : les 10 supplémentaires couvrent les cas d'erreur, qui sont
> le principal terrain de progrès face à l'existant (PP-04).

### 12.2 Anti-exemples (ce qu'on n'écrit pas)

| ✗ À éviter | ✓ À écrire | Pourquoi |
|-----------|-----------|----------|
| « Oups ! Quelque chose s'est mal passé. » | « La communication avec MZ-N710 s'est interrompue. Débranchez puis rebranchez l'appareil. » | PP-04 : le message générique est le pire défaut de l'existant. |
| « Êtes-vous sûr ? » / `OK` `Annuler` | « Effacer les 12 pistes » / « Conserver » | Le bouton doit dire ce qu'il fait. |
| « Erreur 0x8007001F » | « Le pilote n'a pas pu être installé. ▸ Détails (0x8007001F) » | Le code reste, mais n'est plus le message. |
| « Chargement… » | « Lecture du disque… » / « Analyse de 12 fichiers… » | Dire *quoi*. |
| « Transfert terminé avec succès ! » | « Disque gravé · 10 pistes · 47:12 » | Le fait, pas la félicitation. |
| « Qualité : Haute / Moyenne / Basse » | « SP / LP2 / LP4 » avec info-bulles | Le vocabulaire de la communauté est un signe de respect. |

---

## 13. Matrice concurrentielle

### 13.1 Outils NetMD

| Critère | Web MiniDisc Pro | ElectronWMD | Platinum MD | SonicStage 4.3 | **mini-disk (cible)** |
|---------|------------------|-------------|-------------|----------------|------------------------|
| Nature | Web (WebUSB) | Electron | Electron (fork) | Natif Win (abandonné) | **Natif Win, C + OpenGL** |
| Installation | Aucune (mais Zadig) | Installeur | Installeur | Installeur lourd | Installeur léger + portable |
| DRM / OpenMG | Aucun | Aucun | Aucun | **Oui (bloquant)** | Aucun |
| Bibliothèque musicale | ✗ | ✗ | ✗ | Partielle (base OpenMG) | **✔ complète** |
| Import de playlist | ✗ (#84, #44) | ✗ (#30) | ✗ | Partiel | **✔ M3U/PLS/XSPF/CUE** |
| Composition hors appareil | ✗ | ✗ | ✗ | Partielle | **✔ document `.mdplan`** |
| Jauge de capacité live | ✗ (#80) | ✗ | ✗ | Basique | **✔ segmentée, clusters, tri-modale** |
| Mode par piste | ✗ (global) | ✗ | ✗ | ✗ | **✔** |
| Calcul en clusters | ✗ | ✗ | ✗ | ✗ (interne) | **✔ visible** |
| Budget TOC visible | ✗ | ✗ | ✗ | ✗ | **✔** |
| Titrage auto par gabarit | Partiel | Partiel | Partiel | ✔ | **✔ + repli en cascade** |
| Groupes | ✔ | ✔ | ✔ | ✔ | **✔ + coût visible** |
| Normalisation loudness | ✗ | ✗ | ✗ | ✗ | **✔ EBU R128** |
| Cache de transcodage | ✗ | ✗ | ✗ | (base interne) | **✔ adressé par contenu** |
| Annuler / Rétablir | ✗ | ✗ | ✗ | ✗ | **✔ illimité sur le plan** |
| Raccourcis clavier | ✗ (#42) | ✗ | ✗ | Partiels | **✔ complets + palette** |
| Sauvegarde du TOC | ✗ | ✗ | ✗ | ✗ | **✔ automatique** |
| Simulation avant écriture | ✗ | ✗ | ✗ | ✗ | **✔** |
| Extraction MD → PC | ✔ (RH1 + homebrew) | ✔ | ✔ | ✔ (RH1) | ✔ RH1 (v1.1) |
| Hi-MD | Partiel | Partiel | ✔ | ✔ | ✗ v1 (v2) |
| Mode homebrew / factory | **✔ (unique)** | ✔ | ✗ | ✗ | ✗ v1 |
| Multi-plateforme | ✔ | ✔ | ✔ | ✗ | ✗ (Windows d'abord) |
| Messages d'erreur exploitables | ✗ (#41, #43) | ✗ | ✗ | ✗ | **✔ traduits + code copiable** |
| Catalogue de disques | ✗ | ✗ | ✗ | ✗ | **✔ (v1.1)** |
| Empreinte mémoire | ~300 Mo (navigateur) | ~250 Mo | ~250 Mo | ~120 Mo | **< 60 Mo cible** |
| Démarrage à froid | 1–3 s | 3–5 s | 3–5 s | 5–10 s | **< 400 ms cible** |

**Lecture.** Web MiniDisc Pro reste imbattable sur deux axes : la **portabilité** (rien à installer, tous
OS) et le **mode homebrew**. `mini-disk` n'attaque ni l'un ni l'autre. Il attaque le **flux de travail** :
tout ce qui se passe *avant* que le câble USB serve.

### 13.2 Gestionnaires de bibliothèque — ce qu'on emprunte

| Produit | Ce qu'on emprunte | Ce qu'on rejette |
|---------|-------------------|------------------|
| **foobar2000** | Densité extrême, colonnes configurables, moteur de requêtes, tout au clavier, empreinte minuscule, extensibilité. | Configuration initiale décourageante, esthétique par défaut ingrate, aucune considération pour le débutant. |
| **MusicBee** | Vues multiples (arbre / grille / liste), qualité de l'auto-tagging, gestion des pochettes, panneau de propriétés riche. | Densité d'options écrasante, incohérences visuelles entre les écrans. |
| **Swinsia** | Sobriété visuelle, absence de fioritures, focus sur les fichiers locaux, rapidité. | Fonctionnalités limitées, peu d'affordances avancées. |
| **SonicStage** | Le **modèle mental** juste : « bibliothèque à gauche, disque à droite, on transfère au milieu ». C'est la seule chose qu'il faisait bien. | DRM, lenteur, base propriétaire, dialogues modaux en cascade, terminologie opaque. |
| **Roon** | Soin de la typographie et de la mise en page, richesse des métadonnées, sentiment de qualité, transitions calmes. | Lourdeur, dépendance au serveur/réseau, densité insuffisante, prix. |
| **Plexamp** | Fluidité et polissage des micro-interactions, gestion des pochettes, sensation de réactivité. | Orienté streaming, densité faible, pas d'outillage. |

**Synthèse de l'emprunt** : la **structure** vient de SonicStage (corrigée), la **densité et le clavier**
viennent de foobar2000, les **vues et le tagging** de MusicBee, le **soin visuel** de Roon, la
**fluidité** de Plexamp. Ce qui n'existe nulle part et fait le produit : la **jauge**.

### 13.3 Positionnement en une phrase

> Là où Web MiniDisc Pro vous laisse *envoyer* des fichiers vers un MiniDisc, `mini-disk` vous laisse
> **concevoir un MiniDisc**, puis l'exécuter.

---

## 14. Questions ouvertes et défauts recommandés

> Chaque question a un **défaut recommandé** utilisable immédiatement, pour ne pas bloquer la conception.
> `⚑` marque les questions à trancher avant de coder, `○` celles qui peuvent attendre le premier prototype.

### 14.1 Produit et périmètre

| # | Question | Défaut recommandé | Priorité |
|---|----------|-------------------|----------|
| Q-01 | Le Hi-MD entre-t-il en v1 ? | **Non.** NetMD seul. Le Hi-MD représente une part disproportionnée des bugs de l'écosystème (EWMD #40/#47/#28, WMD #82/#98) pour une base installée plus petite. Rouvrir en v2 avec un budget dédié. | ⚑ |
| Q-02 | L'extraction MD → PC (RH1) est-elle en v1 ? | **Non, v1.1.** C'est le besoin de P2 (20 % des utilisateurs) mais c'est un chantier XL. La v1 doit être irréprochable sur la gravure. | ⚑ |
| Q-03 | Supporte-t-on le mode homebrew / factory ? | **Non.** C'est le territoire de Web MiniDisc Pro, et une source de risque matériel. On documente explicitement « pour cela, utilisez Web MiniDisc Pro » — la coopération vaut mieux que la duplication. | ⚑ |
| Q-04 | macOS / Linux ? | **Windows d'abord**, mais l'architecture isole la couche plateforme (USB, système de fichiers, fenêtre) dès le départ. Pas de portage promis publiquement. | ⚑ |
| Q-05 | Le catalogue de disques est-il un produit dans le produit ? | **Le limiter en v1.1** à : nom, date, contenu, emplacement physique en texte libre, recherche. Pas de photos, pas de scans, pas d'export. Réévaluer selon l'usage. | ○ |
| Q-06 | Enregistrement analogique (line-in) ? | **Non.** Le pilotage d'un enregistrement analogique temps réel est un produit différent. P5 sera partiellement servie par la normalisation et les marqueurs. | ○ |

### 14.2 Interaction et UI

| # | Question | Défaut recommandé | Priorité |
|---|----------|-------------------|----------|
| Q-07 | Un seul plan à la fois, ou des onglets ? | **Onglets dès la v1** (B-02). Le coût est modéré et l'usage « préparer une série de disques » est le cœur de P3. | ⚑ |
| Q-08 | Sélection en pleine largeur ou en pilule arrondie ? | **Pleine largeur, rayon 0.** Plus dense, plus « outil », cohérent avec VS Code et foobar2000. La pilule (Fluent/macOS moderne) coûte 4 px horizontaux et adoucit un produit qui ne doit pas l'être. | ⚑ |
| Q-09 | Comment marquer le panneau qui a le focus ? | **Par la couleur de sélection uniquement** (active `#04395E` vs inactive `#37373D`). Pas de liseré de panneau : c'est du bruit visuel permanent pour une information rarement nécessaire. | ○ |
| Q-10 | Le mode est-il par piste ou par disque ? | **Par piste**, avec un mode par défaut du plan appliqué à l'ajout. C'est ce qui rend possible le panneau « Résoudre » et c'est physiquement permis par le format. | ⚑ |
| Q-11 | Le wipe complet exige-t-il de retaper le nom du disque ? | **Non pour un disque sans titre ou vide** ; **oui pour un disque contenant plus de 5 pistes titrées** — la friction doit être proportionnelle à la perte. Alternative si trop lourd : maintenir le bouton 1,2 s. | ○ |
| Q-12 | La jauge affiche-t-elle le padding par défaut ? | **Oui**, mais discrètement (hachures 40 %). Un réglage *Afficher le gaspillage d'arrondi* permet de le désactiver. Le voir par défaut est pédagogique et différenciant. | ○ |
| Q-13 | Échelle de la jauge : mode majoritaire ou toujours SP ? | **Mode majoritaire du plan**, avec le mode indiqué dans le label (`min (SP)`). Toujours-SP est plus stable mais rend l'échelle inutilisable sur un disque entièrement LP4. Un réglage permet de forcer. | ⚑ |
| Q-14 | Que se passe-t-il si l'utilisateur ferme l'app pendant une gravure ? | **Refuser la fermeture** avec un dialogue expliquant le risque de TOC corrompu, et proposer *Annuler la gravure puis quitter*. | ⚑ |
| Q-15 | Densité par défaut ? | **Compact (22 px)** pour les listes, avec un réglage vers 26 et 32 px. Le public visé préfère la densité ; ceux qui souffrent peuvent élargir. | ○ |
| Q-16 | Thème par défaut ? | **Sombre**, mais respecter le réglage système de Windows au premier lancement si l'utilisateur est en thème clair. | ○ |
| Q-17 | La barre de titre est-elle custom ou native ? | **Custom (32 px)** pour intégrer les menus et le nom du plan, avec respect strict des boutons système Windows (positions, zones de survol, Snap Layouts). | ⚑ |

### 14.3 Technique visible par l'utilisateur

| # | Question | Défaut recommandé | Priorité |
|---|----------|-------------------|----------|
| Q-18 | Encodeur ATRAC3 par défaut ? | **Encodeur intégré (type `atracdenc`)**, pour que le chemin nominal ne dépende d'aucun binaire externe (PP-08). Les encodeurs de meilleure qualité sont proposés dans le Diagnostic comme une amélioration facultative, installables en un clic et vérifiés par empreinte. | ⚑ |
| Q-19 | Normalisation activée par défaut ? | **Non.** Modifier l'audio par défaut est une décision qu'on n'a pas le droit de prendre pour P2. Proposée à la première gravure : « Vos morceaux ont des niveaux très différents (−8 à −22 LUFS). Normaliser ? ». | ⚑ |
| Q-20 | Cible de normalisation ? | **−16 LUFS**, cohérent avec l'écoute nomade ; limiteur true-peak à −1 dBTP. Cible réglable. | ○ |
| Q-21 | Rognage de silence activé par défaut ? | **Non** — pour la même raison. Proposé dans le panneau « Résoudre » quand c'est utile. | ○ |
| Q-22 | Gabarit de titre par défaut ? | **`%artist% - %title%`** quand l'album a plusieurs artistes, **`%title%`** quand l'album est mono-artiste (l'artiste va alors dans le titre du disque). Détection automatique, réglage manuel possible. | ⚑ |
| Q-23 | Que faire des caractères non représentables ? | **Translittérer avec prévisualisation** (`é→e`, `ø→o`, `∞→inf`), jamais silencieusement supprimer. Table éditable. Avertir si le nombre de substitutions dépasse 10 % des caractères. | ⚑ |
| Q-24 | Que faire d'un fichier 48 kHz ? | **Rééchantillonner à 44,1 kHz** sans demander (c'est obligatoire pour le MD), mais l'indiquer dans l'inspecteur : `48 kHz → 44,1 kHz`. Un réglage permet de choisir la qualité du rééchantillonnage. | ○ |
| Q-25 | Emplacement du cache de transcodage ? | **`%LOCALAPPDATA%\mini-disk\cache`**, avec un plafond configurable (défaut 8 Go) et une éviction LRU. Déplaçable dans les préférences. | ○ |
| Q-26 | Format du fichier de plan ? | **JSON UTF-8 lisible**, extension `.mdplan`, avec chemins relatifs quand possible, hash SHA-256 des sources, et un numéro de version de schéma. Doit s'ouvrir dans un éditeur de texte : c'est un gage de confiance et de pérennité. | ⚑ |
| Q-27 | Gère-t-on plusieurs appareils branchés ? | **Un seul actif à la fois en v1**, avec un sélecteur si plusieurs sont détectés. Le multi-appareil simultané est un raffinement (D-29). | ○ |
| Q-28 | Reprise après interruption de gravure ? | **Oui dès la v1** — c'est la conséquence naturelle du diff plan↔disque, et c'est ce qui rend acceptable une gravure SP de 80 minutes. | ⚑ |
| Q-29 | Que fait-on si l'espace libre rapporté par l'appareil contredit notre calcul ? | **L'appareil fait foi.** Notre calcul est marqué comme estimation quand aucun disque n'est présent, et se recale silencieusement dès qu'un disque est lu. Si l'écart dépasse 2 clusters, on le journalise pour affiner le modèle. | ⚑ |
| Q-30 | Faut-il un son de fin de gravure ? | **Non par défaut**, notification Windows uniquement. Option *Signal sonore à la fin d'une gravure* pour ceux qui lancent 80 minutes de SP et partent. | ○ |

### 14.4 À valider par la recherche utilisateur

Ces points ne peuvent pas être tranchés depuis le corpus disponible et nécessitent 5 à 8 entretiens :

1. **La métaphore du « plan » est-elle comprise** sans explication, ou faut-il parler de « projet de disque » ?
2. Les utilisateurs **mélangent-ils réellement les modes** sur un même disque, ou est-ce une capacité
   théorique que personne n'exploite ? (Détermine si le mode par piste est un must ou un could.)
3. **La lenteur du SP est-elle un frein réel** ou une contrainte acceptée ? (Détermine l'ampleur de
   l'investissement dans les estimations et la reprise.)
4. Quel est le **volume réel de disques** géré par utilisateur ? (Détermine si le catalogue est un must.)
5. Les utilisateurs veulent-ils **imprimer des jaquettes** ? (B-29 est un pari.)
6. Le **budget TOC** est-il vécu comme une contrainte, ou personne ne titre-t-il assez pour l'atteindre ?
7. Combien acceptent **d'installer un pilote** ? Quel est le taux d'abandon réel à cette étape ?

---

## 15. Proposition de découpage MVP → v1

### 15.1 MVP « la démo qui convainc » (features M du cœur)

Objectif : prouver la thèse en une capture d'écran.

- Bibliothèque : indexation, liste plate, recherche, glisser-déposer (A-01 à A-05, A-10)
- Plan : ajout, réordonnancement, mode par piste, jauge segmentée en clusters, tri-modale
  (B-03 à B-09, B-25)
- Transcodage : décodage, rééchantillonnage, LP2/LP4, cache (C-01 à C-03, C-09)
- Appareil : détection, lecture du TOC, upload avec progression et annulation (D-01, D-04, D-06 à D-09)
- Titrage : gabarit + budget TOC (B-18, B-19)
- QoL : palette de commandes, raccourcis, annuler/rétablir (E-01 à E-04)

### 15.2 v1 « le produit »

Ajoute : import de playlists (A-08), vues arbre/dossiers (A-06, A-07), groupes (B-17, D-16),
panneau Résoudre (B-12), déplacement/effacement (D-13 à D-15), simulation (D-19), traduction des erreurs
(D-24), diagnostic (E-06), installation guidée du pilote (D-02), thèmes (E-11), FR/EN (E-15),
onboarding (E-20).

### 15.3 v1.1 et au-delà

- v1.1 : extraction RH1 (D-20), catalogue de disques (E-09), gravure incrémentale (D-12),
  sauvegarde/restauration de TOC (D-17), normalisation (C-06, C-07).
- v1.2 : CUE (A-09), export de jaquette (B-29), A/B d'encodage (C-08), pré-transcodage anticipé (C-12).
- v2 : Hi-MD (D-25), accessibilité UIA complète (E-14), portage.

### 15.4 Indicateurs de succès proposés

| Indicateur | Cible |
|-----------|-------|
| Temps entre premier lancement et premier disque gravé (P1, sans doc) | < 20 min |
| Taux de gravures qui débordent la capacité | ≈ 0 % |
| Taux de gravures échouées pour cause de source illisible | 0 % (détecté avant gravure) |
| Latence de mise à jour de la jauge après une modification | < 16 ms |
| Latence de recherche sur 100 000 pistes | < 30 ms |
| Démarrage à froid | < 400 ms |
| Mémoire résidente au repos, bibliothèque de 50 000 pistes | < 150 Mo |
| Part des actions réalisables au clavier | 100 % |
| Nombre de clics pour « ajouter un album et graver » | ≤ 4 |

---

## 16. Sources

### Web MiniDisc Pro (`asivery/webminidisc`)
- https://github.com/asivery/webminidisc
- https://raw.githubusercontent.com/asivery/webminidisc/master/README.md
- https://api.github.com/repos/asivery/webminidisc/issues?state=all — corpus d'issues dépouillé
- https://github.com/asivery/webminidisc/issues/104 — MZ-RH1 non reconnu
- https://github.com/asivery/webminidisc/issues/102 — MP3 VBR : crash de l'UI, disque inaccessible
- https://github.com/asivery/webminidisc/issues/101 — upload SP échoue (wireformat SPS non mappé)
- https://github.com/asivery/webminidisc/issues/100 — upload refusé malgré l'espace disponible
- https://github.com/asivery/webminidisc/issues/99 — upload SP/mono bloqué à 0 %
- https://github.com/asivery/webminidisc/issues/98 — accessibilité (bouton sans libellé) et Hi-MD sur Mac
- https://github.com/asivery/webminidisc/issues/95 — uploads AEA silencieux/corrompus
- https://github.com/asivery/webminidisc/issues/94 — reconnaissance de titres pleine largeur incomplète
- https://github.com/asivery/webminidisc/issues/93 — TOC corrompu après édition de titres
- https://github.com/asivery/webminidisc/issues/92 — retrait des numéros de piste en tête de titre
- https://github.com/asivery/webminidisc/issues/91 — AIFF en `application/octet-stream`
- https://github.com/asivery/webminidisc/issues/89 — encodeur ATRAC3 original via VM
- https://github.com/asivery/webminidisc/issues/88 — `Atrac3OSExportService`
- https://github.com/asivery/webminidisc/issues/87 — enregistrement à 48 kHz au lieu de 44,1
- https://github.com/asivery/webminidisc/issues/85 — transcodage bloqué (URL `atracdenc.js` malformée)
- https://github.com/asivery/webminidisc/issues/84 — demande d'import de playlist
- https://github.com/asivery/webminidisc/issues/83 — extractions au tempo accéléré
- https://github.com/asivery/webminidisc/issues/82 — problèmes de connexion Hi-MD
- https://github.com/asivery/webminidisc/issues/80 — afficher le temps restant selon le codec
- https://github.com/asivery/webminidisc/issues/78 — export CSV cassé par les virgules
- https://github.com/asivery/webminidisc/issues/77 — disques SonicStage marqués « protégés »
- https://github.com/asivery/webminidisc/issues/44 — import M3U / XSPF
- https://github.com/asivery/webminidisc/issues/43 — titres au « format groupe » sans groupes
- https://github.com/asivery/webminidisc/issues/42 — menu contextuel et raccourcis clavier
- https://github.com/asivery/webminidisc/issues/41 — « Oops… Something unexpected happened » après wipe
- https://github.com/asivery/webminidisc/issues/38 — caractères spéciaux corrompus au renommage
- https://github.com/asivery/webminidisc/issues/37 — titres chinois
- https://github.com/asivery/webminidisc/issues/36 — wipe échoué sur Panasonic SA-SV1
- https://github.com/asivery/webminidisc/issues/35 — contrôles de lecture / avance rapide
- https://github.com/asivery/webminidisc/issues/32 — métadonnées non appliquées selon les tags ID3
- https://github.com/asivery/webminidisc/issues/31 — règles udev modernes (`uaccess`)
- https://github.com/asivery/webminidisc/issues/26 — encodage bloqué à 0 % sur Windows 10
- https://github.com/asivery/webminidisc/issues/25 — blocage sur « Reading metadata »

### ElectronWMD (`asivery/ElectronWMD`)
- https://github.com/asivery/ElectronWMD
- https://api.github.com/repos/asivery/ElectronWMD/issues?state=all — corpus d'issues dépouillé
- https://github.com/asivery/ElectronWMD/issues/55 — encodeur local, `output.wav` manquant (Win 11)
- https://github.com/asivery/ElectronWMD/issues/54 — réorganisation rendant une piste injouable
- https://github.com/asivery/ElectronWMD/issues/53 — `at3tool` introuvable sur Debian
- https://github.com/asivery/ElectronWMD/issues/52 — upload MZ-N10, AEA silencieux
- https://github.com/asivery/ElectronWMD/issues/49 — différence d'égalisation A3+ (+6 dB vers 160 Hz)
- https://github.com/asivery/ElectronWMD/issues/48 — détection de disque et rechargement du TOC
- https://github.com/asivery/ElectronWMD/issues/47 — Hi-MD PCM : freeze au-delà de 28 min
- https://github.com/asivery/ElectronWMD/issues/44 — « skips » à l'enregistrement
- https://github.com/asivery/ElectronWMD/issues/43 — `LIBUSB_ERROR_NOT_SUPPORTED`
- https://github.com/asivery/ElectronWMD/issues/40 — erreur Hi-MD sur macOS
- https://github.com/asivery/ElectronWMD/issues/36 — titres non lus par un appareil ancien
- https://github.com/asivery/ElectronWMD/issues/33 — corruption après mélange upload PC / enregistrement
- https://github.com/asivery/ElectronWMD/issues/32 — `LIBUSB_ERROR_ACCESS` sur macOS Sequoia
- https://github.com/asivery/ElectronWMD/issues/30 — import de playlist M3U
- https://github.com/asivery/ElectronWMD/issues/28 — MP3 vers Hi-MD sur Mac

### Écosystème MiniDisc
- https://www.minidisc.wiki/
- https://www.minidisc.org/
- https://forums.sonyinsider.com/
- https://github.com/cybercase/webminidisc — projet d'origine
- https://github.com/cybercase/netmd-js — bibliothèque NetMD JavaScript
- https://github.com/glaubitz/linux-minidisc — pile NetMD/Hi-MD historique
- https://github.com/dcherednik/atracdenc — encodeur ATRAC open source
- https://github.com/asivery/netmd-exploits — mode homebrew / factory
- https://www.reddit.com/r/minidisc/ — communauté (non crawlable, à dépouiller manuellement)

### Design systems et tokens (via `docs/research/02b-design-tokens.md`)
- https://raw.githubusercontent.com/microsoft/vscode/main/extensions/theme-defaults/themes/dark_modern.json
- https://raw.githubusercontent.com/microsoft/vscode/main/src/vs/platform/theme/common/colors/listColors.ts
- https://raw.githubusercontent.com/radix-ui/colors/main/src/dark.ts
- https://unpkg.com/@primer/primitives/dist/css/functional/themes/dark.css
- https://raw.githubusercontent.com/microsoft/fluentui/master/packages/tokens/src/alias/darkColor.ts
- https://raw.githubusercontent.com/carbon-design-system/carbon/main/packages/themes/src/dtcg/g100.json
- https://raw.githubusercontent.com/carbon-design-system/carbon/main/packages/type/src/styles.ts
- https://primer.style/foundations/primitives/size

### Typographie et icônes
- https://rsms.me/inter/ — Inter (SIL OFL 1.1)
- https://github.com/rsms/inter/blob/master/LICENSE.txt
- https://www.ibm.com/plex/ — IBM Plex Sans (OFL 1.1)
- https://public-sans.digital.gov/ — Public Sans (OFL 1.1)
- https://www.jetbrains.com/lp/mono/ — JetBrains Mono (OFL 1.1)
- https://lucide.dev/ — Lucide (licence ISC)
- https://github.com/microsoft/vscode-codicons — Codicon (OFL 1.1)

### Accessibilité et référence d'interaction
- https://www.w3.org/WAI/WCAG22/quickref/
- https://learn.microsoft.com/en-us/windows/apps/design/
- https://learn.microsoft.com/en-us/windows/win32/winauto/entry-uiauto-win32 — UI Automation

### Gestionnaires de bibliothèque (analyse d'usage)
- https://www.foobar2000.org/
- https://getmusicbee.com/
- https://roon.app/
- https://plexamp.com/

---

*Fin du document R-02.*

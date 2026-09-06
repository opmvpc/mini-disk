# R-02b — Design tokens (extrait du seul sous-rapport ayant abouti, 2026-09-06)

Source : sous-agent "Design system type scales and dark palettes". Valeurs tirées des fichiers sources des
design systems (Carbon, Fluent 2, Primer, Radix, Ant, VS Code). Sert de base à notre thème sombre.

## Métriques de densité (pro tool)

| Élément | Valeur retenue | Référence |
|---------|----------------|-----------|
| Police UI par défaut | 13 px / interligne 18 | VS Code workbench 13 px, Linear 13 px |
| Caption / secondaire | 12 px / 16 | Carbon caption-01, Fluent Caption1 |
| Micro-label | 11 px / 16 | — |
| Emphase | 14 px / 20 | Carbon body-compact-01, Fluent Body1 |
| Titre de section | 16 px / 24 | Carbon heading-02 |
| Rythme d'espacement | 2 / 4 / 6 / 8 / 12 / 16 / 24 / 32 px | Fluent spacings (base 4 px) |
| Ligne de liste compacte | 22 px (VS Code explorer) ou 24 px (Carbon xs) | `ExplorerDelegate.ITEM_HEIGHT = 22` |
| Ligne standard | 28 / 32 px | Primer Small 28, Ant controlHeight 32 |
| Ligne confortable | 40 px | Carbon md |
| Hauteur de contrôle | 20 / 24 / 28 / 32 px | Primer XSmall 24 → Medium 32 |
| Bordures | 1 px ; radius 2-4 px (6 max) | Ant borderRadius 6, Primer small 3 |
| Barre de titre custom | 30-35 px | VS Code 30 (35 avec command center) |
| Barre de statut | 22 px, texte 12 px | VS Code |
| Onglets | 32 px (modern) | VS Code `EDITOR_TAB_HEIGHT.modernUI` |
| Rail d'activité | 48 px (compact 36) | VS Code |
| Largeur min panneau latéral | 170 px, préférée 300 | VS Code sidebar |
| Icônes | 16 px | codicon |

## Palette sombre — proposition initiale (à figer en ADR thème)

Base : VS Code Dark Modern (chroma) + échelle Radix slate (structure 12 pas).

| Rôle | Hex | Origine |
|------|-----|---------|
| Canvas (fond app) | `#111113` | Radix slate 1 |
| Panneau | `#181818` / `#18191b` | VS Code sideBar / slate 2 |
| Surface éditeur / liste | `#1F1F1F` | VS Code editor.background |
| Contrôle (input, bouton secondaire) | `#212225` → `#313131` | slate 3 / VS Code input |
| Hover | `#272a2d` / `#2A2D2E` | slate 4 / VS Code list.hover |
| Pressé / sélection inactive | `#2e3135` / `#37373D` | slate 5 / VS Code inactiveSelection |
| Sélection active | `#04395E` | VS Code list.activeSelection |
| Bordure subtile | `#2B2B2B` / `#363a3f` | VS Code / slate 6 |
| Bordure contrôle | `#3C3C3C` / `#43484e` | VS Code input.border / slate 7 |
| Bordure hover | `#5a6169` | slate 8 |
| Texte primaire | `#EDEEF0` / `#CCCCCC` | slate 12 / VS Code foreground |
| Texte secondaire | `#B0B4BA` / `#9D9D9D` | slate 11 / VS Code description |
| Texte désactivé | `#696e77` | slate 9 |
| Accent | `#0078D4` (VS Code) ou `#0090FF` (Radix blue 9) | focus, bouton primaire |
| Accent hover | `#026EC1` / `#3b9eff` | |
| Succès | `#2EA043` texte `#3fb950` | Primer |
| Avertissement | `#9E6A03` texte `#d29922` | Primer |
| Danger | `#DA3633` texte `#F85149` | Primer / VS Code |
| Focus ring | accent, 1-2 px, ou blanc (Carbon g100) | |

Couleurs par mode MD (à définir, non issues du rapport) : SP / LP2 / LP4 / mono doivent être distinguables
dans la jauge de capacité. Proposition : SP bleu accent, LP2 vert, LP4 ambre, mono violet `#be95ff`.

## Échelles complètes utiles

- Radix dark slate 1-12 : `#111113 #18191b #212225 #272a2d #2e3135 #363a3f #43484e #5a6169 #696e77 #777b84 #b0b4ba #edeef0`
- Radix dark blue 9/10/11 : `#0090ff #3b9eff #70b8ff`
- Primer dark neutrals 0-13 : `#010409 #0D1117 #151B23 #212830 #262C36 #2A313C #2F3742 #3D444D #656C76 #9198A1 #B7BDC8 #D1D7E0 #F0F6FC #ffffff`
- Fluent dark neutralBackground 1-5 : `#292929 #1f1f1f #141414 #0a0a0a #000000` ; foreground 1-4 : `#ffffff #d6d6d6 #adadad #999999` ; disabled `#5c5c5c`
- Carbon g100 : bg `#161616`, layer-01 `#262626`, layer-02 `#393939`, text-primary `#f4f4f4`, text-secondary `#c6c6c6`, helper `#a8a8a8`, interactive `#4589ff`, error `#fa4d56`, success `#42be65`, warning `#f1c21b`

## Sources principales
- https://raw.githubusercontent.com/microsoft/vscode/main/extensions/theme-defaults/themes/dark_modern.json
- https://raw.githubusercontent.com/microsoft/vscode/main/src/vs/platform/theme/common/colors/listColors.ts
- https://raw.githubusercontent.com/radix-ui/colors/main/src/dark.ts
- https://unpkg.com/@primer/primitives/dist/css/functional/themes/dark.css
- https://raw.githubusercontent.com/microsoft/fluentui/master/packages/tokens/src/alias/darkColor.ts
- https://raw.githubusercontent.com/carbon-design-system/carbon/main/packages/themes/src/dtcg/g100.json
- https://raw.githubusercontent.com/carbon-design-system/carbon/main/packages/type/src/styles.ts
- https://primer.style/foundations/primitives/size

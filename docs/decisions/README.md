# Architecture Decision Records

| # | Titre | Statut |
|---|-------|--------|
| [ADR-001](ADR-001-architecture-trois-couches.md) | Architecture trois couches, core sans dépendance plateforme | accepté |
| [ADR-002](ADR-002-toolchain-no-crt.md) | Toolchain MSVC, release sans CRT, unity build | accepté |
| [ADR-003](ADR-003-opengl-33-core-rendu-a-la-demande.md) | OpenGL 3.3 core, loader maison, rendu à la demande | accepté |
| [ADR-004](ADR-004-ui-immediate-api-retained-core.md) | UI immediate API / retained core (Fleury) | accepté |
| [ADR-005](ADR-005-renderer-primitive-unique-sdf.md) | Renderer : primitive unique SDF, 1 shader, 1 VBO | accepté |
| [ADR-006](ADR-006-texte-directwrite-vers-atlas.md) | Texte : DirectWrite vers atlas, zéro police embarquée | accepté |
| [ADR-007](ADR-007-codecs-et-dsp.md) | Codecs de décodage et pipeline DSP | accepté |
| [ADR-008](ADR-008-usb-winusb-transport-rejouable.md) | USB : WinUSB direct, UsbTransport rejouable | accepté |
| [ADR-009](ADR-009-atrac3-clean-room-en-c.md) | ATRAC3 : clean-room en C, ffmpeg comme oracle | accepté |
| [ADR-010](ADR-010-persistance-binaire-maison.md) | Persistance binaire maison, pas de SQLite | accepté |
| [ADR-011](ADR-011-decisions-produit.md) | Décisions produit structurantes (D1-D10) | accepté |
| [ADR-012](ADR-012-qualite-tests-analyse-statique.md) | Qualité : tests, analyse statique, zéro programmation défensive | accepté |

Le contrat `platform.h` (~90 fonctions) est spécifié dans research/03 §10 et deviendra `src/platform/platform.h`
au ticket T-001 ; toute modification ultérieure passe par un ADR.

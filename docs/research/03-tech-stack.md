# R-03 — Stack technique : décisions

> Rapport de recherche, 2026-09-06. Cible : `minidisk.exe`, Windows x64, C99/C11, exe unique < 1 MB,
> sans CRT en release, UI OpenGL from scratch, core portable.
> Chaque section : **options → tradeoffs → RECOMMANDATION** avec spécifications au niveau du code.
> Tout ce qui est écrit ici est réputé *tranché* : les alternatives sont documentées pour qu'on sache
> pourquoi on ne les a pas prises, pas pour rouvrir le débat à chaque commit.

## Sommaire

1. [Toolchain et exe minimal](#1-toolchain-et-exe-minimal)
2. [Fenêtre et contexte OpenGL sans bibliothèque](#2-fenêtre-et-contexte-opengl-sans-bibliothèque)
3. [Architecture UI](#3-architecture-ui)
4. [Rendu GPU](#4-rendu-gpu)
5. [Texte](#5-texte)
6. [Audio : décodage, DSP, encodage](#6-audio--décodage-dsp-encodage)
7. [ATRAC3 (LP2/LP4)](#7-atrac3-lp2lp4)
8. [USB : WinUSB sans libusb](#8-usb--winusb-sans-libusb)
9. [Persistance](#9-persistance)
10. [Organisation du code, platform.h, build.bat](#10-organisation-du-code-platformh-buildbat)
11. [Risques classés et liste d'URLs](#11-risques-classés-et-liste-durls)

### Tableau de synthèse des dépendances retenues

| Domaine | Retenu | Licence | Single-header | Taille compilée approx. (x64, /O2) |
|---------|--------|---------|---------------|-------------------------------------|
| MP3 | minimp3 | CC0 / domaine public | oui (2 headers) | ~35 KB (dont ~20 KB de tables) |
| FLAC | dr_flac | MIT-0 ou domaine public (au choix) | oui | ~45 KB |
| WAV/AIFF | dr_wav | MIT-0 ou domaine public | oui | ~25 KB (ou 8 KB si on écrit le nôtre) |
| OGG Vorbis | stb_vorbis | MIT ou domaine public | oui | ~60 KB (dont ~10 KB de tables) |
| AAC / ALAC / WMA | Media Foundation (OS) | — | — | ~4 KB de glue COM, 0 octet de codec |
| Opus | *non retenu v1* (voir §6.3) | BSD-3 | non (lib complète) | ~200 KB si un jour |
| Police | stb_truetype + rastérisation à la demande | MIT / domaine public | oui | ~25 KB + police subsetée ~90 KB |
| Fallback Unicode | DirectWrite/GDI en secours d'atlas | — | — | ~6 KB de glue |
| ATRAC3 | port C maison (voir §7) | à écrire | — | ~60-90 KB estimés |
| USB | WinUSB (`winusb.dll` + `setupapi.dll`) | — | — | ~6 KB de glue |
| Persistance | format binaire maison mappé | — | — | ~5 KB |
| **Total dépendances tierces** | | | | **~190 KB de code + ~90 KB de police** |

Budget exe visé : **< 600 KB en release** (hors police embarquée : < 500 KB), avec une marge confortable
sous la limite de 1 MB.

---

## 1. Toolchain et exe minimal

### 1.1 Le point de départ mesuré

`tools/smoke/min.c` produit un exe de **1 536 octets** avec :

```
cl  /nologo /O2 /GS- /Gs9999999 /GR- /EHa- /Oi /W4 /c min.c
link /nologo /NODEFAULTLIB /ENTRY:entry_point /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF
     /MERGE:.rdata=.text /ALIGN:16 /FILEALIGN:16 /NOCOFFGRPINFO /EMITPOGOPHASEINFO
     /STACK:0x100000,0x100000 kernel32.lib user32.lib
```

C'est la bonne base. Ce chiffre valide que **tout ce que pèsera l'exe final sera du code à nous**, et
non du runtime. Il faut comprendre exactement pourquoi chaque flag est là, parce qu'on va en garder
certains et en jeter d'autres quand le programme grossira.

### 1.2 Analyse flag par flag

| Flag | Rôle | À garder ? |
|------|------|-----------|
| `/GS-` | supprime les *stack security cookies* (`__security_check_cookie`, qui vit dans le CRT) | **oui, obligatoire** sans CRT |
| `/Gs9999999` | seuil de *stack probe* : au-delà de 9 999 999 octets de locals, MSVC appelle `__chkstk` (CRT). En pratique : jamais. | **oui** — mais voir 1.3, alternative plus propre |
| `/GR-` | pas de RTTI (C++ only, inoffensif en C) | oui, hygiène |
| `/EHa-` | pas d'unwinding d'exceptions | oui |
| `/Oi` | intrinsics activées → `memcpy`/`memset` inlinés en `rep movsb`/SSE au lieu d'appels CRT | **oui, critique** |
| `/O2` | optimisation vitesse | release |
| `/W4` | warnings | **oui**, et on ajoutera `/WX` en CI |
| `/NODEFAULTLIB` | ignore les `.lib` par défaut (`libcmt`, `oldnames`, ...) | **oui** en release |
| `/ENTRY:entry_point` | point d'entrée à nous, pas `mainCRTStartup` | oui |
| `/OPT:REF /OPT:ICF` | élague les fonctions non référencées, fusionne les fonctions identiques | oui |
| `/MERGE:.rdata=.text` | fusionne les sections → économise un alignement de section entier | oui |
| `/ALIGN:16 /FILEALIGN:16` | alignement de section 16 octets au lieu de 4096/512 | **à surveiller**, voir 1.4 |
| `/STACK:0x100000,0x100000` | 1 MB de pile réservée **et commitée** | à ajuster : voir 1.3 |
| `/NOCOFFGRPINFO` | supprime la section de debug `.debug$S` COFF group info | oui |
| `/EMITPOGOPHASEINFO` | (contre-intuitif) évite l'ajout d'une section de padding PGO | marginal, garder |

### 1.3 Ce qu'on doit ajouter quand le programme grossit

**Le piège du stack probe.** `/Gs9999999` marche tant qu'aucune fonction n'a plus de ~10 MB de locals,
ce qui est trivialement vrai. Mais on veut aussi éviter que MSVC génère `__chkstk` sur les
*variable-length arrays* et les gros buffers temporaires. Notre discipline « arènes, zéro gros buffer
en pile » règle le problème de fait. **Recommandation : garder `/Gs9999999`, et fixer une règle de
revue : aucune fonction n'a plus de 16 KB de locals.**

`/STACK:0x100000,0x100000` commite 1 MB immédiatement pour chaque thread. Avec un job system à 8
threads, ça fait 8 MB de RSS gratuits. **Recommandation : `/STACK:0x100000,0x10000`** (1 MB réservé,
64 KB commité) et création des threads de job avec `dwStackSize = 128*1024` explicitement.

**Les fonctions intrinsèques que MSVC appelle malgré tout.** Même avec `/Oi`, le compilateur émet des
appels à un petit ensemble de symboles CRT. On doit les fournir nous-mêmes :

```c
// base/base_crt_stubs.c — compilé uniquement en release no-CRT
#pragma function(memset)
#pragma function(memcpy)
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
// pas de #pragma function pour ceux-là (non intrinsèques) :
void *memmove(void *dst, const void *src, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
```

Liste exhaustive des symboles que MSVC 14.44 x64 peut réclamer sans CRT, par ordre de probabilité :

| Symbole | Quand | Notre réponse |
|---------|-------|---------------|
| `memset` | initialisation de struct `= {0}` de plus de ~16 octets | implémentation maison + `#pragma function` |
| `memcpy` | copie de struct, passage par valeur | idem |
| `memmove` | rarement émis, mais on l'utilise nous-mêmes | implémentation maison |
| `memcmp` | comparaisons de struct | implémentation maison |
| `__chkstk` | gros frames | évité par `/Gs9999999` |
| `_fltused` | dès qu'on touche un `float` | `int _fltused = 0x9875;` — une ligne |
| `__security_cookie`, `__security_check_cookie` | `/GS` | évité par `/GS-` |
| `_dtoui3`, `_ltod3`, `__ftol2_sse` | conversions float↔int en x86 | **non émis en x64** (SSE2 natif) |
| `strlen`, `strcmp` | seulement si on écrit du code C string | on n'en écrit pas : `String8` partout |

Implémentations recommandées (correctes, pas héroïques ; la vraie perf vient de `/Oi` qui inline
la plupart des cas) :

```c
#pragma function(memset)
void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    unsigned char  v = (unsigned char)c;
    // stos est ce que le CPU fait de mieux sur les CPU >= IvyBridge (ERMSB)
    __stosb(d, v, n);
    return dst;
}
#pragma function(memcpy)
void *memcpy(void *dst, const void *src, size_t n) {
    __movsb((unsigned char *)dst, (const unsigned char *)src, n);
    return dst;
}
```

`__stosb`/`__movsb` sont des intrinsics `<intrin.h>` qui émettent `rep stosb`/`rep movsb`, lesquels
sont *fast-path* microcodés (ERMSB) sur le i7-8550U (Kaby Lake R). Pas de dépendance CRT, ~20 octets
de code chacun. Pour les copies < 128 octets, `/Oi` aura déjà inliné : ces fonctions ne seront
appelées que pour les gros blocs, exactement là où `rep movsb` gagne.

**Attention** : `#pragma function(memcpy)` doit précéder la définition, et le fichier ne doit pas être
compilé avec `/Oi` désactivé. Vérifier avec `dumpbin /symbols` qu'aucun `__imp_` CRT ne subsiste.

**Float en C sans CRT.** `_fltused` est un symbole que MSVC référence dès qu'une unité de compilation
manipule des flottants (héritage x87). Une simple définition suffit :

```c
int _fltused = 0x9875;   // valeur arbitraire, jamais lue
```

Les fonctions mathématiques (`sinf`, `cosf`, `sqrtf`, `powf`, `logf`, `expf`, `floorf`, `fmodf`) sont
dans `libm`, donc absentes. Options :

- **A. `/NODEFAULTLIB` + implémentations maison.** `sqrtf` = `_mm_sqrt_ss` (intrinsic, une
  instruction). `floorf`/`ceilf`/`roundf` = `_mm_round_ss` (SSE4.1, présent sur toute machine
  post-2008). `fabsf` = masque de bit. Restent `sinf/cosf/expf/logf/powf` : approximations
  polynomiales.
- **B. Linker `libvcruntime.lib` seul** (sans `libcmt`) pour récupérer les intrinsics mathématiques.
  Ça marche mais tire des dépendances de CRT initialisation.
- **C. Utiliser les fonctions de l'OS.** `ntdll.dll` exporte `sin`, `cos`, `pow`, `log`, `sqrt`,
  `ceil`, `floor`, `atan`... (héritage de l'implémentation du CRT dans ntdll). Elles sont `double`
  seulement, et non documentées comme API publique, mais présentes depuis NT 3.1 et utilisées par le
  système lui-même.

**RECOMMANDATION : option A**, avec cette répartition :

```c
// base/base_math.h
static inline f32 sqrt_f32(f32 x)  { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x))); }
static inline f32 abs_f32 (f32 x)  { u32 u = f32_bits(x) & 0x7FFFFFFFu; return bits_f32(u); }
static inline f32 floor_f32(f32 x) { return _mm_cvtss_f32(_mm_floor_ss(_mm_set_ss(x), _mm_set_ss(x))); }
static inline f32 ceil_f32 (f32 x) { return _mm_cvtss_f32(_mm_ceil_ss (_mm_set_ss(x), _mm_set_ss(x))); }
static inline f32 round_f32(f32 x) { return _mm_cvtss_f32(_mm_round_ss(_mm_set_ss(x), _mm_set_ss(x),
                                       _MM_FROUND_TO_NEAREST_INT|_MM_FROUND_NO_EXC)); }
f32 sin_f32(f32 x);   // minimax degré 7 sur [-pi, pi], erreur < 1e-6 : suffisant pour l'UI
f32 cos_f32(f32 x);
f32 exp2_f32(f32 x);  // exposant par manipulation de bits + polynôme degré 4 sur la mantisse
f32 log2_f32(f32 x);  // extraction d'exposant + polynôme degré 5
static inline f32 pow_f32(f32 b, f32 e) { return exp2_f32(e * log2_f32(b)); }
static inline f32 db_to_lin(f32 db)     { return exp2_f32(db * (1.0f/6.020599913f)); }
static inline f32 lin_to_db(f32 lin)    { return 6.020599913f * log2_f32(lin); }
```

Où on a réellement besoin de précision : **EBU R128** (log10 pour le loudness, tolérance 0.1 LU →
1e-4 relatif suffit largement) et **l'encodeur ATRAC3** (MDCT : besoin de `cos` précis, mais on
précalcule des tables de twiddle une fois au démarrage avec la version double si nécessaire). Nos
polynômes minimax f32 sont largement au-dessus du besoin. Coût total : ~1.5 KB de code.

Où la précision compte vraiment (twiddles MDCT), on génère la table **à la compilation** dans un
header pré-calculé (`atrac3_tables.h` généré par un petit outil offline compilé avec le CRT), ce qui
supprime le problème et accélère le démarrage.

### 1.4 Le piège `/ALIGN:16`

`/ALIGN:16` est ce qui fait passer l'exe de 4 KB à 1.5 KB : il supprime le padding à 4096 octets entre
sections. **Mais** :

- Windows charge un PE dont `SectionAlignment < PageSize` **en le copiant intégralement en mémoire**
  au lieu de le mapper (mécanisme legacy). Conséquence : pas de partage de pages entre instances,
  et surtout **pas de pages read-only/no-execute distinctes**.
- Les DEP/ASLR fonctionnent quand même (l'image entière est RWX-ish selon les caractéristiques),
  mais certains antivirus considèrent `SectionAlignment=16` comme un marqueur de packer.
- **Ça casse dès que l'image dépasse quelques centaines de KB** dans certains cas de figure, et
  surtout ça empêche `/DYNAMICBASE` de fonctionner comme prévu.

**RECOMMANDATION : abandonner `/ALIGN:16` dès la phase 1.** Le gain est de ~3-6 KB, indifférent quand
l'exe pèse 300 KB, et le risque de faux positif antivirus sur un logiciel qui parle en USB à un
appareil Sony est un vrai risque produit. On garde en revanche `/MERGE:.rdata=.text` (gain réel : une
page entière, et une section de moins dans le header) et `/FILEALIGN:512` (défaut).

Flags release finaux recommandés :

```
/NODEFAULTLIB /ENTRY:entry_point /SUBSYSTEM:WINDOWS
/OPT:REF /OPT:ICF /INCREMENTAL:NO
/MERGE:.rdata=.text
/STACK:0x100000,0x10000
/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA
/MANIFEST:EMBED /MANIFESTINPUT:app.manifest
```

Note : `/DYNAMICBASE` sans CRT exige que le linker génère une table de relocations. Elle est générée
automatiquement pour un exe x64 (adressage RIP-relatif, très peu de relocs). Vérifier avec
`dumpbin /headers minidisk.exe | findstr "Dynamic base"`.

### 1.5 Le point d'entrée sans CRT

```c
// platform/win32/win32_entry.c
void __stdcall entry_point(void) {
    // 1. rien n'est initialisé : pas de globals C++ (on n'en a pas), pas d'atexit, pas de stdio
    // 2. on récupère la ligne de commande nous-mêmes
    LPWSTR cmdline = GetCommandLineW();
    // 3. on initialise nos sous-systèmes dans un ordre explicite
    win32_state_init();
    int code = app_main(cmdline);
    ExitProcess((UINT)code);   // jamais de return : la pile n'a pas de frame de retour valide
}
```

Points d'attention :
- **`ExitProcess` obligatoire.** Un `return` depuis `entry_point` retourne dans `RtlUserThreadStart`,
  ce qui fonctionne par accident mais n'est pas garanti. `ExitProcess(0)` tue proprement.
- **Pas de `CommandLineToArgvW`** en release : elle est dans `shell32.dll` (300 KB de DLL chargée pour
  parser une chaîne). On écrit notre propre split (30 lignes, respecte les règles de quoting MSVC).
- **Le tas Windows reste disponible** : `HeapAlloc(GetProcessHeap(), ...)` marche sans CRT. Mais on ne
  l'utilise pas : on réserve directement avec `VirtualAlloc(MEM_RESERVE)` et on commite à la demande.

### 1.6 Arènes mémoire : le modèle

C'est le socle de tout le programme, donc on le fige ici.

```c
// base/base_arena.h
typedef struct Arena {
    u64 reserved;      // taille réservée (VirtualAlloc MEM_RESERVE)
    u64 committed;     // octets effectivement commités
    u64 pos;           // position d'allocation courante
    u64 align;         // alignement par défaut (16)
    // les données suivent immédiatement l'en-tête : base = (u8*)arena + ARENA_HEADER_SIZE
} Arena;
#define ARENA_HEADER_SIZE 64          // une ligne de cache, l'en-tête ne pollue pas les données
#define ARENA_COMMIT_CHUNK (64*1024)  // on commite par 64 KB (granularité VirtualAlloc)
#define ARENA_DEFAULT_RESERVE (1ull<<34)  // 16 GB de réserve virtuelle : gratuit en x64

Arena *arena_alloc(u64 reserve_size);
void  *arena_push(Arena *a, u64 size, u64 align);
void   arena_pop_to(Arena *a, u64 pos);
void   arena_clear(Arena *a);
void   arena_release(Arena *a);

#define push_array(a, T, n)      (T*)arena_push((a), sizeof(T)*(n), _Alignof(T))
#define push_array_zero(a, T, n) (T*)arena_push_zero((a), sizeof(T)*(n), _Alignof(T))
#define push_struct(a, T)        push_array(a, T, 1)

// Scratch : arènes thread-local, empruntées avec conflit explicite
typedef struct ArenaTemp { Arena *arena; u64 pos; } ArenaTemp;
ArenaTemp scratch_begin(Arena **conflicts, u64 conflict_count);
void      scratch_end(ArenaTemp t);
#define ScratchBegin(c, n) scratch_begin((c), (n))
#define ScratchEnd(t)      scratch_end(t)
```

Le pattern `scratch_begin(conflicts, n)` (issu de la base de Ryan Fleury) résout le problème classique :
une fonction qui prend une arène de sortie et a besoin d'un scratch ne doit pas prendre *la même*
arène comme scratch. On garde 2 arènes scratch par thread (`tls_scratch[2]`) et `scratch_begin`
retourne celle qui n'est pas dans la liste des conflits.

Découpage des arènes du programme :

| Arène | Durée de vie | Réserve | Contenu |
|-------|--------------|---------|---------|
| `arena_permanent` | process | 1 GB | config, atlas CPU, tables générées, state global |
| `arena_library` | rescan complet | 8 GB | index bibliothèque (100k pistes SoA + string table) |
| `arena_frame` | une frame UI | 256 MB | vertex buffers CPU, textes formatés, arbre de layout |
| `arena_plan` | durée du plan | 64 MB | burn list, résultats de capacité |
| `tls_scratch[2]` | par thread | 256 MB chacune | tout le reste |

`arena_frame` est `arena_clear()`ée au début de chaque frame : **zéro free, zéro fragmentation, zéro
malloc dans la boucle de rendu**, et le coût du clear est `pos = 0` (on ne décommite pas ; on
décommite seulement si `pos_max` a dépassé un seuil pendant N frames).

### 1.7 Chargement dynamique de DLL

Ce qu'on lie statiquement (import table) vs ce qu'on charge à la demande :

| DLL | Mode | Pourquoi |
|-----|------|----------|
| `kernel32.dll` | statique | toujours présente, base |
| `user32.dll` | statique | fenêtre, messages |
| `gdi32.dll` | statique | `GetDC`, `SwapBuffers`, `ChoosePixelFormat` |
| `opengl32.dll` | statique | `wglCreateContext`, `wglGetProcAddress` — présent partout depuis NT4 |
| `advapi32.dll` | **dynamique** | registre (préférences fallback) : rarement utilisé |
| `shell32.dll` | **dynamique** | `SHGetKnownFolderPath`, `DragQueryFileW` — chargée quand on en a besoin |
| `ole32.dll` / `oleaut32.dll` | **dynamique** | COM pour Media Foundation et IFileDialog |
| `mf*.dll` (mfplat, mfreadwrite, mfuuid) | **dynamique** | seulement si l'utilisateur ouvre un `.m4a`/`.wma` |
| `winusb.dll` + `setupapi.dll` + `cfgmgr32.dll` | **dynamique** | seulement si un device NetMD est branché |
| `dwrite.dll` | **dynamique** | fallback de glyphes uniquement |
| `avrt.dll` | dynamique | `AvSetMmThreadCharacteristics` pour le thread audio (optionnel) |
| `dbghelp.dll` | dynamique, debug only | mini-dumps sur crash |

Le chargement dynamique se fait par un macro-générateur pour éviter le boilerplate :

```c
// X-macro par DLL
#define WINUSB_FUNCS \
    X(BOOL, WinUsb_Initialize, (HANDLE, PWINUSB_INTERFACE_HANDLE)) \
    X(BOOL, WinUsb_ControlTransfer, (WINUSB_INTERFACE_HANDLE, WINUSB_SETUP_PACKET, PUCHAR, ULONG, PULONG, LPOVERLAPPED)) \
    X(BOOL, WinUsb_ReadPipe, (WINUSB_INTERFACE_HANDLE, UCHAR, PUCHAR, ULONG, PULONG, LPOVERLAPPED)) \
    X(BOOL, WinUsb_WritePipe, (WINUSB_INTERFACE_HANDLE, UCHAR, PUCHAR, ULONG, PULONG, LPOVERLAPPED)) \
    X(BOOL, WinUsb_SetPipePolicy, (WINUSB_INTERFACE_HANDLE, UCHAR, ULONG, ULONG, PVOID)) \
    X(BOOL, WinUsb_Free, (WINUSB_INTERFACE_HANDLE))

#define X(ret, name, args) typedef ret (WINAPI *name##_t) args; static name##_t p##name;
WINUSB_FUNCS
#undef X

static b32 winusb_load(void) {
    HMODULE m = LoadLibraryW(L"winusb.dll");
    if (!m) return 0;
#define X(ret, name, args) p##name = (name##_t)GetProcAddress(m, #name); if (!p##name) return 0;
    WINUSB_FUNCS
#undef X
    return 1;
}
```

Avantages mesurables : démarrage plus rapide (aucune DLL COM chargée si on n'ouvre pas de m4a), et
l'app démarre même sur une installation Windows amputée.

### 1.8 Unity build

Un seul appel `cl.exe`, un seul `.obj`.

```c
// src/main.c — la SEULE unité de compilation
#include "base/base_inc.h"          // headers de base
#include "base/base_inc.c"          // implémentations
#include "platform/platform.h"
#include "platform/win32/win32_inc.c"
#include "core/core_inc.c"
#include "ui/ui_inc.c"
#include "app/app.c"
```

Tradeoffs :

| | Unity build | Compilation séparée |
|---|---|---|
| Temps de build complet | ~1.5-3 s pour 60k lignes | ~15 s + link |
| Temps de build incrémental | identique au complet | plus rapide sur un fichier |
| Inlining inter-modules | total, gratuit | nécessite `/GL` + `/LTCG` (lent) |
| Discipline de nommage | exige `static` partout et des préfixes | forcée par le linker |
| Parallélisme du build | nul | `/MP` |
| Découverte d'un cycle d'include | immédiate | tardive |

**RECOMMANDATION : unity build**, à trois conditions strictes :
1. **Tout est `static`** sauf l'API publique d'un module. On perd le contrôle du linker, donc la
   discipline de préfixes (`netmd_`, `ui_`, `lib_`...) devient obligatoire, pas cosmétique.
2. **Les headers tiers sont dans une unité isolée**. minimp3, stb_vorbis et dr_flac définissent des
   macros et des types génériques (`bs_t`, `L3_gr_info_t`, `drflac_uint32`) qui polluent. On les
   compile dans **un second `.obj`** :
   ```
   cl /c src/third_party/third_party_unity.c   -> third_party.obj
   cl /c src/main.c                            -> main.obj
   link main.obj third_party.obj ...
   ```
   Coût : un `.obj` de plus, gain : zéro collision de macro, et on ne recompile pas 200k lignes de
   codecs à chaque itération (leur `.obj` est en cache).
3. **`build.bat` mesure et affiche la taille de l'exe** à chaque build release. C'est le KPI.

Temps de build attendu à la fin du projet : ~2 s pour `main.obj` (60-80k lignes à nous), ~4 s pour
`third_party.obj` (rebuild rare), < 0.5 s de link. Une itération complète en debug : **sous 3 secondes**.

### 1.9 Debug vs release : deux configurations, deux CRT

| | Debug | Release |
|---|---|---|
| CRT | **oui** (`/MTd`) | non (`/NODEFAULTLIB`) |
| Entry | `entry_point` aussi (cohérence) | `entry_point` |
| Optim | `/Od /Zi` | `/O2 /Zi` (on garde le PDB !) |
| Asserts | `Assert` actifs (int 3) | `Assert` no-op, `AssertAlways` actifs |
| ASan | `/fsanitize=address` disponible | non |
| Taille | ~2 MB, on s'en fiche | KPI |

Avoir le CRT en debug permet ASan, `printf` de debug, et les *runtime checks* `/RTC1`. Le seul risque
est qu'un appel CRT se glisse dans le code et ne soit détecté qu'au build release. Mitigation :
**la CI (ou un `build.bat check`) build en release à chaque commit**, et `dumpbin /symbols main.obj |
findstr "UNDEF"` doit ne montrer que des symboles Win32 + nos stubs.

**Important : garder `/Zi` et générer le PDB en release.** Le PDB est un fichier séparé, il ne pèse
rien dans l'exe (juste ~50 octets de chemin dans la debug directory, qu'on peut même supprimer avec
`/PDBALTPATH`). Sans PDB, on ne débogue aucun crash utilisateur.

### 1.10 Intrinsics et baseline SIMD

Cible : **SSE2 baseline** (garanti par x64), **SSE4.1 requis** (2008+, notre floor/ceil/round),
**AVX2 en chemin optionnel** détecté par `__cpuid`.

```c
// base/base_simd.h
#include <immintrin.h>
typedef struct CpuFeatures { b32 sse41, sse42, avx, avx2, fma, popcnt, bmi2; } CpuFeatures;
CpuFeatures cpu_detect(void);   // __cpuid / __cpuidex, appelé une fois au démarrage
```

Où le SIMD paie réellement dans ce projet :
1. **Resampler sinc polyphase** : produit scalaire sur 32-64 taps → AVX2/FMA, gain ~4x.
2. **Conversion de format** (i16↔f32, entrelacement/désentrelacement) → SSE2 suffit, gain ~4x.
3. **Filtres R128 (K-weighting)** : biquads, difficiles à vectoriser (dépendance série) → traiter
   **les deux canaux en parallèle** dans un `__m128` : gain 2x propre.
4. **MDCT ATRAC3** : le FFT sous-jacent, gain ~2-3x avec SSE.
5. **Recherche texte dans la bibliothèque** : comparaison de 16 octets à la fois pour le filtrage
   incrémental sur 100k pistes.

Le reste (UI, layout, protocole USB) est *scalaire et froid*, on n'y touche pas.

Détection de la baseline : le projet cible des machines qui font tourner Windows 11, donc SSE4.2 est
de fait garanti. **RECOMMANDATION : compiler avec `/arch:SSE2` (défaut x64) et utiliser les intrinsics
SSE4.1 explicitement** (pas de `/arch:AVX2` global, qui ferait crasher sur une machine sans AVX ;
les chemins AVX2 sont dans des fonctions séparées choisies par pointeur au démarrage).

### 1.11 Manifest applicatif

Indispensable et gratuit (~500 octets), embarqué par `/MANIFEST:EMBED /MANIFESTINPUT:app.manifest` :

```xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <!-- DPI awareness v2 : requis, voir §2.6 -->
      <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
      <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/PM</dpiAware>
      <activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>
      <longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware>
    </windowsSettings>
  </application>
  <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
    <application>
      <supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/> <!-- Win 10/11 -->
    </application>
  </compatibility>
</assembly>
```

`longPathAware` évite l'échec de scan sur les bibliothèques musicales à arborescence profonde
(`Artiste/Album (Deluxe Edition Remastered 2019)/CD2/...` dépasse allègrement MAX_PATH). On combine
avec le préfixe `\\?\` dans `os_file_*` de toute façon.

On **n'ajoute pas** `<trustInfo requestedExecutionLevel="asInvoker">`… si, on l'ajoute : sans lui,
l'installeur-detection heuristique de Windows peut décider qu'un exe nommé `*setup*`/`*update*`
nécessite l'élévation. Notre exe s'appelle `minidisk.exe`, mais autant être explicite.

### 1.12 Icône et ressources

`/MANIFEST:EMBED` gère le manifest, mais l'icône passe par un `.res` :

```
rc /nologo /fo build\minidisk.res src\minidisk.rc
link ... build\minidisk.res
```

Coût : une icône multi-résolution (16/32/48/256, PNG-compressée pour la 256) pèse ~30-50 KB. C'est
**le plus gros poste "gratuit"** de l'exe après la police. On garde 16/24/32/48 en BMP et 256 en PNG :
~25 KB. Note : `rc.exe` fait partie du Windows SDK, pas d'outil externe, la contrainte est respectée.

### 1.13 Récapitulatif §1 — RECOMMANDATIONS

1. **Garder** `/NODEFAULTLIB /ENTRY /OPT:REF /OPT:ICF /MERGE:.rdata=.text /GS- /Gs9999999 /Oi`.
2. **Abandonner** `/ALIGN:16 /FILEALIGN:16` dès qu'on dépasse ~50 KB (risque AV, pas de mapping).
3. **Ajouter** `/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA`, manifest DPI v2 + UTF-8 + longPath, PDB en release.
4. **Fournir** `memset/memcpy/memmove/memcmp` + `_fltused` + math f32 maison SSE.
5. **Deux `.obj`** : `main.obj` (notre code, unity) et `third_party.obj` (codecs, rebuild rare).
6. **DLL chargées à la demande** sauf kernel32/user32/gdi32/opengl32.
7. **Debug avec CRT + ASan**, release sans, build release systématique avant commit.
8. **KPI taille exe** affiché par `build.bat` et consigné dans STATUS.md.

---

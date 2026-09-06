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

## 2. Fenêtre et contexte OpenGL sans bibliothèque

### 2.1 Le problème

Créer un contexte OpenGL moderne sur Windows est une danse en deux temps imposée par WGL : pour
appeler `wglChoosePixelFormatARB` et `wglCreateContextAttribsARB`, il faut déjà un contexte GL
courant, et pour avoir un contexte il faut un pixel format, qu'on ne peut choisir correctement
qu'avec `wglChoosePixelFormatARB`. La solution universelle : **une fenêtre bidon jetable**.

### 2.2 Séquence retenue (référence : gist mmozeiko `ed2ad27f75edf9c26053ce332a1f6647`)

```c
// platform/win32/win32_gl.c
static void gl_get_wgl_extensions(void) {
    // 1) fenêtre bidon, classe bidon
    WNDCLASSEXW wc = { .cbSize = sizeof(wc), .lpfnWndProc = DefWindowProcW,
                       .hInstance = GetModuleHandleW(0), .lpszClassName = L"minidisk_dummy" };
    RegisterClassExW(&wc);
    HWND dummy = CreateWindowExW(0, wc.lpszClassName, L"", 0, CW_USEDEFAULT, CW_USEDEFAULT,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, wc.hInstance, 0);
    HDC dc = GetDC(dummy);

    // 2) pixel format minimal via l'API legacy
    PIXELFORMATDESCRIPTOR pfd = { .nSize = sizeof(pfd), .nVersion = 1,
        .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        .iPixelType = PFD_TYPE_RGBA, .cColorBits = 24 };
    int fmt = ChoosePixelFormat(dc, &pfd);
    DescribePixelFormat(dc, fmt, sizeof(pfd), &pfd);
    SetPixelFormat(dc, fmt, &pfd);

    // 3) contexte legacy juste pour récupérer les extensions
    HGLRC rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);
    wglChoosePixelFormatARB    = (void*)wglGetProcAddress("wglChoosePixelFormatARB");
    wglCreateContextAttribsARB = (void*)wglGetProcAddress("wglCreateContextAttribsARB");
    wglSwapIntervalEXT         = (void*)wglGetProcAddress("wglSwapIntervalEXT");
    wglGetExtensionsStringARB  = (void*)wglGetProcAddress("wglGetExtensionsStringARB");

    // 4) démolition complète
    wglMakeCurrent(0, 0);
    wglDeleteContext(rc);
    ReleaseDC(dummy, dc);
    DestroyWindow(dummy);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}
```

Puis, sur la vraie fenêtre :

```c
int attribs[] = {
    WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
    WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
    WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
    WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
    WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
    WGL_COLOR_BITS_ARB,     24,
    WGL_ALPHA_BITS_ARB,      8,
    WGL_DEPTH_BITS_ARB,      0,   // on ne fait pas de 3D : PAS de depth buffer
    WGL_STENCIL_BITS_ARB,    0,   // clipping par scissor, pas par stencil
    WGL_SAMPLE_BUFFERS_ARB,  0,   // MSAA inutile : anti-aliasing analytique en SDF
    0,
};
int format; UINT count;
wglChoosePixelFormatARB(dc, attribs, 0, 1, &format, &count);
PIXELFORMATDESCRIPTOR pfd; DescribePixelFormat(dc, format, sizeof(pfd), &pfd);
SetPixelFormat(dc, format, &pfd);

int ctx_attribs[] = {
    WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
    WGL_CONTEXT_MINOR_VERSION_ARB, 3,
    WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
#if BUILD_DEBUG
    WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
#endif
    0,
};
HGLRC rc = wglCreateContextAttribsARB(dc, 0, ctx_attribs);
wglMakeCurrent(dc, rc);
```

**Important : on ne détruit jamais le `HDC` de la vraie fenêtre.** `GetDC` une fois, gardé pour la vie
de la fenêtre — la classe est enregistrée avec `CS_OWNDC`.

### 2.3 Core 3.3 vs 4.5 DSA : arbitrage

| Critère | GL 3.3 core | GL 4.5 core (+ DSA) |
|---------|-------------|---------------------|
| Disponibilité | ~100 % (tout GPU post-2010, VM, Mesa llvmpipe) | Intel HD 5xx+, NVIDIA 2013+, AMD GCN. Absent en RDP / VM basique |
| DSA (`glNamedBufferData`, `glCreateVertexArrays`) | non | oui, code plus court, pas de binding global |
| `glBufferStorage` (persistent mapping) | via `ARB_buffer_storage` (souvent dispo) | core |
| `glDebugMessageCallback` | ext `KHR_debug` | core |
| Portabilité future | macOS plafonne à **4.1** | macOS ne fera jamais 4.5 |
| Nos deux GPU (UHD 620, 930MX) | oui | oui (4.6 annoncé) |

Le point qui tranche : **macOS plafonne à OpenGL 4.1**. Écrire contre 4.5+DSA = réécrire le renderer
pour macOS. Et notre renderer est tellement simple (1 shader, 1 VBO) que DSA ne ferait gagner que
quelques dizaines de lignes.

**RECOMMANDATION : contexte 3.3 core profile**, avec détection optionnelle des extensions utiles :

```c
typedef struct GlCaps {
    b32 buffer_storage;      // ARB_buffer_storage -> persistent mapping
    b32 debug_output;        // KHR_debug
    b32 texture_storage;     // ARB_texture_storage
    i32 max_texture_size;    // >= 4096 garanti en 3.3
    const char *vendor, *renderer, *version;
} GlCaps;
```

Chemin lent générique (`glBufferData` + orphaning) toujours présent, chemin rapide (persistent
mapping) utilisé si l'extension est là. Moins de 40 lignes chacun.

**Chargement des fonctions GL.** `opengl32.dll` n'exporte que GL 1.1. Tout le reste passe par
`wglGetProcAddress`, **sauf** que `wglGetProcAddress` retourne NULL pour les fonctions GL 1.0/1.1
(`glClear`, `glViewport`, `glDrawArrays`, `glTexImage2D`, `glGenTextures`, `glBindTexture`,
`glEnable`, `glBlendFunc`, `glScissor`, `glTexSubImage2D`, `glTexParameteri`...). Le loader doit
essayer les deux sources :

```c
static void *gl_proc(const char *name) {
    void *p = (void *)wglGetProcAddress(name);
    // certains drivers renvoient 1, 2, 3 ou -1 pour "absent"
    if (p == 0 || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1) {
        static HMODULE gl32; if (!gl32) gl32 = LoadLibraryW(L"opengl32.dll");
        p = (void *)GetProcAddress(gl32, name);
    }
    return p;
}

#define GL_FUNCS \
    X(void,   glGenBuffers,               (GLsizei, GLuint*)) \
    X(void,   glDeleteBuffers,            (GLsizei, const GLuint*)) \
    X(void,   glBindBuffer,               (GLenum, GLuint)) \
    X(void,   glBufferData,               (GLenum, GLsizeiptr, const void*, GLenum)) \
    X(void,   glBufferSubData,            (GLenum, GLintptr, GLsizeiptr, const void*)) \
    X(void*,  glMapBufferRange,           (GLenum, GLintptr, GLsizeiptr, GLbitfield)) \
    X(GLboolean, glUnmapBuffer,           (GLenum)) \
    X(void,   glGenVertexArrays,          (GLsizei, GLuint*)) \
    X(void,   glBindVertexArray,          (GLuint)) \
    X(void,   glVertexAttribPointer,      (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(void,   glVertexAttribIPointer,     (GLuint, GLint, GLenum, GLsizei, const void*)) \
    X(void,   glEnableVertexAttribArray,  (GLuint)) \
    X(GLuint, glCreateShader,             (GLenum)) \
    X(void,   glShaderSource,             (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(void,   glCompileShader,            (GLuint)) \
    X(void,   glGetShaderiv,              (GLuint, GLenum, GLint*)) \
    X(void,   glGetShaderInfoLog,         (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(GLuint, glCreateProgram,            (void)) \
    X(void,   glAttachShader,             (GLuint, GLuint)) \
    X(void,   glLinkProgram,              (GLuint)) \
    X(void,   glGetProgramiv,             (GLuint, GLenum, GLint*)) \
    X(void,   glGetProgramInfoLog,        (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void,   glUseProgram,               (GLuint)) \
    X(void,   glDeleteShader,             (GLuint)) \
    X(GLint,  glGetUniformLocation,       (GLuint, const GLchar*)) \
    X(void,   glUniformMatrix4fv,         (GLint, GLsizei, GLboolean, const GLfloat*)) \
    X(void,   glUniform1i,                (GLint, GLint)) \
    X(void,   glUniform1f,                (GLint, GLfloat)) \
    X(void,   glUniform2f,                (GLint, GLfloat, GLfloat)) \
    X(void,   glUniform4fv,               (GLint, GLsizei, const GLfloat*)) \
    X(void,   glActiveTexture,            (GLenum)) \
    X(void,   glGenerateMipmap,           (GLenum)) \
    X(void,   glBlendFuncSeparate,        (GLenum, GLenum, GLenum, GLenum)) \
    X(void,   glDebugMessageCallback,     (GLDEBUGPROC, const void*)) \
    X(void,   glDebugMessageControl,      (GLenum, GLenum, GLenum, GLsizei, const GLuint*, GLboolean)) \
    X(const GLubyte*, glGetStringi,       (GLenum, GLuint))
    /* + les ~12 fonctions GL 1.1 récupérées via GetProcAddress sur opengl32.dll */

#define X(ret, name, args) typedef ret (APIENTRY *name##_t) args; static name##_t name;
GL_FUNCS
#undef X
```

Une quarantaine de pointeurs, ~1.5 KB de table + code de chargement. **On n'utilise ni glad ni GLEW**
(glad générerait plusieurs centaines de KB de source pour charger 3000 fonctions dont on utilise 45).

**Debug output.** En debug, `glDebugMessageCallback` avec `GL_DEBUG_OUTPUT_SYNCHRONOUS` transforme
chaque erreur GL en breakpoint avec la pile d'appel exacte. C'est **le** gain de productivité du
développement GL ; on l'active dès le premier jour. Coût en release : 0 (compilé out).

### 2.4 sRGB : le sujet que tout le monde rate

Deux façons de gérer le gamma :

- **A. Framebuffer sRGB matériel** : `WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB` + `glEnable(GL_FRAMEBUFFER_SRGB)`.
  Le GPU convertit linéaire→sRGB à l'écriture et sRGB→linéaire à la lecture des textures
  `GL_SRGB8_ALPHA8`. Le blending se fait **en linéaire** : physiquement correct.
- **B. Tout en sRGB non linéaire** (ce que font 95 % des UI, y compris Dear ImGui et les navigateurs
  pour le texte) : le blending se fait dans l'espace sRGB. Physiquement faux, mais c'est ce que fait
  Windows/GDI/Direct2D, donc ce à quoi l'œil de l'utilisateur — et le designer — sont habitués.

Le piège de A : un texte noir anti-aliasé sur fond blanc paraît **plus fin**, et un texte blanc sur
fond noir paraît **plus gras** qu'avec le rendu système. ClearType et CoreText appliquent une
correction gamma spécifique au texte, pas un simple blending linéaire. Passer en linéaire « propre »
donne un texte visiblement différent de tout le reste du bureau.

**RECOMMANDATION : option B**, avec compensation gamma explicite sur le texte.
- On **n'active pas** `GL_FRAMEBUFFER_SRGB` et on ne demande pas la capacité dans le pixel format.
- Les couleurs de `02b-design-tokens.md` sont des valeurs sRGB 8 bits : on les envoie telles quelles
  dans les vertex (`u8 rgba[4]`, normalisées par le GPU). Résultat identique à VS Code.
- **L'atlas de glyphes est en `GL_R8`** (couverture alpha, pas une couleur). On applique dans le
  fragment shader : `coverage = pow(coverage, u_text_gamma)` avec `u_text_gamma ≈ 1/1.2` pour du
  texte clair sur fond sombre. Un uniform réglable permet de caler une bonne fois par comparaison
  côte à côte avec VS Code sur le même écran.
- Les images (pochettes d'album) sont chargées en `GL_RGBA8` et affichées sans conversion : elles
  sont déjà en sRGB, elles vont dans un framebuffer sRGB, c'est cohérent.

C'est un choix pragmatique assumé : « ça ressemble à ce que l'utilisateur connaît » l'emporte sur
« c'est physiquement correct ».

### 2.5 Vsync, présentation, et le rendu à la demande à 0 % CPU

C'est le point d'architecture le plus important de la couche fenêtre, celui qui distingue une app
native soignée d'une démo de jeu.

**Le problème.** Une boucle de jeu classique fait `while(running){ PeekMessage; update; render;
SwapBuffers; }`. Avec vsync, `SwapBuffers` bloque ~16 ms : peu de CPU *utile*, mais un cœur qui
spinne dans le driver, un GPU réveillé 60 fois par seconde et un laptop qui chauffe. Inacceptable
pour une app qui passe 99 % de son temps à afficher une liste immobile.

| Approche | Repos | Réactivité | Complexité |
|----------|-------|-----------|-----------|
| A. Boucle de jeu + vsync | 3-8 % CPU, GPU actif en permanence | parfaite | triviale |
| B. `GetMessage` bloquant, rendu sur événement | **0 %**, GPU endormi | parfaite pour l'input, mais rien ne peut bouger tout seul | faible |
| C. B + réveil programmé (animations, jobs, USB) | **0 %** au repos, 60 fps pendant les animations | parfaite | moyenne |
| D. Thread de rendu séparé | complexe, gain nul ici | | élevée |

**RECOMMANDATION : option C.** Le cœur de la boucle :

```c
// app/app_main.c
for (;;) {
    // 1) Attendre : un message, un handle signalé (job fini, USB, watcher FS),
    //    ou l'échéance d'une animation.
    DWORD timeout = INFINITE;
    if (g_ui.active_animations > 0) timeout = 0;                    // frame suivante tout de suite
    else if (g_wake_at_us)          timeout = ms_until(g_wake_at_us);
    MsgWaitForMultipleObjectsEx(g_wait_count, g_wait_handles, timeout,
                                QS_ALLINPUT, MWMO_INPUTAVAILABLE | MWMO_ALERTABLE);

    // 2) Drainer TOUS les messages
    MSG msg;
    while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) goto done;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);        // la WndProc pousse des OsEvent dans la file
    }
    // 3) Drainer les événements des autres threads (jobs terminés, device USB, hotplug)
    app_drain_worker_events();

    // 4) Redessiner si nécessaire
    if (g_needs_redraw || g_ui.active_animations > 0) {
        g_needs_redraw = 0;
        arena_clear(g_arena_frame);
        app_build_frame();             // construit l'arbre UI + la liste de commandes de dessin
        r_flush();                     // upload VBO + draw calls
        SwapBuffers(g_dc);
    }
}
```

Détails critiques :

- **`WM_PAINT` doit être validé même si on ne dessine pas dans le handler.** Sinon Windows renvoie
  `WM_PAINT` en boucle et la boucle ne dort jamais (bug classique, coûte 100 % d'un cœur) :
  ```c
  case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps);
                   g_needs_redraw = 1; } return 0;
  ```
- **Redimensionnement et déplacement bloquent la boucle** : `WM_ENTERSIZEMOVE` lance une boucle
  modale interne à `user32` ; notre `MsgWaitForMultipleObjects` ne tourne plus, la fenêtre gèle.
  Remède standard :
  ```c
  case WM_ENTERSIZEMOVE: SetTimer(hwnd, TIMER_RESIZE, USER_TIMER_MINIMUM, 0); return 0;
  case WM_EXITSIZEMOVE:  KillTimer(hwnd, TIMER_RESIZE); return 0;
  case WM_TIMER:  if (wparam == TIMER_RESIZE) { app_build_frame(); r_flush(); SwapBuffers(g_dc); }
                  return 0;
  case WM_SIZE:   g_client_w = LOWORD(lparam); g_client_h = HIWORD(lparam);
                  g_needs_redraw = 1; return 0;
  ```
- **Vsync** : `wglSwapIntervalEXT(1)`. Avec le rendu à la demande, on ne swappe qu'en rafale pendant
  les animations ; le vsync évite le tearing sans coûter au repos. Exception : pendant un
  redimensionnement, le vsync rend le resize saccadé — on peut passer `wglSwapIntervalEXT(0)` entre
  `WM_ENTERSIZEMOVE` et `WM_EXITSIZEMOVE`.
- **`DwmFlush()`** : sur Windows composité (toujours depuis Win8), `SwapBuffers` + vsync peut ajouter
  une frame de latence. `DwmFlush()` juste avant `SwapBuffers` se synchronise sur le compositeur. À
  tester en phase 1, à n'activer que si on mesure une latence de curseur perceptible.
- **`timeBeginPeriod`** : on ne l'appelle **pas**. C'est un réglage global qui augmente la
  consommation de toute la machine ; le rendu à la demande n'en a pas besoin (on ne dépend pas de la
  précision du `Sleep`).

**Le compteur d'animations est le mécanisme central du 0 % CPU.** Notre système d'animation (§3.5)
est exponentiel (`x += (target - x) * rate * dt`), qui converge asymptotiquement : sans critère
d'arrêt, on anime éternellement à 60 fps. Règle : quand `|target - x| < eps` (eps = 0.25 px pour les
positions, 1/255 pour les couleurs, 0.002 pour les alphas), on snap sur `target` et on décrémente
`active_animations`. Quand il atteint 0, la boucle se rendort.

**Instrumentation obligatoire** : un overlay debug (F11) qui affiche `active_animations`, les frames
par seconde réelles, le temps de `app_build_frame`, le nombre de vertices et de draw calls. Un bug
sur ce compteur ne se voit pas à l'œil mais coûte 5 % de CPU en permanence.

**Objectif mesurable** : 0.0 % CPU et aucun réveil GPU quand la fenêtre est immobile. Vérification :
Gestionnaire des tâches (colonne « Utilisation de l'alimentation »), et `powercfg /energy` pour les
réveils de timer.

### 2.6 DPI awareness v2

**Toujours PerMonitorV2, déclaré dans le manifest** — et pas par `SetProcessDpiAwarenessContext` au
runtime, qui arrive trop tard (la fenêtre bidon est déjà créée avec la mauvaise notion de DPI).

Ce que PerMonitorV2 apporte par rapport à v1 :
- les zones non-client (barre de titre, bordures) sont mises à l'échelle automatiquement ;
- `WM_DPICHANGED` fournit dans `lparam` un `RECT*` avec la taille suggérée, **qu'il faut appliquer** ;
- les dialogues, menus et infobulles suivent l'échelle.

```c
case WM_DPICHANGED: {
    g_dpi_scale = (f32)HIWORD(wparam) / 96.0f;   // 1.0, 1.25, 1.5, 1.75, 2.0, 3.0
    RECT *r = (RECT *)lparam;
    SetWindowPos(hwnd, 0, r->left, r->top, r->right - r->left, r->bottom - r->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ui_font_atlas_rebuild(g_dpi_scale);          // re-rastériser TOUS les glyphes, voir §5
    g_needs_redraw = 1;
} return 0;
```

Modèle d'échelle interne :
- **le renderer travaille en pixels physiques** ;
- **le layout travaille en unités logiques (`dp`)**, converties par `px = round_f32(dp * dpi_scale)` ;
- **les épaisseurs de 1 px (bordures, séparateurs) sont spécifiées en pixels physiques, jamais en
  dp** : 1 dp à 150 % = 1.5 px = trait flou. Règle : `ui_hairline_px()` retourne 1 en dessous de
  200 %, 2 au-delà ;
- toutes les positions de rectangles opaques sont arrondies à l'entier avant émission de vertex ;
  les positions de glyphes ne le sont **pas** horizontalement (subpixel positioning, §5.6).

DPI initial : `GetDpiForWindow(hwnd)` (Win10 1607+). Surtout pas `GetDeviceCaps(LOGPIXELSX)` qui
renvoie 96 systématiquement en PerMonitorV2.

### 2.7 Barre de titre sombre et fenêtre « moderne »

Trois niveaux d'ambition :

| Niveau | Effet | API | Coût |
|--------|-------|-----|------|
| 1. Barre de titre sombre système | barre noire, boutons système standard | `DwmSetWindowAttribute(hwnd, 20, &TRUE, 4)` | 3 lignes |
| 2. Coins arrondis + bordure colorée | esthétique Win11 | attributs 33 (`WINDOW_CORNER_PREFERENCE`) et 34 (`BORDER_COLOR`) | 4 lignes |
| 3. Barre de titre custom (façon VS Code) | on dessine tout, y compris les boutons | `WM_NCCALCSIZE` + `WM_NCHITTEST` + `WM_NCACTIVATE` | ~250 lignes et beaucoup de cas limites |

Le niveau 3 donne l'apparence « app moderne » (une barre de 32 px qui contient la recherche, le
titre du disque et les boutons), mais implique de re-gérer soi-même : le redimensionnement par les
bords (zone de hit-test de 8 px), le double-clic pour maximiser, le menu système (clic droit /
Alt+Espace), le décalage de 7-8 px en maximisé (`WM_GETMINMAXINFO`, plus la détection d'une barre des
tâches en masquage automatique — sinon la fenêtre maximisée recouvre la barre des tâches et
l'utilisateur ne peut plus l'atteindre), et les Snap Layouts Win11 (survol du bouton Maximiser →
répondre `HTMAXBUTTON` à `WM_NCHITTEST`).

**RECOMMANDATION :**
- **Phase 1 : niveaux 1 + 2.** Effet immédiat, risque nul.
  ```c
  BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
  int corner = 2 /*DWMWCP_ROUND*/;
  DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
  COLORREF border = 0x002B2B2B;  // 0x00BBGGRR
  DwmSetWindowAttribute(hwnd, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));
  ```
  `dwmapi.dll` chargée dynamiquement ; si l'attribut 20 renvoie `E_INVALIDARG`, réessayer avec 19
  (Win10 < 20H1). Tout échec est ignoré silencieusement.
- **Phase 7 (polish) : niveau 3**, isolé dans `win32_titlebar.c`, avec une préférence pour revenir à
  la barre système en cas de bug. Le gain (30 px de hauteur utile, cohérence visuelle) vaut l'effort,
  mais pas avant que le reste marche.

**Le flash blanc au démarrage.** Une fenêtre créée avec `WS_VISIBLE` affiche le brush de la classe
(blanc par défaut) avant notre premier `SwapBuffers`. Remèdes cumulables :
1. `wc.hbrBackground = CreateSolidBrush(RGB(0x11, 0x11, 0x13))` — la couleur canvas de nos tokens ;
2. créer la fenêtre **sans** `WS_VISIBLE`, faire un premier rendu complet, puis `ShowWindow(hwnd, SW_SHOW)` ;
3. `DwmSetWindowAttribute(hwnd, 3 /*TRANSITIONS_FORCEDISABLED*/, ...)` si l'animation d'ouverture gêne.

On fait 1 + 2 : 5 lignes, et le démarrage paraît instantané.

### 2.8 Entrées (input)

| Événement | Message Win32 | Piège |
|-----------|---------------|-------|
| Déplacement souris | `WM_MOUSEMOVE` | `GET_X_LPARAM` = `(int)(short)LOWORD` : le cast en `short` est obligatoire (coordonnées négatives sur multi-écran) |
| Boutons | `WM_LBUTTONDOWN/UP`, `WM_RBUTTON*`, `WM_MBUTTON*`, `WM_XBUTTON*` | `SetCapture`/`ReleaseCapture` obligatoires pendant un drag |
| Sortie de fenêtre | `WM_MOUSELEAVE` | nécessite un `TrackMouseEvent(TME_LEAVE)` ré-armé après chaque leave |
| Molette | `WM_MOUSEWHEEL` | coordonnées en **espace écran**, pas client : `ScreenToClient` obligatoire |
| Molette horizontale | `WM_MOUSEHWHEEL` | pour le scroll horizontal des listes larges |
| Touches | `WM_KEYDOWN`/`WM_KEYUP`/`WM_SYSKEYDOWN`/`WM_SYSKEYUP` | `WM_SYSKEYDOWN` pour Alt+X, sinon Alt est avalé |
| Texte saisi | `WM_CHAR` | valeurs **UTF-16** : recombiner les paires de substitution ; filtrer les codes < 32 sauf Tab |
| IME (japonais) | `WM_IME_STARTCOMPOSITION`, `WM_IME_COMPOSITION`, `WM_IME_ENDCOMPOSITION` | important : les titres MD sont souvent en japonais |
| Focus | `WM_SETFOCUS`/`WM_KILLFOCUS` | vider l'état des modificateurs au `KILLFOCUS`, sinon Ctrl reste « enfoncé » après Alt+Tab |
| Curseur | `WM_SETCURSOR` | retourner TRUE après `SetCursor`, sinon Windows le réinitialise en boucle |

**Vitesse de scroll.** `WM_MOUSEWHEEL` fournit des multiples de `WHEEL_DELTA` (120). Le nombre de
lignes par cran vient de `SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, ...)` (défaut 3), avec la
valeur spéciale `WHEEL_PAGESCROLL` (0xFFFFFFFF) = page entière. À respecter, sinon « le scroll ne
fait pas comme les autres apps » — c'est le genre de détail qui décide de la perception de qualité.

**Double-clic** : ne pas utiliser `CS_DBLCLKS`/`WM_LBUTTONDBLCLK` (comportement figé, pas de triple
clic). On compte nous-mêmes avec `GetDoubleClickTime()` et `SM_CXDOUBLECLK`/`SM_CYDOUBLECLK`, jusqu'à
3 (triple clic = sélectionner toute la ligne dans un champ texte).

**Structure d'événement normalisée dans `platform.h`** — c'est elle qui rend le portage Linux
trivial, parce que la WndProc ne fait *que* de la traduction :

```c
typedef enum OsEventKind {
    OsEvent_None, OsEvent_Quit,
    OsEvent_KeyDown, OsEvent_KeyUp, OsEvent_Text,
    OsEvent_MouseMove, OsEvent_MouseDown, OsEvent_MouseUp, OsEvent_Wheel,
    OsEvent_Resize, OsEvent_DpiChange, OsEvent_FileDrop,
    OsEvent_FocusGain, OsEvent_FocusLose,
    OsEvent_DeviceArrived, OsEvent_DeviceRemoved,
} OsEventKind;

typedef struct OsEvent {
    struct OsEvent *next;   // liste chaînée intrusive allouée sur arena_frame
    OsEventKind kind;
    u32     key;            // OsKey_A .. OsKey_F12 : indépendant du layout (via scancode)
    u32     modifiers;      // OsMod_Ctrl | OsMod_Shift | OsMod_Alt | OsMod_Super
    u32     codepoint;      // OsEvent_Text : un codepoint complet, surrogates recombinés
    Vec2    pos;            // pixels physiques, origine haut-gauche du client
    Vec2    delta;          // wheel : (dx, dy) déjà multipliés par le nombre de lignes
    u32     click_count;    // 1, 2, 3
    String8 *paths;         // OsEvent_FileDrop
    u64     path_count;
    u64     timestamp_us;
} OsEvent;
```

Les événements sont poussés dans une file allouée sur `arena_frame` par la WndProc et consommés
pendant `app_build_frame()`. **Aucune logique métier dans la WndProc**, jamais.

**Touches indépendantes du layout.** `wparam` de `WM_KEYDOWN` est un *virtual key* dépendant du
layout : sur un clavier AZERTY, la touche physique `Q` envoie `VK_A`. Pour les raccourcis positionnels
(WASD, Ctrl+Z), on convertit le scancode (`(lparam >> 16) & 0xFF`) via
`MapVirtualKeyW(scancode, MAPVK_VSC_TO_VK)` sur le layout US. Pour ce projet, seuls les raccourcis
alphabétiques comptent (Ctrl+F, Ctrl+A, Ctrl+Z...) et l'utilisateur attend qu'ils suivent son
layout : **on garde le VK du layout courant** et on documente ce choix.

### 2.9 Drag & drop depuis l'Explorateur

| | `DragAcceptFiles` (shell) | `IDropTarget` (OLE) |
|---|---|---|
| API | `DragAcceptFiles(hwnd, TRUE)` + `WM_DROPFILES` + `DragQueryFileW` | `OleInitialize` + `RegisterDragDrop` + vtable `IDropTarget` |
| Feedback pendant le survol | **aucun** | complet (`DragEnter`/`DragOver` → surligner la cible) |
| Position du drop | `DragQueryPoint` (au drop seulement) | en continu |
| Effet copier/déplacer/lien | non | oui (`DROPEFFECT_COPY`...) |
| Sources non-fichier (texte, URL) | non | oui |
| Coût | ~20 lignes, `shell32.dll` | ~180 lignes de vtable COM manuelle, `ole32.dll` |

Implémenter `IDropTarget` en C pur = écrire la vtable à la main. Le pattern (qui resservira pour
Media Foundation, §6.4) :

```c
typedef struct DropTarget {
    IDropTargetVtbl *lpVtbl;    // DOIT être le premier membre
    LONG ref;
    // notre état applicatif
} DropTarget;

static HRESULT STDMETHODCALLTYPE dt_QueryInterface(IDropTarget *this_, REFIID riid, void **out) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropTarget)) {
        *out = this_; dt_AddRef(this_); return S_OK;
    }
    *out = 0; return E_NOINTERFACE;
}
static IDropTargetVtbl g_drop_vtbl = {
    dt_QueryInterface, dt_AddRef, dt_Release,
    dt_DragEnter, dt_DragOver, dt_DragLeave, dt_Drop,
};
```

Note : `IsEqualIID` compare 16 octets — on l'écrit nous-mêmes (`mem_cmp`), et on définit les GUID
en dur (`static const GUID IID_IDropTarget = {0x00000122,0,0,{0xC0,0,0,0,0,0,0,0x46}};`) pour ne pas
lier `uuid.lib`.

**RECOMMANDATION :**
- **Phases 1-2 : `DragAcceptFiles`.** Suffisant pour « je glisse un dossier, il se scanne ».
- **Phase 7 : `IDropTarget`.** Le feedback pendant le survol (« déposer ici pour insérer à la
  position 5 du plan ») fait partie du workflow ; nécessaire dès qu'il y a plusieurs cibles de drop.

Le drag & drop **interne** (réordonner le plan) n'utilise **pas** OLE : c'est de l'état UI pur
(§3.8), 100 % portable.

### 2.10 Détails divers de la fenêtre

- **Position/taille sauvegardées** : `GetWindowPlacement`/`SetWindowPlacement` sérialise un
  `WINDOWPLACEMENT` (44 octets) qui gère correctement le couple maximisé/restauré. À valider au
  démarrage contre les moniteurs présents (`MonitorFromRect` + `MONITOR_DEFAULTTONULL` → si NULL,
  recentrer sur le moniteur principal), sinon la fenêtre réapparaît hors écran après débranchement
  d'un écran.
- **Instance unique** : `CreateMutexW(0, TRUE, L"Local\\minidisk_single")` ; si
  `GetLastError() == ERROR_ALREADY_EXISTS`, trouver la fenêtre existante (`FindWindowW` sur notre nom
  de classe), lui transmettre les chemins de la ligne de commande par `WM_COPYDATA`, puis quitter.
  Nécessaire pour « Ouvrir avec » depuis l'Explorateur, et pour ne pas ouvrir deux sessions USB.
- **Taille minimale** : `WM_GETMINMAXINFO` → `ptMinTrackSize = { 800*dpi, 520*dpi }` : en dessous, la
  disposition 3 panneaux n'a plus de sens.
- **`CS_OWNDC`** dans la classe : indispensable pour garder un `HDC` valide pour OpenGL.
- **Curseurs** : charger les curseurs système (`IDC_ARROW`, `IDC_IBEAM`, `IDC_SIZEWE`, `IDC_SIZENS`,
  `IDC_HAND`, `IDC_NO`) une fois au démarrage. L'UI positionne `ui_state.cursor = UiCursor_ResizeH`,
  la plateforme applique dans `WM_SETCURSOR`.
- **Perte de contexte GL** : sur changement de GPU (bascule Optimus, mise à jour de pilote, sortie de
  veille), le contexte peut devenir invalide — `glGetError()` renvoie `GL_CONTEXT_LOST` en 4.5,
  ou tout simplement plus rien ne s'affiche. Sur `WM_DISPLAYCHANGE` et `WM_POWERBROADCAST`
  (`PBT_APMRESUMEAUTOMATIC`), on vérifie `wglGetCurrentContext()` et on recrée le contexte +
  ré-uploade l'atlas si nécessaire. À implémenter en phase 7, mais **prévoir dès le départ que
  toutes les ressources GPU sont reconstructibles à partir de données CPU** (l'atlas de glyphes vit
  en RAM dans `arena_permanent` et n'est jamais lu depuis le GPU).

### 2.11 Récapitulatif §2 — RECOMMANDATIONS

1. **Contexte GL 3.3 core**, dummy window + `wglChoosePixelFormatARB` + `wglCreateContextAttribsARB` ;
   pas de depth, pas de stencil, pas de MSAA.
2. **Loader GL maison** (~45 fonctions, X-macro), `wglGetProcAddress` + fallback `opengl32.dll`.
3. **Pas de framebuffer sRGB** : blending en sRGB, correction gamma du texte dans le shader.
4. **`MsgWaitForMultipleObjectsEx` + rendu à la demande + vsync**, compteur d'animations actives,
   objectif **0 % CPU au repos**, overlay debug pour le vérifier.
5. **PerMonitorV2 dans le manifest** ; `WM_DPICHANGED` → `SetWindowPos` + rebuild d'atlas.
6. **Dark mode DWM en phase 1**, barre de titre custom en phase 7.
7. **`DragAcceptFiles` d'abord, `IDropTarget` en polish.**
8. **Aucune logique dans la WndProc** : traduction en `OsEvent` et rien d'autre.

---

## 3. Architecture UI

### 3.1 Le débat immediate mode vs retained mode

C'est la décision structurante du projet côté UI. Trois familles :

**A. Immediate mode « pur » (Dear ImGui, Nuklear, Handmade Hero).**
```c
if (ui_button("Graver")) { start_burn(); }
```
L'appel *est* le widget : il dessine et retourne l'interaction dans la même expression. L'état
persistant (scroll, focus, animation) est stocké dans une table indexée par un ID hashé du chemin
d'appel.
- **+** ergonomie de code imbattable, zéro synchronisation entre modèle et vue, code lisible de haut
  en bas, refactoring trivial.
- **−** layout en un seul passage : on ne connaît pas la taille d'un conteneur avant d'avoir émis ses
  enfants. D'où les hacks classiques (tailles fixes, `SameLine()`, layout « une frame en retard »).
- **−** l'animation et le focus sont possibles mais toujours un peu bricolés.
- **−** difficile de faire des layouts « le parent dépend des enfants ET les enfants dépendent du
  parent » (texte qui wrappe dans une colonne dont la largeur dépend du contenu).

**B. Retained mode classique (Win32, Qt, WPF).**
Un arbre d'objets persistants avec des callbacks. On synchronise manuellement le modèle et la vue.
- **+** layout complet, animation naturelle, performance sur les grands arbres immobiles.
- **−** toute la complexité du monde : invalidation, cycles de vie, synchronisation, ownership. C'est
  exactement ce qu'on ne veut pas écrire en C.

**C. Immediate API / retained core (Ryan Fleury, « UI Series » sur rfleury.com).**
L'API ressemble à de l'immediate mode, mais chaque appel construit un **nœud dans un arbre reconstruit
chaque frame**, dont les données persistantes (`UI_Box`) sont retrouvées par une table de hachage
clé→box. Le layout se fait **après** la construction de l'arbre, en plusieurs passes. L'interaction
d'une frame utilise le rectangle calculé à la frame **précédente** (ce qui est invisible à
l'utilisateur, 16 ms).
- **+** ergonomie de l'immediate mode ;
- **+** layout complet multi-passes (taille en fonction du contenu, du parent, ou des frères) ;
- **+** animation, focus, hot/active persistants et propres, parce que le `UI_Box` survit d'une frame
  à l'autre ;
- **+** on peut inspecter/déboguer l'arbre (un panneau debug qui affiche la hiérarchie) ;
- **−** plus de machinerie : ~1500-2500 lignes de « moteur » avant le premier widget.

**RECOMMANDATION : option C, l'architecture Fleury.** Justifications concrètes pour *ce* projet :
1. On a besoin de **listes virtualisées de 100k lignes** avec des colonnes qui se redimensionnent :
   layout multi-passes obligatoire.
2. On a besoin d'**animations** propres (survol, sélection, jauge de capacité, apparition de popups)
   sans compter les frames à la main.
3. On a besoin d'un **focus clavier navigable** (tout au clavier est une exigence de R-02), ce qui
   suppose un ordre de tabulation dérivé de l'arbre.
4. Le coût (2000 lignes une fois) est amorti sur les 8 phases, et c'est du code sans dépendance,
   parfaitement portable.

### 3.2 Le cœur : `UI_Box` et la clé

```c
// ui/ui_core.h
typedef u64 UI_Key;   // hash du chemin : hash(parent_key, string_after_"##")

typedef u32 UI_Flags;
enum {
    UI_Clickable          = (1u<<0),
    UI_Scrollable         = (1u<<1),
    UI_Focusable          = (1u<<2),
    UI_DrawBackground     = (1u<<3),
    UI_DrawBorder         = (1u<<4),
    UI_DrawText           = (1u<<5),
    UI_DrawDropShadow     = (1u<<6),
    UI_Clip               = (1u<<7),
    UI_HotAnimation       = (1u<<8),
    UI_ActiveAnimation    = (1u<<9),
    UI_FloatingX          = (1u<<10),   // position imposée, ignoré par le layout du parent
    UI_FloatingY          = (1u<<11),
    UI_OverflowX          = (1u<<12),
    UI_OverflowY          = (1u<<13),
    UI_DisableTextTrunc   = (1u<<14),
    UI_Disabled           = (1u<<15),
};

typedef enum UI_SizeKind {
    UI_SizeKind_Null,        // 0 : le nœud n'occupe rien
    UI_SizeKind_Pixels,      // taille fixe (en px physiques, déjà scalée DPI)
    UI_SizeKind_TextContent, // taille du texte + padding
    UI_SizeKind_PercentOfParent,
    UI_SizeKind_ChildrenSum, // somme (axe principal) / max (axe secondaire) des enfants
} UI_SizeKind;

typedef struct UI_Size {
    UI_SizeKind kind;
    f32 value;
    f32 strictness;   // 0 = je me laisse écraser en priorité ; 1 = je ne cède jamais
} UI_Size;

typedef struct UI_Box {
    // -- hachage / arbre --
    struct UI_Box *hash_next, *hash_prev;                 // chaînage dans la table
    struct UI_Box *first, *last, *next, *prev, *parent;   // arbre de la frame courante
    UI_Key  key;
    u64     last_frame_touched;                           // pour l'éviction

    // -- paramètres fournis par l'appelant --
    UI_Flags   flags;
    String8    display_string;    // partie avant "##"
    UI_Size    pref_size[2];      // [Axis_X], [Axis_Y]
    Axis2      child_layout_axis;
    f32        corner_radius[4];  // TL, TR, BR, BL
    f32        border_thickness;
    Vec4       bg_color, border_color, text_color;
    f32        text_padding;
    UI_Font    font;

    // -- calculé par le layout --
    f32   computed_rel_pos[2];
    f32   computed_size[2];
    Rect  rect;                   // absolu, écran, en px physiques
    Rect  clip_rect;

    // -- persistant d'une frame à l'autre --
    Vec2  view_off;               // scroll courant
    Vec2  view_off_target;        // scroll cible (animation)
    f32   hot_t, active_t;        // 0..1, animés
    f32   disabled_t, focus_t;
} UI_Box;
```

La table de hachage est **un tableau de buckets alloué une fois** dans `arena_permanent`
(`4096` buckets, chaînage intrusif). Les `UI_Box` eux-mêmes vivent dans un pool avec free-list :
créés à la première apparition, réutilisés ensuite, évincés quand
`current_frame - last_frame_touched > 2` (un `UI_Box` non touché pendant 2 frames est mort).

**Comment la clé est calculée.** Convention empruntée à ImGui/Fleury :
`ui_label("Graver##burn_button")` → la partie affichée est `"Graver"`, la partie identifiante est
`"burn_button"`. Sans `##`, toute la chaîne sert de clé. Avec `###`, seule la partie après sert de clé
et le label affiché peut changer sans casser l'identité (utile pour un bouton dont le texte change).
La clé finale est `hash_u64(parent->key, id_string)` : deux boutons « Supprimer » dans deux panneaux
différents ont des clés différentes **automatiquement**. Pour les listes, on pousse un seed :
`UI_Seed(track->id) { ... }`.

Une chaîne vide → clé 0 → **nœud non interactif** (pur conteneur de layout). C'est le cas le plus
fréquent et ça évite de nommer des milliers de divs.

### 3.3 La pile de paramètres

Le confort d'écriture vient des piles de paramètres avec macros de portée :

```c
UI_Parent(container) UI_PrefWidth(ui_px(200, 1)) UI_TextColor(theme.text_dim)
{
    ui_labelf("%d pistes", count);
}
```

Implémentation : une pile par paramètre + une macro `DeferLoop` :

```c
#define DeferLoop(begin, end) for (int _i_ = ((begin), 0); !_i_; _i_ += 1, (end))
#define UI_Parent(b)  DeferLoop(ui_push_parent(b), ui_pop_parent())
#define UI_PrefWidth(s) DeferLoop(ui_push_pref_width(s), ui_pop_pref_width())
```

`DeferLoop` est le seul « truc » du système : un `for` qui s'exécute une fois et garantit
l'appariement push/pop même avec un `break` mal placé (non — avec un `break` on saute le pop ; d'où
la règle : **jamais de `break`/`return`/`goto` depuis l'intérieur d'un bloc `UI_*`**, vérifiée en
revue et détectable par une assertion sur la profondeur de pile en fin de frame).

Piles nécessaires : `parent`, `pref_width`, `pref_height`, `child_layout_axis`, `flags`,
`bg_color`, `text_color`, `border_color`, `corner_radius`, `border_thickness`, `font`, `font_size`,
`text_padding`, `seed_key`, `fixed_x`, `fixed_y`, `fixed_width`, `fixed_height`, `hover_cursor`,
`disabled`. Une vingtaine, chacune un tableau de 64 entrées dans `arena_permanent` : ~10 KB.

Détail important : les piles sont **auto-pop** par défaut (« next-only ») dans le style Fleury : un
push s'applique au prochain nœud créé puis se dépile, sauf si on utilise la forme `UI_XxxNext`. En
pratique on garde les deux formes : `UI_PrefWidth(...)` pour un bloc, `ui_set_next_pref_width(...)`
pour un seul nœud. Ça évite 80 % des accolades.

### 3.4 Layout : les passes

Le layout tourne **après** la construction complète de l'arbre, sur chaque axe indépendamment
(X puis Y). Quatre passes par axe :

1. **Tailles indépendantes** (`UI_SizeKind_Pixels`, `UI_SizeKind_TextContent`) : descente préfixe,
   calcul direct. Le `TextContent` appelle `ui_text_dim(font, size, string)` qui mesure via l'atlas
   (avec cache de mesure, §5.4).
2. **Tailles dépendantes de l'ascendance** (`PercentOfParent`) : descente préfixe ; nécessite que le
   parent ait déjà une taille — d'où l'ordre.
3. **Tailles dépendantes de la descendance** (`ChildrenSum`) : remontée postfixe.
4. **Résolution des violations** : sur l'axe de layout du parent, si la somme des tailles des enfants
   dépasse la taille du parent, on retire l'excédent en le distribuant selon
   `(1 - strictness)` pondéré par la taille. Un enfant à `strictness = 1` n'est jamais réduit
   (bouton), un enfant à `strictness = 0` absorbe tout (label tronqué). Sur l'axe secondaire, on
   clippe simplement si `UI_Clip` est posé.
5. **Positionnement final** : descente préfixe, accumulation de l'offset sur l'axe du parent, ajout
   de `-view_off` (scroll), calcul de `rect` absolu et de `clip_rect` (intersection avec celui du
   parent).

Coût : 5 parcours d'arbre. Pour un arbre UI typique de 400-800 nœuds (grâce à la virtualisation),
c'est **de l'ordre de 20-40 µs**. Non mesurable. Le layout n'est jamais le goulot.

Constructeurs de taille :
```c
static inline UI_Size ui_px(f32 v, f32 strictness)      { return (UI_Size){UI_SizeKind_Pixels, v, strictness}; }
static inline UI_Size ui_text_dim(f32 pad, f32 s)       { return (UI_Size){UI_SizeKind_TextContent, pad, s}; }
static inline UI_Size ui_pct(f32 v, f32 strictness)     { return (UI_Size){UI_SizeKind_PercentOfParent, v, strictness}; }
static inline UI_Size ui_children_sum(f32 strictness)   { return (UI_Size){UI_SizeKind_ChildrenSum, 0, strictness}; }
#define ui_fill() ui_pct(1.0f, 0.0f)
```

**Un mot sur ce qu'on ne fait pas** : pas de flexbox complet, pas de grid CSS, pas de contraintes
(Cassowary). Ces cinq passes couvrent 100 % de ce dont une app à 3 panneaux, listes et barres
d'outils a besoin. Si un cas résiste, on utilise `UI_FloatingX/Y` et on calcule à la main.

### 3.5 Interaction : hot, active, et la signature

```c
typedef struct UI_Signal {
    UI_Box *box;
    Vec2 mouse;            // relatif au coin haut-gauche de la box
    Vec2 drag_delta;       // depuis le début du drag
    u32  scroll;
    b32  hovering;         // la souris est dessus et rien d'autre n'est actif
    b32  pressed;          // bouton enfoncé cette frame
    b32  released;         // relâché cette frame
    b32  clicked;          // pressed+released sur la même box (le "vrai" clic)
    b32  double_clicked;
    b32  right_clicked;
    b32  dragging;
    b32  keyboard_pressed; // Espace/Entrée sur un élément focus
} UI_Signal;

UI_Signal ui_signal_from_box(UI_Box *box);
```

État global minimal :
```c
typedef struct UI_State {
    UI_Key hot_key;             // survolé
    UI_Key active_key;          // en cours d'interaction (bouton enfoncé)
    UI_Key focus_key;           // focus clavier
    UI_Key drag_key;
    Vec2   drag_start_mouse;
    u8     drag_state[256];     // payload du DnD interne (voir 3.8)
    u64    frame_index;
    f32    animation_rate;      // 1 - pow(2, -30 * dt)
    i32    active_animations;   // le compteur du 0 % CPU
} UI_State;
```

Règle d'or : `ui_signal_from_box` teste la souris contre `box->rect` **calculé à la frame
précédente** (l'arbre courant n'est pas encore layouté au moment où on appelle `ui_button`). C'est le
compromis fondamental de l'architecture : une frame de latence sur le *hit test* d'un élément qui
vient d'apparaître ou de bouger. Invisible en pratique ; la seule pathologie est « je clique sur un
bouton qui se déplace pendant que je clique », qui n'existe pas dans notre UI.

**Ordre de test.** Le hit test doit respecter l'ordre de dessin inverse (le dernier dessiné est
au-dessus). On maintient une liste des boxes triée par profondeur/ordre d'émission, et
`ui_signal_from_box` vérifie qu'aucune box émise *après* et contenant le point n'existe. Optimisation
simple : on calcule `hot_key` **une fois par frame** en parcourant l'arbre en ordre inverse et en
s'arrêtant au premier `UI_Clickable` qui contient la souris et dont le `clip_rect` la contient aussi.
Les widgets se contentent ensuite de comparer leur clé à `hot_key`.

### 3.6 Animations

Modèle unique, exponentiel, indépendant du framerate :

```c
// dans ui_end_build(), pour chaque box vivante :
f32 rate = 1.0f - pow_f32(2.0f, -30.0f * dt);            // ~ convergence en 120 ms
f32 hot_target = (box->key == ui->hot_key) ? 1.0f : 0.0f;
box->hot_t += (hot_target - box->hot_t) * rate;
if (abs_f32(hot_target - box->hot_t) < 0.002f) box->hot_t = hot_target;
else                                           ui->active_animations += 1;
```

Ce qu'on anime : `hot_t` (survol), `active_t` (pression), `focus_t` (anneau de focus),
`disabled_t`, `view_off` (scroll fluide), les hauteurs de panneaux repliables, la jauge de capacité,
l'opacité des popups et des toasts. **Ce qu'on n'anime pas** : rien qui bouge à chaque frame en
permanence (pas de spinner tournant en boucle sauf pendant un transfert actif — et là on est de
toute façon en train de rafraîchir la progression).

Le `rate` doit dépendre de `dt`, mesuré avec `QueryPerformanceCounter`, **clampé à 100 ms** (si l'app
a été suspendue 3 secondes, on ne veut pas un saut d'animation ; on veut juste reprendre).

**Un seul paramètre esthétique** : la constante 30 (durée ~120 ms). R-02b suggère 100-150 ms pour
les micro-interactions ; on expose `ANIM_RATE_FAST` (150 ms) et `ANIM_RATE_SLOW` (250 ms, pour les
panneaux) et on ne dévie pas.

### 3.7 Focus clavier, popups, modales

**Focus.** L'ordre de tabulation est **l'ordre de parcours préfixe de l'arbre**, filtré sur
`UI_Focusable`. Tab/Shift+Tab avancent dans cette liste (construite une fois par frame, dans
`arena_frame`, coût négligeable). Les flèches naviguent *dans* un widget (liste, grille) et non entre
widgets. `Échap` défocus ou ferme le popup courant.

L'anneau de focus est dessiné **par le widget**, en fonction de `focus_t`, et uniquement si le focus a
été obtenu au clavier (`ui->focus_via_keyboard`) — sinon on met un anneau autour de chaque bouton
cliqué à la souris, ce qui est laid. C'est le comportement `:focus-visible` du web.

**Popups (menu contextuel, autocomplétion, sélecteur de mode).** Options :

| Option | Description | Verdict |
|--------|-------------|---------|
| Fenêtre Win32 séparée (`WS_POPUP`) | vraie fenêtre, peut dépasser de la fenêtre principale | contexte GL à partager, non portable, lourd |
| Nœud flottant dans notre arbre, dessiné en dernier | contraint à la fenêtre, trivial | **retenu** |

**RECOMMANDATION : popups internes.** Un popup est un nœud racine de deuxième niveau
(`ui->root_overlay`), avec `UI_FloatingX|UI_FloatingY`, positionné sous son ancre, repositionné s'il
sort de la fenêtre (flip vertical, puis clamp horizontal). Il est dessiné après tout le reste
(couche `overlay` du renderer, §4.6) et capture le hit test en priorité. Un clic en dehors le ferme.

Conséquence acceptée : un menu ne peut pas dépasser de la fenêtre. Dans une app à 3 panneaux avec une
fenêtre de 1200x800, ça n'arrive jamais si on flippe correctement.

**Modales** (confirmation « effacer le disque ? »). Même mécanisme + un voile plein écran
(`bg_color = (0,0,0,0.45)`) qui absorbe les clics + capture du focus clavier (Tab boucle *dans* la
modale) + Échap = annuler, Entrée = action par défaut. ~80 lignes.

**Toasts** : liste de notifications avec un `expire_at_us`. Attention au 0 % CPU : un toast qui doit
disparaître dans 4 s programme `g_wake_at_us`, il n'y a pas de polling.

### 3.8 Listes virtualisées : 100 000 pistes

C'est le widget critique du projet. Contrainte : afficher, filtrer, trier et sélectionner dans
100k pistes, avec scroll fluide et 0 hoquet.

**Principe.** On ne crée des `UI_Box` que pour les lignes visibles :

```c
typedef struct UI_ListParams {
    u64 item_count;
    f32 item_height;        // hauteur fixe : le cas facile et le seul dont on a besoin
    f32 scroll;             // px
    f32 viewport_height;
} UI_ListParams;

typedef struct UI_ListRange { u64 first, opl; f32 y_offset; } UI_ListRange;

static UI_ListRange ui_list_visible_range(UI_ListParams p) {
    i64 first = (i64)floor_f32(p.scroll / p.item_height) - 1;      // 1 ligne de marge
    i64 count = (i64)ceil_f32(p.viewport_height / p.item_height) + 2;
    first = Clamp(0, first, (i64)p.item_count);
    i64 opl  = Clamp(0, first + count, (i64)p.item_count);
    return (UI_ListRange){ (u64)first, (u64)opl, (f32)first * p.item_height };
}
```

Le conteneur a une hauteur virtuelle `item_count * item_height` (un simple spacer flottant), et le
bloc de lignes visibles est positionné à `y_offset - scroll`. Nombre de `UI_Box` créés : ~60 lignes
× ~6 nœuds = **360 nœuds**, quel que soit le nombre de pistes. Le layout reste à 30 µs.

**Hauteur de ligne fixe : décision assumée.** Les hauteurs variables (une ligne « album » plus haute
qu'une ligne « piste ») imposeraient un index de préfixes (arbre de Fenwick sur les hauteurs) pour
convertir scroll↔index. On s'interdit ce cas : R-02b fixe la ligne compacte à 22-24 px, la ligne
standard à 28-32 px. Les en-têtes de groupe sont gérés comme des **items d'un type différent mais de
même hauteur**, ou par un mode « sections » où l'on maintient un petit tableau des offsets de section
(quelques milliers d'entrées maximum, recherche binaire O(log n)).

**Le filtrage/tri est côté core, pas côté UI.** L'UI reçoit un `u32 *visible_indices` +
`u64 visible_count` produit par `library_query()` : un tableau plat d'indices dans l'index SoA. Le
tri produit une permutation. L'UI ne fait *que* lire `indices[i]` et afficher les champs. C'est ce qui
garantit qu'un filtre sur 100k pistes reste sous les 5 ms (§9.4).

**Sélection.** Un bitset (`u64 *`, `100000/64 = 1563` mots = 12.5 KB) plutôt qu'une liste d'indices :
test O(1), sélection totale O(n/64), et l'affichage d'une ligne ne coûte qu'un test de bit. Les
opérations Shift+clic (plage) et Ctrl+clic (toggle) sont triviales. L'ancre de sélection est un
`u64 anchor_index` dans l'état de la liste.

**Scroll.** `view_off_target` bougé par la molette, `view_off` animé vers lui. Clamp sur
`[0, content_h - viewport_h]`. La barre de défilement est un widget à part (piste + poignée), avec
`UI_Clickable` sur la poignée et une gestion du clic dans la piste (page up/down).

### 3.9 Saisie de texte

Nécessaire pour : la recherche (bibliothèque), le titrage des pistes et du disque, les champs
numériques (gap, gain). Composants :

```c
typedef struct UI_TextEditState {
    u8  *buf;            // UTF-8, dans arena_permanent ou un buffer fixe
    u64  cap, len;
    i64  cursor;         // index d'octet, TOUJOURS sur une frontière de codepoint
    i64  mark;           // autre extrémité de la sélection
    i64  preferred_col;  // colonne visuelle mémorisée pour Haut/Bas (multi-ligne)
    // composition IME
    u8  *composition; u64 composition_len; i64 composition_cursor;
} UI_TextEditState;
```

Opérations à implémenter (~450 lignes, portables) : insertion de codepoint, suppression
avant/arrière, suppression de mot (Ctrl+Backspace/Delete), déplacement caractère/mot/ligne/document,
sélection avec Shift, sélectionner tout, couper/copier/coller, annuler/refaire (pile d'opérations),
double-clic = mot, triple-clic = tout.

**Frontières de mot.** On classe chaque codepoint : espace / ponctuation / alphanumérique / idéogramme.
La segmentation Unicode complète (UAX #29) est hors de portée et inutile ; une heuristique sur ces
4 classes couvre le français, l'anglais et « ça ne fait rien de stupide » en japonais.

**Presse-papiers.** `OpenClipboard/GetClipboardData(CF_UNICODETEXT)/CloseClipboard`, dans
`platform.h` :
```c
b32     os_clipboard_set_text(String8 utf8);
String8 os_clipboard_get_text(Arena *arena);
```
Piège : `OpenClipboard` peut échouer si une autre app le détient — réessayer 5 fois avec un `Sleep(1)`.

**IME.** Le minimum viable : laisser Windows dessiner la fenêtre de composition (comportement par
défaut si on ne gère pas `WM_IME_*`), mais **positionner le caret** via `ImmSetCompositionWindow`
avec `CFS_POINT`, sinon la fenêtre de candidats apparaît en haut à gauche de l'écran. Une trentaine
de lignes (`imm32.dll`, chargée dynamiquement) pour un résultat correct. La composition inline
(afficher le texte en cours de composition souligné, dans notre champ) est une amélioration de phase 7.

### 3.10 Drag & drop interne

Réordonner le plan de gravure, glisser des pistes de la bibliothèque vers le plan. 100 % dans notre
UI, donc portable :

```c
typedef enum UI_DragKind { UI_Drag_None, UI_Drag_LibraryTracks, UI_Drag_PlanItems,
                           UI_Drag_PanelSplitter } UI_DragKind;
typedef struct UI_DragPayload {
    UI_DragKind kind;
    u32 *indices; u64 count;       // alloué dans arena_permanent, libéré au drop
    Vec2 grab_offset;
} UI_DragPayload;
```

Machine à états : `Idle → MaybeDrag` (bouton enfoncé, déplacement < seuil `SM_CXDRAG`, 4 px)
`→ Dragging` (au-delà du seuil) `→ Drop | Cancel` (Échap ou relâchement hors cible).

Rendu : un « fantôme » semi-transparent suivant la souris (dessiné dans la couche overlay), et un
**indicateur d'insertion** (une ligne de 2 px accentuée) entre deux lignes du plan. Le calcul de
l'index d'insertion = `round((mouse_y - list_top + scroll) / item_height)`.

Auto-scroll pendant le drag : si la souris est à moins de 24 px du bord de la liste, on scrolle à
une vitesse proportionnelle. Attention : c'est une animation, donc ça maintient `active_animations`
> 0, donc la boucle tourne — ce qui est correct, on est en train de dragger.

### 3.11 Découplage core / UI / platform

C'est la règle qui décide de la portabilité et de la testabilité. Formulation exécutoire :

```
ui/  -->  base/          (types, arena, string, math)
ui/  -->  core/ (lecture seule : structs plates, jamais de pointeur possédé)
ui/  -->  r_*  (renderer, qui lui-même ne parle qu'à gl_*)
core/ --> base/, platform.h
core/ --> RIEN d'autre. Pas de <windows.h>, pas de GL, pas d'UI.
platform/win32/ --> tout Windows
app/ --> tout le monde, c'est la colle
```

Mécanisme concret pour « l'UI lit le core en lecture seule » : à chaque frame, l'app produit un
**snapshot** — pas une copie des données (100k pistes ne se copient pas), mais une struct de vues :

```c
typedef struct AppView {
    LibraryIndex *lib;          // pointeur const vers l'index (SoA), stable entre les frames
    u32          *visible;      // résultat de la requête courante
    u64           visible_count;
    Plan         *plan;
    DeviceState   device;       // copié par valeur : petit
    JobStats      jobs;         // copié par valeur
    u64           lib_generation;   // change quand l'index est reconstruit -> invalider les caches UI
} AppView;
```

Et « l'UI pousse des commandes » : une file de commandes typées, consommée par l'app après la
construction de la frame.

```c
typedef enum CmdKind {
    Cmd_None, Cmd_ScanFolder, Cmd_PlanAddTracks, Cmd_PlanRemove, Cmd_PlanMove,
    Cmd_PlanSetMode, Cmd_DeviceRefresh, Cmd_BurnStart, Cmd_BurnCancel,
    Cmd_TitleSet, Cmd_PrefsSet, ...
} CmdKind;
typedef struct Cmd { CmdKind kind; union { ... }; } Cmd;
void app_cmd_push(Cmd cmd);     // alloue sur arena_frame
```

Bénéfice immédiat : **le core est testable en ligne de commande**. Un `minidisk.exe --scan D:\Music
--plan out.mdp --dry-run` fait tourner la moitié du programme sans fenêtre, ce qui est le seul moyen
raisonnable de développer le pipeline audio et le protocole NetMD.

Bénéfice n°2 : le jour où on porte sur Linux, `ui/` compile sans modification, et seul
`platform/linux/` est à écrire.

**Test de conformité automatisable** : un `build.bat check` qui grep `#include <windows.h>`,
`gl`, `wgl`, `HWND` dans `src/core/` et `src/base/` et échoue s'il trouve quelque chose. 5 lignes de
batch, une règle qui ne dérive jamais.

### 3.12 Widgets de la v1

Liste minimale et suffisante (chacun ~40-150 lignes au-dessus du moteur) :

| Widget | Utilisation | Notes |
|--------|-------------|-------|
| `ui_button` / `ui_button_icon` | actions | variantes primary/secondary/danger |
| `ui_toggle` / `ui_checkbox` | options | |
| `ui_radio_group` | mode SP/LP2/LP4/mono | |
| `ui_label` / `ui_labelf` | textes | `labelf` formate dans `arena_frame` |
| `ui_text_field` | recherche, titrage | §3.9 |
| `ui_list` (virtualisée) | bibliothèque, plan, pistes du disque | §3.8 |
| `ui_table_header` | colonnes redimensionnables + tri | poignées draggables |
| `ui_scrollbar` | | |
| `ui_splitter` | séparateurs des 3 panneaux | drag horizontal/vertical, min sizes |
| `ui_progress` | transfert par piste, global | |
| `ui_capacity_gauge` | LA jauge (segments colorés par mode) | widget métier, dessiné à la main |
| `ui_menu` / `ui_menu_item` | contextuel + barre | popup interne |
| `ui_tooltip` | délai 500 ms | programme un réveil |
| `ui_toast` | notifications | |
| `ui_modal` | confirmations | |
| `ui_combo` | sélecteurs | popup + liste |

### 3.13 Récapitulatif §3 — RECOMMANDATIONS

1. **Architecture Fleury** : API immediate, cœur retained, arbre reconstruit chaque frame,
   `UI_Box` persistant retrouvé par clé hachée.
2. **Layout en 5 passes** avec `UI_Size {kind, value, strictness}` ; pas de flexbox, pas de grid.
3. **Piles de paramètres + `DeferLoop`** ; interdiction de `break`/`return` dans un bloc `UI_*`.
4. **Hit test sur le rect de la frame précédente**, `hot_key` calculé une fois par frame en ordre
   inverse.
5. **Animations exponentielles** avec critère d'arrêt et compteur `active_animations` (0 % CPU).
6. **Popups et modales internes**, jamais de fenêtre Win32 secondaire.
7. **Listes virtualisées à hauteur fixe**, bitset de sélection, filtrage/tri dans le core.
8. **Découplage vérifié par un script**, snapshot `AppView` + file de commandes.

---

## 4. Rendu GPU

### 4.1 Objectif

Dessiner toute l'UI — rectangles arrondis, bordures, ombres, texte, icônes, images — avec **un seul
programme GLSL, un seul VBO, et moins de 10 draw calls par frame**. Tout ce qui suit découle de cette
contrainte.

### 4.2 Options d'architecture de renderer

| Approche | Description | Verdict |
|----------|-------------|---------|
| A. Triangles pré-tessellés (ImGui) | tout est en triangles, les coins arrondis sont des arcs tessellés en CPU, l'AA est fait par des bandes de triangles alpha | ~30-60 vertices par bouton, CPU dépendant du radius, texte et formes mélangés. Éprouvé mais coûteux en vertices |
| B. **Quad + SDF en fragment shader** | 1 quad (4 vertices / 6 indices) par élément ; le fragment shader calcule la distance signée au rectangle arrondi et en déduit l'alpha | 4 vertices par bouton quel que soit le radius, AA analytique parfait, ombres gratuites, code CPU trivial |
| C. Une instance par élément (instancing) | `glDrawArraysInstanced` avec des attributs par instance | encore moins de bande passante, mais nécessite `ARB_instanced_arrays` (3.3 core, OK) et complique le mélange avec le texte |
| D. Vector renderer complet (paths, béziers) | comme NanoVG / Skia | massivement hors budget |

**RECOMMANDATION : option B**, avec le raffinement suivant : **un seul type de primitive, le
« rect », qui sait tout faire**. Un glyphe est un rect avec une région d'atlas et `corner_radius = 0`.
Une icône aussi. Une ombre est un rect avec un `softness` élevé et pas de texture. Une image est un
rect texturé sans SDF. **Une seule structure de vertex, un seul shader, un seul chemin de code.**

C'est l'approche du renderer de Ryan Fleury et de plusieurs moteurs maison modernes, et c'est celle
qui donne le meilleur ratio qualité/lignes de code.

### 4.3 Layout de vertex

On envoie **4 vertices par rect** (pas d'instancing : ça garde un chemin unique pour le texte et
évite une extension) avec un index buffer statique en pattern quad (0,1,2, 2,1,3) réutilisé
indéfiniment.

```c
// ui/r_types.h
typedef struct R_Vertex {          // 40 octets, aligné 4
    f32 dst_pos[2];      //  0 : position du coin, en pixels écran
    f32 dst_center[2];   //  8 : centre du rect (pour le SDF)
    f32 dst_half[2];     // 16 : demi-dimensions du rect
    f32 src_uv[2];       // 24 : coordonnées d'atlas normalisées [0,1]
    u8  color[4];        // 32 : RGBA8, premultiplied
    u16 corner_radius;   // 36 : en 1/16 px  (0..4095 px)
    u8  border_thickness;// 38 : en 1/2 px   (0 = rempli)
    u8  flags;           // 39 : R_VertFlag_*
} R_Vertex;
StaticAssert(sizeof(R_Vertex) == 40, r_vertex_size);
```

Justification de chaque champ :
- `dst_pos` : la position du **coin** du quad. Le quad est agrandi de `softness + 1 px` par rapport au
  rect logique pour laisser de la place à l'AA et à l'ombre.
- `dst_center` + `dst_half` : le rect **logique**, identique sur les 4 vertices, nécessaire au calcul
  de la SDF dans le fragment shader.
- `src_uv` : région dans l'atlas. Pour un rect non texturé, on pointe sur un texel blanc opaque
  réservé en (0,0) de l'atlas — ça évite un branchement dans le shader.
- `color` : RGBA8 **prémultiplié** (voir 4.5). 4 octets contre 16 en float : sur 100k vertices, ça fait
  1.2 MB économisés par frame de bande passante.
- `corner_radius` en u16 fixe 12.4 : radius jusqu'à 4095 px avec 1/16 px de précision, largement assez
  (nos radius sont de 2 à 6 px). Un seul radius pour les 4 coins — voir la note ci-dessous.
- `border_thickness` en u8 fixe 7.1 : 0 à 127.5 px par pas de 0.5.
- `flags` : bit 0 = texture est du R8 (glyphe/masque) vs RGBA8 (image) ; bit 1 = pas de SDF
  (rect brut, chemin rapide) ; bit 2 = ombre.

**Radius par coin.** Notre design (R-02b : radius 2-4 px, 6 max) n'a jamais besoin de 4 radius
différents… sauf pour les groupes de boutons segmentés (le premier a les coins gauches arrondis).
Solution : au lieu de 4 valeurs, on passe **un seul radius et un masque de coins** dans `flags`
(4 bits). Coût : 0 octet supplémentaire. Si un jour on a vraiment besoin de 4 radius, on découpe le
`u16` en quatre `u8` fixe 6.2 (0..63 px, 0.25 px de précision), ce qui reste dans les 40 octets.

Taille du buffer : une frame typique = ~1500 rects visibles (dont ~1200 glyphes) = 6000 vertices =
**240 KB**. Rien du tout. Le pire cas (fenêtre 4K remplie de texte dense) : ~6000 rects = 960 KB.
On dimensionne le VBO à **4 MB** et on flush si on déborde.

Déclaration des attributs :

```c
glBindVertexArray(r->vao);
glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
#define ATTR(i, n, type, norm, field) \
    glEnableVertexAttribArray(i); \
    glVertexAttribPointer(i, n, type, norm, sizeof(R_Vertex), (void*)OffsetOf(R_Vertex, field))
ATTR(0, 2, GL_FLOAT,         GL_FALSE, dst_pos);
ATTR(1, 2, GL_FLOAT,         GL_FALSE, dst_center);
ATTR(2, 2, GL_FLOAT,         GL_FALSE, dst_half);
ATTR(3, 2, GL_FLOAT,         GL_FALSE, src_uv);
ATTR(4, 4, GL_UNSIGNED_BYTE, GL_TRUE,  color);          // normalisé -> vec4 dans [0,1]
ATTR(5, 1, GL_UNSIGNED_SHORT,GL_FALSE, corner_radius);  // NON normalisé -> float brut
ATTR(6, 1, GL_UNSIGNED_BYTE, GL_FALSE, border_thickness);
glEnableVertexAttribArray(7);
glVertexAttribIPointer(7, 1, GL_UNSIGNED_BYTE, sizeof(R_Vertex), (void*)OffsetOf(R_Vertex, flags));
```

Note : l'attribut 7 utilise `glVertexAttribIPointer` (entier pur, `flat uint` en GLSL) pour pouvoir
faire des tests de bits fiables sans passer par des comparaisons de float.

### 4.4 Le shader

**Vertex shader** (GLSL 330 core) :

```glsl
#version 330 core
layout(location=0) in vec2  a_dst_pos;
layout(location=1) in vec2  a_dst_center;
layout(location=2) in vec2  a_dst_half;
layout(location=3) in vec2  a_src_uv;
layout(location=4) in vec4  a_color;
layout(location=5) in float a_corner_radius;   // en 1/16 px
layout(location=6) in float a_border;          // en 1/2 px
layout(location=7) in uint  a_flags;

uniform vec2 u_viewport;      // largeur, hauteur en pixels

out vec2       v_pos;         // position du fragment en px
out vec2       v_center;
out vec2       v_half;
out vec2       v_uv;
out vec4       v_color;
out float      v_radius;
out float      v_border;
flat out uint  v_flags;

void main() {
    // (0,0) en haut à gauche -> clip space
    vec2 ndc = vec2( 2.0 * a_dst_pos.x / u_viewport.x - 1.0,
                     1.0 - 2.0 * a_dst_pos.y / u_viewport.y );
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_pos    = a_dst_pos;
    v_center = a_dst_center;
    v_half   = a_dst_half;
    v_uv     = a_src_uv;
    v_color  = a_color;
    v_radius = a_corner_radius * (1.0/16.0);
    v_border = a_border * 0.5;
    v_flags  = a_flags;
}
```

**Fragment shader** :

```glsl
#version 330 core
in vec2       v_pos;
in vec2       v_center;
in vec2       v_half;
in vec2       v_uv;
in vec4       v_color;
in float      v_radius;
in float      v_border;
flat in uint  v_flags;

uniform sampler2D u_atlas;     // R8 (glyphes) OU RGBA8 (images) selon le batch
uniform float     u_text_gamma;

out vec4 frag;

#define FLAG_R8      1u   // la texture est mono-canal : c'est une couverture alpha
#define FLAG_NO_SDF  2u   // rect brut, pas de calcul de distance
#define FLAG_SHADOW  4u

// SDF d'un rectangle arrondi centré à l'origine.
// p       : point relatif au centre
// b       : demi-dimensions
// r       : rayon des coins
// Renvoie < 0 à l'intérieur, > 0 à l'extérieur, = 0 sur le bord.
float sdf_round_rect(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec4 c = v_color;

    // --- échantillonnage de l'atlas ---
    if ((v_flags & FLAG_R8) != 0u) {
        float coverage = texture(u_atlas, v_uv).r;
        coverage = pow(coverage, u_text_gamma);   // correction gamma du texte, cf. §2.4
        c *= coverage;                            // couleur prémultipliée -> multiplication directe
    } else if ((v_flags & FLAG_NO_SDF) == 0u || v_uv != vec2(0.0)) {
        c *= texture(u_atlas, v_uv);              // image RGBA prémultipliée
    }

    // --- forme ---
    if ((v_flags & FLAG_NO_SDF) == 0u) {
        float d = sdf_round_rect(v_pos - v_center, v_half, v_radius);

        // largeur d'un pixel dans l'espace de la distance : ici 1, mais fwidth
        // rend le shader robuste si on introduit un jour un zoom global.
        float aa = fwidth(d);

        // remplissage : alpha 1 à l'intérieur, 0 à l'extérieur, transition sur 1 px
        float alpha = 1.0 - smoothstep(-aa * 0.5, aa * 0.5, d);

        if (v_border > 0.0) {
            // anneau : on soustrait l'intérieur décalé de l'épaisseur
            float inner = 1.0 - smoothstep(-aa * 0.5, aa * 0.5, d + v_border);
            alpha -= inner;
        }
        c *= alpha;
    }

    frag = c;   // prémultiplié : le blending est (ONE, ONE_MINUS_SRC_ALPHA)
}
```

Points à souligner :

- **`smoothstep(-aa/2, aa/2, d)`** donne un anti-aliasing analytique exact sur 1 pixel, meilleur que
  du MSAA 8x et gratuit. C'est ce qui permet de se passer complètement de multisampling.
- **`fwidth(d)`** au lieu d'une constante `1.0` : coûte une instruction, rend le shader correct si on
  ajoute un jour un zoom global de l'UI ou un rendu dans une texture à échelle différente.
- **Le radius doit être clampé** côté CPU à `min(radius, min(half.x, half.y))` : un radius supérieur
  à la demi-dimension produit une forme incorrecte. Une ligne dans `r_rect()`.
- **Les branchements sont uniformes par batch** en pratique (un batch de glyphes, un batch de rects),
  donc pas de divergence de warp significative.
- **Ombres.** Une ombre douce est un rect avec un `softness` variable. La formule exacte pour une
  ombre gaussienne d'un rect arrondi existe (approximation de Evan Wallace), mais elle coûte cher. On
  fait plus simple : `alpha = 1 - smoothstep(-softness, softness, d)` avec `softness` = rayon de
  flou, passé dans le champ `border_thickness` réutilisé quand `FLAG_SHADOW` est posé. Qualité
  suffisante pour une ombre de popup à 8-16 px de flou.

**Coût de compilation du shader** : ~1-3 ms au démarrage. On ne cache pas de binaire de programme
(`glGetProgramBinary` n'est pas fiable entre versions de pilote).

**Taille du code GLSL** : ~2.5 KB de source embarquée en chaînes littérales. Négligeable.

### 4.5 Alpha prémultiplié

**Décision : tout est prémultiplié, du CPU au framebuffer.**

```c
glEnable(GL_BLEND);
glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,     // couleur
                    GL_ONE, GL_ONE_MINUS_SRC_ALPHA);    // alpha
```

Pourquoi c'est le bon choix :
1. **La multiplication de couverture est correcte.** Avec de l'alpha non prémultiplié, appliquer une
   couverture de glyphe `k` demande `a *= k` seulement ; avec prémultiplié, `rgba *= k`, ce qui est
   linéairement correct et compose proprement.
2. **Les ombres et les voiles se composent sans halo noir.** Le halo sombre classique autour d'un
   texte anti-aliasé vient d'un blending non prémultiplié sur des textures avec du noir dans les
   texels transparents.
3. **On peut composer plusieurs couches** (voile de modale par-dessus l'UI) sans erreur.

Conversion à la frontière : nos tokens sont en `#RRGGBB` + alpha séparé. Un helper unique :

```c
static inline u32 rgba8_premul(Vec4 c) {
    f32 a = Clamp01(c.a);
    return ((u32)(Clamp01(c.r) * a * 255.0f + 0.5f)      ) |
           ((u32)(Clamp01(c.g) * a * 255.0f + 0.5f) <<  8) |
           ((u32)(Clamp01(c.b) * a * 255.0f + 0.5f) << 16) |
           ((u32)(a * 255.0f + 0.5f)                << 24);
}
```

Les images (pochettes) décodées en RGBA non prémultiplié sont converties **une fois** à l'upload.

### 4.6 Batching, couches et clipping

**Ce qui casse un batch** (donc ce qui force un draw call) :
1. changement de texture d'atlas ;
2. changement de rectangle de scissor (clip) ;
3. dépassement du VBO.

**Clipping : scissor plutôt que stencil ou clip dans le shader.**

| Méthode | Coût | Limite |
|---------|------|--------|
| `glScissor` | un draw call par changement de rect | rectangles alignés aux axes uniquement |
| Stencil buffer | passe supplémentaire, buffer stencil à allouer | formes arbitraires |
| Clip rect dans le shader (4 floats de plus par vertex) | 0 draw call supplémentaire, +16 octets/vertex | rect aligné aux axes |

Nos clips sont **toujours** des rectangles alignés (panneaux, listes). Le troisième choix élimine les
draw calls mais alourdit le vertex de 40 % ; le premier est gratuit en mémoire et coûte quelques
draw calls.

**RECOMMANDATION : `glScissor`.** En pratique, une frame de notre app a 5 à 15 rectangles de clip
distincts (3 panneaux, 2-3 listes, quelques champs texte), donc 5-15 draw calls, et on est *très* loin
d'être limité par les draw calls (on parle de 15 appels, pas 15000). En bonus, on économise la bande
passante vertex. Si un jour on mesure un problème, le passage au clip-dans-le-shader est un
changement local.

**Couches (layers).** On maintient 3 listes de commandes remplies pendant le parcours de l'arbre :

| Couche | Contenu | Ordre |
|--------|---------|-------|
| 0 `Background` | fonds de panneaux, séparateurs | dessinée en premier |
| 1 `Content` | tout le contenu normal (listes, boutons, texte) | |
| 2 `Overlay` | popups, menus, modales, tooltips, fantôme de drag, toasts | dessinée en dernier |

À l'intérieur d'une couche, l'ordre est celui du parcours préfixe de l'arbre UI (parent avant
enfants), ce qui donne naturellement « le fond avant le contenu ». On ne trie jamais.

**Structure de commande** :
```c
typedef struct R_Batch {
    struct R_Batch *next;
    u32   vertex_first, vertex_count;   // dans le buffer de la frame
    u32   texture;                      // handle GL
    Rect  clip;
} R_Batch;
```
`r_rect(...)` écrit ses 4 vertices dans le buffer CPU et étend le batch courant, ou en ouvre un
nouveau si la texture ou le clip a changé. ~60 lignes.

### 4.7 Upload du VBO

Trois stratégies, par ordre de préférence :

**A. Persistent mapping (`ARB_buffer_storage`, GL 4.4 mais très largement dispo en 3.3).**
```c
glBufferStorage(GL_ARRAY_BUFFER, VBO_SIZE * 3,  NULL,
                GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
r->mapped = glMapBufferRange(GL_ARRAY_BUFFER, 0, VBO_SIZE * 3,
                GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
```
Triple buffering manuel : la frame N écrit dans la région `N % 3`, avec une `GLsync` par région
(`glFenceSync` / `glClientWaitSync`) pour ne jamais écrire dans une région encore lue par le GPU.
**On écrit les vertices directement dans la mémoire mappée**, zéro copie.

**B. `glBufferData(NULL)` puis `glBufferSubData` (orphaning).** Universel, une copie, parfaitement
suffisant pour 240 KB par frame (~0.05 ms de memcpy).

**C. `glMapBufferRange` avec `GL_MAP_INVALIDATE_BUFFER_BIT`.** Entre les deux.

**RECOMMANDATION : implémenter B d'abord (10 lignes), ajouter A derrière un `if (caps.buffer_storage)`
en phase 7** si on mesure quelque chose. À 240 KB par frame et un rendu à la demande, l'upload n'est
tout simplement pas un problème. C'est le genre d'optimisation qu'on documente pour ne pas y penser
deux fois.

Note : en persistent mapping, **écrire directement dans la mémoire mappée est écrit en write-combine**
— il faut écrire séquentiellement, ne jamais relire, et ne pas faire de `memcpy` partiel arrière.
Notre générateur de vertex est purement séquentiel, c'est compatible.

### 4.8 L'atlas

**Un seul atlas RGBA8 ? Ou deux (R8 pour le texte, RGBA8 pour les images) ?**

| Option | Mémoire | Draw calls | Complexité |
|--------|---------|-----------|-----------|
| Un atlas RGBA8 unique | 2048×2048×4 = 16 MB | 1 texture, batching maximal | glyphes stockés dans un seul canal = 4x de gâchis |
| Atlas R8 (texte+icônes) + atlas RGBA8 (images) | 1024×1024×1 = 1 MB + 1024×1024×4 = 4 MB | 2 textures, ~2 changements par frame | **retenu** |
| Texture array | idem | 1 bind | nécessite des tailles homogènes |

**RECOMMANDATION : deux atlas.**
- `atlas_mask` : `GL_R8`, 1024×1024 (extensible à 2048), contient **glyphes + icônes monochromes +
  un texel blanc en (0,0)**. Les icônes monochromes sont rastérisées depuis des SVG-simplifiés
  précalculés, ou plus simplement depuis une police d'icônes subsetée (Lucide/Codicon convertis en
  glyphes) — la même mécanique que le texte, zéro code supplémentaire.
- `atlas_color` : `GL_RGBA8`, 1024×1024, pochettes d'albums en vignettes 64×64 ou 128×128 (256 ou 64
  vignettes par atlas ; on alloue plusieurs pages si nécessaire, avec éviction LRU des pochettes
  hors écran).

**Allocation dans l'atlas : skyline bottom-left.** L'algorithme classique (celui de `stb_rect_pack`,
qu'on réimplémente en ~120 lignes plutôt que d'ajouter une dépendance) : on maintient une liste de
segments horizontaux (la « skyline ») et on place chaque rect à la position la plus basse possible.
Bon ratio de remplissage (>90 %) pour des glyphes, et incrémental (on peut ajouter des glyphes en
cours de route sans tout recalculer).

Éviction : on ne réalloue **jamais** les glyphes (ils sont peu nombreux : un jeu latin complet +
katakana + hiragana + les kanji effectivement rencontrés = quelques milliers, voir §5.5). Si l'atlas
mask sature, on **le reconstruit entièrement** (rare, ~5 ms) en repartant des glyphes réellement
utilisés dans les 1000 dernières frames.

**Padding** : 1 texel transparent autour de chaque entrée pour éviter le *bleeding* en filtrage
linéaire. Les glyphes sont échantillonnés en `GL_NEAREST` quand ils sont rendus à leur taille native
(le cas normal) — mais on utilise `GL_LINEAR` pour permettre le positionnement subpixel, donc le
padding est obligatoire.

### 4.9 Ce qu'on NE fait pas

- **Pas de depth buffer, pas de test de profondeur.** L'ordre de dessin fait tout.
- **Pas de MSAA.** L'AA analytique du SDF est meilleur et gratuit.
- **Pas de framebuffer intermédiaire / post-processing.** On dessine directement dans le back buffer.
- **Pas de mipmaps** sur l'atlas mask (les glyphes sont rendus à leur taille exacte). Sur l'atlas
  color, oui, pour les vignettes réduites.
- **Pas de tri de draw calls.** L'ordre est sémantique.
- **Pas d'uniform buffer objects.** 4 uniformes scalaires, `glUniform*` suffit.
- **Pas de compute shader, pas de geometry shader.**

### 4.10 API du renderer

```c
// ui/r_core.h — l'UI n'appelle QUE ça
void r_begin_frame(Vec2 viewport_px);
void r_end_frame(void);       // upload + draw calls + swap fait par l'appelant

void r_push_clip(Rect clip);
void r_pop_clip(void);
void r_push_layer(R_Layer layer);
void r_pop_layer(void);

typedef struct R_RectParams {
    Rect  dst;
    Vec4  color;              // ou 4 couleurs pour un dégradé, voir note
    f32   corner_radius;
    u8    corner_mask;        // bits TL, TR, BR, BL
    f32   border_thickness;
    Vec4  border_color;
    f32   softness;           // > 0 : ombre / halo
    R_TexRegion tex;          // {texture, uv0, uv1} ; {0} = pas de texture
} R_RectParams;
void r_rect(R_RectParams p);

// helpers construits par-dessus
void r_fill (Rect dst, Vec4 color, f32 radius);
void r_border(Rect dst, Vec4 color, f32 radius, f32 thickness);
void r_shadow(Rect dst, Vec4 color, f32 radius, f32 softness, Vec2 offset);
void r_text (Vec2 baseline, UI_Font font, f32 size_px, Vec4 color, String8 utf8);
void r_glyph(Rect dst, R_TexRegion region, Vec4 color);
void r_image(Rect dst, R_TexRegion region, f32 radius);
void r_line (Vec2 a, Vec2 b, f32 thickness, Vec4 color);   // rect tourné : cas rare, dégénéré
```

**Note sur les dégradés.** La jauge de capacité et les fonds de sélection peuvent vouloir un dégradé
vertical. Comme `color` est par vertex, un dégradé linéaire est **gratuit** : il suffit d'écrire des
couleurs différentes sur les 4 coins. On ajoute donc une variante `r_rect_gradient(Rect, Vec4 c[4], ...)`
qui n'ajoute aucune donnée ni aucun code de shader.

**Ordre des `r_rect` d'un widget.** Un bouton avec fond, bordure et texte = 3 primitives : le fond
(rect rempli SDF), la bordure (rect anneau SDF, même géométrie), le texte (N glyphes). On pourrait
fusionner fond+bordure en une passe (le shader sait faire les deux : `alpha_fill` et `alpha_border`
avec deux couleurs), ce qui économiserait un quad par widget. **Décision : on garde deux quads**, plus
simple, et 800 quads en plus par frame ne coûtent rien. Optimisation notée si jamais on la veut.

### 4.11 Budget et instrumentation

Compteurs affichés par l'overlay debug (F11) :

| Métrique | Cible |
|----------|-------|
| Vertices par frame | < 12 000 (3000 rects) |
| Draw calls | < 15 |
| Temps `app_build_frame` (CPU) | < 1.5 ms |
| Temps `r_flush` (CPU) | < 0.3 ms |
| Octets uploadés | < 500 KB |
| Frames par seconde pendant une animation | 60 (ou le refresh de l'écran) |
| Nœuds UI vivants | < 1200 |

Un `AssertAlways` en debug si le nombre de vertices dépasse la capacité du VBO (signe qu'on a oublié
de virtualiser une liste).

### 4.12 Récapitulatif §4 — RECOMMANDATIONS

1. **Une primitive unique** (« rect » avec SDF, texture optionnelle, bordure, ombre), **un shader**,
   **un VBO**, **un index buffer statique**.
2. **Vertex de 40 octets**, RGBA8 prémultiplié, radius en 12.4, flags entiers.
3. **SDF de rectangle arrondi + `smoothstep(fwidth)`** : anti-aliasing analytique, pas de MSAA.
4. **Alpha prémultiplié partout**, `glBlendFuncSeparate(ONE, ONE_MINUS_SRC_ALPHA, ...)`.
5. **Clipping par `glScissor`**, 3 couches (background / content / overlay), ordre = parcours d'arbre.
6. **`glBufferData` orphaning d'abord**, persistent mapping en option détectée.
7. **Deux atlas** : `R8` pour glyphes+icônes, `RGBA8` pour les pochettes ; packing skyline maison.
8. **Dégradés gratuits** par couleur de vertex.

---

## 5. Texte

C'est le sujet où une UI maison se trahit le plus vite. Un texte mal rendu ou mal mesuré ruine
l'impression de qualité même si tout le reste est parfait. Et notre cas est plus dur que la moyenne :
**les titres MiniDisc sont fréquemment en japonais** (kanji, hiragana, katakana, et le fameux
katakana demi-chasse imposé par le format TOC).

### 5.1 Les trois approches

**A. `stb_truetype` + police embarquée.**
On embarque un `.ttf` dans l'exe, on le rastérise nous-mêmes en niveaux de gris, on remplit l'atlas.
- **+** zéro dépendance OS, comportement **identique** sur les trois plateformes ;
- **+** ~5000 lignes de source, un seul header, domaine public ;
- **+** contrôle total : hinting off, positionnement subpixel, tailles arbitraires ;
- **−** poids : une police latine complète pèse 200-800 KB ; subsetée, 60-120 KB ;
- **−** **le japonais est impossible à embarquer** : Noto Sans JP pèse 4-8 MB. Rédhibitoire pour un
  exe < 1 MB ;
- **−** pas de hinting TrueType complet (stb ignore les instructions de hinting) : à 13 px, sans
  hinting, le rendu est plus « flou » que ClearType ;
- **−** pas de shaping (ligatures, kerning contextuel) — le kerning simple (table `kern` / `GPOS`
  format 1) est disponible via `stbtt_GetCodepointKernAdvance` mais limité.

**B. DirectWrite / GDI comme rastériseur, dans notre atlas.**
On demande à DirectWrite de nous rendre les glyphes en bitmaps 8 bits, qu'on colle dans notre atlas.
Notre renderer ne change pas d'un iota.
- **+** **0 octet de police dans l'exe** ;
- **+** fallback Unicode gratuit et complet : `IDWriteFontFallback` trouve automatiquement la police
  qui contient un kanji, un emoji, du cyrillique... C'est *le* point décisif ;
- **+** hinting et rendu identiques au reste du système (le texte ressemble à VS Code) ;
- **+** métriques exactes, kerning, shaping complet si on utilise `IDWriteTextLayout` ;
- **−** ~500-800 lignes de COM en C (vtables manuelles) ;
- **−** **spécifique Windows** : il faudra FreeType/CoreText pour Linux/macOS ;
- **−** dépend des polices installées ; sur une machine amputée, on doit avoir un plan B.

**C. Champs de distance (SDF / MSDF).**
On rastérise chaque glyphe une fois en champ de distance, on l'affiche à n'importe quelle taille.
- **+** une seule rastérisation par glyphe, tout DPI, mise à l'échelle continue gratuite ;
- **+** contours, ombres portées de texte, animations d'échelle gratuits ;
- **−** **mauvais à petite taille**, et notre taille de référence est 13 px. Les traits fins
  disparaissent, les jonctions bavent. C'est le problème connu du SDF pour l'UI ;
- **−** MSDF (multi-canal) corrige les coins mais pas la finesse à 13 px, et `msdfgen` est du C++
  lourd (ou `msdf-atlas-gen`), hors budget ;
- **−** 3-4x plus de mémoire d'atlas par glyphe.

### 5.2 RECOMMANDATION : approche hybride B (primaire) + A (secours), jamais C

**Décision :**

1. **Sur Windows, le rastériseur primaire est DirectWrite** (`IDWriteFontFace::CreateGlyphRunAnalysis`
   → `CreateAlphaTexture` en `DWRITE_TEXTURE_ALIASED_1x1`, c'est-à-dire un bitmap 8 bits de
   couverture). On récupère les glyphes de la police système, on les colle dans `atlas_mask`.
   Bénéfice principal : **le japonais fonctionne du premier coup, sans peser un octet**, et le texte
   ressemble au reste de Windows.
2. **`stb_truetype` reste dans le binaire** comme rastériseur de secours et comme **implémentation de
   référence pour Linux/macOS** (où il jouera le rôle de rastériseur au-dessus des polices trouvées
   par fontconfig / CoreText). Il rastérise aussi **notre police d'icônes embarquée** (subset
   Lucide/Codicon, ~40 glyphes, ~8 KB) pour laquelle on ne veut dépendre de rien.
3. **Aucun SDF pour le texte.** Le SDF reste réservé aux formes géométriques (§4).

L'interface abstraite qui rend ce choix indolore :

```c
// platform.h — le core/l'UI ne connaît que ça
typedef struct OsFontHandle { u64 v; } OsFontHandle;   // opaque

typedef struct OsGlyphMetrics {
    f32 advance;            // px
    i32 bearing_x, bearing_y;
    u32 w, h;               // dimensions du bitmap
} OsGlyphMetrics;

typedef struct OsFontMetrics {
    f32 ascent, descent, line_gap, x_height, cap_height, underline_pos, underline_thickness;
} OsFontMetrics;

OsFontHandle os_font_open(String8 family, f32 size_px, b32 bold, b32 italic);
void         os_font_close(OsFontHandle f);
OsFontMetrics os_font_metrics(OsFontHandle f);
u32          os_font_glyph_index(OsFontHandle f, u32 codepoint);   // 0 = absent
// rastérise le glyphe dans un buffer 8 bits fourni ; subpixel_x_offset dans [0,1)
b32 os_font_rasterize(OsFontHandle f, u32 glyph_index, f32 subpixel_x_offset,
                      u8 *out, u32 out_stride, OsGlyphMetrics *out_metrics);
// fallback : quelle police contient ce codepoint ?
OsFontHandle os_font_fallback(OsFontHandle base, u32 codepoint);
f32 os_font_kern(OsFontHandle f, u32 g0, u32 g1);
```

Deux implémentations : `win32_font_dwrite.c` et `generic_font_stbtt.c`. Le reste du programme ne sait
pas laquelle tourne.

### 5.3 DirectWrite en C : le squelette

DirectWrite en C pur, sans C++, est verbeux mais mécanique. Les appels passent par `lpVtbl` :

```c
// platform/win32/win32_font_dwrite.c
static IDWriteFactory *g_dw;

static b32 dwrite_init(void) {
    HMODULE m = LoadLibraryW(L"dwrite.dll");
    if (!m) return 0;
    HRESULT (WINAPI *pDWriteCreateFactory)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**) =
        (void *)GetProcAddress(m, "DWriteCreateFactory");
    if (!pDWriteCreateFactory) return 0;
    static const GUID IID_IDWriteFactory =
        {0xb859ee5a,0xd838,0x4b5b,{0xa2,0xe8,0x1a,0xdc,0x7d,0x93,0xdb,0x48}};
    HRESULT hr = pDWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory,
                                      (IUnknown **)&g_dw);
    return SUCCEEDED(hr);
}
```

Chaîne d'objets à traverser pour obtenir un `IDWriteFontFace` à partir d'un nom de famille :

```
IDWriteFactory::GetSystemFontCollection   -> IDWriteFontCollection
IDWriteFontCollection::FindFamilyName     -> index
IDWriteFontCollection::GetFontFamily      -> IDWriteFontFamily
IDWriteFontFamily::GetFirstMatchingFont(weight, stretch, style) -> IDWriteFont
IDWriteFont::CreateFontFace               -> IDWriteFontFace
```

Rastérisation d'un glyphe :

```c
DWRITE_GLYPH_RUN run = {
    .fontFace = face, .fontEmSize = size_px,
    .glyphCount = 1, .glyphIndices = &glyph_index,
    .glyphAdvances = &zero, .glyphOffsets = &offset,
    .isSideways = FALSE, .bidiLevel = 0,
};
IDWriteGlyphRunAnalysis *ana;
IDWriteFactory_CreateGlyphRunAnalysis(g_dw, &run, 1.0f /*pixelsPerDip*/, NULL /*transform*/,
        DWRITE_RENDERING_MODE_NATURAL,          // voir note ci-dessous
        DWRITE_MEASURING_MODE_NATURAL,
        baseline_x, baseline_y, &ana);
RECT bounds;
IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(ana, DWRITE_TEXTURE_ALIASED_1x1, &bounds);
u32 w = bounds.right - bounds.left, h = bounds.bottom - bounds.top;
IDWriteGlyphRunAnalysis_CreateAlphaTexture(ana, DWRITE_TEXTURE_ALIASED_1x1, &bounds,
                                           out_buffer, w * h);   // 1 octet par pixel
IDWriteGlyphRunAnalysis_Release(ana);
```

**Choix du mode de rendu.** Trois candidats :

| Mode | Résultat | Verdict |
|------|----------|---------|
| `DWRITE_TEXTURE_CLEARTYPE_3x1` + `RENDERING_MODE_NATURAL_SYMMETRIC` | subpixel RGB, 3 octets par pixel, le plus net sur LCD | frange colorée si le fond n'est pas opaque ; blending 3-canaux impossible avec notre shader mono-passe ; casse sur écran tourné |
| `DWRITE_TEXTURE_ALIASED_1x1` + `RENDERING_MODE_NATURAL` | niveaux de gris anti-aliasés, 1 octet par pixel | **retenu** |
| `RENDERING_MODE_OUTLINE` | contours vectoriels bruts | pour du très grand texte seulement |

**RECOMMANDATION : `ALIASED_1x1` + `NATURAL` (grayscale AA).** Le ClearType subpixel exigerait un
blending à 3 canaux (`glBlendFuncSeparate` avec `GL_SRC1_COLOR`, extension `ARB_blend_func_extended`)
et un second draw call par run de texte, pour un gain qui a largement disparu à l'ère des écrans
haute densité — et qui est *faux* dès qu'on anime l'opacité d'un panneau. macOS a supprimé le
subpixel AA en 2018 pour les mêmes raisons.

À 13 px sur un écran 96 DPI, le grayscale AA de DirectWrite en mode NATURAL reste très propre parce
qu'il applique quand même le hinting vertical.

**Fallback Unicode.** C'est le gain principal :

```c
IDWriteFactory2 *f2;                 // QueryInterface depuis IDWriteFactory
IDWriteFontFallback *fallback;
IDWriteFactory2_GetSystemFontFallback(f2, &fallback);
IDWriteFontFallback_MapCharacters(fallback, text_source, 0, length,
        base_collection, base_family, weight, style, stretch,
        &mapped_length, &mapped_font, &scale);
```

Un titre `"宇多田ヒカル - First Love"` produit deux runs : un run Yu Gothic UI pour le japonais, un run
Segoe UI pour le latin. C'est **exactement** ce dont on a besoin, et l'écrire soi-même impliquerait
d'embarquer une police CJK de 6 MB.

**Simplification acceptable pour la v1** : au lieu du `IDWriteFontFallback` complet (qui demande
d'implémenter `IDWriteTextAnalysisSource`, ~150 lignes de COM), on peut faire un fallback
**par codepoint** : pour chaque codepoint absent de la police primaire, on parcourt une liste courte
de familles candidates (`Yu Gothic UI`, `Meiryo UI`, `Malgun Gothic`, `Microsoft YaHei UI`,
`Segoe UI Symbol`, `Segoe UI Emoji`) et on prend la première qui a le glyphe
(`IDWriteFont::HasCharacter`). C'est moins « correct » linguistiquement (pas de désambiguïsation
Han unifié) mais c'est 40 lignes et ça couvre 100 % de nos besoins. **RECOMMANDATION : fallback par
codepoint en v1, `IDWriteFontFallback` seulement si un cas réel échoue.**

### 5.4 Cache de glyphes et cache de mesure

```c
typedef struct GlyphKey {
    u32 font_id;          // index dans la table des polices ouvertes (famille+taille+poids)
    u32 glyph_index;
    u8  subpixel;         // 0..3 : quart de pixel d'offset horizontal
} GlyphKey;   // 12 octets, hashable en un mot

typedef struct GlyphEntry {
    GlyphKey key;
    u16 atlas_x, atlas_y, w, h;
    i16 bearing_x, bearing_y;
    f32 advance;
    u64 last_used_frame;
} GlyphEntry;   // 32 octets
```

Table ouverte à sondage linéaire, 8192 entrées = 256 KB, chargée à ~50 % au pire. Un miss déclenche
une rastérisation (~20-60 µs) et un `glTexSubImage2D` de quelques centaines d'octets.

**Le vrai piège de performance n'est pas la rastérisation, c'est la mesure.** Une liste virtualisée
appelle `ui_text_width()` sur chaque cellule visible à chaque frame : 60 lignes × 5 colonnes = 300
mesures par frame, chacune parcourant une chaîne de 30 caractères = 9000 lookups de glyphe par frame.
C'est faisable (~200 µs) mais gaspilleur. Deux niveaux de cache :

1. **Cache de largeur de chaîne** : `hash(font_id, string) -> f32 width`, 4096 entrées, invalidé au
   changement de DPI/police. Un hit coûte un hash de 30 octets (~15 ns).
2. **Cache d'élision** : le résultat d'une troncature (`"Un titre très long..."` + sa largeur) est
   caché avec la largeur disponible dans la clé, parce que calculer une ellipse coûte une recherche
   binaire sur les préfixes.

**Invalidation** : un `u64 font_generation` incrémenté au `WM_DPICHANGED` ou au changement de thème
purge les deux caches et l'atlas.

### 5.5 Ce qu'on embarque quand même

Même avec DirectWrite, on embarque **deux petites choses** :

1. **Une police d'icônes subsetée** (~40 glyphes : play, pause, disque, USB, dossier, recherche,
   chevrons, croix, coche, alerte, corbeille, glisser...). On les convertit en un `.ttf` minimal
   (avec `fonttools pyftsubset` hors ligne, une fois) ou, plus simple et plus contrôlable, en **une
   table de rectangles arrondis et de triangles décrits en dur dans le code** pour les icônes
   géométriques, et un petit `.ttf` pour les autres. Poids : **~8 KB**.
2. **Un dernier recours de police texte** : si `dwrite.dll` est absent ou si aucune police système
   n'est trouvable (cas pathologique), on ne veut pas d'une fenêtre vide. Option retenue : **pas de
   police embarquée**, mais un chemin de secours qui charge `C:\Windows\Fonts\segoeui.ttf` (ou
   `tahoma.ttf`, ou `arial.ttf`) par `os_file_read` et le rastérise avec `stb_truetype`. Zéro octet
   embarqué, et si même ça échoue, on affiche un message d'erreur avec des rectangles.

Ce point mérite d'être souligné parce qu'il est contre-intuitif : **la meilleure façon de tenir sous
1 MB tout en supportant le japonais est de ne pas embarquer de police du tout.**

### 5.6 Positionnement subpixel et arrondis

Décisions :
- **Horizontalement : positionnement subpixel à 1/4 de pixel.** On rastérise 4 variantes de chaque
  glyphe (offsets 0, 0.25, 0.5, 0.75) et on choisit selon la partie fractionnaire de la position
  d'avance. Coût : 4x l'atlas pour les glyphes latins fréquents (~2000 glyphes × 4 × ~100 octets =
  800 KB dans un atlas de 1 MB : ça passe, mais on limite le subpixel aux glyphes de largeur < 20 px
  et on retombe sur 1 variante pour les kanji, qui sont larges et dont l'espacement est régulier).
  Gain : l'espacement des mots est régulier, le texte ne « danse » pas quand on scrolle.
- **Verticalement : jamais de subpixel.** La ligne de base est arrondie à l'entier
  (`baseline_y = round_f32(...)`). Un texte à baseline fractionnaire est flou, toujours.
- **Le rect d'un glyphe est aligné à l'entier** en Y, fractionnaire en X.

Alternative plus simple si le subpixel s'avère coûteux : **arrondir toutes les avances à l'entier**
(comme le fait GDI en mode `GGO_BITMAP`). Le texte est net mais l'espacement est irrégulier à 13 px.
**RECOMMANDATION : subpixel 1/4 en X**, et si l'atlas sature, dégrader en 1/2 puis en entier via une
constante `TEXT_SUBPIXEL_STEPS`.

### 5.7 UTF-8, UTF-16 et le japonais

**Règle absolue : tout est en UTF-8 en interne (`String8`), l'UTF-16 n'existe qu'à la frontière
Win32.** Deux fonctions dans `base/base_string.c` :

```c
typedef struct DecodedCodepoint { u32 codepoint; u32 advance; } DecodedCodepoint;
DecodedCodepoint utf8_decode(u8 *str, u64 max);        // advance = 1..4, 0xFFFD si invalide
u32              utf8_encode(u8 *out, u32 codepoint);  // renvoie 1..4
DecodedCodepoint utf16_decode(u16 *str, u64 max);
u32              utf16_encode(u16 *out, u32 codepoint);
String8  str8_from_str16(Arena *a, String16 s);
String16 str16_from_str8(Arena *a, String8 s);
```

Aucun appel à `MultiByteToWideChar` : 60 lignes à nous, testables, portables, et qui gèrent
explicitement les séquences invalides (les noms de fichiers Windows peuvent contenir des surrogates
non appariés — un vrai cas rencontré sur des bibliothèques musicales importées).

**Le katakana demi-chasse (halfwidth katakana, U+FF61..U+FF9F).** Le format NetMD stocke les titres
en ASCII étendu ou en **Shift-JIS demi-chasse**, avec un budget de caractères très serré (voir R-01).
Trois conséquences pour le texte :

1. **Conversion** : on doit convertir un titre en pleine chasse (`ハローワールド`) vers la demi-chasse
   (`ﾊﾛｰﾜｰﾙﾄﾞ`) pour économiser des octets. C'est une table de correspondance de ~100 entrées
   (pleine → demie, avec décomposition des dakuten/handakuten : `ガ` → `ｶ` + `ﾞ`, deux caractères).
   Cette table vit dans `core/netmd/netmd_charset.c`, ~1.5 KB.
2. **Affichage** : ces caractères demi-chasse **doivent s'afficher à demi-largeur**. DirectWrite le
   fait naturellement (Yu Gothic UI a les bonnes métriques). Notre moteur n'a rien à faire de spécial
   puisqu'il utilise les avances de la police. C'est un argument de plus pour DirectWrite : avec une
   police latine embarquée, ces caractères seraient des tofus.
3. **Aperçu** : l'UI doit montrer côte à côte « ce que vous voyez » et « ce qui sera écrit sur le
   disque », avec le compteur de caractères restants. C'est du métier (R-02), mais ça repose sur le
   fait que les deux formes s'affichent correctement.

**Largeur de caractère est-asiatique.** Pour le calcul de troncature, on ne suppose jamais qu'un
codepoint fait 1 « colonne » : on utilise toujours les avances réelles de la police. Aucune table
`wcwidth` n'est nécessaire.

### 5.8 Troncature, ellipse et chiffres tabulaires

**Ellipse.** Toujours `…` (U+2026), jamais trois points. Algorithme :
```c
// place autant de codepoints que possible, puis "…"
f32 ell_w = text_width(font, str8_lit("…"));
if (full_width <= avail) return full;
f32 budget = avail - ell_w;
// recherche linéaire depuis le début (les chaînes sont courtes) en accumulant les avances
```
Coût : O(n) sur une chaîne de 30 caractères avec cache de glyphe = ~1 µs. Mis en cache (§5.4).

Variante nécessaire : **l'ellipse au milieu** pour les chemins de fichiers
(`D:\Music\...\track.mp3`). Deux budgets, on remplit par les deux bouts.

**Chiffres tabulaires.** Les durées (`03:47`), les tailles et les compteurs doivent s'aligner
verticalement dans les colonnes. Deux moyens :
- **A. Activer la feature OpenType `tnum`** : `IDWriteTypography` + `DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES`.
  Nécessite de passer par `IDWriteTextLayout` (shaping complet), ce qu'on veut éviter.
- **B. Forcer l'avance des chiffres à la main** : on mesure l'avance de `'0'..'9'`, on prend le max,
  et on centre chaque chiffre dans cette avance. 15 lignes, marche avec n'importe quelle police, et
  c'est exactement ce que fait `tnum`.

**RECOMMANDATION : option B**, activée par un flag `TextFlag_TabularNumbers` sur le run de texte.
Segoe UI a de toute façon des chiffres presque tabulaires ; le forçage règle les cas résiduels.

**Alignement à droite des durées** : la colonne « durée » est alignée à droite, ce qui rend le
problème encore moins visible. On fait les deux.

### 5.9 Shaping : ce qu'on fait et ce qu'on ne fait pas

On **ne fait pas** de shaping complexe : pas de HarfBuzz (500 KB), pas de bidi, pas d'indien, pas
d'arabe. On fait :
- **kerning** par paires : via `os_font_kern` (DirectWrite : `IDWriteFontFace1::GetKerningPairAdjustments`,
  ou plus simplement rien du tout — Segoe UI a peu de kerning problématique à 13 px). **Décision : pas
  de kerning en v1.** On l'ajoute si un cas visible apparaît ;
- **combinaison de codepoints** : aucune. Les caractères combinants (accents décomposés) sont rares
  dans les tags musicaux mais existent (fichiers venant de macOS, qui utilise NFD !). Mitigation :
  **normaliser en NFC à l'import des tags**, via `NormalizeString(NormalizationC, ...)` de
  `normaliz.dll` (Win32) ou une table minimale pour le latin. Sinon `é` s'affiche `é` avec un accent
  mal placé. C'est un vrai bug utilisateur, à traiter en phase 2.

Si un jour du texte arabe ou hébreu apparaît dans une balise, il s'affichera dans le mauvais ordre.
C'est un défaut assumé et documenté.

### 5.10 Rendu d'un run de texte

```c
void r_text(Vec2 baseline, UI_Font font, f32 size_px, Vec4 color, String8 utf8) {
    f32 x = baseline.x;
    f32 y = round_f32(baseline.y);
    u64 off = 0;
    while (off < utf8.size) {
        DecodedCodepoint dc = utf8_decode(utf8.str + off, utf8.size - off);
        off += dc.advance;
        u32 gi = font_glyph_index(font, dc.codepoint);
        UI_Font f = font;
        if (gi == 0) { f = font_fallback(font, dc.codepoint); gi = font_glyph_index(f, dc.codepoint); }
        f32 frac = x - floor_f32(x);
        u8  sub  = (u8)(frac * TEXT_SUBPIXEL_STEPS);
        GlyphEntry *g = glyph_cache_get(f, gi, sub, size_px);   // rastérise au besoin
        if (g->w) {
            Rect dst = { floor_f32(x) + g->bearing_x, y - g->bearing_y,
                         floor_f32(x) + g->bearing_x + g->w, y - g->bearing_y + g->h };
            r_glyph(dst, glyph_region(g), color);
        }
        x += g->advance;
    }
}
```

Une seule boucle, un quad par glyphe, aucune allocation. Une ligne de 40 caractères = 40 quads =
160 vertices = 6.4 KB.

### 5.11 Hi-DPI

- Les tailles de police sont en **dp** dans le thème (13, 12, 11, 14, 16) et converties en px
  physiques : `size_px = round_f32(size_dp * dpi_scale)`. **Arrondi à l'entier** : rastériser à
  16.25 px n'a pas de sens et double le nombre d'entrées de cache.
- Le changement de DPI vide l'atlas et les caches (§5.4). Coût : ~10 ms, une fois, invisible.
- À 200 %, les glyphes font 26 px : l'atlas de 1024×1024 tient encore un jeu latin complet + les
  kanji réellement utilisés. Au-delà (300 %, écran 8K), on passe l'atlas à 2048×2048
  (`caps.max_texture_size` garantit ≥ 4096 en 3.3).

### 5.12 Récapitulatif §5 — RECOMMANDATIONS

1. **DirectWrite comme rastériseur primaire sur Windows**, `CreateGlyphRunAnalysis` +
   `CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1)` → notre atlas `R8`. **0 octet de police
   embarquée**, japonais gratuit.
2. **Grayscale AA, pas de ClearType subpixel.**
3. **Fallback par codepoint** sur une liste courte de familles ; `IDWriteFontFallback` seulement si
   nécessaire.
4. **`stb_truetype` conservé** comme secours (police système lue depuis le disque) et comme
   implémentation Linux/macOS future ; il rastérise aussi notre police d'icônes subsetée (~8 KB).
5. **Pas de SDF pour le texte.**
6. **UTF-8 partout**, encodeurs/décodeurs maison, UTF-16 seulement à la frontière Win32,
   normalisation NFC à l'import des tags.
7. **Subpixel 1/4 en X, jamais en Y** ; baseline arrondie.
8. **Chiffres tabulaires par forçage d'avance**, ellipse `…` avec cache, variante milieu pour les
   chemins.
9. **Table de conversion katakana pleine↔demi chasse** dans `core/netmd/netmd_charset.c`.

---

## 6. Audio : décodage, DSP, encodage

### 6.1 Ce que le pipeline doit faire

```
fichier source ─► décodage ─► resample 44 100 Hz ─► gain (R128) ─► trim/fade/gap
                                                        │
                                    ┌───────────────────┴──────────────────┐
                                    ▼                                      ▼
                        dither 16 bits + PCM big-endian            encodeur ATRAC3
                              (mode SP, 1.4 Mbit/s)              (LP2 132k / LP4 66k)
                                    │                                      │
                                    └──────────────► upload NetMD ◄────────┘
```

Contraintes :
- **44 100 Hz, 16 bits, stéréo** est le seul format que le MiniDisc accepte. Tout le reste doit y être
  converti.
- Le SP envoie du **PCM brut big-endian** (chiffré, voir R-01) : le device encode l'ATRAC1 lui-même.
- Le LP2/LP4 envoie de l'**ATRAC3 encodé côté hôte**.
- Débit visé : **≥ 50x temps réel** pour le décodage+DSP (une piste de 4 min traitée en < 5 s), afin
  que le goulot soit l'USB (~1.5x temps réel en SP) et pas le CPU.

### 6.2 Décodeurs : choix et poids

| Format | Bibliothèque | URL | Licence | Single-header | Taille compilée | Notes |
|--------|-------------|-----|---------|---------------|-----------------|-------|
| MP3 | **minimp3** | https://github.com/lieff/minimp3 | CC0 (domaine public) | oui (`minimp3.h` + `minimp3_ex.h`) | ~35 KB | pas de malloc en mode bas niveau ; SSE/NEON intégré |
| FLAC | **dr_flac** | https://github.com/mackron/dr_libs | domaine public **ou** MIT-0 au choix | oui (`dr_flac.h`) | ~45 KB | supporte Ogg-FLAC ; `DR_FLAC_NO_STDIO` |
| WAV / AIFF | **dr_wav** | https://github.com/mackron/dr_libs | domaine public ou MIT-0 | oui | ~25 KB | ou parseur maison ~8 KB (voir ci-dessous) |
| Ogg Vorbis | **stb_vorbis** | https://github.com/nothings/stb | domaine public ou MIT | oui (`stb_vorbis.c`) | ~60 KB | allocateur fourni possible (`stb_vorbis_alloc`) |
| AAC / M4A / ALAC / WMA | **Media Foundation** | OS | — | — | ~4 KB de glue | 0 octet de codec, mais COM |
| Opus | *reporté* | https://opus-codec.org | BSD-3 | non | ~200 KB | voir 6.3 |

**Configuration des headers** (à mettre en tête de `third_party_unity.c`) :

```c
#define MINIMP3_ONLY_MP3            /* pas de MP1/MP2 : -4 KB */
#define MINIMP3_NO_STDIO
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO            /* on fournit nos propres callbacks via os_file_* */
#define DR_FLAC_NO_OGG              /* si on décide de ne pas gérer Ogg-FLAC : -8 KB */
#define DR_FLAC_NO_CRC              /* NON : on garde le CRC, c'est notre détection de corruption */
#include "dr_flac.h"

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#define DR_WAV_NO_CONVERSION_API    /* on fait nos conversions nous-mêmes */
#include "dr_wav.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API  /* on lit tout en mémoire : -10 KB */
#define STB_VORBIS_MAX_CHANNELS 2   /* -2 KB de tables */
#include "stb_vorbis.c"
```

**Le problème du CRT dans les bibliothèques tierces.** Ces headers appellent `malloc`, `free`,
`memcpy`, `memset`, parfois `assert`, `pow`, `floor`. Solutions :

- `dr_flac` / `dr_wav` : macros `DRFLAC_MALLOC`, `DRFLAC_FREE`, `DRFLAC_REALLOC`,
  `DRFLAC_COPY_MEMORY`, `DRFLAC_ZERO_MEMORY`, `DRFLAC_ASSERT` → redirigées vers nos arènes. Propre et
  documenté par l'auteur.
- `minimp3` : en mode bas niveau (`mp3dec_decode_frame`), **aucune allocation**. On n'utilise pas
  `minimp3_ex.h` (qui lui alloue) ; on fait notre propre boucle de trames. C'est ~40 lignes et ça
  supprime le problème.
- `stb_vorbis` : accepte un `stb_vorbis_alloc { char *alloc_buffer; int alloc_buffer_length_in_bytes; }`
  → on lui donne un bloc pris sur une arène scratch (il faut ~200-400 KB pour un fichier stéréo).
  Reste `pow`, `floor`, `exp`, `log`, `sqrt`, `ldexp`, `abs` → nos versions f32/f64 maison, exposées
  via des macros `#define pow(a,b) our_pow(a,b)` avant l'include. **Attention** : les fonctions
  mathématiques de stb_vorbis servent au décodage du *floor 1* et de l'inverse-MDCT ; une
  approximation trop grossière dégraderait l'audio. On utilise donc des versions **double
  précision correctes** (polynômes minimax degré 8-10) pour ce sous-ensemble, pas les versions
  « assez bonnes pour l'UI ». ~2 KB de code supplémentaire.

**dr_wav ou parseur maison ?** Le WAV est trivial (RIFF, `fmt `, `data`), l'AIFF un peu moins
(big-endian, `COMM` avec un float 80 bits IEEE extended pour le sample rate). Écrire les deux : ~250
lignes, 8 KB compilés, contre 25 KB pour dr_wav. **RECOMMANDATION : dr_wav en v1** (il gère les
formats exotiques — WAV 24 bits, float, extensible, W64, RF64 — qu'on rencontrera dans une vraie
bibliothèque), **parseur maison seulement si la taille de l'exe devient un problème**. On écrit en
revanche **l'écriture de WAV nous-mêmes** (60 lignes) pour l'export/debug.

**Total décodeurs : ~165 KB.** C'est le plus gros poste de l'exe, et c'est justifié : ce sont les
formats que l'utilisateur possède.

### 6.3 Opus : décision

Arguments pour : format moderne, présent dans les bibliothèques récentes, et `.opus` apparaît dans les
téléchargements Bandcamp/YouTube.

Arguments contre :
- `libopus` + `libopusfile` + `libogg` : ~200-300 KB compilés, plusieurs dizaines de fichiers, build
  system autoconf, **pas single-header**, licence BSD-3 (OK) ;
- il n'existe pas de décodeur Opus single-header crédible ;
- Media Foundation **ne décode pas Opus** nativement (sauf via une extension Store) ;
- part de marché dans une bibliothèque musicale locale typique : très faible.

**RECOMMANDATION : pas d'Opus en v1.** On affiche « format non supporté » proprement avec une raison
explicite. Si le besoin se manifeste, deux voies : (a) intégrer libopus dans `third_party.obj`
(+250 KB, on serait toujours sous 1 MB), ou (b) déléguer à `ffmpeg.exe` s'il est dans le PATH. La
voie (a) est la bonne le jour venu ; on ne fait pas de dépendance à un exe externe.

Même raisonnement, même réponse, pour **APE (Monkey's Audio)**, **WavPack**, **Musepack**, **TAK**.

### 6.4 Media Foundation pour AAC / ALAC / WMA

Ces formats sont **fréquents** (iTunes, Apple Music téléchargé, vieux Windows Media). Les décoder
nous-mêmes est hors budget (un décodeur AAC-LC = 100+ KB et beaucoup de travail). Media Foundation
les décode tous, gratuitement, avec ~250 lignes de COM.

Interface la plus simple : **`IMFSourceReader`** en mode « décoder vers PCM ».

```c
// platform/win32/win32_audio_mf.c
static b32 mf_init(void) {
    // mfplat.dll : MFStartup, MFCreateAttributes, MFCreateMediaType
    // mfreadwrite.dll : MFCreateSourceReaderFromURL
    return load_mf_functions();
}

b32 os_audio_decode_open(String8 path, OsAudioDecoder *out) {
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    IMFSourceReader *reader = 0;
    String16 w = str16_from_str8(scratch, path);
    HRESULT hr = MFCreateSourceReaderFromURL(w.str, NULL, &reader);
    if (FAILED(hr)) return 0;

    // désactiver tous les flux sauf le premier flux audio
    IMFSourceReader_SetStreamSelection(reader, MF_SOURCE_READER_ALL_STREAMS, FALSE);
    IMFSourceReader_SetStreamSelection(reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    // imposer le format de sortie : PCM 16 bits (MF fera la conversion)
    IMFMediaType *type = 0;
    MFCreateMediaType(&type);
    IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE,    &MFAudioFormat_PCM);
    IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    hr = IMFSourceReader_SetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, type);
    IMFMediaType_Release(type);
    if (FAILED(hr)) { IMFSourceReader_Release(reader); return 0; }

    // relire le type effectif pour connaître le taux et le nombre de canaux
    IMFMediaType *actual = 0;
    IMFSourceReader_GetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual);
    IMFMediaType_GetUINT32(actual, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &out->sample_rate);
    IMFMediaType_GetUINT32(actual, &MF_MT_AUDIO_NUM_CHANNELS,       &out->channels);
    IMFMediaType_Release(actual);
    out->handle = reader;
    return 1;
}

u64 os_audio_decode_read(OsAudioDecoder *d, i16 *out, u64 frames_max) {
    IMFSample *sample = 0; DWORD flags = 0; LONGLONG ts = 0;
    HRESULT hr = IMFSourceReader_ReadSample((IMFSourceReader*)d->handle,
                     MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &flags, &ts, &sample);
    if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM) || !sample) return 0;
    IMFMediaBuffer *buf = 0;
    IMFSample_ConvertToContiguousBuffer(sample, &buf);
    BYTE *p = 0; DWORD cur = 0, max = 0;
    IMFMediaBuffer_Lock(buf, &p, &max, &cur);
    u64 frames = Min(cur / (2 * d->channels), frames_max);
    mem_copy(out, p, frames * 2 * d->channels);
    IMFMediaBuffer_Unlock(buf);
    IMFMediaBuffer_Release(buf);
    IMFSample_Release(sample);
    return frames;
}
```

Détails qui font perdre une journée si on ne les connaît pas :

- **`MFStartup(MF_VERSION, MFSTARTUP_LITE)`** : `MF_VERSION` est `0x00020070` sur les SDK récents.
  `MFSTARTUP_LITE` évite de démarrer le sous-système réseau.
- **COM apartment** : `CoInitializeEx(NULL, COINIT_MULTITHREADED)` sur le thread qui appelle. Si le
  décodage tourne sur un thread de job, chaque thread doit s'initialiser. On le fait une fois à la
  création du thread.
- **Les GUID doivent être définis en dur** (on ne lie pas `mfuuid.lib` en no-CRT) :
  ```c
  static const GUID MFMediaType_Audio =
      {0x73647561,0x0000,0x0010,{0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}};
  static const GUID MFAudioFormat_PCM =
      {0x00000001,0x0000,0x0010,{0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}};
  ```
  (`mfuuid.lib` est en fait une lib statique de constantes ; on **peut** la lier même en no-CRT, elle
  ne contient aucun code. **RECOMMANDATION : la lier**, c'est plus sûr que de recopier des GUID à la
  main. Coût : quelques centaines d'octets de données, `/OPT:REF` élaguant le reste.)
- **ALAC** est supporté depuis Windows 10. **WMA Lossless** aussi. **AAC-HE** oui. **Ne comptez pas
  sur MF pour du FLAC** (il le supporte depuis Win10 1803, mais dr_flac est plus rapide et
  déterministe : on garde dr_flac).
- **MF est lent à démarrer** (~30-80 ms pour le premier `MFCreateSourceReaderFromURL`, chargement de
  DLL et enregistrement de transformes). D'où le chargement paresseux : on n'appelle `MFStartup` que
  la première fois qu'on rencontre un `.m4a`/`.wma`/`.aac`.
- **Cas d'échec propre** : si `MFCreateSourceReaderFromURL` échoue (codec absent, DRM FairPlay), on
  remonte `CodecError_Unsupported` avec un message explicite (« fichier protégé par DRM » si
  `MF_E_DRM_UNSUPPORTED`), on n'ajoute pas la piste au plan, et on l'affiche grisée dans la
  bibliothèque avec l'icône d'alerte.

**Coût total : ~300 lignes, 0 octet de codec.** C'est le meilleur rapport de tout le projet.

**Portabilité** : `os_audio_decode_*` est dans `platform.h`. Sur Linux, l'implémentation renverra
« non supporté » pour AAC (ou utilisera GStreamer si un jour on veut). Le core ne change pas.

### 6.5 Lecture des tags

**Décision : on écrit notre propre lecteur de tags.** Raisons : TagLib est du C++ (et 1 MB à lui
seul) ; les alternatives C sont partielles ; et le parsing de tags est du travail simple, borné, où
on veut un contrôle total (encodages foireux, tags tronqués, images intégrées à ignorer sans les
charger).

| Format | Conteneur | Effort | Notes |
|--------|-----------|--------|-------|
| ID3v2.3 / v2.4 | MP3, parfois AIFF/WAV | ~350 lignes | frames `TIT2 TPE1 TPE2 TALB TRCK TPOS TYER TDRC TCON APIC`, désynchronisation, taille synchsafe, encodages 0=Latin1 1=UTF-16BOM 2=UTF-16BE 3=UTF-8 |
| ID3v1 | MP3 | ~40 lignes | 128 derniers octets, `TAG`, Latin-1, secours seulement |
| Vorbis comments | FLAC, Ogg | ~120 lignes | `ARTIST=`, `TITLE=`, `ALBUM=`, `TRACKNUMBER=`, `DATE=`, UTF-8 garanti, `METADATA_BLOCK_PICTURE` en base64 (à ignorer sauf pochette) |
| MP4 / iTunes atoms | M4A, ALAC | ~250 lignes | arbre d'atomes `moov.udta.meta.ilst`, clés `©nam ©ART ©alb trkn disk covr aART` |
| APEv2 | WavPack, parfois MP3 | ~100 lignes | optionnel |
| WMA (ASF) | WMA | ~150 lignes | optionnel — ou lire les tags via Media Foundation (`MFCreateSourceReader` + `IMFMetadata`) |

**Gains ReplayGain / R128 déjà présents** : `REPLAYGAIN_TRACK_GAIN` (Vorbis), `TXXX:replaygain_track_gain`
(ID3), `R128_TRACK_GAIN` (Opus). **Important** : s'ils existent, on les utilise directement et on
**évite un scan de loudness complet**, ce qui accélère massivement l'ajout au plan. Sinon on calcule
(§6.8).

**Pochettes** : on lit l'offset et la taille de l'image sans la charger. Le décodage JPEG/PNG est
différé (job de basse priorité) et n'est fait que pour les pochettes visibles. Décodeur : **on
n'embarque pas stb_image (~60 KB)** — on utilise **WIC** (Windows Imaging Component,
`IWICImagingFactory`, ~120 lignes de COM, 0 octet) qui décode JPEG/PNG/BMP/GIF/WebP/HEIF et sait
**redimensionner** (`IWICBitmapScaler`) directement en 64×64, ce qui évite de décoder une pochette
3000×3000 en pleine résolution. C'est le même argument que Media Foundation.

**Encodages douteux** : les tags ID3v2.3 en Latin-1 contiennent souvent en réalité du CP1252 ou du
Shift-JIS. Heuristique : essayer UTF-8 (validation stricte), sinon CP1252, et proposer une option
« forcer Shift-JIS » dans les préférences pour les bibliothèques japonaises. ~60 lignes + une table
CP1252→Unicode de 128 entrées.

### 6.6 Resampling

**Cas réels** : la grande majorité des fichiers sont déjà en 44 100 Hz (CD, MP3, AAC iTunes). Les
autres sont en 48 000 (téléchargements vidéo, Bandcamp), 88 200/96 000 (hi-res), 32 000 (rare),
22 050 (vieux).

**Chemin rapide obligatoire** : `if (src_rate == 44100) → pas de resampling du tout`. Ça couvre 80 %
des pistes et évite toute perte de qualité.

Pour le reste, options :

| Algorithme | Qualité | Coût | Code |
|-----------|---------|------|------|
| Linéaire | mauvais (aliasing audible) | trivial | 10 lignes |
| Cubique / Hermite | moyen | faible | 30 lignes |
| **Sinc polyphase fenêtré** | excellent (SNR > 120 dB avec 64 taps) | modéré | ~200 lignes + table |
| speexdsp resampler | très bon | dépendance externe (BSD, ~30 KB) | — |
| libsoxr | excellent | LGPL, gros | — |

**RECOMMANDATION : sinc polyphase fenêtré, écrit à la main.**

Principe : pour un ratio rationnel `L/M` (48000→44100 = 147/160), le filtre passe-bas idéal
`sinc(x)` fenêtré (Kaiser, β ≈ 8-10) est pré-calculé en **L phases** de **N taps**. Chaque échantillon
de sortie est un produit scalaire de N taps avec l'entrée, en choisissant la phase selon la position
fractionnaire.

```c
typedef struct Resampler {
    f32 *taps;          // L phases * N taps, contigus, alignés 32
    u32  phases;        // L
    u32  taps_per_phase;// N (64 en qualité haute, 32 en rapide)
    u32  in_rate, out_rate, gcd_num, gcd_den;
    f32 *history;       // N-1 échantillons du bloc précédent, par canal
} Resampler;

void resampler_init(Resampler *r, Arena *a, u32 in_rate, u32 out_rate, u32 taps);
u64  resampler_process(Resampler *r, const f32 *in, u64 in_frames, f32 *out, u64 out_cap, u32 channels);
```

Paramètres :
- **N = 64 taps**, fenêtre de **Kaiser β = 9.0**, fréquence de coupure à `0.92 * min(in,out)/2`.
  Bande d'arrêt à ~-100 dB, ondulation en bande passante < 0.01 dB, réponse plate jusqu'à ~20.2 kHz
  en 48→44.1. Largement au-dessus de ce qu'un MiniDisc peut restituer (l'ATRAC coupe vers 16-20 kHz).
- Pour les **ratios non rationnels simples** (pas notre cas, mais 44056 Hz existe sur de vieux
  fichiers), on interpole linéairement entre deux phases (un « polyphase à interpolation »),
  ce qui donne un SNR de ~90 dB avec 256 phases. Suffisant.
- Table : 147 phases × 64 taps × 4 octets = **37 KB**, allouée à l'init, calculée une fois
  (`sinc` + `bessel_i0` en double : ~50 lignes, exécutées une fois, coût ~1 ms).

**SIMD** : le produit scalaire de 64 taps est le cas idéal pour FMA/AVX2.
```c
// noyau AVX2 : 64 taps = 8 itérations de 8 floats
__m256 acc = _mm256_setzero_ps();
for (u32 k = 0; k < 64; k += 8)
    acc = _mm256_fmadd_ps(_mm256_loadu_ps(in + k), _mm256_load_ps(tap + k), acc);
```
Gain mesuré typique : 3-4x sur le SSE2 scalaire équivalent, ~8x sur du C naïf. Chemin SSE2 conservé
comme baseline.

**Débit attendu** : ~2 000 000 échantillons/s/cœur en scalaire, ~8 000 000 en AVX2, soit **180x temps
réel** pour du stéréo 44.1 kHz sur un seul cœur. Le resampling n'est jamais le goulot.

**Ordre des opérations** : on resample en `f32` **avant** le gain et le dither ; le dither est
toujours la dernière opération avant la quantification.

### 6.7 Format interne

**Décision : `f32` désentrelacé, par canal, par blocs.**

```c
typedef struct AudioBuffer {
    f32 *ch[2];        // pointeurs vers des blocs contigus, alignés 32 octets
    u64  frames;
    u32  channels;     // 1 ou 2
    u32  sample_rate;
} AudioBuffer;
```

Pourquoi désentrelacé : tous les traitements (resample, gain, filtres R128, MDCT ATRAC3) traitent un
canal à la fois ; l'entrelacement force des `shuffle` permanents. Les décodeurs sortent de
l'entrelacé, on désentrelace une fois à l'entrée (opération SIMD triviale).

Pourquoi `f32` et pas `f64` : 24 bits de mantisse = 144 dB de plage dynamique, très au-dessus des
96 dB du 16 bits cible. Les seules opérations où `f64` est nécessaire : l'accumulation du loudness
R128 (somme de millions de carrés) et les coefficients des filtres.

**Taille de bloc : 4096 frames** (~93 ms). Assez grand pour amortir les appels, assez petit pour tenir
en L2 (4096 × 2 canaux × 4 octets = 32 KB) et pour donner une progression fluide.

### 6.8 Normalisation EBU R128

Objectif : que les pistes d'un même disque aient le même volume perçu, sans clipping. C'est une
fonctionnalité produit majeure (un MiniDisc de compilation avec des volumes hétérogènes est
désagréable, et on ne peut pas le corriger après gravure).

**Norme** : EBU R128 / ITU-R BS.1770-4. Chaîne :
1. **Filtrage K** : deux biquads en cascade par canal — un *shelving* haute fréquence (+4 dB au-dessus
   de ~1.5 kHz) et un passe-haut RLB à ~38 Hz. Les coefficients pour 48 kHz sont donnés dans la
   norme ; pour 44.1 kHz **il faut les recalculer** (transformation bilinéaire à partir des pôles
   analogiques). C'est une source de bugs classique : on met les coefficients 44.1 kHz en dur, dérivés
   une fois, et on les vérifie contre `ffmpeg -af ebur128` sur un fichier de test.
2. **Puissance moyenne** par blocs de 400 ms avec 75 % de recouvrement (donc un bloc tous les 100 ms).
3. **Gating** : seuil absolu à -70 LUFS, puis seuil relatif à (loudness moyen des blocs retenus
   - 10 LU). Moyenne des blocs au-dessus des deux seuils → **LUFS intégré**.
4. **True peak** : suréchantillonnage ×4 (filtre polyphase 4 phases, 48 taps) et recherche du maximum
   absolu → dBTP. Nécessaire : une piste à 0 dBFS d'échantillon peut atteindre +1.5 dBTP après
   reconstruction, ce qui *clippe le DAC du walkman*.

```c
typedef struct R128State {
    f64 filt_state[2][4];      // 2 canaux, 2 biquads (2 états chacun)
    f64 *block_energies;       // énergie de chaque bloc de 400 ms
    u64  block_count, block_cap;
    f64  sum_400ms; u64 samples_in_block;
    f32  true_peak;
} R128State;

void r128_feed(R128State *s, const f32 *l, const f32 *r, u64 frames, u32 sample_rate);
f32  r128_integrated_lufs(R128State *s);   // en LUFS
f32  r128_true_peak_dbtp(R128State *s);
```

**Alternative** : `libebur128` (MIT, https://github.com/jiixyj/libebur128, ~2500 lignes C, ~20 KB
compilé, pas single-header mais 2 fichiers). C'est une implémentation de référence, testée.

**RECOMMANDATION : implémentation maison (~400 lignes)**, pour trois raisons : (a) on veut faire le
calcul **pendant** le décodage, dans le même passage, pas dans un second parcours ; (b) libebur128
alloue avec malloc et gère des cas qu'on n'a pas (7.1 canaux, LRA, historique glissant) ; (c) on veut
le true peak dans la même passe. **Mais on valide contre libebur128 et contre `ffmpeg -af ebur128`
sur une batterie de 20 fichiers**, avec une tolérance de ±0.1 LU. C'est un test automatisé du
`build.bat test`.

**Application du gain** :
```
gain_db = target_lufs - measured_lufs            // target par défaut : -14 LUFS
// éviter le clipping :
headroom_db = ceiling_dbtp - (measured_true_peak_dbtp + gain_db)   // ceiling = -1.0 dBTP
if (headroom_db < 0) gain_db += headroom_db;     // on réduit le gain plutôt que de clipper
```
Pas de limiteur, pas de compression : **on ne modifie jamais la dynamique**, on ne fait que déplacer
le niveau. Si l'utilisateur veut plus fort au prix du clipping, une option « limiteur doux » pourra
être ajoutée en phase 7, mais par défaut on est transparent.

Modes proposés à l'utilisateur : *aucun* / *par piste* (chaque piste à -14 LUFS) / *par album*
(gain unique par album, préserve la dynamique inter-pistes d'un album) / *manuel*.

### 6.9 Dither et quantification 16 bits

Quantifier du f32 en i16 sans dither produit de la distorsion de quantification corrélée au signal,
audible sur les fondus et les passages calmes.

| Option | Bruit ajouté | Qualité perçue | Code |
|--------|-------------|----------------|------|
| Troncature | — | mauvais sur les fondus | 1 ligne |
| Arrondi | — | légèrement mieux | 1 ligne |
| **TPDF (triangular PDF), 1 LSB** | +4.8 dB de plancher de bruit | correct, standard | 10 lignes |
| TPDF + noise shaping (courbe pondérée psychoacoustiquement) | bruit repoussé > 15 kHz | meilleur | ~40 lignes |

**RECOMMANDATION : TPDF par défaut, noise shaping en option.**

```c
static inline i16 quantize_dither(f32 x, u32 *rng) {
    // TPDF = somme de deux uniformes indépendants sur [-0.5, 0.5] LSB
    f32 r1 = (f32)(rand_u32(rng) >> 8) * (1.0f/16777216.0f) - 0.5f;
    f32 r2 = (f32)(rand_u32(rng) >> 8) * (1.0f/16777216.0f) - 0.5f;
    f32 v = x * 32767.0f + r1 + r2;
    i32 i = (i32)(v + (v >= 0 ? 0.5f : -0.5f));
    return (i16)Clamp(-32768, i, 32767);
}
```

Le générateur est un xorshift32 par canal, jamais partagé entre threads.

**Cas particulier : la source est déjà en 16 bits / 44.1 kHz.** Alors **aucun traitement** : pas de
resample, pas de dither, et si le gain R128 est désactivé, on copie les échantillons **bit à bit**.
C'est le mode « bit-perfect » et c'est le comportement par défaut pour un FLAC 16/44.1 sans
normalisation. Important à dire à l'utilisateur (un indicateur « bit-perfect » dans l'UI).

**Ordre canonique du pipeline** (à figer en ADR) :
```
décodage -> f32 -> [resample si != 44100] -> [gain R128] -> [trim silence] -> [fade in/out]
         -> [dither TPDF] -> i16 -> [byte-swap BE si SP] -> chiffrement DES -> USB
```

### 6.10 Job system

Besoins : scan de bibliothèque (I/O + parsing de tags, très parallèle), décodage/transcodage
(CPU-bound, très parallèle), décodage de pochettes (basse priorité), le tout sans jamais bloquer
l'UI.

```c
// base/base_jobs.h
typedef void JobFunc(void *data, u32 thread_index);
typedef struct JobQueue JobQueue;

JobQueue *jobs_create(Arena *arena, u32 thread_count);
void      jobs_push(JobQueue *q, JobFunc *f, void *data, JobPriority prio);
void      jobs_wait_all(JobQueue *q);         // participe au travail pendant l'attente
u32       jobs_thread_index(void);            // pour indexer des ressources par thread
```

Implémentation : **file circulaire à index atomiques + sémaphore**, le modèle Handmade Hero.

```c
struct JobQueue {
    JobEntry entries[4096];               // puissance de 2
    volatile u32 write_index, read_index; // InterlockedCompareExchange
    volatile u32 completion_goal, completion_count;
    HANDLE semaphore;
    u32 thread_count;
};
```

- `jobs_push` : `InterlockedIncrement` sur `write_index`, écriture de l'entrée, `ReleaseSemaphore`.
- Un thread worker : `WaitForSingleObject(semaphore)`, puis `InterlockedCompareExchange` sur
  `read_index` pour réclamer une entrée.
- **Pas de vol de travail** (work stealing) : inutile à cette échelle, et une file unique avec
  ~50 000 tâches (une par fichier) sature parfaitement 8 threads.

**Nombre de threads** : `min(GetActiveProcessorCount(ALL_GROUPS) - 1, 15)`. On laisse un cœur au
thread principal/UI. Sur le i7-8550U : 7 workers.

**Priorités** : deux files (haute = ce qui bloque un transfert en cours ; basse = pochettes, scan de
fond). Un worker vide la haute avant la basse.

**Communication des résultats vers l'UI** : chaque job écrit son résultat dans une structure
pré-allouée (indexée par l'ID de la tâche, jamais de malloc), puis pousse un `AppEvent` dans une file
MPSC lock-free, et fait `SetEvent(g_app_wakeup)` — ce handle est dans les `wait_handles` de la boucle
principale (§2.5), donc l'UI se réveille immédiatement. **Zéro polling.**

**Arènes par thread** : `tls_scratch[2]` par worker. Un job qui a besoin de produire un résultat
durable écrit dans une arène qui lui est assignée (`arena_library` protégée par un mutex uniquement
pour l'allocation, ou mieux : chaque worker alloue dans **sa** sous-arène et on concatène à la fin du
scan — pas de contention du tout).

**Annulation** : un `volatile u32 cancel_generation` par opération de haut niveau ; les jobs vérifient
`if (my_generation != *cancel_generation) return;` à chaque bloc. Simple, suffisant, pas de
`TerminateThread` (jamais).

### 6.11 Débits attendus (à mesurer en phase 5)

| Étape | Débit visé (1 cœur) | Notes |
|-------|--------------------|-------|
| Décodage MP3 (minimp3) | ~200x temps réel | ~35 Mo/s de PCM |
| Décodage FLAC (dr_flac) | ~150x | I/O-bound sur HDD |
| Décodage AAC (MF) | ~80x | dépend du décodeur système |
| Resample 48→44.1, 64 taps AVX2 | ~180x | |
| R128 (filtres + gating) | ~300x | |
| Dither + quantification | ~2000x | |
| **Pipeline complet SP** | **≥ 50x** sur 1 cœur, **≥ 300x** sur 7 | l'USB (~1.5x) est le goulot |
| Scan de tags | ~3000 fichiers/s sur SSD, 7 threads | I/O-bound |

**L'implication produit** : le transcodage de tout un disque de 80 min prend ~15 s sur un cœur, ~3 s
sur 7. On peut donc **tout transcoder avant de commencer à graver**, ce qui évite les famines de
buffer USB et permet d'afficher une estimation exacte. C'est la stratégie retenue.

### 6.12 Récapitulatif §6 — RECOMMANDATIONS

1. **minimp3 (CC0) + dr_flac + dr_wav (MIT-0) + stb_vorbis (domaine public)** en `third_party.obj`,
   avec redirection des macros d'allocation vers nos arènes ; ~165 KB.
2. **Media Foundation `IMFSourceReader`** pour AAC/ALAC/WMA : ~300 lignes, 0 octet de codec, chargée
   paresseusement. **WIC** pour les pochettes.
3. **Pas d'Opus, pas d'APE, pas de WavPack en v1** ; message « non supporté » explicite.
4. **Lecteur de tags maison** (ID3v2/v1, Vorbis, MP4 atoms), normalisation NFC, heuristique
   d'encodage, ReplayGain réutilisé s'il est présent.
5. **f32 désentrelacé, blocs de 4096 frames**, chemin bit-perfect si 16/44.1 sans traitement.
6. **Resampler sinc polyphase 64 taps Kaiser β=9**, table de 37 KB, noyau AVX2 + fallback SSE2,
   court-circuité si la source est déjà en 44.1 kHz.
7. **EBU R128 maison** (filtres K recalculés pour 44.1 kHz, gating, true peak ×4), validé contre
   `ffmpeg -af ebur128` à ±0.1 LU ; gain réduit pour respecter un plafond de -1 dBTP.
8. **Dither TPDF 1 LSB** en dernière étape, xorshift par canal.
9. **Job system file circulaire + sémaphore, N-1 threads**, réveil de l'UI par `SetEvent`, annulation
   par génération.

---

## 7. ATRAC3 (LP2/LP4)

Ce sujet est traité en profondeur par R-04. On ne tranche ici que ce qui concerne **la stack
technique** : quel code, quelle licence, quelle forme d'intégration, quel impact sur l'exe.

### 7.1 L'état de l'art

Il n'existe **qu'un seul encodeur ATRAC3 libre** : **atracdenc**, de Daniel Cherednik.

| | atracdenc |
|---|---|
| URL | https://github.com/dcherednik/atracdenc |
| Licence | **LGPL-2.1** |
| Langage | **C++17** |
| Build | CMake |
| Contenu | encodeurs ATRAC1, ATRAC3, ATRAC3plus (expérimental) |
| Dépendances | libsndfile *ou* Media Foundation pour l'I/O ; le cœur de l'encodeur est autonome |

FFmpeg **décode** l'ATRAC3 (`atrac3.c`, LGPL) mais **ne l'encode pas**. Il n'y a donc pas d'autre
implémentation de référence.

### 7.2 Options d'intégration

| Option | Description | Licence | Taille | Verdict |
|--------|-------------|---------|--------|---------|
| **A. Lier atracdenc statiquement** | on compile son C++ dans notre exe | **LGPL-2.1 : incompatible avec un exe statique propriétaire**, sauf à publier nos objets ou à passer le projet en LGPL/GPL | +150-250 KB, et il faut le CRT C++ (exceptions, `std::vector`) → **détruit le no-CRT** | **non** |
| **B. DLL séparée** | `atrac3.dll` LGPL à côté de l'exe | conforme LGPL (lien dynamique + possibilité de remplacer la DLL) | +200 KB de DLL | casse « un seul exe portable » |
| **C. Exe séparé embarqué** | `atracdenc.exe` extrait dans `%TEMP%` et lancé | conforme (processus séparé) | +300 KB dans l'exe, ou téléchargement | fonctionne, mais lancement de processus, I/O par fichiers temporaires, antivirus, et ça reste du LGPL à redistribuer |
| **D. Port en C, dérivé du source atracdenc** | traduction ligne à ligne | **œuvre dérivée → LGPL s'applique** | +60-90 KB | juridiquement identique à A |
| **E. Réimplémentation clean-room** en C à partir de la **description du format** (décodeur FFmpeg comme spécification du bitstream, littérature sur les codecs par transformée, brevets Sony expirés) | on écrit notre propre encodeur | notre licence | +60-90 KB | **retenu** |

**RECOMMANDATION : option E, avec l'option C comme filet de sécurité temporaire.**

Précisions importantes :
- L'option E n'est pas une astuce juridique : **encoder pour un format dont le décodeur est
  spécifié n'exige pas de copier l'encodeur**. Le bitstream ATRAC3 est entièrement documenté par le
  décodeur (structure des trames, tables de quantification, allocation de bits, bandes, MDCT à
  512/256 points, QMF à 3 bandes, joint stereo). La partie « créative » d'un encodeur est le
  **modèle psychoacoustique et l'allocation de bits**, qu'on écrit nous-mêmes.
- **atracdenc reste utilisable comme oracle de test** : on encode le même WAV avec atracdenc et avec
  notre encodeur, on décode les deux avec FFmpeg, et on compare le PSNR / la différence spectrale.
  Utiliser un programme comme référence de comparaison ne crée pas d'œuvre dérivée.
- **Les brevets ATRAC de Sony sont expirés** (dépôts 1991-1999, expiration 2011-2019). Aucun risque
  de ce côté en 2026.

### 7.3 Conséquences sur la stack

1. **Le module `core/atrac3/` est du C99 pur, sans dépendance**, comme le reste. Pas de C++, donc
   le no-CRT tient.
2. Il a besoin de : une **MDCT** (512 et 256 points) et un **QMF** (banc de filtres à 2 étages,
   3 bandes : 0-5.5k, 5.5-11k, 11-22k). La MDCT se ramène à une FFT complexe de N/4 points ; on écrit
   une FFT radix-4 en place (~250 lignes, SSE) qui **sert aussi au calcul du spectre pour la
   visualisation** si on en veut une.
3. **Tables générées hors ligne** : twiddles MDCT, fenêtres, tables de Huffman/quantification du
   format. Un petit outil `tools/gen_tables.c` (compilé avec le CRT) produit
   `src/core/atrac3/atrac3_tables.h`. Taille : ~25-40 KB de données constantes dans `.rdata`
   (fusionné dans `.text`). C'est le seul poste significatif de l'ATRAC3 dans l'exe.
4. **Débit visé** : ≥ 20x temps réel par cœur. LP2 = 132 kbit/s, LP4 = 66 kbit/s.
5. **Ordonnancement** : phase 5 = SP uniquement (aucun ATRAC3 nécessaire, le device encode) ; phase 6
   = ATRAC3. Le produit est utile dès la phase 5.

### 7.4 Filet de sécurité

Si la phase 6 dérape, le repli est l'**option C** : télécharger/embarquer `atracdenc.exe`, l'extraire
dans `%LOCALAPPDATA%\minidisk\`, l'invoquer avec `CreateProcessW` sur des WAV temporaires, respecter
la LGPL (mention + source disponible). Ce n'est pas satisfaisant (ça casse le « un seul exe »), mais
ça débloque la fonctionnalité LP2/LP4. Le point clé pour la stack : **l'interface du module doit être
identique dans les deux cas** :

```c
// core/atrac3/atrac3.h
typedef enum Atrac3Mode { Atrac3Mode_LP2, Atrac3Mode_LP4 } Atrac3Mode;
typedef struct Atrac3Encoder Atrac3Encoder;

Atrac3Encoder *atrac3_encoder_create(Arena *arena, Atrac3Mode mode);
// in  : 1024 frames stéréo f32 désentrelacé
// out : une trame ATRAC3 (192 octets en LP2, 96 en LP4 par canal-unit)
u64 atrac3_encode_frame(Atrac3Encoder *e, const f32 *l, const f32 *r, u8 *out, u64 out_cap);
void atrac3_encoder_destroy(Atrac3Encoder *e);
```

Une implémentation « proxy » qui appelle un exe externe respecte cette interface (en bufferisant), et
le reste du pipeline ne change pas d'une ligne.

---

## 8. USB : WinUSB sans libusb

### 8.1 Pourquoi pas libusb

libusb-1.0 (LGPL-2.1) est excellent, mais : ~180 KB de DLL ou de code statique, une abstraction dont
on n'utilise que 5 %, une boucle d'événements qui lui est propre, et une licence LGPL qui pose le
même problème qu'ATRAC3 pour un exe statique unique. Sur Windows, libusb est de toute façon **un
wrapper au-dessus de WinUSB**. On appelle WinUSB directement.

Ce qu'on a besoin de faire avec le device NetMD (voir R-01) :
- l'énumérer et l'identifier (VID 0x054C, PID 0x0084 pour notre Walkman) ;
- ouvrir une interface WinUSB ;
- des **control transfers** vendor-specific (le protocole NetMD passe presque entièrement par là) ;
- des **bulk transfers** pour l'upload des données audio ;
- réagir au branchement/débranchement.

C'est exactement ce que WinUSB expose, en 6 fonctions.

### 8.2 Énumération

Deux API : `SetupAPI` (classique) et `CfgMgr32` (`CM_Get_Device_Interface_List`, plus moderne,
moins de handles). Les deux sont dans le SDK, chargées dynamiquement.

```c
// platform/win32/win32_usb.c
static const GUID GUID_DEVINTERFACE_USB_DEVICE =
    {0xA5DCBF10,0x6530,0x11D2,{0x90,0x1F,0x00,0xC0,0x4F,0xB9,0x51,0xED}};

b32 os_usb_enumerate(Arena *arena, u16 vid, u16 pid, OsUsbDeviceInfo **out, u64 *out_count) {
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_DEVICE, NULL, NULL,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return 0;
    SP_DEVICE_INTERFACE_DATA ifd = { .cbSize = sizeof(ifd) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, NULL, &GUID_DEVINTERFACE_USB_DEVICE, i, &ifd); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, NULL, 0, &need, NULL);
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *det = arena_push(scratch, need, 8);
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);   // PAS `need` : piège classique
        SP_DEVINFO_DATA devinfo = { .cbSize = sizeof(devinfo) };
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, NULL, &devinfo)) continue;
        // det->DevicePath ressemble à :
        //   \\?\usb#vid_054c&pid_0084#5&1a2b3c4d&0&2#{a5dcbf10-...}
        // on peut filtrer VID/PID par simple recherche de sous-chaîne, sans ouvrir le device
        if (!path_matches_vid_pid(det->DevicePath, vid, pid)) continue;
        push_device(out, det->DevicePath, devinfo);
    }
    SetupDiDestroyDeviceInfoList(set);
    return 1;
}
```

Pièges :
- **`det->cbSize` doit valoir `sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)` (8 en x64), pas la taille
  allouée.** C'est l'erreur n°1 de SetupAPI.
- Le chemin de device (`DevicePath`) est **la seule chose à conserver** : il sert de clé stable pour
  le hotplug et pour `CreateFileW`.
- On lit aussi le *friendly name* (`SetupDiGetDeviceRegistryPropertyW` avec `SPDRP_FRIENDLYNAME` ou
  `SPDRP_DEVICEDESC`) pour l'afficher : « Sony Net MD Walkman ».
- Filtrer par VID/PID **sur la chaîne du chemin** évite d'ouvrir chaque device de la machine (ce qui
  serait lent et pourrait perturber d'autres périphériques).

### 8.3 Ouverture et transferts

```c
HANDLE h = CreateFileW(device_path, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
WINUSB_INTERFACE_HANDLE wh;
if (!WinUsb_Initialize(h, &wh)) { /* driver absent -> voir 8.5 */ }
```

**Si `CreateFileW` échoue avec `ERROR_FILE_NOT_FOUND` ou si `WinUsb_Initialize` échoue avec
`ERROR_NOT_SUPPORTED` (50), c'est que le driver WinUSB n'est pas installé sur le device.** C'est
exactement le problème P-001. On distingue ce cas de tous les autres et on affiche l'écran guidé.

**Control transfer** (le cœur du protocole NetMD) :

```c
typedef struct WINUSB_SETUP_PACKET {
    UCHAR  RequestType;   // bmRequestType
    UCHAR  Request;       // bRequest
    USHORT Value;         // wValue
    USHORT Index;         // wIndex
    USHORT Length;        // wLength
} WINUSB_SETUP_PACKET;

b32 os_usb_control(OsUsbDevice *d, u8 request_type, u8 request, u16 value, u16 index,
                   u8 *buf, u32 len, u32 *transferred, u32 timeout_ms) {
    WINUSB_SETUP_PACKET sp = { request_type, request, value, index, (USHORT)len };
    ULONG got = 0;
    // le timeout se règle par pipe policy ; pour le pipe de contrôle, c'est le pipe 0
    BOOL ok = WinUsb_ControlTransfer(d->wh, sp, buf, len, &got, NULL /* synchrone */);
    if (transferred) *transferred = got;
    return ok != 0;
}
```

Pour le NetMD, les échanges typiques (issus des transcriptions netmd-js / linux-minidisc) sont :
- `bmRequestType = 0x41` (host→device, **vendor**, interface), `bRequest = 0x80` : envoyer une
  commande ;
- `bmRequestType = 0xC1` (device→host, vendor, interface), `bRequest = 0x81` : lire la réponse ;
- `bmRequestType = 0xC1`, `bRequest = 0x01`, `wLength = 4` : *poll* (le device indique si une réponse
  est prête, et sa longueur).

Le protocole est donc : envoyer, puis **poller jusqu'à ce que la réponse soit prête** (avec un
back-off : 0 ms, 1 ms, 2 ms, 5 ms, 10 ms... jusqu'à un timeout de 5-10 s pour les opérations longues
comme l'écriture de la TOC). C'est cette boucle de poll qui impose un **thread device dédié** : elle
ne doit jamais s'exécuter sur le thread UI.

**Bulk transfer** (upload audio) :

```c
WinUsb_WritePipe(d->wh, d->bulk_out_pipe_id, data, len, &transferred, NULL);
WinUsb_ReadPipe (d->wh, d->bulk_in_pipe_id,  buf,  len, &transferred, NULL);
```

Les IDs de pipe se découvrent avec :
```c
USB_INTERFACE_DESCRIPTOR ifd;
WinUsb_QueryInterfaceSettings(wh, 0, &ifd);
for (UCHAR i = 0; i < ifd.bNumEndpoints; i++) {
    WINUSB_PIPE_INFORMATION pi;
    WinUsb_QueryPipe(wh, 0, i, &pi);
    // pi.PipeId : bit 7 = direction (1 = IN) ; pi.PipeType = UsbdPipeTypeBulk
}
```

**Pipe policies à régler impérativement** :

```c
ULONG timeout = 5000;                       // ms
WinUsb_SetPipePolicy(wh, pipe, PIPE_TRANSFER_TIMEOUT, sizeof(ULONG), &timeout);
UCHAR raw = TRUE;                           // pas de découpage par le driver
WinUsb_SetPipePolicy(wh, pipe, RAW_IO, sizeof(UCHAR), &raw);
UCHAR short_ok = TRUE;                      // accepter un transfert plus court que demandé
WinUsb_SetPipePolicy(wh, pipe, ALLOW_PARTIAL_READS, sizeof(UCHAR), &short_ok);
UCHAR auto_clear = TRUE;                    // clear stall automatique
WinUsb_SetPipePolicy(wh, pipe, AUTO_CLEAR_STALL, sizeof(UCHAR), &auto_clear);
```

**`RAW_IO = TRUE` est important pour le débit** : les transferts doivent alors être des multiples de
`MaximumPacketSize` et ≤ `MaximumTransferSize`, mais le driver ne fait plus de copie intermédiaire.
Taille de transfert recommandée : **64 KB** par `WinUsb_WritePipe`. Le NetMD en USB 1.1 full-speed
plafonne à ~1 Mbit/s utile, soit ~1.5x temps réel en SP ; l'objectif est de ne pas être *en dessous*.

**Synchrone ou asynchrone ?** WinUSB supporte l'`OVERLAPPED`. Comme on a un thread device dédié qui
n'a rien d'autre à faire, **on fait tout en synchrone** : le code du protocole se lit comme une
séquence linéaire, ce qui est un énorme gain de clarté pour un protocole aussi capricieux. L'UI ne
bloque pas puisqu'elle est sur un autre thread. Seule exception : l'annulation, qui se fait par
`WinUsb_AbortPipe` depuis le thread UI (c'est thread-safe et prévu pour ça).

### 8.4 Thread device et machine à états

```c
// core/netmd/netmd_session.h — indépendant de la plateforme
typedef struct UsbTransport {
    void *user;
    b32 (*control)(void *user, u8 rt, u8 req, u16 val, u16 idx, u8 *buf, u32 len, u32 *got, u32 timeout_ms);
    b32 (*bulk_out)(void *user, const u8 *buf, u32 len, u32 *sent, u32 timeout_ms);
    b32 (*bulk_in) (void *user, u8 *buf, u32 len, u32 *got, u32 timeout_ms);
    void (*sleep_ms)(void *user, u32 ms);
} UsbTransport;
```

**C'est l'abstraction clé du projet côté device.** Tout `core/netmd/` est écrit contre cette
interface. Trois implémentations :
1. `win32_usb_transport` (WinUSB) ;
2. `linux_usb_transport` (usbfs / libusb, phase 8) ;
3. **`replay_transport`** : rejoue une transcription capturée d'une vraie session (fichier texte
   `>` commande / `<` réponse en hexadécimal). C'est ce qui permet de **développer et tester le
   protocole sans appareil branché**, et de vérifier byte à byte qu'on produit les mêmes trames que
   netmd-js. Vu la fragilité du protocole (session sécurisée, EKB, DES), c'est non négociable.

Le thread device consomme une file de commandes (`NetMDCmd_Refresh`, `NetMDCmd_ReadDisc`,
`NetMDCmd_Upload`, `NetMDCmd_SetTitle`, `NetMDCmd_Erase`...) et publie des événements
(`NetMDEvent_DiscInfo`, `NetMDEvent_Progress`, `NetMDEvent_Error`) vers l'UI via la même file MPSC
+ `SetEvent` que les jobs (§6.10).

### 8.5 Le problème du driver (P-001)

Un NetMD Walkman se présente en classe `FF/00/00` (vendor-specific) : **Windows n'a pas de driver
pour lui**, d'où `ProblemCode 28`. Trois voies :

| Voie | Description | Verdict |
|------|-------------|---------|
| **A. Zadig manuel** | l'utilisateur télécharge Zadig et installe WinUSB sur le device | fonctionne, mais friction énorme et effrayant pour un utilisateur normal |
| **B. Installation programmée du driver** | on génère un `.inf` WinUSB et on appelle `DiInstallDriverW` / `UpdateDriverForPlugAndPlayDevicesW` | nécessite **l'élévation** et un **INF signé** (Windows 10/11 exige la signature du package driver par le WHQL/Attestation depuis Win10 1607) → non viable pour un projet perso |
| **C. libwdi (le moteur de Zadig)** | crée un driver auto-signé et l'installe | LGPL-3, ~200 KB, exige quand même l'élévation, et l'auto-signature ne passe plus le Secure Boot sur Win11 |
| **D. WCID / descripteurs Microsoft OS** | un device qui expose les descripteurs `MS OS 2.0` obtient WinUSB automatiquement | **impossible** : ça se joue côté firmware du Walkman, qui date de 2003 |

**RECOMMANDATION : A, avec l'expérience la plus soignée possible.**

Concrètement, l'app doit :
1. **Détecter précisément l'état** : device présent mais sans driver. Via `CM_Get_DevNode_Status` →
   `DN_HAS_PROBLEM` + `CM_PROB_FAILED_INSTALL (28)`, ou plus simplement : le device apparaît dans
   l'énumération `GUID_DEVINTERFACE_USB_DEVICE` mais `WinUsb_Initialize` échoue. On distingue aussi
   le cas « un autre driver est installé » (par exemple l'ancien driver Sony NetMD, ou libusb0/
   libusbK posés par un autre logiciel) — dans ce cas, WinUSB n'est pas le driver actif et il faut le
   dire clairement.
2. **Afficher un écran dédié** (pas une boîte de dialogue d'erreur) : « Votre Sony MZ-N510 est
   détecté, mais Windows n'a pas de pilote pour lui. Voici les 4 étapes pour l'installer », avec le
   VID/PID exact affiché, un bouton « copier les informations », un lien vers Zadig, et une
   **détection automatique** du succès (on re-teste toutes les 2 s, et l'écran disparaît tout seul dès
   que ça marche).
3. **Vérifier régulièrement** : le driver peut disparaître (mise à jour Windows, autre port USB — le
   driver est installé **par port** dans certains cas). L'écran doit pouvoir revenir sans redémarrage.

C'est un point produit majeur : c'est la première chose que l'utilisateur rencontrera, et c'est là
que Web MiniDisc Pro (qui utilise WebUSB et souffre du même problème) perd des utilisateurs.

### 8.6 Hotplug

```c
DEV_BROADCAST_DEVICEINTERFACE_W filter = {
    .dbcc_size = sizeof(filter),
    .dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE,
    .dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE,
};
HDEVNOTIFY notify = RegisterDeviceNotificationW(hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
// dans la WndProc :
case WM_DEVICECHANGE:
    if (wparam == DBT_DEVICEARRIVAL || wparam == DBT_DEVICEREMOVECOMPLETE) {
        DEV_BROADCAST_HDR *hdr = (DEV_BROADCAST_HDR *)lparam;
        if (hdr && hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
            os_event_push(wparam == DBT_DEVICEARRIVAL ? OsEvent_DeviceArrived
                                                      : OsEvent_DeviceRemoved);
    }
    return TRUE;
```

Alternative sans fenêtre (utile pour le mode ligne de commande) : `CM_Register_Notification` avec
`CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE`, qui appelle un callback sur un thread système. On implémente
les deux (la version fenêtre pour l'app, la version CM pour le mode CLI).

Piège : `DBT_DEVICEREMOVECOMPLETE` arrive **après** que les handles sont invalidés. Toute opération
USB en cours échouera avec `ERROR_DEVICE_NOT_CONNECTED` (1167) ou `ERROR_GEN_FAILURE` (31) — le
thread device doit traiter ces codes comme « débranchement » et non comme une erreur de protocole,
puis fermer proprement le handle et repasser à l'état « aucun appareil ».

**Débranchement pendant une gravure** : c'est le scénario catastrophe (TOC potentiellement non
écrite → disque corrompu). L'app doit (a) avertir explicitement avant de commencer, (b) afficher un
message très clair si ça arrive, (c) proposer la procédure de récupération (réinsérer le disque dans
le walkman qui reconstruira ou signalera la TOC). Rien de tout ça n'est technique, mais ça influence
la structure : **la machine à états du transfert doit avoir un état `Aborted_DeviceLost` distinct**.

### 8.7 Sécurité et robustesse

- **Toujours un timeout** sur chaque transfert. Un device NetMD peut se figer ; il ne doit jamais
  figer l'app.
- **`AssertAlways`** sur les invariants du protocole (longueurs de trames, codes de statut) même en
  release : mieux vaut un crash net et un rapport qu'un disque corrompu.
- **Journal de session** : toutes les trames envoyées/reçues sont journalisées (en anneau, 4 MB en
  mémoire, écrites sur disque à la demande ou en cas d'erreur). C'est ce qui permettra de déboguer
  les modèles qu'on ne possède pas, et de générer de nouvelles transcriptions pour
  `replay_transport`.
- **Une seule session à la fois** : le mutex d'instance unique (§2.10) garantit qu'un deuxième
  minidisk.exe ne viendra pas parler au device en même temps.

### 8.8 Récapitulatif §8 — RECOMMANDATIONS

1. **WinUSB direct**, pas de libusb : `winusb.dll` + `setupapi.dll` (ou `cfgmgr32.dll`) chargées
   dynamiquement, ~6 KB de glue.
2. **Énumération par `SetupDiGetClassDevs` + filtre VID/PID sur le `DevicePath`**, sans ouvrir les
   autres devices.
3. **Transferts synchrones sur un thread device dédié**, `RAW_IO`, timeouts, `AUTO_CLEAR_STALL`,
   blocs de 64 KB, annulation par `WinUsb_AbortPipe`.
4. **`UsbTransport` comme unique contrat** entre `core/netmd/` et la plateforme, avec une
   implémentation **de rejeu de transcriptions** pour les tests.
5. **Détection fine du driver manquant** (`WinUsb_Initialize` échoue / `CM_PROB_FAILED_INSTALL`) et
   écran guidé Zadig avec re-détection automatique. Pas d'installation de driver programmée.
6. **Hotplug par `RegisterDeviceNotificationW`** + variante `CM_Register_Notification` pour le CLI ;
   état `Aborted_DeviceLost` explicite.
7. **Journal de trames en anneau** pour le débogage à distance.

---

## 9. Persistance

### 9.1 Ce qu'il faut stocker

| Donnée | Volume | Fréquence d'écriture | Criticité |
|--------|--------|---------------------|-----------|
| Cache de bibliothèque (index de 100k pistes + tags) | 20-40 MB | à chaque scan | reconstructible → faible |
| Plans de gravure sauvegardés | quelques KB chacun | à chaque édition (autosave) | **haute** : travail de l'utilisateur |
| Préférences | < 4 KB | rare | moyenne |
| Historique des gravures | quelques centaines de KB | après chaque gravure | moyenne |
| Vignettes de pochettes | 5-50 MB | pendant le scan | reconstructible → faible |
| Journal de session USB | 1-4 MB | debug | nulle |

### 9.2 Options

| Option | Poids | Vitesse de chargement (100k pistes) | Complexité | Requêtes |
|--------|-------|-------------------------------------|-----------|----------|
| **SQLite** | ~700 KB compilé (amalgamation ~250k lignes) | ~1-3 s (parsing SQL, index B-tree) | faible à écrire, forte à optimiser | SQL complet, FTS5 |
| **Fichier binaire mappé** (`CreateFileMapping` + `MapViewOfFile`) | ~5 KB de code | **~5 ms** (le mapping est paresseux, l'OS pagine à la demande) | moyenne (versionnage, endianness) | ce qu'on code |
| Fichier binaire lu en bloc | ~3 KB | ~80 ms pour 30 MB sur SSD | faible | idem |
| JSON / TOML | +parseur 15 KB | ~2-5 s | faible | — |

**Le poids de SQLite (700 KB) mangerait à lui seul les deux tiers du budget de l'exe.** C'est
disqualifiant, et c'est aussi la mauvaise structure : notre requête dominante n'est pas « SELECT
WHERE » mais « filtrer 100k lignes par sous-chaîne pendant que l'utilisateur tape », ce qu'un
tableau SoA en mémoire fait mieux qu'un index B-tree.

**RECOMMANDATION : fichiers binaires maison, mappés en mémoire pour le cache de bibliothèque.**

### 9.3 Format de fichier

Conventions communes à tous nos fichiers :

```c
typedef struct FileHeader {
    u8  magic[8];        // "MDSKLIB\0", "MDSKPLAN", "MDSKPREF"
    u32 version;         // incrémentée à chaque changement de layout
    u32 flags;
    u64 payload_size;
    u64 payload_hash;    // xxhash64 du contenu, vérifié au chargement
    u64 created_unix;
    u8  reserved[16];
} FileHeader;   // 64 octets, aligné cache line
```

Règles :
1. **Little-endian assumé** (x86, ARM ; on note le champ `flags` bit 0 pour un futur big-endian).
2. **Aucun pointeur stocké** : tout est en **offsets relatifs au début du fichier** (`u32` ou `u64`).
   C'est ce qui rend le mapping direct possible : on mappe, on cast, on utilise.
3. **Toutes les structures sont `#pragma pack`-libres mais explicitement paddées** à des tailles
   multiples de 8 avec des `StaticAssert(sizeof(X) == N)`. Un changement de layout accidentel casse
   la compilation, pas les données de l'utilisateur.
4. **Versionnage strict** : si `version != CURRENT`, on **ne migre pas** le cache de bibliothèque —
   on le supprime et on rescanne (c'est reconstructible). Pour les **plans** (non reconstructibles),
   on écrit un vrai chemin de migration `v1 → v2 → v3`.
5. **Écriture atomique** : écrire dans `fichier.tmp`, `FlushFileBuffers`, puis
   `MoveFileExW(tmp, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`. Jamais d'écriture en
   place sur un fichier que l'utilisateur pourrait perdre.

### 9.4 Le cache de bibliothèque

Structure de données en mémoire (et sur disque, **identique**) :

```c
// core/library/library.h
typedef struct LibraryIndex {
    u64 track_count;
    // --- SoA : chaque tableau fait track_count éléments ---
    u32 *path_off;         // offset dans la string table
    u32 *title_off;
    u32 *artist_off;       // index d'artiste interné, pas une chaîne
    u32 *album_off;
    u32 *duration_ms;
    u32 *file_size_kb;
    u16 *track_no;
    u16 *disc_no;
    u16 *year;
    u8  *format;           // CodecFormat_MP3 ...
    u8  *channels;
    u32 *sample_rate;
    u32 *bitrate_kbps;
    i16 *replay_gain_q8;   // gain en 1/256 dB, INT16_MIN = absent
    u64 *mtime;            // FILETIME du fichier, pour la détection de modification
    u64 *content_hash;     // hash du chemin + mtime + taille : identité stable
    // --- tables partagées ---
    u8  *strings;          // string table UTF-8, chaînes préfixées par leur longueur (u16)
    u64  strings_size;
    u32 *artist_ids; u64 artist_count;   // artistes internés (dédupliqués)
    u32 *album_ids;  u64 album_count;
} LibraryIndex;
```

Pourquoi SoA : la requête dominante (« filtrer par sous-chaîne sur titre+artiste ») ne touche que
2 tableaux ; en AoS, chaque ligne tirerait une struct de 120 octets en cache pour n'en lire que 8.
Avec le SoA, on parcourt 400 KB au lieu de 12 MB. **Facteur 30 sur le temps de filtrage.**

**Recherche incrémentale.** Objectif R-02 : < 5 ms sur 100k pistes. Stratégie :
1. **Filtre grossier par bloom-like** : pour chaque piste, un `u32 char_mask` où le bit `i` est mis si
   la piste contient la lettre `i` (a-z, 0-9, autres → bits partagés). Une requête « bea » teste
   `(mask & needed) == needed` en une instruction et élimine 95 % des pistes. 400 KB de masques,
   parcourus en ~40 µs avec AVX2.
2. **Comparaison réelle** sur les survivantes (~5000 pistes) : recherche de sous-chaîne
   insensible à la casse et aux accents, sur des chaînes **pré-normalisées** stockées à côté
   (minuscules, accents retirés, ~30 % de mémoire en plus, mais la comparaison devient un `mem_find`
   trivial).
3. **Filtrage incrémental** : si la nouvelle requête est un préfixe étendu de la précédente
   (l'utilisateur a tapé une lettre de plus), on ne filtre que le résultat précédent. Le cas
   dominant, et il est en O(résultats précédents), soit quelques centaines de µs.

**Tri.** Radix sort LSD sur les clés (durée, année, taille) ou tri par index sur les chaînes
pré-normalisées. 100k éléments : ~4 ms pour un tri de chaînes, ~1 ms pour un radix. Le tri est
**mis en cache** : on garde les permutations pour les 3 derniers critères.

**Détection des changements au rescan** : on compare `(path, mtime, size)`. Un fichier dont ces trois
valeurs sont identiques n'est pas relu. Un rescan d'une bibliothèque inchangée de 100k fichiers
coûte alors ~2-4 s (dominé par `FindFirstFileW`/`FindNextFileW`), sans ouvrir un seul fichier.

**Mapping** :
```c
HANDLE f  = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, 0);
HANDLE m  = CreateFileMappingW(f, 0, PAGE_READONLY, 0, 0, 0);
void  *base = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
// puis : lib->path_off = (u32*)((u8*)base + header->path_off_offset); ...
```
Chargement = 3 appels système, ~200 µs, et les pages sont chargées à la demande par l'OS. Une
recherche qui ne touche que les titres ne paginera jamais les chemins. C'est objectivement meilleur
qu'une base de données pour ce cas d'usage.

**Attention** : les données mappées sont en lecture seule et peuvent **disparaître** si le fichier
est supprimé/tronqué par un autre processus → `EXCEPTION_IN_PAGE_ERROR`. On accepte le risque
(le fichier est dans `%LOCALAPPDATA%`, personne n'y touche) mais on le documente, et le
gestionnaire d'exceptions de haut niveau (`SetUnhandledExceptionFilter`) affiche « cache corrompu,
relancez pour rescanner » et supprime le fichier.

### 9.5 Plans, préférences, historique

Ce sont de petits fichiers (< 100 KB). Pas de mapping : `os_file_read` complet dans une arène,
parsing, et une structure en mémoire indépendante du fichier.

**Plans** (`.mdplan`) : la liste ordonnée des pistes avec leur mode, plus les métadonnées de titrage.
Les pistes sont référencées par leur `content_hash` **et** leur chemin : si la bibliothèque a été
rescannée et que les index ont bougé, on retrouve les pistes par le hash ; si le fichier a été
déplacé, on retrouve par le chemin ; si les deux échouent, la piste est marquée « introuvable » dans
l'UI plutôt que silencieusement perdue.

**Préférences** : struct plate versionnée. Pas de registre Windows (non portable, invisible,
difficile à sauvegarder). Emplacement :
`%LOCALAPPDATA%\minidisk\` via `SHGetKnownFolderPath(&FOLDERID_LocalAppData, ...)`.
**Mode portable** : si un fichier `minidisk.portable` existe à côté de l'exe, tout est stocké dans un
sous-dossier `data\` à côté de l'exe. C'est cohérent avec la promesse « un seul exe portable ».

**Autosave** : le plan courant est écrit toutes les 30 s **si modifié**, et à la fermeture.
Attention au 0 % CPU : l'autosave programme un réveil (`g_wake_at_us`), il ne polle pas.

### 9.6 Récapitulatif §9 — RECOMMANDATIONS

1. **Pas de SQLite** (700 KB, mauvais modèle pour notre requête dominante).
2. **Cache de bibliothèque = fichier binaire SoA mappé** (`CreateFileMapping`/`MapViewOfFile`),
   offsets relatifs, aucun pointeur, chargement en ~200 µs.
3. **Recherche : masque de caractères + chaînes pré-normalisées + filtrage incrémental** ; < 5 ms
   garanti sur 100k pistes.
4. **En-tête commun versionné avec hash** ; cache jeté si la version change, plans **migrés**.
5. **Écriture atomique** (`tmp` + `MoveFileEx`), autosave du plan toutes les 30 s.
6. **`%LOCALAPPDATA%\minidisk\`** par défaut, **mode portable** si `minidisk.portable` est présent.
   Aucun accès au registre.

---

## 10. Organisation du code, platform.h, build.bat

### 10.1 Arborescence

```
mini-disk/
├─ build.bat                     # LE build : debug | release | check | test | clean
├─ src/
│  ├─ main.c                     # unity build : la seule unité de compilation "à nous"
│  ├─ minidisk.rc                # icône
│  ├─ app.manifest               # DPI v2, UTF-8, longPath, asInvoker
│  │
│  ├─ base/                      # zéro dépendance, portable, testable
│  │   ├─ base_inc.h  base_inc.c
│  │   ├─ base_types.h           # u8..f64, b32, Vec2/3/4, Rect, macros (Assert, Min, Max, Clamp,
│  │   │                         #   ArrayCount, OffsetOf, StaticAssert, DeferLoop)
│  │   ├─ base_arena.h/.c        # Arena, ArenaTemp, scratch thread-local
│  │   ├─ base_string.h/.c       # String8, String16, UTF-8/16, format, split/join, compare
│  │   ├─ base_math.h/.c         # sqrt/floor/sin/cos/exp2/log2 SSE, matrices 2D
│  │   ├─ base_hash.h/.c         # xxhash64, table ouverte générique
│  │   ├─ base_simd.h            # détection CPU, helpers
│  │   ├─ base_sort.h/.c         # radix, tri par index
│  │   ├─ base_jobs.h/.c         # file circulaire + sémaphore (utilise platform.h)
│  │   ├─ base_log.h/.c          # log en anneau, niveaux, dump sur crash
│  │   └─ base_crt_stubs.c       # memset/memcpy/memmove/memcmp/_fltused (release only)
│  │
│  ├─ platform/
│  │   ├─ platform.h             # LE contrat (voir 10.2)
│  │   ├─ win32/
│  │   │   ├─ win32_inc.c
│  │   │   ├─ win32_entry.c      # entry_point, parsing de ligne de commande
│  │   │   ├─ win32_window.c     # classe, WndProc, boucle d'attente, DPI, dark mode
│  │   │   ├─ win32_gl.c         # WGL, loader GL
│  │   │   ├─ win32_file.c       # os_file_*, mapping, énumération de dossiers
│  │   │   ├─ win32_thread.c     # threads, mutex, sémaphores, atomiques, TLS
│  │   │   ├─ win32_time.c       # QPC, horloge murale
│  │   │   ├─ win32_usb.c        # SetupAPI + WinUSB
│  │   │   ├─ win32_font_dwrite.c
│  │   │   ├─ win32_audio_mf.c   # Media Foundation (AAC/ALAC/WMA)
│  │   │   ├─ win32_image_wic.c  # WIC (pochettes)
│  │   │   ├─ win32_dialog.c     # IFileDialog, clipboard
│  │   │   └─ win32_titlebar.c   # (phase 7)
│  │   ├─ linux/                 # (phase 8)
│  │   └─ generic/
│  │       └─ generic_font_stbtt.c
│  │
│  ├─ core/                      # AUCUN #include <windows.h>, AUCUN GL, AUCUNE UI
│  │   ├─ core_inc.c
│  │   ├─ library/  library.h/.c  library_scan.c  library_query.c  tags_id3.c
│  │   │                          tags_vorbis.c   tags_mp4.c       charset.c
│  │   ├─ plan/     plan.h/.c     capacity.c      title_budget.c
│  │   ├─ pipeline/ pipeline.h/.c decode.c  resample.c  r128.c  dither.c
│  │   ├─ codecs/   codec.h/.c    (façades au-dessus de third_party + platform.h)
│  │   ├─ atrac3/   atrac3.h/.c   mdct.c  qmf.c  bitalloc.c  atrac3_tables.h (généré)
│  │   ├─ netmd/    netmd.h/.c    netmd_proto.c  netmd_session.c  netmd_secure.c
│  │   │                          netmd_charset.c  netmd_replay.c
│  │   └─ persist/  persist.h/.c  lib_cache.c  plan_file.c  prefs.c
│  │
│  ├─ ui/
│  │   ├─ ui_inc.c
│  │   ├─ ui_core.h/.c           # UI_Box, clés, piles, layout, signaux, animations
│  │   ├─ ui_widgets.c           # boutons, listes, champs, jauge, menus
│  │   ├─ ui_theme.h/.c          # tokens de 02b-design-tokens
│  │   ├─ ui_text.h/.c           # cache de glyphes, mesure, ellipse, tabular
│  │   ├─ ui_panels.c            # les 3 panneaux de l'app
│  │   ├─ r_core.h/.c            # renderer : batches, atlas, vertices
│  │   ├─ r_gl.c                 # backend OpenGL (le seul fichier qui parle GL)
│  │   └─ r_shaders.h            # sources GLSL en chaînes littérales
│  │
│  ├─ app/
│  │   ├─ app.c                  # boucle, câblage, AppView, file de commandes
│  │   ├─ app_cli.c              # mode ligne de commande (--scan, --plan, --burn, --replay)
│  │   └─ app_events.c
│  │
│  └─ third_party/
│      ├─ third_party_unity.c    # SECOND .obj : les 4 codecs + stb_truetype
│      ├─ minimp3.h  dr_flac.h  dr_wav.h  stb_vorbis.c  stb_truetype.h
│      └─ LICENSES.md
│
├─ tools/
│  ├─ smoke/                     # l'exe minimal de référence (1 536 octets)
│  ├─ gen_tables.c               # génère atrac3_tables.h, tables de resampler, katakana
│  └─ replay/                    # transcriptions de sessions NetMD (.txt hex)
├─ tests/
│  ├─ test_main.c                # tests unitaires, buildé en exe séparé (avec CRT)
│  └─ data/                      # fichiers audio de référence, tags tordus
└─ docs/
```

**Règles de dépendance, vérifiables mécaniquement :**

| Répertoire | Peut inclure |
|-----------|--------------|
| `base/` | rien (sauf `platform.h` pour `base_jobs`) |
| `platform/win32/` | `base/`, `platform.h`, Windows |
| `core/` | `base/`, `platform.h`, `third_party/` |
| `ui/` | `base/`, `core/` (en lecture), `platform.h`, GL (dans `r_gl.c` uniquement) |
| `app/` | tout |

### 10.2 `platform.h` — l'esquisse complète

```c
// platform/platform.h — le SEUL contrat vers l'OS. Une implémentation par plateforme.
#ifndef PLATFORM_H
#define PLATFORM_H
#include "base/base_types.h"

// ─── temps ──────────────────────────────────────────────────────────────────
u64  os_now_us(void);                   // horloge monotone, microsecondes
u64  os_now_unix(void);                 // horloge murale, secondes
void os_sleep_ms(u32 ms);

// ─── mémoire ────────────────────────────────────────────────────────────────
void *os_reserve(u64 size);
b32   os_commit(void *ptr, u64 size);
void  os_decommit(void *ptr, u64 size);
void  os_release(void *ptr, u64 size);
u64   os_page_size(void);

// ─── fichiers ───────────────────────────────────────────────────────────────
typedef struct OsFile { u64 v; } OsFile;
typedef enum OsFileMode { OsFileMode_Read, OsFileMode_Write, OsFileMode_Append } OsFileMode;
typedef struct OsFileInfo {
    String8 name;      // nom seul
    u64  size;
    u64  mtime;        // unix us
    b32  is_directory;
} OsFileInfo;

OsFile  os_file_open(String8 path, OsFileMode mode);
void    os_file_close(OsFile f);
u64     os_file_read (OsFile f, u64 offset, void *buf, u64 size);
u64     os_file_write(OsFile f, u64 offset, const void *buf, u64 size);
b32     os_file_info(String8 path, OsFileInfo *out);
String8 os_file_read_all(Arena *arena, String8 path);
b32     os_file_write_all_atomic(String8 path, String8 data);   // tmp + rename
b32     os_file_delete(String8 path);
b32     os_dir_make(String8 path);

// itération de répertoire (non récursive ; la récursion est dans core/library)
typedef struct OsDirIter { u64 v[4]; } OsDirIter;
b32  os_dir_begin(OsDirIter *it, String8 path);
b32  os_dir_next (OsDirIter *it, OsFileInfo *out);   // remplit un buffer interne
void os_dir_end  (OsDirIter *it);

// mapping mémoire (lecture seule)
typedef struct OsMap { void *base; u64 size; u64 handles[2]; } OsMap;
b32  os_map_read(String8 path, OsMap *out);
void os_unmap(OsMap *m);

// chemins spéciaux
String8 os_path_app_data(Arena *a);      // %LOCALAPPDATA%\minidisk ou ~/.local/share/minidisk
String8 os_path_executable(Arena *a);
String8 os_path_music(Arena *a);         // dossier Musique de l'utilisateur

// ─── threads et synchronisation ─────────────────────────────────────────────
typedef struct OsThread    { u64 v; } OsThread;
typedef struct OsMutex     { u64 v[5]; } OsMutex;      // CRITICAL_SECTION = 40 octets x64
typedef struct OsSemaphore { u64 v; } OsSemaphore;
typedef struct OsEventFlag { u64 v; } OsEventFlag;     // auto-reset event
typedef void OsThreadFunc(void *user);

OsThread os_thread_start(OsThreadFunc *f, void *user, u64 stack_size, String8 name);
void     os_thread_join(OsThread t);
u32      os_logical_core_count(void);

void os_mutex_init(OsMutex *m);     void os_mutex_destroy(OsMutex *m);
void os_mutex_lock(OsMutex *m);     void os_mutex_unlock(OsMutex *m);
void os_sem_init(OsSemaphore *s, u32 initial);
void os_sem_post(OsSemaphore *s, u32 count);
void os_sem_wait(OsSemaphore *s);
void os_event_init(OsEventFlag *e); void os_event_signal(OsEventFlag *e);
b32  os_event_wait(OsEventFlag *e, u32 timeout_ms);

// atomiques (mappées sur Interlocked* / __atomic_*)
u32  os_atomic_inc_u32(volatile u32 *p);
u32  os_atomic_cas_u32(volatile u32 *p, u32 expected, u32 desired);   // renvoie l'ancienne valeur
u64  os_atomic_add_u64(volatile u64 *p, u64 delta);

// ─── fenêtre, événements, GL ────────────────────────────────────────────────
typedef struct OsWindow { u64 v; } OsWindow;
OsWindow os_window_open(String8 title, u32 w, u32 h);
void     os_window_close(OsWindow w);
Vec2     os_window_client_size(OsWindow w);       // pixels physiques
f32      os_window_dpi_scale(OsWindow w);
void     os_window_set_title(OsWindow w, String8 title);
void     os_window_set_cursor(OsWindow w, OsCursor c);
b32      os_gl_context_create(OsWindow w);        // 3.3 core
void     os_gl_swap(OsWindow w);
void     os_gl_set_vsync(b32 on);
void    *os_gl_proc(const char *name);

// attente unifiée : messages, handles, timeout. Cœur du rendu à la demande.
void     os_wait_for_events(u64 wake_at_us, OsEventFlag **extra, u32 extra_count);
OsEvent *os_events_pump(Arena *arena);            // liste chaînée des événements de la frame

// ─── dialogues ──────────────────────────────────────────────────────────────
b32     os_dialog_open_folder(Arena *a, String8 title, String8 *out);
b32     os_dialog_open_files (Arena *a, String8 title, String8 filters, String8 **out, u64 *n);
b32     os_dialog_save_file  (Arena *a, String8 title, String8 default_name, String8 *out);
b32     os_clipboard_set_text(String8 utf8);
String8 os_clipboard_get_text(Arena *a);
void    os_shell_open(String8 url_or_path);       // ouvrir Zadig / un dossier

// ─── polices ────────────────────────────────────────────────────────────────
// (voir §5.2 : os_font_open / os_font_metrics / os_font_glyph_index /
//              os_font_rasterize / os_font_fallback)

// ─── décodage audio délégué à l'OS (AAC/ALAC/WMA) ───────────────────────────
typedef struct OsAudioDecoder { u64 handle; u32 sample_rate, channels; u64 total_frames; } OsAudioDecoder;
b32 os_audio_decode_open (String8 path, OsAudioDecoder *out);
u64 os_audio_decode_read (OsAudioDecoder *d, i16 *out, u64 frames_max);
void os_audio_decode_close(OsAudioDecoder *d);

// ─── décodage d'image délégué à l'OS (pochettes) ────────────────────────────
b32 os_image_decode(Arena *a, String8 bytes, u32 max_w, u32 max_h,
                    u8 **out_rgba, u32 *out_w, u32 *out_h);

// ─── USB ────────────────────────────────────────────────────────────────────
typedef struct OsUsbDevice { u64 v[2]; } OsUsbDevice;
typedef struct OsUsbDeviceInfo {
    String8 path;             // identifiant stable
    String8 friendly_name;
    u16 vid, pid;
    b32 driver_ok;            // WinUsb_Initialize réussirait-il ?
} OsUsbDeviceInfo;

b32  os_usb_enumerate(Arena *a, u16 vid, u16 pid_any, OsUsbDeviceInfo **out, u64 *count);
b32  os_usb_open (String8 device_path, OsUsbDevice *out);
void os_usb_close(OsUsbDevice *d);
b32  os_usb_control (OsUsbDevice *d, u8 request_type, u8 request, u16 value, u16 index,
                     u8 *buf, u32 len, u32 *transferred, u32 timeout_ms);
b32  os_usb_bulk_out(OsUsbDevice *d, const u8 *buf, u32 len, u32 *sent, u32 timeout_ms);
b32  os_usb_bulk_in (OsUsbDevice *d, u8 *buf, u32 len, u32 *got,  u32 timeout_ms);
void os_usb_abort(OsUsbDevice *d);                 // annulation depuis un autre thread

// ─── divers ─────────────────────────────────────────────────────────────────
void os_debug_print(String8 s);        // OutputDebugStringA / stderr
void os_abort(String8 message);        // message + dump + ExitProcess(1)

#endif // PLATFORM_H
```

**~90 fonctions.** C'est le budget complet du portage : réécrire ces 90 fonctions pour Linux, et
`base/`, `core/`, `ui/` compilent sans une ligne modifiée. Estimation du portage Linux :
`os_file_*`/`os_thread_*`/`os_time_*` ~400 lignes (POSIX direct), fenêtre X11/Wayland + EGL ~800
lignes, USB via usbfs ~250 lignes, polices via fontconfig + stb_truetype ~200 lignes, décodage
AAC : non supporté (message propre). **Environ 2000 lignes.**

### 10.3 `build.bat`

```bat
@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

if not defined VSCMD_VER (
  call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
)

set MODE=%1
if "%MODE%"=="" set MODE=debug

if not exist build mkdir build

REM ─────────────────────────────────────────────────────────────────────────
REM  Flags communs
REM ─────────────────────────────────────────────────────────────────────────
set WARN=/W4 /wd4201 /wd4189 /wd4100 /wd4505
REM   4201 : struct/union anonyme (C11, on l'utilise)   4189 : variable locale non utilisée
REM   4100 : paramètre non utilisé                      4505 : fonction static non référencée
set COMMON=/nologo /std:c11 /Zi /Isrc /FC /diagnostics:column %WARN%
set DEFS_COMMON=/DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX

REM ─────────────────────────────────────────────────────────────────────────
REM  third_party.obj : recompilé seulement s'il manque ou si un header a changé
REM ─────────────────────────────────────────────────────────────────────────
set TP_OBJ=build\third_party.obj
if "%MODE%"=="clean" goto :clean
if not exist %TP_OBJ% (
  echo [third_party] compilation des codecs...
  cl %COMMON% %DEFS_COMMON% /O2 /GS- /Gs9999999 /Oi /wd4244 /wd4245 /wd4267 /wd4456 /wd4457 ^
     /c src\third_party\third_party_unity.c /Fo%TP_OBJ% /Fdbuild\third_party.pdb || exit /b 1
)

if "%MODE%"=="release" goto :release
if "%MODE%"=="check"   goto :check
if "%MODE%"=="test"    goto :test

REM ─────────────────────────────────────────────────────────────────────────
:debug
echo [debug] build...
rc /nologo /fobuild\minidisk.res src\minidisk.rc || exit /b 1
cl %COMMON% %DEFS_COMMON% /DBUILD_DEBUG=1 /Od /MTd /RTC1 /fsanitize=address ^
   src\main.c /Fobuild\ /Fdbuild\minidisk_debug.pdb ^
   /link /INCREMENTAL:NO /SUBSYSTEM:WINDOWS /ENTRY:entry_point ^
   /OUT:build\minidisk_debug.exe %TP_OBJ% build\minidisk.res ^
   kernel32.lib user32.lib gdi32.lib opengl32.lib mfuuid.lib || exit /b 1
goto :size

REM ─────────────────────────────────────────────────────────────────────────
:release
echo [release] build...
rc /nologo /fobuild\minidisk.res src\minidisk.rc || exit /b 1
cl %COMMON% %DEFS_COMMON% /DBUILD_DEBUG=0 /O2 /Oi /GS- /Gs9999999 /GR- /EHa- /GL ^
   src\main.c /Fobuild\ /Fdbuild\minidisk.pdb ^
   /link /LTCG /INCREMENTAL:NO ^
   /NODEFAULTLIB /ENTRY:entry_point /SUBSYSTEM:WINDOWS ^
   /OPT:REF /OPT:ICF /MERGE:.rdata=.text ^
   /STACK:0x100000,0x10000 ^
   /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA ^
   /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest ^
   /PDBALTPATH:%%_PDB%% ^
   /OUT:build\minidisk.exe %TP_OBJ% build\minidisk.res ^
   kernel32.lib user32.lib gdi32.lib opengl32.lib mfuuid.lib || exit /b 1
goto :size

REM ─────────────────────────────────────────────────────────────────────────
:check
echo [check] regles de dependance...
findstr /S /I /M /C:"windows.h" src\core\*.c src\core\*.h src\base\*.c src\base\*.h >nul 2>&1
if not errorlevel 1 ( echo ERREUR: windows.h inclus dans core/ ou base/ & exit /b 1 )
findstr /S /I /M /C:"gl.h" /C:"HWND" /C:"wgl" src\core\*.c src\core\*.h >nul 2>&1
if not errorlevel 1 ( echo ERREUR: OpenGL ou Win32 reference dans core/ & exit /b 1 )
findstr /S /I /M /C:"malloc(" /C:"free(" src\core\*.c src\ui\*.c src\base\*.c >nul 2>&1
if not errorlevel 1 ( echo ERREUR: malloc/free hors platform/ & exit /b 1 )
echo   OK
call "%~f0" release || exit /b 1
echo [check] symboles CRT residuels :
dumpbin /nologo /symbols build\minidisk.obj | findstr /C:"UNDEF" | findstr /V /C:"__imp_" 
goto :eof

REM ─────────────────────────────────────────────────────────────────────────
:test
cl %COMMON% %DEFS_COMMON% /DBUILD_TEST=1 /Od /MTd /Zi tests\test_main.c /Fobuild\ ^
   /link /OUT:build\tests.exe %TP_OBJ% kernel32.lib || exit /b 1
build\tests.exe || exit /b 1
goto :eof

REM ─────────────────────────────────────────────────────────────────────────
:size
for %%F in (build\minidisk*.exe) do echo   %%~nxF : %%~zF octets
goto :eof

:clean
if exist build rmdir /s /q build
goto :eof
```

Points notables :
- **`/GL` + `/LTCG` en release** : la génération de code inter-modules permet au compilateur
  d'optimiser à travers `main.obj` et `third_party.obj`. Coût : +3-5 s de link. Gain : ~5-8 % de
  taille et de vitesse. À activer une fois que le build est stable ; en cas de bug bizarre, le
  désactiver est le premier test.
- **`build.bat check`** applique mécaniquement les règles de la §3.11. Cinq `findstr`, une garantie
  qui ne dérive jamais.
- **`/PDBALTPATH:%%_PDB%%`** met seulement le nom du PDB (pas le chemin absolu de la machine de dev)
  dans l'exe : ça évite de divulguer `C:\Users\admin\...` et supprime quelques dizaines d'octets.
- **La taille de l'exe est affichée à chaque build.** C'est le KPI.
- **`mfuuid.lib`** est une lib de constantes GUID pure : elle se lie sans CRT.

### 10.4 Tests

Trois niveaux, tous exécutables sans appareil :

1. **Unitaires** (`build.bat test`) : arènes, `String8`, UTF-8/16, hash, tri, resampler (réponse
   impulsionnelle vs référence), R128 (contre `ffmpeg -af ebur128`), parseurs de tags (batterie de
   fichiers tordus), format de fichier (round-trip), calcul de capacité MD.
2. **Rejeu du protocole** : `minidisk.exe --replay tools\replay\mz-n510-upload.txt` rejoue une
   session capturée et vérifie qu'on émet exactement les mêmes trames. C'est le filet de sécurité du
   module le plus risqué.
3. **Golden files audio** : encoder un ensemble de fichiers de référence et comparer les sorties bit
   à bit (SP) ou par PSNR (ATRAC3) à des références archivées.

### 10.5 Récapitulatif §10 — RECOMMANDATIONS

1. **Arborescence `base / platform / core / ui / app / third_party`**, un module = un fichier,
   `static` par défaut, préfixe de module obligatoire.
2. **`platform.h` : ~90 fonctions**, tout ce qui touche l'OS. Portage Linux estimé à ~2000 lignes.
3. **Deux `.obj`** (`main.obj` unity + `third_party.obj` en cache), build complet < 5 s.
4. **`build.bat debug | release | check | test | clean`**, aucun outil hors MSVC + SDK.
5. **`check` applique les règles de dépendance mécaniquement** et affiche les symboles CRT résiduels.
6. **Taille de l'exe affichée à chaque build**, consignée dans STATUS.md à chaque jalon.

---

## 11. Risques classés et liste d'URLs

### 11.1 Risques, du plus grave au plus bénin

Cotation : **P** = probabilité (1-5), **I** = impact (1-5), **S** = P × I.

---

#### R1 — Encodeur ATRAC3 : le seul encodeur libre est LGPL et en C++ · P=4 I=5 **S=20**

**Le risque.** atracdenc est LGPL-2.1 et C++17 : ni sa liaison statique ni sa traduction en C ne sont
compatibles avec « un exe unique, sans CRT, sous notre licence ». Une réimplémentation clean-room
d'un encodeur par transformée avec allocation de bits psychoacoustique est un travail de plusieurs
semaines dont la **qualité audio est difficile à garantir**.

**Signaux d'alerte.** Après 2 semaines sur la phase 6, le PSNR de round-trip reste sous 45 dB, ou des
artefacts (pré-écho, sifflements) sont audibles sur les transitoires.

**Mitigations, dans l'ordre :**
1. **Livrer le SP en phase 5** : le produit est complet et utile sans ATRAC3 (80 min par disque).
2. **Oracle de test dès le premier jour** de la phase 6 : atracdenc comme référence de comparaison
   (usage légitime), FFmpeg comme décodeur de vérification, batterie de 20 extraits (transitoires,
   applaudissements, voix seule, silence, sinus purs).
3. **Filet** : implémentation « proxy » derrière `atrac3.h` qui invoque `atracdenc.exe` (§7.4). On
   perd le « un seul exe » pour le LP uniquement, en attendant.
4. **Cadrage temporel strict** : si la phase 6 dépasse 4 semaines, on bascule sur le filet et on
   revient plus tard.

---

#### R2 — Protocole NetMD sécurisé (EKB, DES, session) · P=4 I=5 **S=20**

**Le risque.** L'upload passe par une session chiffrée mal documentée, dont le comportement varie
selon le modèle. Une erreur peut **corrompre la TOC** d'un disque (perte de données de
l'utilisateur), et le débogage sans matériel de rechange est douloureux.

**Mitigations :**
1. **`UsbTransport` + `replay_transport`** (§8.4) : on développe et on teste contre des
   transcriptions de vraies sessions de netmd-js, en comparant **byte à byte**. C'est la mitigation
   principale et elle est structurelle.
2. **Journal de trames en anneau** systématique, exportable en un clic.
3. **`AssertAlways` en release** sur tous les invariants de trame.
4. **Ordre de développement** : lecture seule (disque, pistes, titres) en phase 3, écriture en phase
   5. On ne touche à l'écriture qu'une fois la lecture parfaitement fiable.
5. **Disque de test dédié**, jamais un disque contenant quelque chose.

---

#### R3 — Le driver WinUSB n'est pas installable proprement (Zadig obligatoire) · P=5 I=3 **S=15**

**Le risque.** Certain (P=5) : Windows 11 n'a pas de driver pour un device classe FF/00/00, et
l'installation programmée exige un package signé WHQL, hors de portée. Chaque utilisateur devra
passer par Zadig. Impact : abandon de certains utilisateurs, support, mauvaise première impression.

**Mitigations :** écran guidé de haute qualité avec détection automatique du succès (§8.5) ; détection
fine du cas « autre driver installé » ; documentation avec captures d'écran ; ne jamais laisser
l'utilisateur face à un message technique brut. **Ce n'est pas un risque technique, c'est un risque
produit** — et le traiter bien est un avantage concurrentiel sur Web MiniDisc Pro.

---

#### R4 — Le no-CRT devient un fardeau · P=3 I=3 **S=9**

**Le risque.** Chaque nouvelle bibliothèque tierce, chaque nouvelle API Windows peut réintroduire une
dépendance CRT (`__chkstk` sur un gros buffer, `_ftol`, une fonction mathématique, l'initialisation
statique d'une lib). Le symptôme est un `LNK2019` obscur en fin de build release, souvent longtemps
après le commit fautif.

**Mitigations :** `build.bat check` en release **à chaque commit** (le problème est détecté dans la
minute) ; la liste des symboles autorisés est documentée (§1.3) ; le CRT reste disponible en debug,
donc on ne perd jamais ASan ni le confort de développement ; et si un jour un cas résiste vraiment
(par exemple une dépendance C++ incontournable), **la porte de sortie est de lier `libvcruntime.lib`
seul**, ce qui coûterait ~15-30 KB. Le no-CRT est une discipline, pas un dogme : on le documente
comme tel.

---

#### R5 — Texte : DirectWrite lie la qualité du rendu à Windows · P=3 I=3 **S=9**

**Le risque.** On mise sur DirectWrite pour éviter d'embarquer une police CJK. Conséquences : (a) le
portage Linux/macOS devra refaire ce travail (FreeType/fontconfig, CoreText), (b) le rendu ne sera
pas identique entre plateformes, (c) une machine sans les polices japonaises installées affichera des
tofus.

**Mitigations :** l'abstraction `os_font_*` (§5.2) contient le risque à ~600 lignes par plateforme ;
`stb_truetype` est déjà dans le binaire comme rastériseur générique, donc le portage consiste
surtout à trouver les fichiers de police (fontconfig) ; le cas « pas de police japonaise » est réel
mais rare sur Windows 11 (Yu Gothic UI est installé par défaut) et se détecte (on affiche un
avertissement plutôt que des tofus silencieux).

---

#### R6 — Media Foundation en C : verbosité et cas d'échec · P=3 I=2 **S=6**

**Le risque.** ~300 lignes de COM manuel, des GUID, des `HRESULT`, une initialisation par thread, et
des échecs variés (DRM, codec absent, fichier corrompu). Bugs de comptage de références → fuites.

**Mitigations :** chemin d'erreur unique et testé (« format non supporté », piste grisée) ; MF chargé
paresseusement, donc un échec de MF n'empêche jamais l'app de démarrer ; test sur une batterie de
fichiers M4A/WMA/ALAC réels incluant un fichier DRM ; ASan en debug pour les fuites.

---

#### R7 — Ambition : trop de modules pour un projet solo · P=4 I=3 **S=12**

**Le risque.** Renderer, moteur UI, moteur texte, 5 décodeurs, DSP, encodeur ATRAC3, protocole USB,
persistance : chacun est un projet en soi. Le risque n'est pas l'échec technique mais
**l'enlisement** — 6 mois de fondations sans jamais graver un disque.

**Mitigations :** les 8 phases sont **strictement séquentielles avec une démo fonctionnelle en
sortie** ; le KPI de taille d'exe et les critères de sortie de chaque phase sont dans STATUS.md ;
le mode CLI (`--scan`, `--replay`, `--burn`) permet de valider le core sans attendre l'UI ; et l'ordre
des phases est choisi pour que **la phase 5 produise déjà un logiciel utile** (graver en SP couvre
80 % de l'usage réel).

Corollaire de méthode : **écrire la fonctionnalité la plus risquée le plus tôt possible à l'intérieur
de chaque phase**, jamais l'inverse.

---

#### R8 — Le rendu à la demande fuit (l'app consomme au repos) · P=3 I=2 **S=6**

**Le risque.** Un `WM_PAINT` non validé, une animation qui ne converge jamais, un job qui signale en
boucle, un tooltip qui reprogramme un réveil indéfiniment : l'app tourne à 3-8 % de CPU en
permanence et personne ne s'en rend compte pendant des semaines.

**Mitigations :** overlay debug (F11) affichant `active_animations`, FPS réel et nombre de réveils par
seconde ; un **test automatisé** qui lance l'app, attend 10 s sans input, et vérifie via
`GetProcessTimes` que le temps CPU consommé sur les 5 dernières secondes est < 20 ms ; et le critère
« 0 % CPU au repos » figure explicitement dans les critères de sortie de la phase 1.

---

#### R9 — `/ALIGN:16` et l'antivirus · P=2 I=4 **S=8**

**Le risque.** Un exe avec `SectionAlignment=16`, non signé, qui parle en USB à un périphérique et
écrit dans `%LOCALAPPDATA%` coche plusieurs cases heuristiques. Un faux positif Defender/SmartScreen
tuerait la distribution.

**Mitigations :** **abandonner `/ALIGN:16`** (§1.4), garder `/DYNAMICBASE /NXCOMPAT`, embarquer un
manifest et une icône (un exe sans ressources est plus suspect), et à terme envisager une signature
de code (un certificat OV coûte ~200 €/an ; SmartScreen demande de la réputation avant d'être
silencieux). À défaut, documenter la procédure « Informations complémentaires → Exécuter quand même ».

---

#### R10 — Perte de contexte OpenGL · P=2 I=3 **S=6**

**Le risque.** Bascule Optimus, mise à jour de pilote, sortie de veille, RDP : le contexte GL devient
invalide et l'app affiche une fenêtre noire jusqu'au redémarrage.

**Mitigations :** **toutes les ressources GPU sont reconstructibles à partir de la RAM** (l'atlas vit
dans `arena_permanent`, jamais lu depuis le GPU) — c'est la décision architecturale qui rend le
remède trivial ; détection sur `WM_DISPLAYCHANGE` et `PBT_APMRESUMEAUTOMATIC` ; recréation complète
du contexte + ré-upload en une fonction (~40 lignes) testable manuellement (débrancher/rebrancher un
écran, mettre en veille).

---

#### R11 — Unity build : explosion de la compilation ou collisions · P=2 I=2 **S=4**

**Le risque.** Un header tiers qui définit `min`/`max`/`near`/`far`, un `static` oublié, un temps de
compilation qui dérape à 30 s.

**Mitigations :** `third_party.obj` séparé (§1.8) ; `/DNOMINMAX` ; mesure du temps de build affichée ;
si le temps dépasse 8 s, on découpe en 3-4 unités (le passage unity → semi-unity est mécanique).

---

#### R12 — Tags mal encodés / Unicode mal normalisé · P=4 I=1 **S=4**

**Le risque.** Des `é` au lieu de `é`, des titres japonais en mojibake, des tags Shift-JIS
étiquetés Latin-1. Très visible, peu grave.

**Mitigations :** normalisation NFC à l'import, heuristique UTF-8 → CP1252, option « forcer
Shift-JIS », et un écran d'aperçu du titrage avant gravure qui montre exactement ce qui sera écrit.

---

#### R13 — Le format de fichier binaire casse entre versions · P=3 I=1 **S=3**

**Le risque.** Un champ ajouté au milieu d'une struct, et le cache de l'utilisateur devient illisible
(bénin) ou, pire, ses **plans** deviennent illisibles (pas bénin).

**Mitigations :** `StaticAssert` sur toutes les tailles de struct sérialisées ; hash de contenu ;
cache jeté sans état d'âme si la version change ; migration explicite et testée pour les plans ;
un test de round-trip par version dans `build.bat test`.

---

#### R14 — Débits USB insuffisants · P=1 I=3 **S=3**

Le NetMD est en USB 1.1 full-speed : ~1.5x temps réel en SP au mieux. Ce n'est pas un risque de
notre côté (le matériel plafonne), mais **une attente utilisateur à gérer** : afficher un ETA honnête
dès le départ, transcoder **tout** avant de commencer à graver (§6.11) pour ne jamais affamer le
pipe, et permettre de continuer à utiliser l'app pendant le transfert.

---

### 11.2 Tableau récapitulatif

| # | Risque | P | I | S | Phase concernée | Mitigation principale |
|---|--------|---|---|---|-----------------|----------------------|
| R1 | Encodeur ATRAC3 (licence + qualité) | 4 | 5 | 20 | 6 | SP d'abord ; clean-room ; filet exe externe |
| R2 | Protocole NetMD sécurisé | 4 | 5 | 20 | 3, 5 | `replay_transport` byte à byte |
| R3 | Driver WinUSB / Zadig | 5 | 3 | 15 | 3 | écran guidé + re-détection auto |
| R7 | Ambition / enlisement | 4 | 3 | 12 | toutes | phases séquentielles, CLI, démo à chaque sortie |
| R4 | Fardeau du no-CRT | 3 | 3 | 9 | toutes | `build.bat check` à chaque commit |
| R5 | Dépendance DirectWrite | 3 | 3 | 9 | 1, 8 | abstraction `os_font_*`, stb_truetype en secours |
| R9 | Faux positif antivirus | 2 | 4 | 8 | 7 | abandon `/ALIGN:16`, manifest, icône, signature |
| R6 | Media Foundation en C | 3 | 2 | 6 | 5 | chargement paresseux, chemin d'échec propre |
| R8 | Fuite du rendu à la demande | 3 | 2 | 6 | 1 | overlay debug + test CPU automatisé |
| R10 | Perte de contexte GL | 2 | 3 | 6 | 7 | ressources GPU reconstructibles |
| R11 | Unity build | 2 | 2 | 4 | 1 | `third_party.obj` séparé |
| R12 | Encodage des tags | 4 | 1 | 4 | 2 | NFC + heuristiques + aperçu |
| R13 | Format de fichier | 3 | 1 | 3 | 2 | StaticAssert, hash, migration des plans |
| R14 | Débit USB | 1 | 3 | 3 | 5 | ETA honnête, transcodage préalable |

### 11.3 Décisions à graver dans des ADR

À l'issue de ce rapport, les ADR suivants doivent être rédigés et acceptés avant la phase 1 :

| ADR | Sujet | Décision |
|-----|-------|----------|
| ADR-002 | Toolchain et flags | no-CRT en release, deux `.obj`, abandon de `/ALIGN:16`, `/GL /LTCG` |
| ADR-003 | Contexte graphique | OpenGL 3.3 core, loader maison, pas de sRGB matériel |
| ADR-004 | Boucle et présentation | rendu à la demande, `MsgWaitForMultipleObjectsEx`, 0 % CPU au repos |
| ADR-005 | Architecture UI | immediate API / retained core (Fleury), layout 5 passes |
| ADR-006 | Renderer | primitive unique SDF, vertex 40 octets, 1 shader, scissor |
| ADR-007 | Texte | DirectWrite → atlas R8, pas de police embarquée, stb_truetype en secours |
| ADR-008 | Codecs | minimp3 / dr_flac / dr_wav / stb_vorbis + Media Foundation ; pas d'Opus en v1 |
| ADR-009 | DSP | f32 désentrelacé, sinc polyphase 64 taps, R128 maison, dither TPDF |
| ADR-010 | ATRAC3 | clean-room en C ; atracdenc uniquement comme oracle de test |
| ADR-011 | USB | WinUSB direct, `UsbTransport`, transport de rejeu |
| ADR-012 | Persistance | binaire maison mappé, pas de SQLite, écriture atomique |
| ADR-013 | `platform.h` | le contrat de ~90 fonctions, vérifié par `build.bat check` |

### 11.4 Liste d'URLs

**Bibliothèques retenues**
- minimp3 (CC0) — https://github.com/lieff/minimp3
- dr_libs : dr_flac, dr_wav (domaine public ou MIT-0) — https://github.com/mackron/dr_libs
- stb : stb_vorbis, stb_truetype, stb_rect_pack (domaine public ou MIT) — https://github.com/nothings/stb
- libebur128 (MIT) — *référence de validation uniquement* — https://github.com/jiixyj/libebur128
- atracdenc (LGPL-2.1, C++17) — *oracle de test uniquement* — https://github.com/dcherednik/atracdenc
- Opus (BSD-3) — *non retenu en v1* — https://opus-codec.org/

**Références techniques Windows / OpenGL**
- mmozeiko — création d'un contexte OpenGL moderne sur Windows (WGL, sans bibliothèque) —
  https://gist.github.com/mmozeiko/ed2ad27f75edf9c26053ce332a1f6647
- mmozeiko — collection de gists Win32 minimalistes — https://gist.github.com/mmozeiko
- WGL_ARB_create_context — https://registry.khronos.org/OpenGL/extensions/ARB/WGL_ARB_create_context.txt
- WGL_ARB_pixel_format — https://registry.khronos.org/OpenGL/extensions/ARB/WGL_ARB_pixel_format.txt
- ARB_buffer_storage (persistent mapping) — https://registry.khronos.org/OpenGL/extensions/ARB/ARB_buffer_storage.txt
- OpenGL 3.3 core specification — https://registry.khronos.org/OpenGL/specs/gl/glspec33.core.pdf
- High DPI Desktop Application Development (PerMonitorV2) —
  https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows
- `DwmSetWindowAttribute` (dark mode, coins, bordure) —
  https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmsetwindowattribute
- `MsgWaitForMultipleObjectsEx` —
  https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-msgwaitformultipleobjectsex
- Raymond Chen, *The Old New Thing* (comportements Win32, boucles de messages, WM_PAINT) —
  https://devblogs.microsoft.com/oldnewthing/
- Application manifests (dpiAwareness, activeCodePage, longPathAware) —
  https://learn.microsoft.com/en-us/windows/win32/sbscs/application-manifests

**Texte**
- DirectWrite — `IDWriteGlyphRunAnalysis` —
  https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nn-dwrite-idwriteglyphrunanalysis
- `IDWriteGlyphRunAnalysis::CreateAlphaTexture` —
  https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwriteglyphrunanalysis-createalphatexture
- `IDWriteFontFallback` —
  https://learn.microsoft.com/en-us/windows/win32/api/dwrite_2/nn-dwrite_2-idwritefontfallback
- stb_truetype — https://github.com/nothings/stb/blob/master/stb_truetype.h
- Halfwidth and Fullwidth Forms (U+FF00–U+FFEF) — https://www.unicode.org/charts/PDF/UFF00.pdf

**Architecture UI**
- Ryan Fleury — *UI Series* (immediate API / retained core, layout, clés) —
  https://www.rfleury.com/p/ui-series-table-of-contents
- Ryan Fleury — *Untangling Lifetimes: The Arena Allocator* —
  https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator
- Casey Muratori — *Handmade Hero* (boucle, arènes, job system, plateforme) —
  https://handmadehero.org/
- Casey Muratori — *Immediate-Mode Graphical User Interfaces* (2005) —
  https://caseymuratori.com/blog_0001
- Dear ImGui — internals, gestion des ID (référence de comparaison) —
  https://github.com/ocornut/imgui/blob/master/docs/FAQ.md
- Inigo Quilez — *2D distance functions* (SDF du rectangle arrondi) —
  https://iquilezles.org/articles/distfunctions2d/
- sokol_gfx (référence de renderer minimaliste) — https://github.com/floooh/sokol

**Audio / DSP**
- EBU R 128 (loudness) — https://tech.ebu.ch/publications/r128
- ITU-R BS.1770-4 (algorithme de mesure, filtres K) —
  https://www.itu.int/rec/R-REC-BS.1770/en
- EBU Tech 3341 (metering) / 3342 (loudness range) — https://tech.ebu.ch/publications/tech3341
- Julius O. Smith — *Digital Audio Resampling Home Page* (sinc polyphase, Kaiser) —
  https://ccrma.stanford.edu/~jos/resample/
- ID3v2.3 — https://id3.org/id3v2.3.0 ; ID3v2.4 — https://id3.org/id3v2.4.0-frames
- Vorbis comment — https://www.xiph.org/vorbis/doc/v-comment.html
- FLAC format — https://xiph.org/flac/format.html
- ISO base media file format / atomes iTunes — https://developer.apple.com/documentation/quicktime-file-format

**Media Foundation / WIC**
- `IMFSourceReader` — https://learn.microsoft.com/en-us/windows/win32/api/mfreadwrite/nn-mfreadwrite-imfsourcereader
- Source Reader — décodage audio —
  https://learn.microsoft.com/en-us/windows/win32/medfound/using-the-source-reader-to-process-media-data
- Media Foundation en C (appels par vtable) —
  https://learn.microsoft.com/en-us/windows/win32/medfound/media-foundation-programming--essential-concepts
- Windows Imaging Component — https://learn.microsoft.com/en-us/windows/win32/wic/-wic-lh

**USB**
- WinUSB — https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/winusb
- `WinUsb_ControlTransfer` —
  https://learn.microsoft.com/en-us/windows/win32/api/winusb/nf-winusb-winusb_controltransfer
- Pipe policies (`RAW_IO`, `PIPE_TRANSFER_TIMEOUT`, `AUTO_CLEAR_STALL`) —
  https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/winusb-functions-for-pipe-policy-modification
- SetupAPI — `SetupDiGetClassDevs` —
  https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdigetclassdevsw
- `RegisterDeviceNotificationW` / `WM_DEVICECHANGE` —
  https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerdevicenotificationw
- Zadig (installation de WinUSB par l'utilisateur) — https://zadig.akeo.ie/
- libusb (non retenu, référence) — https://libusb.info/
- netmd-js (implémentation de référence du protocole NetMD) — https://github.com/cybercase/netmd-js
- linux-minidisc (documentation historique du protocole) — https://github.com/glaubitz/linux-minidisc
- Web MiniDisc Pro (produit de référence) — https://web.minidisc.wiki/

**Build / no-CRT**
- Options du compilateur MSVC — https://learn.microsoft.com/en-us/cpp/build/reference/compiler-options
- Options de l'éditeur de liens MSVC — https://learn.microsoft.com/en-us/cpp/build/reference/linker-options
- `#pragma function` — https://learn.microsoft.com/en-us/cpp/preprocessor/function-c-cpp
- Intrinsics x64 (`__stosb`, `__movsb`, `_mm_*`) —
  https://learn.microsoft.com/en-us/cpp/intrinsics/x64-amd64-intrinsics-list
- PE format (SectionAlignment, relocations) —
  https://learn.microsoft.com/en-us/windows/win32/debug/pe-format

**Design tokens (déjà consolidés dans `docs/research/02b-design-tokens.md`)**
- VS Code Dark Modern — https://github.com/microsoft/vscode/blob/main/extensions/theme-defaults/themes/dark_modern.json
- Radix Colors — https://github.com/radix-ui/colors
- Primer Primitives — https://primer.style/foundations/primitives/color
- Fluent 2 tokens — https://github.com/microsoft/fluentui/tree/master/packages/tokens
- Carbon Design System — https://carbondesignsystem.com/

---

## Annexe A — Budget de taille de l'exe (estimation)

| Poste | Taille estimée |
|-------|----------------|
| `base/` (arènes, strings, math, hash, tri, jobs, log) | 25 KB |
| `platform/win32/` (fenêtre, GL, fichiers, threads, USB, DWrite, MF, WIC, dialogues) | 55 KB |
| `ui/` (moteur UI, layout, widgets, texte, renderer) | 90 KB |
| `core/library` (scan, tags, index, requêtes) | 45 KB |
| `core/plan` + `core/persist` | 20 KB |
| `core/pipeline` (decode, resample, R128, dither) | 35 KB |
| `core/netmd` (protocole, session, sécurisé, charset) | 45 KB |
| `core/atrac3` (code) | 60 KB |
| `core/atrac3` (tables générées) | 35 KB |
| Table du resampler (générée au runtime, 0 dans l'exe) | 0 KB |
| `third_party` : minimp3 | 35 KB |
| `third_party` : dr_flac | 45 KB |
| `third_party` : dr_wav | 25 KB |
| `third_party` : stb_vorbis | 60 KB |
| `third_party` : stb_truetype | 25 KB |
| Police d'icônes subsetée | 8 KB |
| Icône applicative (.res) | 25 KB |
| Manifest | 1 KB |
| Sources GLSL | 3 KB |
| Table de chaînes i18n FR/EN | 12 KB |
| En-têtes PE, import table, relocations | 6 KB |
| **Total estimé** | **~655 KB** |

Marge sous la limite de 1 MB : **~35 %**. Les postes compressibles si nécessaire : stb_vorbis
(supprimer le support Ogg si on ne le veut pas), dr_wav (parseur maison, −17 KB), l'icône (−15 KB),
les tables ATRAC3 (compression RLE, −20 KB). On a donc de la marge, mais pas au point de pouvoir
ajouter libopus **et** une police CJK.

**Jalons de mesure** (à consigner dans STATUS.md) :

| Fin de phase | Cible |
|--------------|-------|
| 1 — Fondations | < 100 KB |
| 2 — Bibliothèque | < 250 KB |
| 3 — Device lecture | < 300 KB |
| 4 — Plan & capacité | < 320 KB |
| 5 — Pipeline & SP | < 500 KB |
| 6 — ATRAC3 & LP | < 620 KB |
| 7 — Polish | < 700 KB |

## Annexe B — Ordre d'implémentation recommandé pour la phase 1

Pour que la phase 1 produise une démo à 60 fps sous 100 KB, dans cet ordre exact :

1. `base_types.h`, `base_arena`, `base_string` (UTF-8), `base_math` (SSE), `base_crt_stubs`,
   `build.bat debug|release|check`. **Critère : `build.bat release` produit un exe qui affiche
   une fenêtre noire, < 8 KB.**
2. `win32_window` (classe, WndProc, `OsEvent`, boucle `MsgWaitForMultipleObjectsEx`, DPI v2, dark
   mode). **Critère : 0 % CPU au repos, mesuré.**
3. `win32_gl` + loader + `r_gl` (un triangle, puis un quad SDF plein écran). **Critère : le shader
   compile et un rectangle arrondi anti-aliasé s'affiche.**
4. `r_core` (batches, vertex buffer, scissor, atlas + packing skyline).
5. `win32_font_dwrite` + `ui_text` (cache de glyphes, mesure). **Critère : « Hello 世界 » s'affiche
   correctement.**
6. `ui_core` (UI_Box, clés, piles, layout 5 passes, signaux, animations).
7. `ui_theme` (les tokens de 02b) + 5 widgets : bouton, label, champ texte, liste virtualisée,
   splitter. **Critère : une liste de 100 000 lignes factices scrolle à 60 fps, sélection multiple,
   redimensionnement de panneaux, exe < 100 KB.**
8. `base_jobs` + overlay debug F11.

Chaque étape est démontrable et mesurable. Aucune ne dépend d'un travail non terminé de la suivante.

# R-03 — Recherche stack technique (Opus, 2026-09-06)

Rôle : programmeur système tradition Muratori / RAD / Handmade. Cible : exe unique < 1 MB, C99/C11, sans CRT en
release, UI OpenGL from scratch (pas de ImGui/Nuklear/SDL/GLFW), core indépendant de la plateforme.

Sources : Handmade Hero, Ryan Fleury UI series, Our Machinery, internals Dear ImGui, stb_truetype, msdfgen, sokol,
Raymond Chen, articles "no CRT MSVC", gists mmozeiko (WGL), stb, dr_libs, minimp3, WinUSB MSDN, DirectWrite.

Sections : toolchain & exe minimal (flags MSVC no-CRT, intrinsics, /MERGE, tailles attendues, APIs Win32 nécessaires,
chargement dynamique, unity build, build.bat) ; fenêtre + contexte WGL sans lib (core 3.3 vs 4.5 DSA, sRGB, vsync,
DPI v2, input, dark title bar, rendu à la demande 0 % CPU, drag&drop Explorer) ; architecture UI (immediate API /
retained core à la Fleury, autolayout 2 passes, animations, focus, popups, listes virtualisées 100k, text input,
DnD interne, découplage core/UI/platform via platform.h) ; rendu GPU (1 VBO, 1 shader, SDF rounded rects GLSL,
atlas, premultiplied alpha, clip, persistent mapping, layout vertex) ; texte (stb_truetype vs SDF vs DirectWrite
rasterisation dans notre atlas, UTF-8, katakana half-width fallback, tabular numbers, ellipsis, hi-DPI) ; audio
(minimp3, dr_flac, dr_wav, stb_vorbis, Media Foundation pour AAC/ALAC/WMA, Opus ?, tags, resampler sinc polyphase,
EBU R128, true-peak, dither, SIMD, job system, arènes) ; ATRAC3 (atracdenc : port C vs exe embarqué, licence) ;
USB WinUSB sans libusb (enum SetupAPI, control transfers, installation driver, hotplug, thread device) ;
persistance (fichier binaire mappable vs SQLite) ; organisation du code ; risques.
Format : Markdown français, 1500-3000 lignes, options/tradeoffs/RECOMMANDATION, spécifique et tranché.

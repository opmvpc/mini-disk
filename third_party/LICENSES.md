# Bibliothèques tierces vendorisées

Sources récupérées le 2026-09-07 depuis les dépôts officiels (raw GitHub), non modifiées.
Toute mise à jour se fait par un nouveau `curl` sur le même chemin, jamais par un patch local :
les réglages d'intégration (allocateurs, math, absence de stdio) sont des macros posées avant
l'inclusion, dans `src/third_party.h` et `src/third_party.c`.

| Fichier | Projet | Version | Commit épinglé | Licence |
|---------|--------|---------|----------------|---------|
| `minimp3.h` | [lieff/minimp3](https://github.com/lieff/minimp3) | master | `ea99364f61c14656440e8d77e9c233ccf3124633` (2026-07-27) | CC0-1.0 |
| `minimp3_ex.h` | idem | master | idem | CC0-1.0 |
| `dr_flac.h` | [mackron/dr_libs](https://github.com/mackron/dr_libs) | 0.13.4 | `dfe8377631000664666519fdb83da193fd8037f4` (2026-08-31) | Unlicense **ou** MIT-0 |
| `dr_wav.h` | idem | 0.14.6 | idem | Unlicense **ou** MIT-0 |
| `stb_vorbis.c` | [nothings/stb](https://github.com/nothings/stb) | 1.22 | `2c980bb59875b0d32144a71867fbdebb2f77cd20` (2026-08-02) | MIT **ou** domaine public (Unlicense) |

`minimp3_ex.h` est vendorisé pour référence mais **n'est pas compilé** : sa couche de commodité
alloue avec `malloc` et lit le fichier entier ou le mmap, ce que ni ADR-002 (pas de CRT) ni le
budget mémoire de T-040 (lecture par blocs de 256 KB) n'autorisent. Le décodage MP3 utilise
uniquement `mp3dec_decode_frame()` de `minimp3.h`, piloté par `src/core/codecs/codec_mp3.c`.

Pour chaque bibliothèque on retient l'alternative permissive sans obligation d'attribution
(CC0 / Unlicense / MIT-0) ; le texte des deux alternatives est reproduit ci-dessous quand la
bibliothèque en propose deux.

---

## minimp3 — CC0 1.0 Universal (domaine public)

> To the extent possible under law, the author(s) have dedicated all copyright and related and
> neighboring rights to this software to the public domain worldwide.
> This software is distributed without any warranty.
> See <http://creativecommons.org/publicdomain/zero/1.0/>

---

## dr_flac / dr_wav — au choix

### ALTERNATIVE 1 — domaine public (www.unlicense.org)

> This is free and unencumbered software released into the public domain.
>
> Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software,
> either in source code form or as a compiled binary, for any purpose, commercial or
> non-commercial, and by any means.
>
> In jurisdictions that recognize copyright laws, the author or authors of this software dedicate
> any and all copyright interest in the software to the public domain. We make this dedication for
> the benefit of the public at large and to the detriment of our heirs and successors. We intend
> this dedication to be an overt act of relinquishment in perpetuity of all present and future
> rights to this software under copyright law.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
> BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
> NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
> CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
>
> For more information, please refer to <http://unlicense.org/>

### ALTERNATIVE 2 — MIT No Attribution (retenue)

> Copyright 2023 David Reid
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
> associated documentation files (the "Software"), to deal in the Software without restriction,
> including without limitation the rights to use, copy, modify, merge, publish, distribute,
> sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
> furnished to do so.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
> NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
> NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
> DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

---

## stb_vorbis — au choix

### ALTERNATIVE A — MIT

> Copyright (c) 2017 Sean Barrett
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
> associated documentation files (the "Software"), to deal in the Software without restriction,
> including without limitation the rights to use, copy, modify, merge, publish, distribute,
> sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or
> substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
> NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
> NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
> DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

### ALTERNATIVE B — domaine public (www.unlicense.org, retenue)

> This is free and unencumbered software released into the public domain.
>
> Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, either
> in source code form or as a compiled binary, for any purpose, commercial or non-commercial, and
> by any means.
>
> In jurisdictions that recognize copyright laws, the author or authors of this software dedicate
> any and all copyright interest in the software to the public domain. We make this dedication for
> the benefit of the public at large and to the detriment of our heirs and successors. We intend
> this dedication to be an overt act of relinquishment in perpetuity of all present and future
> rights to this software under copyright law.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
> NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
> NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
> CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

---

## Formats couverts par Windows, sans code tiers

AAC/M4A, ALAC et WMA passent par **Media Foundation** (`mfplat.dll`, `mfreadwrite.dll`), chargée
dynamiquement dans `src/platform/win32/win32_media.c`. Composant inbox de Windows : aucune licence
tierce, aucun octet ajouté à l'exécutable.

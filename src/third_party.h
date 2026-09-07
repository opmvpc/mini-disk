// third_party.h - the declarations of the vendored decoders, and only those.
//
// The implementations live in third_party.c, compiled as its own translation
// unit so their macros never reach ours (ADR-002). This header carries the
// knobs that change the *declarations* (no stdio, float output); the knobs that
// only change the *implementation* (allocators, math, assertions) stay in
// third_party.c, where they are documented one by one.
//
// core/codecs/*.c include this header to see the types and prototypes. That is
// the whole coupling: no third party symbol is exported anywhere else.
#ifndef THIRD_PARTY_H
#define THIRD_PARTY_H

#include "base/base.h"
#include "base/base_arena.h"

// --- the arena the vendored code allocates from ----------------------------
// Each decoder owns one arena and binds it around the calls that may allocate;
// closing the decoder releases the arena whole, so nothing here ever frees.
// Thread local: two jobs decoding two files at once must not share a binding.
void   tp_arena_bind(Arena *arena);
void   tp_arena_unbind(void);
Arena *tp_arena_bound(void);

// --- minimp3 ---------------------------------------------------------------
// Float output: the pipeline of ADR-007 starts in f32, converting to s16 and
// back would cost a quantisation for nothing.
#define MINIMP3_FLOAT_OUTPUT
#include "../third_party/minimp3.h"

// --- dr_flac ---------------------------------------------------------------
// No stdio: files are read through os_file_read_at behind drflac_read_proc.
// No Ogg: Ogg-encapsulated FLAC is vanishingly rare and costs ~8 KB of code.
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#include "../third_party/dr_flac.h"

// --- dr_wav ----------------------------------------------------------------
// Decoding only: nothing in minidisk writes a WAV file.
#define DR_WAV_NO_STDIO
#define DR_WAV_NO_WCHAR
#include "../third_party/dr_wav.h"

// --- stb_vorbis ------------------------------------------------------------
// Header only here; the implementation half is included by third_party_vorbis.c.
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

#endif  // THIRD_PARTY_H

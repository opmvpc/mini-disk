// third_party.c - unity build of the vendored decoders (T-040, ADR-007).
//
// Its own translation unit for one reason: these libraries define macros named
// `malloc`, `NULL`, `floor`, `cos`... Compiling them here means none of that can
// reach our code, and `build.bat check` keeps working on src/core untouched.
// stb_vorbis gets a second one, third_party_vorbis.c: it shares a static
// `get_bits` with minimp3, so the two cannot sit in the same unity build.
//
// ---------------------------------------------------------------------------
//  Every knob, and why it is set
// ---------------------------------------------------------------------------
//  Memory - the libraries must never call the CRT allocator (ADR-002, and the
//  release build links with /NODEFAULTLIB so `malloc` would simply not resolve).
//    DRFLAC_MALLOC / DRFLAC_REALLOC / DRFLAC_FREE      -> tp_malloc / tp_realloc / tp_release
//    DRWAV_MALLOC  / DRWAV_REALLOC  / DRWAV_FREE       -> idem
//    STB_VORBIS_NO_CRT + stb_vorbis_alloc              -> one arena block, see below
//    minimp3                                           -> allocates nothing at all
//  tp_malloc pushes on the arena the caller bound with tp_arena_bind(); there is
//  one arena per open decoder and codec_close() releases it whole, so tp_release
//  is a no-op and tp_realloc always copies forward. The 16 byte header in front
//  of each block is what makes realloc able to copy: the libraries do not pass
//  the old size back.
//
//  stdio - the decoders read through our own callbacks over os_file_read_at.
//    DR_FLAC_NO_STDIO, DR_WAV_NO_STDIO (in third_party.h, they hide prototypes)
//    STB_VORBIS_NO_STDIO
//
//  libm - there is none. minimp3 and dr_* use tables and integer maths only;
//  stb_vorbis needs seven functions, all during setup (MDCT twiddles, windows,
//  codebook lookup tables) plus ldexp on the decode path. third_party_vorbis.c
//  supplies them in double precision and maps the names with macros right
//  before the include, so no external pow/exp/log/sqrt/floor/cos/sin remains.
//    STB_VORBIS_NO_CRT also removes <math.h>, so M_PI is ours too.
//    alloca: the only use is inside temp_alloc(), whose other branch is
//    setup_temp_malloc(f, size) and whose `f` is in scope at every call site.
//    We point alloca at that branch so no dynamic stack allocation - and hence
//    no _alloca_probe, which /NODEFAULTLIB could not resolve - is ever emitted.
//    qsort: one call site (codebook sorting); ours is a heapsort, no recursion.
//
//  Assertions - disabled. These decoders are fed files somebody else wrote, so
//  a malformed stream must return an error, not trip an assert (ADR-012: third
//  party code is the one place a validation boundary lives, and the header fuzz
//  test of tests/test_codecs.c walks straight through it).
//
//  Not compiled - minimp3_ex.h: it mallocs and reads whole files, see
//  third_party/LICENSES.md.
// ---------------------------------------------------------------------------

#include "third_party.h"

// MSVC warnings we accept from code we do not own: unreferenced parameters and
// locals, constant conditionals, signed/unsigned mismatches, shadowing,
// possibly uninitialised locals, unreachable code, assignment in a condition.
#pragma warning(push, 0)
#pragma warning(disable : 4005 4018 4100 4127 4189 4244 4245 4267 4456 4457 4459 4701 4702 4703 4706)

// /GL turns a memcpy in code we do not own into a "library helper" call that the
// linker refuses to resolve against our #pragma function stub (C2268 - the trap
// CONVENTIONS.md records from T-004). The vendored code therefore calls our
// intrinsic based helpers directly. <string.h> and <stdlib.h> are included here
// first so their declarations are read before the names become macros.
#include <stdlib.h>
#include <string.h>

#define memcpy(dst, src, size)   mem_copy((dst), (src), (u64)(size))
#define memmove(dst, src, size)  mem_move((dst), (src), (u64)(size))
#define memset(dst, value, size) mem_set((dst), (u8)(value), (u64)(size))
#define memcmp(a, b, size)       mem_cmp((a), (b), (u64)(size))

// ---------------------------------------------------------------------------
//  Arena backed allocator
// ---------------------------------------------------------------------------

typedef struct TpAllocHeader {
    u64 size;     // payload bytes, what a realloc has to carry over
    u64 padding;  // keeps the payload 16 byte aligned
} TpAllocHeader;

thread_var Arena *tp_bound_arena;

void tp_arena_bind(Arena *arena) { tp_bound_arena = arena; }
void tp_arena_unbind(void) { tp_bound_arena = 0; }
Arena *tp_arena_bound(void) { return tp_bound_arena; }

static void *tp_malloc(u64 size) {
    // A decoder allocating outside a bound window is a bug in our glue, not a
    // runtime condition: the arena is bound around every call that can allocate.
    AssertAlways(tp_bound_arena != 0);
    TpAllocHeader *header =
        (TpAllocHeader *)arena_push(tp_bound_arena, sizeof(TpAllocHeader) + size, 16);
    header->size = size;
    header->padding = 0;
    return header + 1;
}

static void *tp_realloc(void *block, u64 size) {
    if (!block) { return tp_malloc(size); }
    TpAllocHeader *header = (TpAllocHeader *)block - 1;
    if (size <= header->size) {
        header->size = size;
        return block;
    }
    void *grown = tp_malloc(size);
    mem_copy(grown, block, header->size);
    return grown;
}

// The arena is released in one go by codec_close(); nothing is freed piecemeal.
static void tp_release(void *block) { Unused(block); }

// ---------------------------------------------------------------------------
//  minimp3 - allocates nothing, needs nothing but memcpy/memset
// ---------------------------------------------------------------------------
//  ONLY_MP3 drops the Layer I and Layer II tables: minidisk imports .mp3 files
//  and ADR-007 lists Layer III alone. Worth ~6 KB.
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "../third_party/minimp3.h"

// ---------------------------------------------------------------------------
//  dr_flac / dr_wav
// ---------------------------------------------------------------------------
//  NO_SIMD: dr_flac ships three copies of the Rice residual decoder (scalar,
//  SSE4.1 32 bit, SSE4.1 64 bit) and /O2 unrolls each of them; together they are
//  50 KB of the exe, 27 KB of which is the two SIMD ones. The scalar decoder
//  still runs far above the >= 300x realtime that ADR-007 asks of FLAC, and the
//  size budget of T-040 is the tighter constraint. Measured in tests/bench.
#define DR_FLAC_NO_SIMD
#define DRFLAC_ASSERT(expression)        ((void)0)
#define DRFLAC_MALLOC(sz)                tp_malloc((u64)(sz))
#define DRFLAC_REALLOC(p, sz)            tp_realloc((p), (u64)(sz))
#define DRFLAC_FREE(p)                   tp_release((p))
#define DRFLAC_COPY_MEMORY(dst, src, sz) mem_copy((dst), (src), (u64)(sz))
#define DRFLAC_ZERO_MEMORY(p, sz)        mem_zero((p), (u64)(sz))
#define DR_FLAC_IMPLEMENTATION
#include "../third_party/dr_flac.h"

#define DRWAV_ASSERT(expression)        ((void)0)
#define DRWAV_MALLOC(sz)                tp_malloc((u64)(sz))
#define DRWAV_REALLOC(p, sz)            tp_realloc((p), (u64)(sz))
#define DRWAV_FREE(p)                   tp_release((p))
#define DRWAV_COPY_MEMORY(dst, src, sz) mem_copy((dst), (src), (u64)(sz))
#define DRWAV_ZERO_MEMORY(p, sz)        mem_zero((p), (u64)(sz))
#define DR_WAV_IMPLEMENTATION
#include "../third_party/dr_wav.h"

#pragma warning(pop)

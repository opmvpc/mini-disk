// third_party_vorbis.c - stb_vorbis, alone in its own translation unit.
//
// It cannot share third_party.c: minimp3 and stb_vorbis both define a static
// `get_bits`, and one unity build of the two does not compile. Splitting is
// also what lets stb_vorbis have `floor`, `cos`, `qsort`... as macros without
// dr_flac and dr_wav seeing them.
//
// The knobs and the reasoning behind them are documented at the top of
// third_party.c; this file carries the half that belongs to stb_vorbis.
#include "third_party.h"

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
//  The maths stb_vorbis needs, without libm
// ---------------------------------------------------------------------------
//  Double precision throughout: the twiddle tables are built once per file and
//  a float-precision cosine there shows up as MDCT ringing.

#define TP_PI    3.14159265358979323846
#define TP_LN2   0.69314718055994530942
#define TP_LOG2E 1.44269504088896340736

typedef union TpF64Bits {
    f64 value;
    u64 bits;
} TpF64Bits;

static f64 tp_floor(f64 x) {
    // Beyond 2^52 every double is already an integer, and the i64 cast would
    // overflow.
    if (!(x > -4.503599627370496e15 && x < 4.503599627370496e15)) { return x; }
    f64 truncated = (f64)(i64)x;
    return (truncated > x) ? truncated - 1.0 : truncated;
}

// 2^n by writing the exponent field, split in two steps so both a huge |n| and
// the denormal range come out right.
static f64 tp_ldexp(f64 x, i32 n) {
    if (n > 1023) {
        n -= 1023;
        x *= 8.98846567431158e307;  // 2^1023
        if (n > 1023) { return x * 8.98846567431158e307; }
    } else if (n < -1022) {
        n += 969;
        x *= 2.0041683600089728e-292;  // 2^-969
        if (n < -1022) { return x * 2.0041683600089728e-292; }
    }
    TpF64Bits scale;
    scale.bits = (u64)(1023 + n) << 52;
    return x * scale.value;
}

// exp(x) = 2^k * e^r with r = x - k*ln2, |r| <= ln2/2. Taylor to r^11 leaves
// about 1e-14 relative, far below the f32 the caller casts to.
static f64 tp_exp(f64 x) {
    if (x > 709.0) { return 1.0e308 * 1.0e308; }  // +inf
    if (x < -745.0) { return 0.0; }
    f64 k = tp_floor(x * TP_LOG2E + 0.5);
    f64 r = x - k * TP_LN2;
    f64 sum = 1.0 / 39916800.0;  // 1/11!
    sum = sum * r + 1.0 / 3628800.0;
    sum = sum * r + 1.0 / 362880.0;
    sum = sum * r + 1.0 / 40320.0;
    sum = sum * r + 1.0 / 5040.0;
    sum = sum * r + 1.0 / 720.0;
    sum = sum * r + 1.0 / 120.0;
    sum = sum * r + 1.0 / 24.0;
    sum = sum * r + 1.0 / 6.0;
    sum = sum * r + 0.5;
    sum = sum * r + 1.0;
    sum = sum * r + 1.0;
    return tp_ldexp(sum, (i32)k);
}

// log(x) = e*ln2 + 2*atanh(f), f = (m-1)/(m+1) with the mantissa folded into
// [sqrt(1/2), sqrt(2)) so |f| <= 0.1716 and eight terms are plenty.
static f64 tp_log(f64 x) {
    if (!(x > 0.0)) { return -1.0e308 * 1.0e308; }  // -inf
    TpF64Bits split;
    split.value = x;
    i32 exponent = (i32)((split.bits >> 52) & 0x7FF) - 1023;
    split.bits = (split.bits & 0x000FFFFFFFFFFFFFull) | ((u64)1023 << 52);
    f64 mantissa = split.value;
    if (mantissa > 1.4142135623730951) {
        mantissa *= 0.5;
        exponent += 1;
    }
    f64 f = (mantissa - 1.0) / (mantissa + 1.0);
    f64 f2 = f * f;
    f64 sum = 1.0 / 15.0;
    sum = sum * f2 + 1.0 / 13.0;
    sum = sum * f2 + 1.0 / 11.0;
    sum = sum * f2 + 1.0 / 9.0;
    sum = sum * f2 + 1.0 / 7.0;
    sum = sum * f2 + 1.0 / 5.0;
    sum = sum * f2 + 1.0 / 3.0;
    sum = sum * f2 + 1.0;
    return (f64)exponent * TP_LN2 + 2.0 * f * sum;
}

// stb_vorbis only ever raises a small positive number to a small non negative
// integer (codebook lookup1 dimensions). Repeated squaring keeps those exact,
// which matters: the result is compared against an integer right after.
static f64 tp_pow(f64 x, f64 y) {
    f64 rounded = tp_floor(y + 0.5);
    if (rounded == y && rounded >= 0.0 && rounded <= 64.0) {
        f64 result = 1.0;
        f64 base = x;
        u32 exponent = (u32)rounded;
        while (exponent) {
            if (exponent & 1) { result *= base; }
            base *= base;
            exponent >>= 1;
        }
        return result;
    }
    return tp_exp(y * tp_log(x));
}

// Taylor on |r| <= pi/4: sin to r^13 and cos to r^14 both land under 1e-14.
static f64 tp_sin_kernel(f64 r) {
    f64 r2 = r * r;
    f64 odd = 1.0 / 6227020800.0;  // 1/13!
    odd = odd * r2 - 1.0 / 39916800.0;
    odd = odd * r2 + 1.0 / 362880.0;
    odd = odd * r2 - 1.0 / 5040.0;
    odd = odd * r2 + 1.0 / 120.0;
    odd = odd * r2 - 1.0 / 6.0;
    return r + r * r2 * odd;
}

static f64 tp_cos_kernel(f64 r) {
    f64 r2 = r * r;
    f64 even = -1.0 / 87178291200.0;  // -1/14!
    even = even * r2 + 1.0 / 479001600.0;
    even = even * r2 - 1.0 / 3628800.0;
    even = even * r2 + 1.0 / 40320.0;
    even = even * r2 - 1.0 / 720.0;
    even = even * r2 + 1.0 / 24.0;
    even = even * r2 - 0.5;
    return 1.0 + r2 * even;
}

// Cody-Waite reduction on pi/2 split in two halves: stb_vorbis never asks for
// an angle past a few hundred radians, and two terms hold every bit of those.
#define TP_PI_2_HI 1.5707963267341256e+00
#define TP_PI_2_LO 6.0771005065061922e-11

static f64 tp_sin_cos(f64 x, b32 want_cosine) {
    i64 quadrant = (i64)tp_floor(x * (2.0 / TP_PI) + (x < 0.0 ? -0.5 : 0.5));
    f64 r = (x - (f64)quadrant * TP_PI_2_HI) - (f64)quadrant * TP_PI_2_LO;
    u32 phase = (u32)((quadrant + (want_cosine ? 1 : 0)) & 3);
    switch (phase) {
        case 0: return tp_sin_kernel(r);
        case 1: return tp_cos_kernel(r);
        case 2: return -tp_sin_kernel(r);
        default: return -tp_cos_kernel(r);
    }
}

static f64 tp_sin(f64 x) { return tp_sin_cos(x, 0); }
static f64 tp_cos(f64 x) { return tp_sin_cos(x, 1); }
static f64 tp_sqrt(f64 x) { return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(x))); }
static i32 tp_abs_i32(i32 x) { return (x < 0) ? -x : x; }

// Heapsort: no recursion, no scratch beyond one element, and the element moves
// go through mem_copy so /GL cannot turn them into an unresolvable memcpy call.
#define TP_SORT_ELEM_MAX 16

static void tp_sort_swap(u8 *a, u8 *b, u64 width) {
    u8 temp[TP_SORT_ELEM_MAX];
    mem_copy(temp, a, width);
    mem_copy(a, b, width);
    mem_copy(b, temp, width);
}

static void tp_sift_down(u8 *items, u64 root, u64 end, u64 width,
                         int (*compare)(const void *, const void *)) {
    for (;;) {
        u64 child = 2 * root + 1;
        if (child >= end) { return; }
        if (child + 1 < end && compare(items + child * width, items + (child + 1) * width) < 0) {
            child += 1;
        }
        if (compare(items + root * width, items + child * width) >= 0) { return; }
        tp_sort_swap(items + root * width, items + child * width, width);
        root = child;
    }
}

static void tp_qsort(void *base, u64 count, u64 width,
                     int (*compare)(const void *, const void *)) {
    AssertAlways(width <= TP_SORT_ELEM_MAX);
    if (count < 2) { return; }
    u8 *items = (u8 *)base;
    for (u64 start = count / 2; start > 0;) {
        start -= 1;
        tp_sift_down(items, start, count, width, compare);
    }
    for (u64 end = count - 1; end > 0; end -= 1) {
        tp_sort_swap(items, items + end * width, width);
        tp_sift_down(items, 0, end, width, compare);
    }
}

// ---------------------------------------------------------------------------
//  stb_vorbis - the one that wants libm, and the one we starve of it
// ---------------------------------------------------------------------------
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_CRT
// codec_ogg.c uses the pushdata API only; the pulldata half brings its own
// seeking, memory and file machinery that nothing here would ever call.
#define STB_VORBIS_NO_PULLDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION  // the pipeline is f32 end to end
#define M_PI TP_PI

// stb_vorbis redefines NULL under NO_CRT; drop ours so the redefinition is silent.
#undef NULL

#define floor(x)          tp_floor((f64)(x))
#define ldexp(x, n)       tp_ldexp((f64)(x), (i32)(n))
#define exp(x)            tp_exp((f64)(x))
#define log(x)            tp_log((f64)(x))
#define pow(x, y)         tp_pow((f64)(x), (f64)(y))
#define sin(x)            tp_sin((f64)(x))
#define cos(x)            tp_cos((f64)(x))
#define sqrt(x)           tp_sqrt((f64)(x))
#define abs(x)            tp_abs_i32((i32)(x))
#define qsort(b, n, w, c) tp_qsort((b), (u64)(n), (u64)(w), (c))
#define assert(x)         ((void)0)
// See the header comment: `f` is in scope wherever temp_alloc expands.
#define alloca(sz) setup_temp_malloc(f, (int)(sz))

#include "../third_party/stb_vorbis.c"

#pragma warning(pop)

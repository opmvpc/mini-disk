// base.h - fundamental types, macros and memory primitives. No OS dependency.
#ifndef BASE_H
#define BASE_H

#include <intrin.h>

// --- types -----------------------------------------------------------------
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed char        i8;
typedef short              i16;
typedef int                i32;
typedef long long          i64;
typedef float              f32;
typedef double             f64;
typedef i32                b32;

#define U8_MAX  0xFFu
#define U16_MAX 0xFFFFu
#define U32_MAX 0xFFFFFFFFu
#define U64_MAX 0xFFFFFFFFFFFFFFFFull
#define I32_MIN (-2147483647 - 1)
#define I32_MAX 2147483647
#define I64_MIN (-9223372036854775807ll - 1)
#define I64_MAX 9223372036854775807ll

// --- attributes ------------------------------------------------------------
#define md_inline  __forceinline
#define no_return  __declspec(noreturn)
#define global     static
#define thread_var __declspec(thread)
#define Unused(x)  ((void)(x))

// --- macros ----------------------------------------------------------------
#define ArrayCount(a)  (sizeof(a) / sizeof((a)[0]))
#define Min(a, b)      ((a) < (b) ? (a) : (b))
#define Max(a, b)      ((a) > (b) ? (a) : (b))
#define Clamp(x, lo, hi) Min(Max(x, lo), hi)
#define KB(n)          ((u64)(n) << 10)
#define MB(n)          ((u64)(n) << 20)
#define GB(n)          ((u64)(n) << 30)
#define AlignPow2(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define IsPow2(x)      (((x) != 0) && (((x) & ((x) - 1)) == 0))
#define OffsetOf(T, m) ((u64)&(((T *)0)->m))
#define Swap(T, a, b)  do { T swap_tmp_ = (a); (a) = (b); (b) = swap_tmp_; } while (0)
#define StaticAssert(c, name) typedef char static_assert_##name[(c) ? 1 : -1]

// --- assertions ------------------------------------------------------------
// Assert: internal invariants, debug only. AssertAlways: invariants whose
// violation would corrupt user data - kept in release (ADR-012).
no_return void os_exit(i32 code);

#define AssertAlways(x)  do { if (!(x)) { __debugbreak(); os_exit(3); } } while (0)

#if BUILD_DEBUG
#  define Assert(x) do { if (!(x)) { __debugbreak(); } } while (0)
#else
#  define Assert(x) ((void)0)
#endif

// --- raw memory ------------------------------------------------------------
// Implemented with rep movsb / rep stosb: no CRT call, ERMSB fast path.
md_inline void mem_copy(void *dst, const void *src, u64 size) {
    __movsb((u8 *)dst, (const u8 *)src, size);
}
md_inline void mem_set(void *dst, u8 value, u64 size) {
    __stosb((u8 *)dst, value, size);
}
md_inline void mem_zero(void *dst, u64 size) { __stosb((u8 *)dst, 0, size); }

md_inline void mem_move(void *dst, const void *src, u64 size) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    if (d == s || size == 0) { return; }
    if (d < s || d >= s + size) {
        __movsb(d, s, size);
    } else {
        for (u64 i = size; i > 0; i -= 1) { d[i - 1] = s[i - 1]; }
    }
}
md_inline i32 mem_cmp(const void *a, const void *b, u64 size) {
    const u8 *x = (const u8 *)a;
    const u8 *y = (const u8 *)b;
    for (u64 i = 0; i < size; i += 1) {
        if (x[i] != y[i]) { return (x[i] < y[i]) ? -1 : 1; }
    }
    return 0;
}

#define StructZero(p) mem_zero((p), sizeof(*(p)))

#endif // BASE_H

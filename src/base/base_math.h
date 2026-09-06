// base_math.h - f32 helpers built on SSE intrinsics, no libm.
#ifndef BASE_MATH_H
#define BASE_MATH_H

#include <smmintrin.h>

#include "base.h"

#define PI_F32     3.14159265358979323846f
#define TAU_F32    6.28318530717958647692f
#define EPSILON_F32 1.192092896e-07f

typedef union V2 {
    struct { f32 x, y; };
    f32 v[2];
} V2;

typedef struct Rect {
    V2 min;
    V2 max;
} Rect;

md_inline u32 f32_bits(f32 x) {
    u32 bits;
    mem_copy(&bits, &x, sizeof(bits));
    return bits;
}
md_inline f32 bits_f32(u32 bits) {
    f32 x;
    mem_copy(&x, &bits, sizeof(x));
    return x;
}

md_inline f32 sqrt_f32(f32 x) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x))); }
md_inline f32 abs_f32(f32 x) { return bits_f32(f32_bits(x) & 0x7FFFFFFFu); }
md_inline f32 floor_f32(f32 x) {
    return _mm_cvtss_f32(_mm_floor_ss(_mm_set_ss(x), _mm_set_ss(x)));
}
md_inline f32 ceil_f32(f32 x) { return _mm_cvtss_f32(_mm_ceil_ss(_mm_set_ss(x), _mm_set_ss(x))); }
md_inline f32 round_f32(f32 x) {
    return _mm_cvtss_f32(_mm_round_ss(_mm_set_ss(x), _mm_set_ss(x),
                                      _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
}
md_inline f32 min_f32(f32 a, f32 b) { return _mm_cvtss_f32(_mm_min_ss(_mm_set_ss(a), _mm_set_ss(b))); }
md_inline f32 max_f32(f32 a, f32 b) { return _mm_cvtss_f32(_mm_max_ss(_mm_set_ss(a), _mm_set_ss(b))); }
md_inline f32 clamp_f32(f32 x, f32 lo, f32 hi) { return min_f32(max_f32(x, lo), hi); }
md_inline f32 lerp_f32(f32 a, f32 b, f32 t) { return a + (b - a) * t; }

md_inline V2 v2(f32 x, f32 y) {
    V2 result;
    result.x = x;
    result.y = y;
    return result;
}
md_inline V2 v2_add(V2 a, V2 b) { return v2(a.x + b.x, a.y + b.y); }
md_inline V2 v2_sub(V2 a, V2 b) { return v2(a.x - b.x, a.y - b.y); }
md_inline V2 v2_scale(V2 a, f32 s) { return v2(a.x * s, a.y * s); }
md_inline f32 v2_dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
md_inline f32 v2_length(V2 a) { return sqrt_f32(v2_dot(a, a)); }

md_inline Rect rect(f32 x0, f32 y0, f32 x1, f32 y1) {
    Rect result;
    result.min = v2(x0, y0);
    result.max = v2(x1, y1);
    return result;
}
md_inline f32 rect_width(Rect r) { return r.max.x - r.min.x; }
md_inline f32 rect_height(Rect r) { return r.max.y - r.min.y; }
md_inline b32 rect_contains(Rect r, V2 p) {
    return p.x >= r.min.x && p.x < r.max.x && p.y >= r.min.y && p.y < r.max.y;
}
Rect rect_intersect(Rect a, Rect b);

#endif // BASE_MATH_H

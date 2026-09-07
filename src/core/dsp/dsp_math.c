#include "dsp_math.h"

#include <emmintrin.h>

f64 dsp_sqrt_f64(f64 x) { return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(x))); }

f64 dsp_abs_f64(f64 x) {
    u64 bits;
    mem_copy(&bits, &x, sizeof(bits));
    bits &= 0x7FFFFFFFFFFFFFFFull;
    f64 result;
    mem_copy(&result, &bits, sizeof(result));
    return result;
}

f64 dsp_round_f64(f64 x) {
    // cvttsd2si truncates toward zero; adding half of the sign first turns that
    // into round-half-away-from-zero without touching the rounding mode.
    f64 shifted = (x >= 0.0) ? (x + 0.5) : (x - 0.5);
    i64 truncated = (i64)shifted;
    return (f64)truncated;
}

// 2^k for |k| < 1000, built straight into the exponent field.
static f64 dsp_ldexp_f64(f64 x, i32 k) {
    // Two halves so a k near the extremes still goes through a normal double.
    i32 half = k / 2;
    u64 bits_a = (u64)(1023 + half) << 52;
    u64 bits_b = (u64)(1023 + (k - half)) << 52;
    f64 a, b;
    mem_copy(&a, &bits_a, sizeof(a));
    mem_copy(&b, &bits_b, sizeof(b));
    return x * a * b;
}

// --- sine / cosine ---------------------------------------------------------
// Cody-Waite reduction on three pieces of pi/2 (the argument never exceeds a
// few hundred here: a Kaiser sinc runs to +-32 periods), then the Taylor series
// on |r| <= pi/4 where the first dropped term is r^17/17! ~ 4e-17.
#define DSP_PIO2_HI 1.57079632679489655800e+00
#define DSP_PIO2_MI 6.12323399573676603587e-17
#define DSP_PIO2_LO 1.49798624421214621919e-33

static f64 dsp_sin_poly(f64 r) {
    f64 r2 = r * r;
    f64 p = 1.0 - r2 * (1.0 / (14.0 * 15.0));
    p = 1.0 - r2 * (1.0 / (12.0 * 13.0)) * p;
    p = 1.0 - r2 * (1.0 / (10.0 * 11.0)) * p;
    p = 1.0 - r2 * (1.0 / (8.0 * 9.0)) * p;
    p = 1.0 - r2 * (1.0 / (6.0 * 7.0)) * p;
    p = 1.0 - r2 * (1.0 / (4.0 * 5.0)) * p;
    p = 1.0 - r2 * (1.0 / (2.0 * 3.0)) * p;
    return r * p;
}

static f64 dsp_cos_poly(f64 r) {
    f64 r2 = r * r;
    f64 p = 1.0 - r2 * (1.0 / (13.0 * 14.0));
    p = 1.0 - r2 * (1.0 / (11.0 * 12.0)) * p;
    p = 1.0 - r2 * (1.0 / (9.0 * 10.0)) * p;
    p = 1.0 - r2 * (1.0 / (7.0 * 8.0)) * p;
    p = 1.0 - r2 * (1.0 / (5.0 * 6.0)) * p;
    p = 1.0 - r2 * (1.0 / (3.0 * 4.0)) * p;
    return 1.0 - r2 * 0.5 * p;
}

// Returns the reduced argument and writes the quadrant 0..3.
static f64 dsp_reduce_pio2(f64 x, i32 *quadrant) {
    f64 n = dsp_round_f64(x * (2.0 / DSP_PI));
    f64 r = x - n * DSP_PIO2_HI;
    r = r - n * DSP_PIO2_MI;
    r = r - n * DSP_PIO2_LO;
    i64 q = (i64)n;
    *quadrant = (i32)(q & 3);
    return r;
}

f64 dsp_sin_f64(f64 x) {
    i32 quadrant;
    f64 r = dsp_reduce_pio2(x, &quadrant);
    switch (quadrant) {
        case 0: return dsp_sin_poly(r);
        case 1: return dsp_cos_poly(r);
        case 2: return -dsp_sin_poly(r);
        default: return -dsp_cos_poly(r);
    }
}

f64 dsp_cos_f64(f64 x) {
    i32 quadrant;
    f64 r = dsp_reduce_pio2(x, &quadrant);
    switch (quadrant) {
        case 0: return dsp_cos_poly(r);
        case 1: return -dsp_sin_poly(r);
        case 2: return -dsp_cos_poly(r);
        default: return dsp_sin_poly(r);
    }
}

f64 dsp_tan_f64(f64 x) {
    i32 quadrant;
    f64 r = dsp_reduce_pio2(x, &quadrant);
    f64 s = dsp_sin_poly(r);
    f64 c = dsp_cos_poly(r);
    // Odd quadrants swap sine and cosine, which flips the ratio and its sign.
    return (quadrant & 1) ? (-c / s) : (s / c);
}

// --- exp / log -------------------------------------------------------------
f64 dsp_exp_f64(f64 x) {
    if (x > 709.0) { return 1.7976931348623157e308; }
    if (x < -745.0) { return 0.0; }
    f64 n = dsp_round_f64(x * DSP_INV_LN2);
    // ln2 split in two so n*ln2 stays exact to more than 53 bits.
    f64 r = x - n * 6.93147180369123816490e-01;
    r = r - n * 1.90821492927058770002e-10;
    // exp(r) = 1 + r(1 + r/2(1 + r/3(... (1 + r/14))))  on |r| <= 0.3466.
    f64 p = 1.0 + r * (1.0 / 14.0);
    for (i32 k = 13; k >= 1; k -= 1) { p = 1.0 + r * (1.0 / (f64)k) * p; }
    return dsp_ldexp_f64(p, (i32)n);
}

f64 dsp_log_f64(f64 x) {
    if (x <= 0.0) { return DSP_LOG_ZERO; }
    u64 bits;
    mem_copy(&bits, &x, sizeof(bits));
    i32 exponent = (i32)((bits >> 52) & 0x7FFull) - 1023;
    if (exponent == -1023) {
        // Subnormal: scale it into the normal range and pay for it in the
        // exponent. Only silence-adjacent samples ever land here.
        x *= 18014398509481984.0;  // 2^54
        mem_copy(&bits, &x, sizeof(bits));
        exponent = (i32)((bits >> 52) & 0x7FFull) - 1023 - 54;
    }
    bits = (bits & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;
    f64 m;
    mem_copy(&m, &bits, sizeof(m));
    if (m > 1.41421356237309504880) {
        m *= 0.5;
        exponent += 1;
    }
    // atanh series: log(m) = 2*(s + s^3/3 + ... ), s = (m-1)/(m+1), |s| < 0.1716.
    f64 s = (m - 1.0) / (m + 1.0);
    f64 s2 = s * s;
    f64 p = 2.0 / 19.0;
    p = 2.0 / 17.0 + s2 * p;
    p = 2.0 / 15.0 + s2 * p;
    p = 2.0 / 13.0 + s2 * p;
    p = 2.0 / 11.0 + s2 * p;
    p = 2.0 / 9.0 + s2 * p;
    p = 2.0 / 7.0 + s2 * p;
    p = 2.0 / 5.0 + s2 * p;
    p = 2.0 / 3.0 + s2 * p;
    p = 2.0 + s2 * p;
    return s * p + (f64)exponent * DSP_LN2;
}

f64 dsp_log10_f64(f64 x) { return dsp_log_f64(x) * (1.0 / DSP_LN10); }

f64 dsp_pow_f64(f64 base, f64 exponent) { return dsp_exp_f64(exponent * dsp_log_f64(base)); }

// --- window and interpolation kernels --------------------------------------
f64 dsp_sinc(f64 x) {
    if (x == 0.0) { return 1.0; }
    f64 a = DSP_PI * x;
    return dsp_sin_f64(a) / a;
}

f64 dsp_bessel_i0(f64 x) {
    // I0(x) = sum_k ((x/2)^k / k!)^2, each term from the previous one.
    f64 half = x * 0.5;
    f64 term = 1.0;
    f64 sum = 1.0;
    for (i32 k = 1; k < 64; k += 1) {
        f64 ratio = half / (f64)k;
        term *= ratio * ratio;
        sum += term;
        if (term < sum * 1e-18) { break; }
    }
    return sum;
}

// --- dB --------------------------------------------------------------------
f32 dsp_db_from_amp(f32 amplitude) {
    f64 a = (f64)((amplitude < 0.0f) ? -amplitude : amplitude);
    if (a <= 1e-30) { return DSP_DB_SILENCE; }
    return (f32)(20.0 * dsp_log10_f64(a));
}

f32 dsp_amp_from_db(f32 db) { return (f32)dsp_exp_f64((f64)db * (DSP_LN10 / 20.0)); }

f32 dsp_db_from_power(f32 power) {
    if (power <= 1e-60f) { return DSP_DB_SILENCE; }
    return (f32)(10.0 * dsp_log10_f64((f64)power));
}

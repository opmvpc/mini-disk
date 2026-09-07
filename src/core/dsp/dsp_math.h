// dsp_math.h - the handful of transcendentals the DSP needs, in f64, without libm.
//
// We link /NODEFAULTLIB, so sin/cos/exp/log/pow simply do not exist (ADR-002).
// Everything here is written from a range reduction plus a Taylor series whose
// last kept term is below the double epsilon over the reduced interval: these
// functions run at table-build time and once per dB conversion, never in a
// sample loop, so a short branchless polynomial is worth more than a minimax
// fit we would have to justify.
#ifndef DSP_MATH_H
#define DSP_MATH_H

#include "../../base/base.h"

#define DSP_PI      3.14159265358979323846
#define DSP_LN2     0.69314718055994530942
#define DSP_LN10    2.30258509299404568402
#define DSP_INV_LN2 1.44269504088896340736

f64 dsp_sqrt_f64(f64 x);
f64 dsp_abs_f64(f64 x);
f64 dsp_round_f64(f64 x);  // half away from zero, |x| < 2^52

f64 dsp_sin_f64(f64 x);
f64 dsp_cos_f64(f64 x);
f64 dsp_tan_f64(f64 x);

f64 dsp_exp_f64(f64 x);
f64 dsp_log_f64(f64 x);  // x <= 0 gives DSP_LOG_ZERO, the "digital silence" floor
f64 dsp_log10_f64(f64 x);
f64 dsp_pow_f64(f64 base, f64 exponent);  // base > 0

// sin(pi*x)/(pi*x), exactly 1 at x == 0.
f64 dsp_sinc(f64 x);
// Modified Bessel function of the first kind, order 0: the Kaiser window kernel.
f64 dsp_bessel_i0(f64 x);

// dB helpers. Amplitude ratios (20*log10), the convention of every level in
// this project: dBFS, dBTP, LUFS offsets.
#define DSP_LOG_ZERO (-745.0)  // log(smallest normal double), our -inf stand-in
#define DSP_DB_SILENCE (-200.0f)

f32 dsp_db_from_amp(f32 amplitude);  // 20*log10, DSP_DB_SILENCE at 0
f32 dsp_amp_from_db(f32 db);
f32 dsp_db_from_power(f32 power);  // 10*log10

#endif  // DSP_MATH_H

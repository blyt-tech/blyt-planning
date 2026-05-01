/* math.h stand-in for the Lua rv32emu port.
 * With LUA_32BITS, lua_Number is float and Lua calls the *f variants
 * (sinf, sqrtf, etc.).  We provide:
 *   - sqrtf via the rv32f `fsqrt.s` instruction (real)
 *   - fabs/fabsf via IEEE 754 sign-bit clear (real)
 *   - floor/ceil/trunc via integer truncation (real for the magnitudes Lua hits)
 *   - all transcendentals (sin/cos/exp/log/etc.) as STUBS that return 0.
 * Benchmarks chosen for this spike (mandelbrot, nbody, spectral-norm,
 * binarytrees, fannkuch, fasta, entity_update) only touch sqrtf/fabsf/floorf
 * in their hot paths, so the stubs are unreachable at runtime — they exist
 * only to satisfy the linker when lmathlib registers the full math library.
 */

#ifndef _MATH_H
#define _MATH_H

#define HUGE_VAL  (1e300 * 1e300)
#define HUGE_VALF (1e30f * 1e30f)
#define INFINITY  HUGE_VALF
#define NAN       __builtin_nanf("")
#define M_PI      3.14159265358979323846

/* float */
float  sqrtf (float);
float  fabsf (float);
float  floorf(float);
float  ceilf (float);
float  truncf(float);
float  fmodf (float, float);
float  modff (float, float *);
float  ldexpf(float, int);
float  frexpf(float, int *);
float  sinf  (float);
float  cosf  (float);
float  tanf  (float);
float  asinf (float);
float  acosf (float);
float  atanf (float);
float  atan2f(float, float);
float  expf  (float);
float  logf  (float);
float  log2f (float);
float  log10f(float);
float  powf  (float, float);
float  hypotf(float, float);

/* double — used only by varargs (printf-style) and a few luaconf paths. */
double sqrt  (double);
double fabs  (double);
double floor (double);
double ceil  (double);
double trunc (double);
double fmod  (double, double);
double modf  (double, double *);
double ldexp (double, int);
double frexp (double, int *);
double sin   (double);
double cos   (double);
double tan   (double);
double asin  (double);
double acos  (double);
double atan  (double);
double atan2 (double, double);
double exp   (double);
double log   (double);
double log2  (double);
double log10 (double);
double pow   (double, double);
double hypot (double, double);

#endif /* _MATH_H */

/* Spike D math.h — superset of spike-b's math.h shim.
 *
 * Same prototypes as spike-b/ports/rv32emu/include/math.h, plus
 * isnan / isinf / isfinite macros and a few extra constants needed by
 * the vendored musl float-precision transcendentals.  Spike D's
 * Makefile puts this on the include path BEFORE spike-b's, so any C
 * file that #include's <math.h> picks this version up.
 *
 * We do not implement sin/cos/exp/log/etc. inline here — those come
 * from libm-rv32.a (built from vendored musl 1.2.5 sources).  This file
 * is purely declarations + macros.
 */

#ifndef _MATH_H
#define _MATH_H

#define HUGE_VAL    (1e300 * 1e300)
#define HUGE_VALF   (1e30f * 1e30f)
#define HUGE_VALL   HUGE_VAL
#define INFINITY    HUGE_VALF
#define NAN         __builtin_nanf("")
#define M_PI        3.14159265358979323846
#define M_PI_2      1.57079632679489661923
#define M_PI_4      0.78539816339744830962
#define M_LN2       0.69314718055994530942
#define M_LOG2E     1.44269504088896340736
#define M_SQRT2     1.41421356237309504880

/* GCC builtins; deterministic and host-independent (operate on bit pattern). */
#define isnan(x)     __builtin_isnan(x)
#define isinf(x)     __builtin_isinf(x)
#define isfinite(x)  __builtin_isfinite(x)
#define signbit(x)   __builtin_signbit(x)

/* C99 evaluation-precision typedefs.  On RV32 with -fno-fast-math and
 * default FLT_EVAL_METHOD=0, both reduce to plain float/double (no excess
 * precision). */
typedef float  float_t;
typedef double double_t;

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
float  scalbnf(float, int);
double scalbn (double, int);
float  sinf  (float);
float  cosf  (float);
float  tanf  (float);
float  asinf (float);
float  acosf (float);
float  atanf (float);
float  atan2f(float, float);
float  expf  (float);
float  exp2f (float);
float  logf  (float);
float  log2f (float);
float  log10f(float);
float  powf  (float, float);
float  hypotf(float, float);
void   sincosf(float, float *, float *);

/* double — only used by varargs (printf-style) and a few luaconf paths;
 * lua_runtime.c provides simple double wrappers around the float versions. */
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

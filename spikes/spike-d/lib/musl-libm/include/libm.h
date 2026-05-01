/* Self-contained shim of musl's src/internal/libm.h for Spike D's
 * freestanding RV32IMFC build.
 *
 * musl's own libm.h pulls in src/internal/fp_arch.h and the system's
 * <endian.h>, neither of which we want to satisfy from a freestanding
 * RV32 cross-build.  The shim provides only what the float-precision
 * transcendentals we vendor actually use:
 *
 *   - asuint / asfloat / asuint64 / asdouble / GET_FLOAT_WORD / FORCE_EVAL
 *   - predict_true / predict_false
 *   - eval_as_float / eval_as_double
 *   - fp_barrier{f,} / fp_force_eval{f,}
 *   - WANT_ROUNDING / WANT_SNAN / TOINT_INTRINSICS toggles
 *   - hidden function declarations for __rem_pio2f / __sindf / __cosdf /
 *     __tandf and the __math_*f error helpers
 *
 * No long-double support, no signaling-NaN support, no fenv.
 */

#ifndef _LIBM_H
#define _LIBM_H

#include <stdint.h>
#include <float.h>
#include <math.h>

/* musl's libm.h declares helpers as `hidden`; this expands to the GCC
 * visibility attribute.  Spike D builds with `-fvisibility=hidden`-equivalent
 * by default for these helpers, but we don't enforce it — the keyword is
 * only used to mark the symbols, not to drive linker behaviour. */
#define hidden __attribute__((__visibility__("hidden")))

#define WANT_ROUNDING 1
#define WANT_SNAN     0
#define TOINT_INTRINSICS 0

#if WANT_SNAN
#error SNaN is unsupported
#else
#define issignalingf_inline(x) 0
#define issignaling_inline(x)  0
#endif

/* GCC __builtin_expect-based branch hints. */
#define predict_true(x)  __builtin_expect(!!(x), 1)
#define predict_false(x) __builtin_expect((x),   0)

/* Eval-as-T: pin a value at type T; under default GCC settings on RV32 with
 * -ffloat-store this is just an assignment. */
static inline float  eval_as_float (float  x) { float  y = x; return y; }
static inline double eval_as_double(double x) { double y = x; return y; }

/* fp_barrier: opaque-to-the-optimizer identity (volatile copy). */
static inline float  fp_barrierf(float  x) { volatile float  y = x; return y; }
static inline double fp_barrier (double x) { volatile double y = x; return y; }

/* fp_force_eval: ensure side-effecting FP work is materialized. */
static inline void fp_force_evalf(float  x) { volatile float  y; y = x; (void)y; }
static inline void fp_force_eval (double x) { volatile double y; y = x; (void)y; }

#define FORCE_EVAL(x) do {                       \
    if (sizeof(x) == sizeof(float))  fp_force_evalf((float)(x));  \
    else                             fp_force_eval ((double)(x)); \
} while (0)

/* Bit-cast helpers. */
#define asuint(f)   ((union { float  _f; uint32_t _i; }){ ._f = (f) })._i
#define asfloat(i)  ((union { uint32_t _i; float  _f; }){ ._i = (i) })._f
#define asuint64(f) ((union { double _f; uint64_t _i; }){ ._f = (f) })._i
#define asdouble(i) ((union { uint64_t _i; double _f; }){ ._i = (i) })._f

#define EXTRACT_WORDS(hi, lo, d) do { \
    uint64_t __u = asuint64(d);       \
    (hi) = (uint32_t)(__u >> 32);     \
    (lo) = (uint32_t)(__u);           \
} while (0)

#define GET_HIGH_WORD(hi, d) do { (hi) = (uint32_t)(asuint64(d) >> 32); } while (0)
#define GET_LOW_WORD(lo, d)  do { (lo) = (uint32_t)(asuint64(d));       } while (0)

#define INSERT_WORDS(d, hi, lo) do { \
    (d) = asdouble(((uint64_t)(hi) << 32) | (uint32_t)(lo)); \
} while (0)

#define GET_FLOAT_WORD(w, f) do { (w) = asuint(f); } while (0)
#define SET_FLOAT_WORD(f, w) do { (f) = asfloat(w); } while (0)

/* Hidden helpers used by the float-precision transcendentals we vendor.
 * Declarations only; the implementations come from musl's src/math/__*.c. */
hidden int    __rem_pio2_large(double*, double*, int, int, int);
hidden int    __rem_pio2f(float, double *);
hidden float  __sindf(double);
hidden float  __cosdf(double);
hidden float  __tandf(double, int);
hidden float  __expo2f(float, float);

hidden float  __math_xflowf  (uint32_t, float);
hidden float  __math_uflowf  (uint32_t);
hidden float  __math_oflowf  (uint32_t);
hidden float  __math_divzerof(uint32_t);
hidden float  __math_invalidf(float);

#endif /* _LIBM_H */

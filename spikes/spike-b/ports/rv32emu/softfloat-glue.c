/*
 * libgcc-name glue for Berkeley SoftFloat 3.
 *
 * GCC inserts calls to __subdf3, __muldf3, __ltdf2, etc. for double-precision
 * operations on RV32IMFC (which has no D extension). The Ubuntu rv64
 * libgcc.a is ELFCLASS64 and can't be linked, so we provide these symbols
 * here, forwarding to softfloat's f64_* API.
 *
 * Calling convention note: on rv32 ilp32f, `double` is passed in a0+a1 and
 * softfloat's `float64_t` (a struct wrapping a uint64) is passed the same way,
 * so a union bit-cast is sufficient bridge — no marshalling overhead.
 */
#include <stdint.h>
#include "softfloat.h"

static inline float64_t d2f64(double d)
{
    union { double d; float64_t f; } u = { .d = d };
    return u.f;
}
static inline double f642d(float64_t f)
{
    union { double d; float64_t f; } u = { .f = f };
    return u.d;
}

/* ── arithmetic ──────────────────────────────────────────────────────────── */

double __adddf3(double a, double b) { return f642d(f64_add(d2f64(a), d2f64(b))); }
double __subdf3(double a, double b) { return f642d(f64_sub(d2f64(a), d2f64(b))); }
double __muldf3(double a, double b) { return f642d(f64_mul(d2f64(a), d2f64(b))); }
double __divdf3(double a, double b) { return f642d(f64_div(d2f64(a), d2f64(b))); }
double __negdf2(double a)
{
    union { double d; uint64_t u; } u = { .d = a };
    u.u ^= (uint64_t)1 << 63;
    return u.d;
}

/* ── comparisons ─────────────────────────────────────────────────────────── */
/* libgcc convention: callers test the sign of the return.
 *   __ltdf2 < 0 ⇔ a < b ; for unordered, return non-negative.
 *   __gtdf2 > 0 ⇔ a > b ; for unordered, return non-positive.
 *   __eqdf2 == 0 ⇔ a == b ; for !=, non-zero.
 */
int __ltdf2(double a, double b)
{
    float64_t fa = d2f64(a), fb = d2f64(b);
    if (f64_lt(fa, fb)) return -1;
    if (f64_eq(fa, fb)) return 0;
    return 1;       /* a > b OR unordered */
}
int __gtdf2(double a, double b)
{
    float64_t fa = d2f64(a), fb = d2f64(b);
    if (f64_lt(fb, fa)) return 1;
    if (f64_eq(fa, fb)) return 0;
    return -1;      /* a < b OR unordered */
}
int __ledf2(double a, double b) { return __ltdf2(a, b); }
int __gedf2(double a, double b) { return __gtdf2(a, b); }
int __eqdf2(double a, double b) { return f64_eq(d2f64(a), d2f64(b)) ? 0 : 1; }
int __nedf2(double a, double b) { return __eqdf2(a, b); }
int __unorddf2(double a, double b)
{
    /* Returns non-zero if either operand is NaN (i.e., unordered). */
    float64_t fa = d2f64(a), fb = d2f64(b);
    return !(f64_le(fa, fb) || f64_lt(fb, fa));
}

/* ── int/double conversions ──────────────────────────────────────────────── */

double __floatsidf(int32_t i)    { return f642d(i32_to_f64(i)); }
double __floatunsidf(uint32_t u) { return f642d(ui32_to_f64(u)); }
double __floatdidf(int64_t i)    { return f642d(i64_to_f64(i)); }
double __floatundidf(uint64_t u) { return f642d(ui64_to_f64(u)); }

int32_t __fixdfsi(double a)        { return f64_to_i32_r_minMag(d2f64(a), 0); }
uint32_t __fixunsdfsi(double a)    { return f64_to_ui32_r_minMag(d2f64(a), 0); }
int64_t __fixdfdi(double a)        { return f64_to_i64_r_minMag(d2f64(a), 0); }
uint64_t __fixunsdfdi(double a)    { return f64_to_ui64_r_minMag(d2f64(a), 0); }

/* ── single ↔ double conversions ─────────────────────────────────────────── */

double __extendsfdf2(float a)
{
    union { float f; uint32_t u; } u = { .f = a };
    union { uint32_t v; float32_t f; } x = { .v = u.u };
    return f642d(f32_to_f64(x.f));
}
float __truncdfsf2(double a)
{
    float32_t r = f64_to_f32(d2f64(a));
    union { uint32_t v; float f; } u = { .v = r.v };
    return u.f;
}

/* ── 64-bit unsigned division (used by our timing math) ──────────────────── */
/* The Ubuntu rv64 libgcc has no rv32 __udivdi3. CoreMark and our port don't
 * need it once HAS_FLOAT=1 routes time math through doubles, but keep it
 * as a portable shift-subtract implementation in case it slips back in. */
uint64_t __udivdi3(uint64_t n, uint64_t d)
{
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; --i) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= (uint64_t)1 << i; }
    }
    return q;
}
uint64_t __umoddi3(uint64_t n, uint64_t d)
{
    uint64_t q = __udivdi3(n, d);
    return n - q * d;
}

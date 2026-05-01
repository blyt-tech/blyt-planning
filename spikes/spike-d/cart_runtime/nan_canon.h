/* Canonical NaN policy for the determinism spike.
 *
 * ADR-0007 names NaN canonicalization as a load-bearing policy: any field
 * written into the state buffer that may be NaN must be replaced with a
 * single, fixed bit pattern before serialization.  Otherwise two valid
 * runs on different hosts could each produce a "valid NaN" — say
 * 0x7fc00000 vs. 0x7fc80000 — and the digest would diverge for a
 * non-bug.
 *
 * The canonical pattern here is the IEEE 754 single-precision quiet NaN
 * with payload zero: sign=0, exp=0xff, frac=0x400000 → 0x7fc00000.
 * This is what most x86-64 and arm64 FPUs mint by default, so the
 * canonicalization is a no-op for "normal" host outputs and only
 * matters when something exotic appears.
 */

#ifndef CART_RUNTIME_NAN_CANON_H
#define CART_RUNTIME_NAN_CANON_H

#include <stdint.h>

#define CANONICAL_QNAN_BITS 0x7fc00000u

static inline float canonicalize_nanf(float x)
{
    union { float f; uint32_t u; } v = { .f = x };
    uint32_t exp_bits = (v.u >> 23) & 0xffu;
    uint32_t frac     = v.u & 0x7fffffu;
    if (exp_bits == 0xff && frac != 0) {
        v.u = CANONICAL_QNAN_BITS;
    }
    return v.f;
}

#endif /* CART_RUNTIME_NAN_CANON_H */

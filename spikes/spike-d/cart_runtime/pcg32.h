/* PCG32 (Permuted Congruential Generator, 32-bit output, 64-bit state).
 *
 * Reference: https://www.pcg-random.org/  Melissa O'Neill, 2014.
 * The recurrence and output permutation are reproduced exactly from the
 * published reference C implementation.  Pinning this implementation
 * (and the seed) into the cart means RNG state is bit-identical across
 * any host that runs the cart binary — that's the property we need for
 * cross-host digest comparison.
 *
 * The state is kept inside frame_state so that the digest covers RNG
 * progress alongside the rest of the per-frame work.
 */

#ifndef CART_RUNTIME_PCG32_H
#define CART_RUNTIME_PCG32_H

#include <stdint.h>

/* Default seed and stream constants for the determinism spike.
 * SEED is arbitrary; INC is the published default odd-stream constant. */
#define PCG_DEFAULT_STATE  0xfc320001ULL
#define PCG_DEFAULT_INC    0x14057b7ef767814fULL

typedef struct pcg32 {
    uint64_t state;
    uint64_t inc;     /* must be odd; low bit forced on by pcg32_seed */
} pcg32_t;

static inline void pcg32_seed(pcg32_t *r, uint64_t state, uint64_t inc)
{
    r->state = 0u;
    r->inc   = (inc << 1u) | 1u;
    /* one step to mix the initial seed */
    r->state = r->state * 6364136223846793005ULL + r->inc;
    r->state += state;
    r->state = r->state * 6364136223846793005ULL + r->inc;
}

static inline uint32_t pcg32_next(pcg32_t *r)
{
    uint64_t old = r->state;
    r->state = old * 6364136223846793005ULL + r->inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot        = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
}

/* Float in [0, 1).  Uses upper 24 bits of the PCG32 output, scaled by
 * 2^-24, so the result is the same on every host (no double-precision
 * intermediate, no host-specific rounding). */
static inline float pcg32_unit_float(pcg32_t *r)
{
    uint32_t u = pcg32_next(r) >> 8;             /* 24 bits */
    return (float)u * (1.0f / 16777216.0f);      /* 2^-24 */
}

#endif /* CART_RUNTIME_PCG32_H */

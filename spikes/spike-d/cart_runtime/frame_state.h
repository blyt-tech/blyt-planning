/* Per-frame state buffer for Spike D.
 *
 * The struct layout is the digest's input.  Field order, padding, and
 * size all matter — any change here changes every digest the spike
 * produces.  Treat it as a wire format: do not reorder fields, do not
 * insert padding, do not change types without thinking through how
 * NaN canonicalization and PCG32 state propagate into the hash.
 *
 * `__attribute__((packed))` removes inter-field padding so the digest
 * is the byte image of the named fields and nothing else.  That makes
 * the hash stable across compilers and hosts (no "did GCC pad differently
 * here" surprises).
 *
 * MAX_MOBS is a fixed upper bound; carts that use fewer mobs leave
 * the trailing entries zeroed.  Zero-initialization of frame_state is
 * required at cart entry to prevent uninitialised stack/BSS bytes from
 * leaking into the digest.
 */

#ifndef CART_RUNTIME_FRAME_STATE_H
#define CART_RUNTIME_FRAME_STATE_H

#include <stdint.h>
#include "pcg32.h"

#define FRAME_STATE_MAX_MOBS 64

typedef struct __attribute__((packed)) {
    float    x;
    float    y;
    float    vx;
    float    vy;
    uint32_t state;
} frame_state_mob;

typedef struct __attribute__((packed)) {
    uint32_t        frame;
    uint64_t        rng_state;
    uint64_t        rng_inc;
    float           accum_sin;
    float           accum_cos;
    float           accum_sqrt;
    float           accum_misc;
    frame_state_mob mobs[FRAME_STATE_MAX_MOBS];
} frame_state_t;

#endif /* CART_RUNTIME_FRAME_STATE_H */

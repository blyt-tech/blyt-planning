/* Spike K — screen shake region (ADR-0051).
 *
 * `runtime_screen_shake_t` is a 4-field POD struct.  When `remaining_frames`
 * is positive, the runtime computes a per-frame `(dx, dy)` offset from
 * `(frame_count, seed, intensity)` via the deterministic noise function
 * `screen_shake_offset()` defined here, then decrements
 * `remaining_frames` and scales `intensity` by `decay`.
 *
 * The region's bytes round-trip via the standard save/load mechanism.
 * Carts that wish to verify shake state survives a save (Stage 5)
 * accumulate the per-frame offsets into `frame_state.accum_misc`; any
 * cross-host divergence shows up in the digest stream.
 */

#ifndef CART_RUNTIME_REGION_SCREEN_SHAKE_H
#define CART_RUNTIME_REGION_SCREEN_SHAKE_H

#include <stdint.h>
#include "runtime_tracked.h"

typedef struct __attribute__((packed)) {
    int32_t  remaining_frames;
    float    intensity;
    float    decay;
    int32_t  seed;
} runtime_screen_shake_t;

extern const runtime_tracked_region_t region_screen_shake;

runtime_screen_shake_t *region_screen_shake_get(void);

/* Public API. */
void  blyt_screen_shake(int32_t frames, float intensity);          /* trigger */
void  screen_shake_tick(uint32_t frame, float *dx, float *dy);     /* per-frame offset */

#endif /* CART_RUNTIME_REGION_SCREEN_SHAKE_H */

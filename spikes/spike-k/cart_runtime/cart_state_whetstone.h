/* Spike K — whetstone's cart_state_t.
 *
 * Whetstone's main() carries a small set of f32 module-local accumulators
 * (a, b, c, d, e1[4], t, u) that participate in every frame's arithmetic.
 * Spike D's whetstone.c keeps them as `main` locals — fine because that
 * cart runs straight through.  Spike K's save/load split breaks the run
 * across two processes, so any state that survives across frames must
 * live in a tracked region.
 *
 * cart_state_whetstone_t is a tiny POD struct that holds exactly those
 * locals.  It mirrors what an ADR-0009 packer would emit: every field has
 * a stable name, type, and offset; the layout descriptor for layout_hash
 * names them in declaration order.  Real production carts will declare
 * cart_state_t per cart; spike K hand-authors this descriptor as a stand-
 * in for the packer (PLAN.md § "Cart-declared state buffers are described
 * by a hand-authored layout descriptor").
 */

#ifndef CART_RUNTIME_CART_STATE_WHETSTONE_H
#define CART_RUNTIME_CART_STATE_WHETSTONE_H

#include <stdint.h>
#include "runtime_tracked.h"

typedef struct __attribute__((packed)) {
    float a;
    float b;
    float c;
    float d;
    float e1[4];
    float t;
    float u;
} cart_state_whetstone_t;

extern const runtime_tracked_region_t region_cart_state_whetstone;

cart_state_whetstone_t *cart_state_whetstone_get(void);

#endif /* CART_RUNTIME_CART_STATE_WHETSTONE_H */

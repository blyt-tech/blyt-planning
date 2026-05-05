/* Spike K Stage 2 — cart_state for the det_lua_simple Lua workload.
 *
 * Eight entities × (x, y, a) f32 — small enough to keep the layout
 * description compact, big enough to exercise an array-of-struct
 * region across hosts.  The fields stay in cart_state instead of
 * Lua tables specifically so that save/load can round-trip them
 * via the standard region mechanism — Stage 2's load-bearing
 * property (PLAN.md § Stage 2 step 8 — "frame_state_t is the
 * digest's input; cart_state_t is the persistent simulation state").
 *
 * Production carts will have packer-generated layouts (ADR-0009);
 * this hand-authored descriptor mirrors the packer's output shape.
 */

#ifndef CART_RUNTIME_CART_STATE_LUA_SIMPLE_H
#define CART_RUNTIME_CART_STATE_LUA_SIMPLE_H

#include <stdint.h>
#include "runtime_tracked.h"

#define LUA_SIMPLE_NUM_ENTITIES 8

typedef struct __attribute__((packed)) {
    struct __attribute__((packed)) {
        float x;
        float y;
        float a;
    } entities[LUA_SIMPLE_NUM_ENTITIES];
} cart_state_lua_simple_t;

extern const runtime_tracked_region_t region_cart_state_lua_simple;

cart_state_lua_simple_t *cart_state_lua_simple_get(void);

#endif /* CART_RUNTIME_CART_STATE_LUA_SIMPLE_H */

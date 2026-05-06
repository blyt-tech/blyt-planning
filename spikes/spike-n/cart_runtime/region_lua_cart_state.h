/* Spike N — Lua cart POD state region.
 *
 * The Lua cart's C-side POD buffer evolves through edits l2-l4.  Compile
 * with -DLUA_STATE_VERSION=N to select the appropriate struct version.
 *
 * Version history:
 *   v0  baseline (l1):  { i32 score; i32 step; i32 combo; }             12 bytes
 *   v2  after l2 (add): { i32 score; i32 step; i32 combo; i32 bonus; }  16 bytes
 *   v3  after l3 (rem): { i32 score; i32 step; i32 combo; }             12 bytes (same as v0, diff hash)
 *   v4  after l4 (ret): { i32 score; i32 step; f32 combo_mult; }        12 bytes
 *
 * The `score`, `step`, `combo` fields map to Lua-side variables that the
 * Lua cart reads/writes each frame via console.set_cart_state /
 * console.get_cart_state bindings declared in lua_det_bindings_n.c.
 */

#ifndef CART_RUNTIME_REGION_LUA_CART_STATE_H
#define CART_RUNTIME_REGION_LUA_CART_STATE_H

#include <stdint.h>
#include "runtime_tracked.h"
#include "migrate.h"

#ifndef LUA_STATE_VERSION
#  define LUA_STATE_VERSION 0
#endif

#if LUA_STATE_VERSION == 0
typedef struct __attribute__((packed)) {
    int32_t  score;
    int32_t  step;
    int32_t  combo;
} lua_cart_state_t;

#elif LUA_STATE_VERSION == 2
typedef struct __attribute__((packed)) {
    int32_t  score;
    int32_t  step;
    int32_t  combo;
    int32_t  bonus;
} lua_cart_state_t;

#elif LUA_STATE_VERSION == 3
typedef struct __attribute__((packed)) {
    int32_t  score;
    int32_t  step;
    int32_t  combo;
} lua_cart_state_t;

#elif LUA_STATE_VERSION == 4
typedef struct __attribute__((packed)) {
    int32_t  score;
    int32_t  step;
    float    combo_mult;
} lua_cart_state_t;

#else
#  error "Unknown LUA_STATE_VERSION — must be 0, 2, 3, or 4"
#endif

extern const runtime_tracked_region_t region_lua_cart_state;

lua_cart_state_t *region_lua_cart_state_get(void);

/* All-version layout descriptors (defined in region_lua_cart_state.c). */
extern const mlayout_t lua_cart_state_layout_v0;
extern const mlayout_t lua_cart_state_layout_v2;
extern const mlayout_t lua_cart_state_layout_v3;
extern const mlayout_t lua_cart_state_layout_v4;

/* on_retype callback for l4: i32 combo → f32 combo_mult (cast+scale). */
int lua_cart_state_retype_combo_to_mult(const uint8_t *old_field, mfield_type_t old_type,
                                         uint8_t *new_field, mfield_type_t new_type);

#endif /* CART_RUNTIME_REGION_LUA_CART_STATE_H */

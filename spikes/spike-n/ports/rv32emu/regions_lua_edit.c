/* Spike N Stages 3-5 — Lua edit cart region registry.
 *
 * Compile with -DLUA_EDIT_NUM=N -DLUA_EDIT_SIDE_POST=0/1
 * and -DLUA_STATE_VERSION=V (matching the struct version for this
 * edit + side combination).
 *
 * Migration descriptors are declared for the POST (load) ELFs of l2-l4:
 *   l2 POST: v0 → v2 (add bonus field, zero-init)
 *   l3 POST: v2 → v3 (remove bonus field)
 *   l4 POST: v3 → v4 (retype combo i32 → f32 combo_mult)
 *
 * l1 and l5 POST have no struct change (same layout_hash as PRE);
 * save_state_load_migrate falls through to save_state_load.
 */

#include "../../cart_runtime/runtime_tracked.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/cart_state_lua_simple.h"
#include "../../cart_runtime/region_persistent_scripts.h"
#include "../../cart_runtime/region_lua_cart_state.h"
#include "../../cart_runtime/save_state_n.h"

const runtime_tracked_region_t *runtime_tracked_regions[] = {
    &region_frame_state,
    &region_cart_state_lua_simple,
    &region_persistent_scripts,
    &region_lua_cart_state,
};

const uint32_t runtime_tracked_region_count =
    sizeof(runtime_tracked_regions) / sizeof(runtime_tracked_regions[0]);

/* ── Migration descriptors (POST side, schema-changing edits only) ── */

#if defined(LUA_EDIT_SIDE_POST) && LUA_EDIT_SIDE_POST == 1

#if LUA_EDIT_NUM == 2
/* l2 POST: v0 (12 bytes) → v2 (16 bytes, add bonus zero-init) */
static const migrate_region_t migrate_lua_l2 = {
    "lua_cart_state",
    12,
    &lua_cart_state_layout_v0,
    &lua_cart_state_layout_v2,
    0
};
const migrate_region_t *migrate_regions[] = { &migrate_lua_l2 };
uint32_t                migrate_region_count = 1;

#elif LUA_EDIT_NUM == 3
/* l3 POST: v2 (16 bytes) → v3 (12 bytes, drop bonus) */
static const migrate_region_t migrate_lua_l3 = {
    "lua_cart_state",
    16,
    &lua_cart_state_layout_v2,
    &lua_cart_state_layout_v3,
    0
};
const migrate_region_t *migrate_regions[] = { &migrate_lua_l3 };
uint32_t                migrate_region_count = 1;

#elif LUA_EDIT_NUM == 4
/* l4 POST: v3 (12 bytes) → v4 (12 bytes, retype combo i32→f32) */
static const migrate_region_t migrate_lua_l4 = {
    "lua_cart_state",
    12,
    &lua_cart_state_layout_v3,
    &lua_cart_state_layout_v4,
    lua_cart_state_retype_combo_to_mult
};
const migrate_region_t *migrate_regions[] = { &migrate_lua_l4 };
uint32_t                migrate_region_count = 1;

#endif  /* LUA_EDIT_NUM switch */

#endif  /* LUA_EDIT_SIDE_POST */

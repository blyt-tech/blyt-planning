/* Spike N Stage 1 — Lua no-op cart region registry.
 *
 * Registers frame_state + cart_state_lua_simple + persistent_scripts
 * (same set as spike-m's cutscene_linear) and lua_cart_state (new in N).
 */

#include "../../cart_runtime/runtime_tracked.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/cart_state_lua_simple.h"
#include "../../cart_runtime/region_persistent_scripts.h"
#include "../../cart_runtime/region_lua_cart_state.h"

const runtime_tracked_region_t *runtime_tracked_regions[] = {
    &region_frame_state,
    &region_cart_state_lua_simple,
    &region_persistent_scripts,
    &region_lua_cart_state,
};

const uint32_t runtime_tracked_region_count =
    sizeof(runtime_tracked_regions) / sizeof(runtime_tracked_regions[0]);

/* Spike M Stage 3 — det_two_scripts cart region registry.
 *
 * Same region set as Stages 1-2: frame_state + cart_state_lua_simple
 * + persistent_scripts.  The two scripts each occupy a slot in
 * persistent_scripts; per-frame digest folds in fields from both
 * via console.add_misc, so cross-pollination at the slot level
 * surfaces as a localised digest divergence.
 */

#include "../../cart_runtime/runtime_tracked.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/cart_state_lua_simple.h"
#include "../../cart_runtime/region_persistent_scripts.h"

const runtime_tracked_region_t *runtime_tracked_regions[] = {
    &region_frame_state,
    &region_cart_state_lua_simple,
    &region_persistent_scripts,
};

const uint32_t runtime_tracked_region_count =
    sizeof(runtime_tracked_regions) / sizeof(runtime_tracked_regions[0]);

/* Spike M Stage 2 — det_cutscene_branched cart region registry.
 *
 * Identical region set to det_cutscene_linear: frame_state +
 * cart_state_lua_simple + persistent_scripts.  Layout-hash gate
 * accepts buffers from the linear cart since the regions match —
 * but the workloads are different, so the slot bytes will not.
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

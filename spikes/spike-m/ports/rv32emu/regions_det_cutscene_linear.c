/* Spike M Stage 1 — det_cutscene_linear cart region registry.
 *
 * Registers frame_state, cart_state_lua_simple (kept in the registry so
 * the digest fold's accumulator is reset cleanly on every commit and the
 * region's bytes round-trip via save/load — even though Stage 1's workload
 * doesn't touch the entity rows; future stages will), and the
 * persistent_scripts slot table.
 *
 * Order matters: it determines the body emit order in the save buffer
 * and feeds the layout-hash gate.  Adding a region kind reorders this
 * array; existing buffers become unloadable, which is the gate's
 * intended behaviour.
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

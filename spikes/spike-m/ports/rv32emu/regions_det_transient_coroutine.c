/* Spike M Stage 5 — det_transient_coroutine cart region registry.
 *
 * No persistent_scripts region — this cart only uses transient
 * coroutines.  frame_state + cart_state_lua_simple suffice.  Excluding
 * persistent_scripts also confirms that managed scripts and transient
 * tracking are independent: a cart with no managed scripts still gets
 * the correct boundary-cross behavior via the cart-side mark hook.
 */

#include "../../cart_runtime/runtime_tracked.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/cart_state_lua_simple.h"

const runtime_tracked_region_t *runtime_tracked_regions[] = {
    &region_frame_state,
    &region_cart_state_lua_simple,
};

const uint32_t runtime_tracked_region_count =
    sizeof(runtime_tracked_regions) / sizeof(runtime_tracked_regions[0]);

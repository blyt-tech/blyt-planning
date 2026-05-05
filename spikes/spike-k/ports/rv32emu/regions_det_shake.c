/* Spike K Stage 5 — det_shake cart region registry.
 *
 * Body emit order matches PLAN.md § "Save state is the byte image of
 * all tracked regions, in a fixed order":
 *   1. frame_state         (digest input + RNG)
 *   2. screen_shake        (ADR-0051 — 4-field POD)
 *
 * det_shake doesn't use the audio voice-end queue, the cart_state
 * region, or coroutine save blobs — those are added by other carts'
 * registries.  Order across registries is consistent for any region a
 * cart does include.
 */

#include "../../cart_runtime/runtime_tracked.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_screen_shake.h"

const runtime_tracked_region_t *runtime_tracked_regions[] = {
    &region_frame_state,
    &region_screen_shake,
};

const uint32_t runtime_tracked_region_count =
    sizeof(runtime_tracked_regions) / sizeof(runtime_tracked_regions[0]);

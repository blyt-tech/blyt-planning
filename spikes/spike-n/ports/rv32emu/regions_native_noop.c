/* Spike N Stage 1 — no-op native cart region registry.
 *
 * Registers frame_state and native_state (v0).
 * This region list is used by both the noop ELFs and as the PRE (save)
 * variant of native edits n1 and n2 (which have no struct change).
 */

#include "../../cart_runtime/runtime_tracked.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_native_state.h"

const runtime_tracked_region_t *runtime_tracked_regions[] = {
    &region_frame_state,
    &region_native_state,
};

const uint32_t runtime_tracked_region_count =
    sizeof(runtime_tracked_regions) / sizeof(runtime_tracked_regions[0]);

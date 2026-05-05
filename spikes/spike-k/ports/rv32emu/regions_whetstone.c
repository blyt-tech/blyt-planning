/* Spike K — whetstone cart region registry.
 *
 * Stage 1 floor case: the only tracked region is frame_state.  Later
 * stages (audio, screen-shake, coroutine) introduce additional regions;
 * each cart that uses them adds them to its own registry array in
 * declaration order matching PLAN.md's body emit order:
 *
 *   1. frame_state
 *   2. screen_shake
 *   3. voice_end_queue
 *   4. cart_state (per declared layout)
 *   5. coroutine_save_blob
 *
 * The registry is a const array of pointers; the order of pointers is
 * the on-wire body order.  The layout_hash gate makes order changes
 * an explicit reject — no silent compatibility breaks.
 */

#include "../../cart_runtime/runtime_tracked.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/cart_state_whetstone.h"

const runtime_tracked_region_t *runtime_tracked_regions[] = {
    &region_frame_state,
    &region_cart_state_whetstone,
};

const uint32_t runtime_tracked_region_count =
    sizeof(runtime_tracked_regions) / sizeof(runtime_tracked_regions[0]);

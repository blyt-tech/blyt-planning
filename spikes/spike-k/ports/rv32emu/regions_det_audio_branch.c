/* Spike K Stage 3 — det_audio_branch cart region registry.
 *
 * Body emit order matches PLAN.md:
 *   1. frame_state
 *   2. voice_end_queue   (audio voice-end region per ADR-0106)
 */

#include "../../cart_runtime/runtime_tracked.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_voice_end_queue.h"

const runtime_tracked_region_t *runtime_tracked_regions[] = {
    &region_frame_state,
    &region_voice_end_queue,
};

const uint32_t runtime_tracked_region_count =
    sizeof(runtime_tracked_regions) / sizeof(runtime_tracked_regions[0]);

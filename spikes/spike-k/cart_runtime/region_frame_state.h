/* Spike K — frame_state region.
 *
 * Wraps the spike-d `frame_state_t` (the digest input + per-frame work
 * accumulators) as a save-state tracked region.  This is the floor case
 * in PLAN.md Stage 1 — a struct already proven byte-equal across hosts
 * by Spike D, exercised here through the new save/load mechanism.
 *
 * The single global instance lives here; the cart code reaches it via
 * `region_frame_state_get()`.  Spike D's lua_det_bindings.c kept its
 * own static `g_fs`; in spike-K the same struct moves here so the
 * region's save/load can refer to a single source of truth.
 */

#ifndef CART_RUNTIME_REGION_FRAME_STATE_H
#define CART_RUNTIME_REGION_FRAME_STATE_H

#include "frame_state.h"
#include "runtime_tracked.h"

extern const runtime_tracked_region_t region_frame_state;

frame_state_t *region_frame_state_get(void);

#endif /* CART_RUNTIME_REGION_FRAME_STATE_H */

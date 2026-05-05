/* Spike K — voice-end queue region (ADR-0106).
 *
 * Two pieces of state, both serialized verbatim:
 *
 *   • `logical_view_bits` — a 64-bit bitmap.  Bit `i` is set iff voice
 *     handle `i` is in the cart-observable "is_playing" view.  Cart-
 *     visible state.  Mutated only at the start of an update tick when
 *     the FIFO is drained.
 *
 *   • `pending[]` — a small FIFO of voice-end events captured at the
 *     end of the most recent frame, before the next update applies
 *     them.  Cart-invisible state until the next update; serialized
 *     verbatim if a save lands between frames.
 *
 * Saves between frames may be taken at either of two boundaries:
 *
 *   (post-application) — start of frame N, just after pending was
 *   applied to logical_view_bits.  pending is empty.
 *
 *   (pending-application) — end of frame N, just after the synthetic
 *   mixer reported events.  pending is non-empty; logical_view_bits has
 *   not yet been updated.
 *
 * Both must round-trip — see PLAN.md Stage 3 step 17.
 */

#ifndef CART_RUNTIME_REGION_VOICE_END_QUEUE_H
#define CART_RUNTIME_REGION_VOICE_END_QUEUE_H

#include <stdint.h>
#include "runtime_tracked.h"

#define VOICE_END_PENDING_MAX 8

typedef struct __attribute__((packed)) {
    uint32_t frame;
    uint8_t  handle;
    uint8_t  kind;       /* 0 = voice_end, 1 = music_end (reserved) */
} voice_end_event_t;

typedef struct __attribute__((packed)) {
    uint64_t            logical_view_bits;
    uint8_t             pending_count;
    voice_end_event_t   pending[VOICE_END_PENDING_MAX];
} runtime_voice_end_queue_t;

extern const runtime_tracked_region_t region_voice_end_queue;

runtime_voice_end_queue_t *region_voice_end_queue_get(void);

/* Cart-side helpers. */
void  voice_start(uint8_t handle);                                /* set bit + clear pending for handle */
int   voice_is_playing(uint8_t handle);                           /* read logical_view_bits */
void  voice_end_apply_pending(void);                              /* start-of-update; drain FIFO into bits */
void  voice_end_record(uint32_t frame, uint8_t handle, uint8_t kind); /* end-of-frame; called by mixer */

#endif /* CART_RUNTIME_REGION_VOICE_END_QUEUE_H */

/* Spike K — synthetic deterministic mixer (ADR-0106 stand-in).
 *
 * Spike K does not link an audible mixer.  ADR-0106 already establishes
 * that the audible mixer is not bit-identical across hosts; the cart-
 * observable behaviour (logical view, `is_playing` answers) comes from
 * the recorded voice-end events, not from the mixer.  The synthetic
 * mixer's deterministic schedule IS the recorded events; cross-host
 * portability turns on those events surviving the round trip.
 *
 * The schedule is per-cart, declared statically at cart compile time.
 * The mixer reports a voice-end event for any voice whose
 * `ends_at_frame == current_frame` at end-of-frame.  The runtime
 * (region_voice_end_queue.c) appends each event to its `pending` FIFO;
 * the cart applies the FIFO at the start of the next update.
 */

#ifndef LIB_SYNTHETIC_MIXER_H
#define LIB_SYNTHETIC_MIXER_H

#include <stdint.h>

typedef struct {
    uint8_t  handle;          /* voice handle (1..63 — 0 reserved) */
    uint32_t ends_at_frame;
} synthetic_mixer_event_t;

/* The cart provides one global schedule via this declaration.  Order
 * within the schedule does not matter — the mixer scans for events
 * matching the current frame each tick. */
extern const synthetic_mixer_event_t synthetic_mixer_schedule[];
extern const uint32_t                synthetic_mixer_schedule_count;

/* Called once at cart entry — sets the mixer's internal frame counter
 * to 0.  After save_state_load() the runtime resyncs the mixer to the
 * restored frame_state.frame value via synthetic_mixer_resync(). */
void synthetic_mixer_init(void);
void synthetic_mixer_resync(uint32_t frame);

/* End-of-frame reporting.  Walks the schedule, appends a voice-end
 * event to the runtime voice-end queue for every (handle, frame) match. */
void synthetic_mixer_report_end_of_frame(uint32_t frame);

#endif /* LIB_SYNTHETIC_MIXER_H */

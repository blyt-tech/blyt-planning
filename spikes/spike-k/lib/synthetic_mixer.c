/* Spike K — synthetic mixer.  See header for rationale.
 *
 * The mixer is stateless apart from a frame counter; the schedule is a
 * const array provided by the cart's compilation unit.  At end-of-frame,
 * walk the schedule for any (handle, frame) pair matching the current
 * frame and append a voice-end event to the runtime queue.
 */

#include <stdint.h>

#include "synthetic_mixer.h"
#include "../cart_runtime/region_voice_end_queue.h"

static uint32_t g_mixer_frame;

void synthetic_mixer_init(void)        { g_mixer_frame = 0; }
void synthetic_mixer_resync(uint32_t f){ g_mixer_frame = f; }

void synthetic_mixer_report_end_of_frame(uint32_t frame)
{
    g_mixer_frame = frame;
    for (uint32_t i = 0; i < synthetic_mixer_schedule_count; i++) {
        if (synthetic_mixer_schedule[i].ends_at_frame == frame) {
            voice_end_record(frame,
                             synthetic_mixer_schedule[i].handle,
                             /*kind=*/0u);
        }
    }
}

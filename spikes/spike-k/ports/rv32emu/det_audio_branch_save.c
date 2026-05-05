/* Spike K Stage 3 — det_audio_branch_save.elf.
 *
 * Two voices started at frame 0 (handles 1 and 2).  Synthetic mixer
 * schedule: handle 1 ends at frame 5, handle 2 ends at frame 12.
 *
 * Each frame, accumulate `frame_state.accum_misc +=
 *   (is_playing(1) ? 1.0 : 10.0) + (is_playing(2) ? 100.0 : 1000.0)`
 * so the digest visibly diverges if `is_playing` answers diverge by
 * even one frame (PLAN.md Stage 3 step 15).
 *
 * Save points (selected via argv[1]):
 *   • default 11   — post-application save: pending FIFO empty,
 *                    logical_view_bits = 0b10 (handle 2 still playing).
 *   • 5            — pending-application save: end of frame 5 has
 *                    just queued voice 1's end into the pending FIFO,
 *                    logical_view_bits still 0b11 (will become 0b10 on
 *                    next update).  PLAN.md Stage 3 step 17 names this
 *                    boundary as the test for FIFO round-trip.
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_voice_end_queue.h"
#include "../../cart_runtime/save_state.h"
#include "../../lib/synthetic_mixer.h"

extern int   printf(const char *, ...);
extern int   atoi(const char *);

#ifndef NFRAMES
#  define NFRAMES 30
#endif
#ifndef SAVE_FRAME_DEFAULT
#  define SAVE_FRAME_DEFAULT 11
#endif

const synthetic_mixer_event_t synthetic_mixer_schedule[] = {
    { .handle = 1, .ends_at_frame = 5 },
    { .handle = 2, .ends_at_frame = 12 },
};
const uint32_t synthetic_mixer_schedule_count =
    sizeof(synthetic_mixer_schedule) / sizeof(synthetic_mixer_schedule[0]);

static void accumulate(void)
{
    frame_state_t *fs = region_frame_state_get();
    fs->accum_misc += (voice_is_playing(1) ? 1.0f   : 10.0f);
    fs->accum_misc += (voice_is_playing(2) ? 100.0f : 1000.0f);
}

int main(int argc, char **argv)
{
    uint32_t save_frame = SAVE_FRAME_DEFAULT;
    if (argc >= 2) save_frame = (uint32_t)atoi(argv[1]);

    frame_state_t *fs = region_frame_state_get();
    {
        unsigned char *p = (unsigned char *)fs;
        for (size_t i = 0; i < sizeof(*fs); i++) p[i] = 0;
    }
    synthetic_mixer_init();
    save_state_init();

    voice_start(1);
    voice_start(2);

    for (uint32_t f = 0; f <= save_frame; f++) {
        /* Start of update: drain pending into logical_view_bits.  At
         * frame 0 the pending FIFO is empty, so this is a no-op. */
        voice_end_apply_pending();

        /* Cart frame work: read is_playing, accumulate. */
        accumulate();

        /* End of frame: synthetic mixer reports voice-end events for
         * any voice whose ends_at_frame == f.  These land in the
         * pending FIFO and are NOT yet visible to the cart's
         * is_playing() — that observation only changes at the next
         * voice_end_apply_pending(). */
        synthetic_mixer_report_end_of_frame(f);

        fs->frame = f;
    }

    static uint8_t buf[8192];
    uint32_t n = save_state_save(buf, sizeof(buf), save_frame);
    if (!n) {
        printf("PANIC det_audio_branch_save: save_state_save returned 0\n");
        return 1;
    }
    save_state_emit_hex(buf, n);
    return 0;
}

/* Spike K Stage 3 — det_audio_branch_full.elf.
 *
 * Straight-through baseline.  Same workload, same schedule, runs all
 * 30 frames emitting `DIGEST` lines.  Used as the comparison baseline
 * for the strong gate: load continuation digest stream must equal the
 * suffix of this stream from frame N+1 onward.
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_voice_end_queue.h"
#include "../../cart_runtime/digest.h"
#include "../../cart_runtime/save_state.h"
#include "../../lib/synthetic_mixer.h"

extern int   printf(const char *, ...);

#ifndef NFRAMES
#  define NFRAMES 30
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

int main(void)
{
    frame_state_t *fs = region_frame_state_get();
    {
        unsigned char *p = (unsigned char *)fs;
        for (size_t i = 0; i < sizeof(*fs); i++) p[i] = 0;
    }
    synthetic_mixer_init();
    save_state_init();

    voice_start(1);
    voice_start(2);

    for (uint32_t f = 0; f < NFRAMES; f++) {
        voice_end_apply_pending();
        accumulate();
        synthetic_mixer_report_end_of_frame(f);
        fs->frame = f;
        frame_state_emit_digest(fs);
    }
    return 0;
}

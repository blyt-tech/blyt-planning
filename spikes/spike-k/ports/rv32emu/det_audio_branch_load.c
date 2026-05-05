/* Spike K Stage 3 — det_audio_branch_load.elf.
 *
 * Reads the saved buffer (argv[1]), restores frame_state and
 * runtime_voice_end_queue_t, resyncs the synthetic mixer's frame
 * counter to the restored frame_state.frame, then runs frames N+1..29
 * emitting digests.  If the save was taken with a non-empty pending
 * FIFO, the *first* post-restore voice_end_apply_pending() drains it
 * before any cart code runs (PLAN.md Stage 3 step 17 — sequencing).
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_voice_end_queue.h"
#include "../../cart_runtime/digest.h"
#include "../../cart_runtime/save_state.h"
#include "../../cart_runtime/save_io.h"
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

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("PANIC det_audio_branch_load: usage: <buffer-hex-file>\n");
        return 1;
    }

    save_state_init();

    frame_state_t *fs = region_frame_state_get();
    {
        unsigned char *p = (unsigned char *)fs;
        for (size_t i = 0; i < sizeof(*fs); i++) p[i] = 0;
    }
    synthetic_mixer_init();

    static char hexbuf[16384];
    long n = save_io_read_file(argv[1], hexbuf, sizeof(hexbuf) - 1);
    if (n < 0) { printf("PANIC det_audio_branch_load: cannot read buffer\n"); return 1; }
    hexbuf[n] = '\0';

    static uint8_t bin[8192];
    long m = save_io_parse_hex(hexbuf, (size_t)n, bin, sizeof(bin));
    if (m < 0) { printf("PANIC det_audio_branch_load: hex parse failed\n"); return 1; }
    if (!save_state_load(bin, (uint32_t)m)) {
        printf("PANIC det_audio_branch_load: save_state_load rejected buffer\n");
        return 1;
    }

    synthetic_mixer_resync(fs->frame);

    {
        runtime_voice_end_queue_t *q = region_voice_end_queue_get();
        printf("VOICEQ logical=%08x%08x pending_count=%u\n",
               (unsigned)(q->logical_view_bits >> 32),
               (unsigned)(q->logical_view_bits & 0xffffffffu),
               (unsigned)q->pending_count);
    }

    uint32_t resume_frame = fs->frame + 1;
    for (uint32_t f = resume_frame; f < NFRAMES; f++) {
        voice_end_apply_pending();
        accumulate();
        synthetic_mixer_report_end_of_frame(f);
        fs->frame = f;
        frame_state_emit_digest(fs);
    }
    return 0;
}

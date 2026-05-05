/* Spike K Stage 5 — det_shake_full.elf.
 *
 * Straight-through reference: runs all 30 frames emitting `DIGEST` lines.
 * No save / load — same workload as det_shake_save / det_shake_load,
 * driven through every frame in a single process.  Used as the
 * comparison baseline for the strong gate (load continuation digest
 * stream must equal the suffix of this stream from frame N+1 onward).
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_screen_shake.h"
#include "../../cart_runtime/digest.h"
#include "../../cart_runtime/save_state.h"

extern int   printf(const char *, ...);

#ifndef NFRAMES
#  define NFRAMES 30
#endif
#ifndef SHAKE_TRIGGER_FRAME
#  define SHAKE_TRIGGER_FRAME 3
#endif
#ifndef SHAKE_FRAMES
#  define SHAKE_FRAMES 20
#endif
#ifndef SHAKE_INTENSITY
#  define SHAKE_INTENSITY 4.0f
#endif

static void fold_offset_into_digest(float dx, float dy)
{
    frame_state_t *fs = region_frame_state_get();
    union { float f; uint32_t u; } x = { .f = dx };
    union { float f; uint32_t u; } y = { .f = dy };
    fs->accum_misc += dx * 1.5f + dy * 2.5f;
    fs->accum_sin += (float)((x.u >>  8) & 0xff) * 0.001953125f;
    fs->accum_cos += (float)((y.u >>  8) & 0xff) * 0.001953125f;
}

int main(void)
{
    frame_state_t *fs = region_frame_state_get();
    {
        unsigned char *p = (unsigned char *)fs;
        for (size_t i = 0; i < sizeof(*fs); i++) p[i] = 0;
    }
    save_state_init();

    for (uint32_t f = 0; f < NFRAMES; f++) {
        if (f == SHAKE_TRIGGER_FRAME) {
            blyt_screen_shake(SHAKE_FRAMES, SHAKE_INTENSITY);
        }
        float dx, dy;
        screen_shake_tick(f, &dx, &dy);
        fold_offset_into_digest(dx, dy);
        fs->frame = f;
        frame_state_emit_digest(fs);
    }
    return 0;
}

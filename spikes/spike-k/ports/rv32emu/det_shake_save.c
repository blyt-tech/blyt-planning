/* Spike K Stage 5 — det_shake_save.elf.
 *
 * Triggers a 20-frame screen shake at frame 3 (intensity 4.0, decay
 * 0.95).  Each frame folds the computed (dx, dy) offset bytes into
 * `frame_state.accum_misc` so any divergence in the deterministic
 * shake noise propagates into the per-frame digest.
 *
 * Saves at frame 10 (mid-shake — 13 frames remaining, intensity decayed
 * by 0.95^7 ≈ 2.4).  PLAN.md Stage 5 step 24 names this save point
 * specifically as "mid-shake" so the test exercises a non-zero
 * intensity / non-zero remaining_frames buffer payload.
 *
 * No digest emission — only the buffer hex is captured.  The companion
 * det_shake_load.elf consumes the buffer and emits frames 11..29 with
 * digests, matching what spike-D's straight-through det_shake.elf
 * (also produced by this same source, just running through frame 29
 * without saving) would have produced.
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_screen_shake.h"
#include "../../cart_runtime/save_state.h"

extern int   printf(const char *, ...);
extern int   atoi(const char *);

#ifndef NFRAMES
#  define NFRAMES 30
#endif
#ifndef SAVE_FRAME_DEFAULT
#  define SAVE_FRAME_DEFAULT 10
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
    /* Read the f32 bit patterns into u32, mix into accum_misc as a tiny
     * linear combination.  Folding the raw bits (not just adding the
     * floats) makes the digest sensitive to NaN payload, sign-of-zero,
     * and similar edge cases that f32 addition would smooth over. */
    union { float f; uint32_t u; } x = { .f = dx };
    union { float f; uint32_t u; } y = { .f = dy };
    fs->accum_misc += dx * 1.5f + dy * 2.5f;
    /* Touch accum_sin / accum_cos with hashed bits so a one-bit divergence
     * in (dx, dy) becomes a multi-byte digest divergence.  The test is
     * sensitivity, not numerical fidelity. */
    fs->accum_sin += (float)((x.u >>  8) & 0xff) * 0.001953125f; /* /512 */
    fs->accum_cos += (float)((y.u >>  8) & 0xff) * 0.001953125f;
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

    save_state_init();

    for (uint32_t f = 0; f <= save_frame; f++) {
        if (f == SHAKE_TRIGGER_FRAME) {
            blyt_screen_shake(SHAKE_FRAMES, SHAKE_INTENSITY);
        }
        float dx, dy;
        screen_shake_tick(f, &dx, &dy);
        fold_offset_into_digest(dx, dy);
        fs->frame = f;
    }

    static uint8_t buf[8192];
    uint32_t n = save_state_save(buf, sizeof(buf), save_frame);
    if (!n) {
        printf("PANIC det_shake_save: save_state_save returned 0\n");
        return 1;
    }
    save_state_emit_hex(buf, n);
    return 0;
}

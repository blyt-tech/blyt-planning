/* Spike K Stage 5 — det_shake_load.elf.
 *
 * Reads a saved buffer (argv[1]), deserializes frame_state and
 * runtime_screen_shake_t, then runs frames N+1..29 emitting digests.
 * The per-frame shake offsets must match the same-host straight-through
 * run for the spike to claim the screen-shake region round-trips.
 *
 * The load also emits a non-digest accounting line so spike harnesses
 * can confirm shake state was non-trivially populated by the buffer
 * (catches a "load silently produced zeros" failure mode):
 *
 *   SHAKE remaining=NN intensity=<bits> decay=<bits> seed=NN
 *
 * The harness greps `^DIGEST ` for the comparison; the SHAKE line is
 * informational.
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_screen_shake.h"
#include "../../cart_runtime/digest.h"
#include "../../cart_runtime/save_state.h"
#include "../../cart_runtime/save_io.h"

extern int   printf(const char *, ...);

#ifndef NFRAMES
#  define NFRAMES 30
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

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("PANIC det_shake_load: usage: det_shake_load.elf <buffer-hex-file>\n");
        return 1;
    }

    save_state_init();

    frame_state_t *fs = region_frame_state_get();
    {
        unsigned char *p = (unsigned char *)fs;
        for (size_t i = 0; i < sizeof(*fs); i++) p[i] = 0;
    }
    /* runtime_screen_shake_t starts at all-zero (BSS); the load fills
     * it.  No fresh blyt_screen_shake() call here — the buffer carries
     * mid-shake state. */

    static char hexbuf[16384];
    long n = save_io_read_file(argv[1], hexbuf, sizeof(hexbuf) - 1);
    if (n < 0) {
        printf("PANIC det_shake_load: cannot read buffer file: %s\n", argv[1]);
        return 1;
    }
    hexbuf[n] = '\0';

    static uint8_t bin[8192];
    long m = save_io_parse_hex(hexbuf, (size_t)n, bin, sizeof(bin));
    if (m < 0) {
        printf("PANIC det_shake_load: hex parse failed (n=%ld)\n", n);
        return 1;
    }

    if (!save_state_load(bin, (uint32_t)m)) {
        printf("PANIC det_shake_load: save_state_load rejected buffer\n");
        return 1;
    }

    {
        runtime_screen_shake_t *s = region_screen_shake_get();
        union { float f; uint32_t u; } iv = { .f = s->intensity };
        union { float f; uint32_t u; } dv = { .f = s->decay };
        printf("SHAKE remaining=%d intensity=%08x decay=%08x seed=%d\n",
               (int)s->remaining_frames, (unsigned)iv.u, (unsigned)dv.u,
               (int)s->seed);
    }

    uint32_t resume_frame = fs->frame + 1;
    for (uint32_t f = resume_frame; f < NFRAMES; f++) {
        float dx, dy;
        screen_shake_tick(f, &dx, &dy);
        fold_offset_into_digest(dx, dy);
        fs->frame = f;
        frame_state_emit_digest(fs);
    }

    return 0;
}

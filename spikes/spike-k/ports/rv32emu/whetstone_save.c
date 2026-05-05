/* Spike K — whetstone_save.elf.
 *
 * Spike D's whetstone.c, plus a single save at frame N (default 15) that
 * writes the buffer hex to stdout and exits.  No digest stream is emitted
 * — the harness only needs the buffer.  The companion whetstone_load.elf
 * resumes from the saved buffer and emits the digest stream from frame
 * N+1 onward.
 *
 * The module-local accumulators (a/b/c/d/e1/t/u) live in
 * cart_state_whetstone — a tracked POD region — so the save captures
 * everything that affects future frames.  This is the floor case for
 * "all simulation state lives in declared regions" (ADR-0010).
 *
 * Run:
 *   rv32emu whetstone_save.elf            # save at the default frame
 *   rv32emu whetstone_save.elf 11         # save at frame 11
 *
 * The cart prints exactly one `BUFFER <frame> <hex...>` line and nothing
 * else.  The harness greps `^BUFFER ` to capture it.
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/cart_state_whetstone.h"
#include "../../cart_runtime/pcg32.h"
#include "../../cart_runtime/save_state.h"

extern int    printf(const char *, ...);
extern int    atoi(const char *);
extern float  sqrtf(float);
extern float  sinf(float);
extern float  cosf(float);
extern float  atan2f(float, float);
extern float  expf(float);
extern float  logf(float);
extern float  powf(float, float);
extern float  fabsf(float);

#ifndef NFRAMES
#  define NFRAMES 30
#endif
#ifndef SAVE_FRAME_DEFAULT
#  define SAVE_FRAME_DEFAULT 15
#endif
#define INNER 200

static pcg32_t rng;

static void module_1(void)
{
    frame_state_t          *fs = region_frame_state_get();
    cart_state_whetstone_t *cs = cart_state_whetstone_get();
    const float t = 0.499975f;
    for (int i = 0; i < INNER; i++) {
        cs->a = (cs->a + cs->b + cs->c - cs->d) * t;
        cs->b = (cs->a + cs->b - cs->c + cs->d) * t;
        cs->c = (cs->a - cs->b + cs->c + cs->d) * t;
        cs->d = (-cs->a + cs->b + cs->c + cs->d) * t;
    }
    fs->accum_misc += cs->a + cs->b + cs->c + cs->d;
}

static void module_2(void)
{
    frame_state_t          *fs = region_frame_state_get();
    cart_state_whetstone_t *cs = cart_state_whetstone_get();
    float *e1 = cs->e1;
    for (int i = 0; i < INNER; i++) {
        e1[0] = (e1[0] + e1[1] + e1[2] - e1[3]) * 0.5f;
        e1[1] = (e1[0] + e1[1] - e1[2] + e1[3]) * 0.5f;
        e1[2] = (e1[0] - e1[1] + e1[2] + e1[3]) * 0.5f;
        e1[3] = (-e1[0] + e1[1] + e1[2] + e1[3]) * 0.5f;
        e1[0] = sqrtf(fabsf(e1[0]) + 1.0f);
        e1[1] = sqrtf(fabsf(e1[1]) + 1.0f);
        e1[2] = sqrtf(fabsf(e1[2]) + 1.0f);
        e1[3] = sqrtf(fabsf(e1[3]) + 1.0f);
    }
    fs->accum_sqrt += e1[0] + e1[1] + e1[2] + e1[3];
}

static void module_3(void)
{
    frame_state_t          *fs = region_frame_state_get();
    cart_state_whetstone_t *cs = cart_state_whetstone_get();
    for (int i = 0; i < INNER; i++) {
        cs->t = atan2f(sqrtf(cs->t * cs->t + 1.0f), cs->t);
        fs->accum_sin += sinf(cs->t);
        fs->accum_cos += cosf(cs->t);
    }
}

static void module_4(void)
{
    frame_state_t          *fs = region_frame_state_get();
    cart_state_whetstone_t *cs = cart_state_whetstone_get();
    for (int i = 0; i < INNER; i++) {
        cs->u = expf(logf(fabsf(cs->u) + 1.0f));
        cs->u = powf(cs->u, 0.5f);
        fs->accum_misc += cs->u;
    }
}

static void module_5(uint32_t frame)
{
    frame_state_t *fs = region_frame_state_get();
    for (int i = 0; i < FRAME_STATE_MAX_MOBS; i++) {
        float fi = (float)(i + 1);
        float ff = (float)frame;
        float a  = (fi * 0.1f) + (ff * 0.05f);
        fs->mobs[i].x     = sinf(a) * 64.0f + 64.0f;
        fs->mobs[i].y     = cosf(a) * 64.0f + 64.0f;
        fs->mobs[i].vx    = sinf(a*2.0f);
        fs->mobs[i].vy    = cosf(a*2.0f);
        fs->mobs[i].state = pcg32_next(&rng) & 0xffu;
    }
}

int main(int argc, char **argv)
{
    uint32_t save_frame = SAVE_FRAME_DEFAULT;
    if (argc >= 2) save_frame = (uint32_t)atoi(argv[1]);

    pcg32_seed(&rng, PCG_DEFAULT_STATE, PCG_DEFAULT_INC);

    /* Zero frame_state explicitly; cart_state_whetstone is initialised by
     * its declaration to whetstone's fresh values.  Both regions are
     * tracked, both round-trip on save/load. */
    frame_state_t *fs = region_frame_state_get();
    {
        unsigned char *p = (unsigned char *)fs;
        for (size_t i = 0; i < sizeof(*fs); i++) p[i] = 0;
    }

    save_state_init();

    for (uint32_t f = 0; f <= save_frame; f++) {
        module_1();
        module_2();
        module_3();
        module_4();
        module_5(f);

        fs->frame     = f;
        fs->rng_state = rng.state;
        fs->rng_inc   = rng.inc;
        /* No digest emission — only the save payload is captured. */
    }

    static uint8_t buf[8192];
    uint32_t n = save_state_save(buf, sizeof(buf), save_frame);
    if (!n) {
        printf("PANIC whetstone_save: save_state_save returned 0\n");
        return 1;
    }
    save_state_emit_hex(buf, n);
    return 0;
}

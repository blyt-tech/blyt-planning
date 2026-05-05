/* Spike K — whetstone_load.elf.
 *
 * Reads a saved whetstone buffer from a host file (path = argv[1]),
 * deserializes it into both tracked regions (frame_state and
 * cart_state_whetstone), then runs frames N+1..29 emitting the digest
 * stream exactly as Spike D's whetstone.elf would have for those frames.
 *
 * Every value the post-restore frames depend on is in a tracked region:
 *   • frame_state — accumulators, mobs, RNG, frame counter
 *   • cart_state_whetstone — module-local accumulators (a, b, c, d, e1, t, u)
 *
 * The local pcg32 in this file is reseeded from the restored
 * frame_state's rng_state / rng_inc — that mirrors what the cart's
 * `init` would do in production: rebuild caches from declared regions.
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/cart_state_whetstone.h"
#include "../../cart_runtime/pcg32.h"
#include "../../cart_runtime/digest.h"
#include "../../cart_runtime/save_state.h"
#include "../../cart_runtime/save_io.h"

extern int    printf(const char *, ...);
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
    if (argc < 2) {
        printf("PANIC whetstone_load: usage: whetstone_load.elf <buffer-hex-file>\n");
        return 1;
    }

    save_state_init();

    /* Fresh init mirrors the contract: the cart's `init` (here, this
     * function up to the load call) sets up storage; the load fills
     * tracked regions from the buffer; transient state stays at fresh
     * defaults.  Whetstone's only transient state is the local rng — and
     * even rng is restored from frame_state below. */
    frame_state_t *fs = region_frame_state_get();
    {
        unsigned char *p = (unsigned char *)fs;
        for (size_t i = 0; i < sizeof(*fs); i++) p[i] = 0;
    }
    pcg32_seed(&rng, PCG_DEFAULT_STATE, PCG_DEFAULT_INC);

    static char hexbuf[16384];
    long n = save_io_read_file(argv[1], hexbuf, sizeof(hexbuf) - 1);
    if (n < 0) {
        printf("PANIC whetstone_load: cannot read buffer file: %s\n", argv[1]);
        return 1;
    }
    hexbuf[n] = '\0';

    static uint8_t bin[8192];
    long m = save_io_parse_hex(hexbuf, (size_t)n, bin, sizeof(bin));
    if (m < 0) {
        printf("PANIC whetstone_load: hex parse failed (n=%ld)\n", n);
        return 1;
    }

    if (!save_state_load(bin, (uint32_t)m)) {
        printf("PANIC whetstone_load: save_state_load rejected buffer (size=%ld, layout_hash=%08x%08x)\n",
               m,
               (unsigned)(save_state_layout_hash() >> 32),
               (unsigned)(save_state_layout_hash() & 0xffffffffu));
        return 1;
    }

    /* Restore local rng from the just-loaded frame_state.  Module-local
     * accumulators are already in cart_state_whetstone — accessed via
     * cart_state_whetstone_get() inside each module. */
    rng.state = fs->rng_state;
    rng.inc   = fs->rng_inc;

    uint32_t resume_frame = fs->frame + 1;
    for (uint32_t f = resume_frame; f < NFRAMES; f++) {
        module_1();
        module_2();
        module_3();
        module_4();
        module_5(f);

        fs->frame     = f;
        fs->rng_state = rng.state;
        fs->rng_inc   = rng.inc;
        frame_state_emit_digest(fs);
    }

    return 0;
}

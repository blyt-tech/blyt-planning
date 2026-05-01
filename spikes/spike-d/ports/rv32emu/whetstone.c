/* whetstone.c — Whetstone-shaped f32 FP workload, no Lua VM.
 *
 * Loosely modelled on the classic Whetstone benchmark: a small set of
 * inner loops that each exercise a different family of FP operations
 * (basic add/sub/mul/div, sqrt, sin/cos/atan2, exp/log, pow), then a
 * mob-array stage so the cart's frame_state has populated `mobs[]`
 * fields that the digest can pick up.
 *
 * The point of including Whetstone in Spike D is to get a transcendental-
 * heavy workload with NO Lua-VM in the loop, so a cross-host divergence
 * here points squarely at libm / SoftFloat / rv32emu and not at Lua.
 *
 * All FP is f32 throughout (single-precision).  This matches the
 * production runtime constraint (rv32imfc has F but no D) and matches
 * Spike B's existing benchmarks.
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/pcg32.h"
#include "../../cart_runtime/digest.h"

extern int   printf(const char *, ...);
extern float sqrtf(float);
extern float sinf(float);
extern float cosf(float);
extern float atan2f(float, float);
extern float expf(float);
extern float logf(float);
extern float powf(float, float);
extern float fabsf(float);

#define NFRAMES 30
#define INNER   200    /* iterations of each inner kernel per frame */

static frame_state_t fs;
static pcg32_t       rng;

/* Module 1 — basic arithmetic on a 4-vector.
 * Matches the original Whetstone module 1 in shape: small-vector update
 * with constant coefficients, no transcendentals. */
static void module_1(float *a, float *b, float *c, float *d)
{
    const float t = 0.499975f;
    for (int i = 0; i < INNER; i++) {
        *a = (*a + *b + *c - *d) * t;
        *b = (*a + *b - *c + *d) * t;
        *c = (*a - *b + *c + *d) * t;
        *d = (-*a + *b + *c + *d) * t;
    }
    fs.accum_misc += *a + *b + *c + *d;
}

/* Module 2 — sqrt-heavy.  Exercises the rv32f fsqrt.s instruction (which
 * SoftFloat services on the host) without involving libm. */
static void module_2(float *e1)
{
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
    fs.accum_sqrt += e1[0] + e1[1] + e1[2] + e1[3];
}

/* Module 3 — transcendentals.  This is the libm-sensitive part. */
static void module_3(float *t)
{
    for (int i = 0; i < INNER; i++) {
        *t = atan2f(sqrtf(*t * *t + 1.0f), *t);
        fs.accum_sin += sinf(*t);
        fs.accum_cos += cosf(*t);
    }
}

static void module_4(float *t)
{
    for (int i = 0; i < INNER; i++) {
        *t = expf(logf(fabsf(*t) + 1.0f));
        *t = powf(*t, 0.5f);
        fs.accum_misc += *t;
    }
}

/* Module 5 — populate mobs[] so the digest picks up varying state. */
static void module_5(uint32_t frame)
{
    for (int i = 0; i < FRAME_STATE_MAX_MOBS; i++) {
        float fi = (float)(i + 1);
        float ff = (float)frame;
        float a  = (fi * 0.1f) + (ff * 0.05f);
        fs.mobs[i].x     = sinf(a)    * 64.0f + 64.0f;
        fs.mobs[i].y     = cosf(a)    * 64.0f + 64.0f;
        fs.mobs[i].vx    = sinf(a*2.0f);
        fs.mobs[i].vy    = cosf(a*2.0f);
        fs.mobs[i].state = pcg32_next(&rng) & 0xffu;
    }
}

int main(void)
{
    pcg32_seed(&rng, PCG_DEFAULT_STATE, PCG_DEFAULT_INC);

    /* Zero the frame_state once.  Subsequent frames retain accumulators
     * (so a transcendental drift in frame N would propagate forward). */
    {
        unsigned char *p = (unsigned char *)&fs;
        for (size_t i = 0; i < sizeof(fs); i++) p[i] = 0;
    }

    float a = 1.0f, b = -1.0f, c = -1.0f, d = -1.0f;
    float e1[4] = { 1.0f, -1.0f, -1.0f, -1.0f };
    float t = 0.5f;
    float u = 0.75f;

    for (uint32_t f = 0; f < NFRAMES; f++) {
        module_1(&a, &b, &c, &d);
        module_2(e1);
        module_3(&t);
        module_4(&u);
        module_5(f);

        fs.frame     = f;
        fs.rng_state = rng.state;
        fs.rng_inc   = rng.inc;
        frame_state_emit_digest(&fs);
    }

    return 0;
}

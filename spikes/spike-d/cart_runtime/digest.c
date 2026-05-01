/* FNV-1a-64 over the packed frame_state, with NaN canonicalization on
 * every float field.  Emit one DIGEST line to stdout per frame. */

#include <stddef.h>
#include <stdint.h>

#include "digest.h"
#include "frame_state.h"
#include "nan_canon.h"

extern int printf(const char *, ...);

#define FNV1A_64_OFFSET  0xcbf29ce484222325ULL
#define FNV1A_64_PRIME   0x00000100000001b3ULL

void frame_state_canonicalize(frame_state_t *s)
{
    s->accum_sin  = canonicalize_nanf(s->accum_sin);
    s->accum_cos  = canonicalize_nanf(s->accum_cos);
    s->accum_sqrt = canonicalize_nanf(s->accum_sqrt);
    s->accum_misc = canonicalize_nanf(s->accum_misc);
    for (int i = 0; i < FRAME_STATE_MAX_MOBS; i++) {
        s->mobs[i].x  = canonicalize_nanf(s->mobs[i].x);
        s->mobs[i].y  = canonicalize_nanf(s->mobs[i].y);
        s->mobs[i].vx = canonicalize_nanf(s->mobs[i].vx);
        s->mobs[i].vy = canonicalize_nanf(s->mobs[i].vy);
    }
}

uint64_t frame_state_fnv1a(const frame_state_t *s)
{
    uint64_t h = FNV1A_64_OFFSET;
    const unsigned char *p = (const unsigned char *)s;
    for (size_t i = 0; i < sizeof(*s); i++) {
        h ^= (uint64_t)p[i];
        h *= FNV1A_64_PRIME;
    }
    return h;
}

void frame_state_emit_digest(frame_state_t *s)
{
    frame_state_canonicalize(s);
    uint64_t h = frame_state_fnv1a(s);
    /* Emit hi/lo halves separately because our snprintf does not implement
     * `%016llx` on a 64-bit value robustly under -mabi=ilp32f.  Two %08x
     * halves produce the same canonical hex string with no host-specific
     * formatting paths. */
    uint32_t hi = (uint32_t)(h >> 32);
    uint32_t lo = (uint32_t)(h & 0xffffffffu);
    printf("DIGEST %u %08x%08x\n", (unsigned)s->frame, hi, lo);
}

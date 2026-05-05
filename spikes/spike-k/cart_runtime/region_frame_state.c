/* Spike K — frame_state region implementation.
 *
 * The save callback memcpys the global frame_state into the buffer
 * after canonicalizing every f32 field.  The load callback memcpys back
 * after a bounds check; the destination's NaN canonicalization is done
 * implicitly because the sender already canonicalized at write time
 * (write-side canonicalization is the load-bearing rule for cross-host
 * portability — see PLAN.md § risk notes).
 *
 * The describe callback emits the same per-field text Spike K uses to
 * compute layout_hash.  Field offsets are evaluated via offsetof() so the
 * hash captures the compiler's actual layout, not just the source-level
 * declaration.  Two binaries that *should* have the same layout but are
 * compiled with different toolchains and produce different offsets are
 * correctly rejected — see PLAN.md § risk notes "layout_hash collisions
 * vs false-mismatches".
 */

#include <stdint.h>
#include <stddef.h>

#include "region_frame_state.h"
#include "frame_state.h"
#include "nan_canon.h"
#include "runtime_tracked.h"

/* The frame_state instance is owned here.  Lua bindings (and the
 * whetstone main()) call region_frame_state_get() to mutate it. */
static frame_state_t g_fs;

frame_state_t *region_frame_state_get(void) { return &g_fs; }

static void describe(string_sink_t *out)
{
    /* `frame` u32 @ offset 0; `rng_state` u64; `rng_inc` u64; the four
     * accum_* f32; then 64 mob entries each (x,y,vx,vy:f32, state:u32).
     * The offsets are pinned at compile time via offsetof() so the
     * description reflects the binary's actual layout — see header
     * comment for why this matters for layout_hash. */
    string_sink_puts(out, "frame:u32@");      string_sink_putu(out, offsetof(frame_state_t, frame));     string_sink_putc(out, ',');
    string_sink_puts(out, "rng_state:u64@");  string_sink_putu(out, offsetof(frame_state_t, rng_state)); string_sink_putc(out, ',');
    string_sink_puts(out, "rng_inc:u64@");    string_sink_putu(out, offsetof(frame_state_t, rng_inc));   string_sink_putc(out, ',');
    string_sink_puts(out, "accum_sin:f32@");  string_sink_putu(out, offsetof(frame_state_t, accum_sin)); string_sink_putc(out, ',');
    string_sink_puts(out, "accum_cos:f32@");  string_sink_putu(out, offsetof(frame_state_t, accum_cos)); string_sink_putc(out, ',');
    string_sink_puts(out, "accum_sqrt:f32@"); string_sink_putu(out, offsetof(frame_state_t, accum_sqrt));string_sink_putc(out, ',');
    string_sink_puts(out, "accum_misc:f32@"); string_sink_putu(out, offsetof(frame_state_t, accum_misc));string_sink_putc(out, ',');
    string_sink_puts(out, "mobs[64]={x:f32,y:f32,vx:f32,vy:f32,state:u32}@");
    string_sink_putu(out, offsetof(frame_state_t, mobs));
}

static void save(uint8_t *dst)
{
    /* Canonicalize NaN on every f32 field FIRST, then memcpy the
     * (now-canonicalized) struct into the buffer.  Spike D canonicalized
     * only at digest emission; spike K extends the rule to the buffer
     * write boundary so a non-canonical NaN that exists between digest
     * emissions cannot diverge across hosts.  See nan_canon.h. */
    g_fs.accum_sin  = canonicalize_nanf(g_fs.accum_sin);
    g_fs.accum_cos  = canonicalize_nanf(g_fs.accum_cos);
    g_fs.accum_sqrt = canonicalize_nanf(g_fs.accum_sqrt);
    g_fs.accum_misc = canonicalize_nanf(g_fs.accum_misc);
    for (int i = 0; i < FRAME_STATE_MAX_MOBS; i++) {
        g_fs.mobs[i].x  = canonicalize_nanf(g_fs.mobs[i].x);
        g_fs.mobs[i].y  = canonicalize_nanf(g_fs.mobs[i].y);
        g_fs.mobs[i].vx = canonicalize_nanf(g_fs.mobs[i].vx);
        g_fs.mobs[i].vy = canonicalize_nanf(g_fs.mobs[i].vy);
    }
    const uint8_t *src = (const uint8_t *)&g_fs;
    for (size_t i = 0; i < sizeof(g_fs); i++) dst[i] = src[i];
}

static int load(const uint8_t *src, uint32_t n)
{
    if (n != sizeof(g_fs)) return 0;
    uint8_t *dst = (uint8_t *)&g_fs;
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    return 1;
}

const runtime_tracked_region_t region_frame_state = {
    .name     = "frame_state",
    .size     = (uint32_t)sizeof(frame_state_t),
    .describe = describe,
    .save     = save,
    .load     = load,
};

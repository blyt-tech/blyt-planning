/* Spike K — screen shake region implementation (ADR-0051).
 *
 * The deterministic noise is FNV-1a-32 over `(frame, seed, axis)` mixed
 * into a 24-bit unit float — same approach as PCG32 in the spike but
 * cheaper, since we only need a few bits per call and we want the output
 * to depend on the (frame, seed) pair without extra state.  The output
 * is scaled by the current intensity to give `dx, dy`.
 *
 * Per-frame state evolution: when `remaining_frames > 0` the tick consumes
 * the current intensity to produce offsets, then decays intensity by
 * `decay` and decrements `remaining_frames`.  When `remaining_frames` hits
 * zero the offsets are zero regardless of intensity (so a save mid-shake
 * with `remaining_frames=0` still restores cleanly — the cart sees zero
 * offsets on the first post-restore frame).
 */

#include <stdint.h>
#include <stddef.h>

#include "region_screen_shake.h"
#include "nan_canon.h"

static runtime_screen_shake_t g_shake;

runtime_screen_shake_t *region_screen_shake_get(void) { return &g_shake; }

void blyt_screen_shake(int32_t frames, float intensity)
{
    g_shake.remaining_frames = frames;
    g_shake.intensity        = intensity;
    g_shake.decay            = 0.95f;
    /* Seed the per-shake-instance RNG with the current frames+intensity
     * pattern so two separate shakes triggered at different frames mint
     * different offset sequences without the cart having to choose a
     * seed.  In production an author-supplied seed would be desirable;
     * deferred to follow-up. */
    union { float f; uint32_t u; } v = { .f = intensity };
    g_shake.seed = (int32_t)(v.u ^ (uint32_t)frames * 0x9e3779b1u);
}

/* FNV-1a-32 over a small bag of integer inputs.  Mixes (frame, seed, axis)
 * into a 32-bit hash; the upper 24 bits become an f32 in [-0.5, 0.5). */
static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t OFFS = 0x811c9dc5u;
    const uint32_t PRIME = 0x01000193u;
    uint32_t h = OFFS;
    const uint8_t *p;
    p = (const uint8_t *)&a; for (int i = 0; i < 4; i++) { h ^= p[i]; h *= PRIME; }
    p = (const uint8_t *)&b; for (int i = 0; i < 4; i++) { h ^= p[i]; h *= PRIME; }
    p = (const uint8_t *)&c; for (int i = 0; i < 4; i++) { h ^= p[i]; h *= PRIME; }
    return h;
}

static float hash_to_unit_signed(uint32_t h)
{
    /* Upper 24 bits → [0, 1), then -0.5 → [-0.5, +0.5). */
    uint32_t u = h >> 8;
    float f = (float)u * (1.0f / 16777216.0f);
    return f - 0.5f;
}

void screen_shake_tick(uint32_t frame, float *dx, float *dy)
{
    if (g_shake.remaining_frames <= 0) {
        *dx = 0.0f; *dy = 0.0f;
        return;
    }
    uint32_t hx = hash3(frame, (uint32_t)g_shake.seed, 0u);
    uint32_t hy = hash3(frame, (uint32_t)g_shake.seed, 1u);
    *dx = hash_to_unit_signed(hx) * g_shake.intensity;
    *dy = hash_to_unit_signed(hy) * g_shake.intensity;
    g_shake.intensity *= g_shake.decay;
    g_shake.remaining_frames--;
}

/* ── tracked region callbacks ────────────────────────────────────────────── */

static void describe(string_sink_t *out)
{
    string_sink_puts(out, "remaining_frames:i32@"); string_sink_putu(out, offsetof(runtime_screen_shake_t, remaining_frames)); string_sink_putc(out, ',');
    string_sink_puts(out, "intensity:f32@");        string_sink_putu(out, offsetof(runtime_screen_shake_t, intensity));        string_sink_putc(out, ',');
    string_sink_puts(out, "decay:f32@");            string_sink_putu(out, offsetof(runtime_screen_shake_t, decay));            string_sink_putc(out, ',');
    string_sink_puts(out, "seed:i32@");             string_sink_putu(out, offsetof(runtime_screen_shake_t, seed));
}

static void save(uint8_t *dst)
{
    g_shake.intensity = canonicalize_nanf(g_shake.intensity);
    g_shake.decay     = canonicalize_nanf(g_shake.decay);
    const uint8_t *src = (const uint8_t *)&g_shake;
    for (size_t i = 0; i < sizeof(g_shake); i++) dst[i] = src[i];
}

static int load(const uint8_t *src, uint32_t n)
{
    if (n != sizeof(g_shake)) return 0;
    uint8_t *dst = (uint8_t *)&g_shake;
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    return 1;
}

const runtime_tracked_region_t region_screen_shake = {
    .name     = "screen_shake",
    .size     = (uint32_t)sizeof(runtime_screen_shake_t),
    .describe = describe,
    .save     = save,
    .load     = load,
};

/* Spike K — voice-end queue region implementation (ADR-0106).
 *
 * Save/load is straight memcpy of the packed struct.  The serializer
 * does NOT canonicalize or rewrite the FIFO order — ADR-0106 specifies
 * handle-order application, but the FIFO itself is captured in the
 * order events were recorded (which on a deterministic synthetic mixer
 * is reproducible per-host without further sorting).  Cross-host
 * divergence here would be a mixer-determinism bug at record time, not
 * a serializer bug — exactly the failure mode the spike is set up to
 * catch (see PLAN.md § risk notes "Voice-end pending FIFO ordering").
 */

#include <stdint.h>
#include <stddef.h>

#include "region_voice_end_queue.h"

static runtime_voice_end_queue_t g_q;

runtime_voice_end_queue_t *region_voice_end_queue_get(void) { return &g_q; }

void voice_start(uint8_t handle)
{
    /* Set the logical-view bit; clear any prior pending event for the
     * same handle so a "start before previous end was applied" sequence
     * doesn't leave a stale event in the FIFO. */
    g_q.logical_view_bits |= ((uint64_t)1u << (uint64_t)handle);
    uint8_t out = 0;
    for (uint8_t i = 0; i < g_q.pending_count; i++) {
        if (g_q.pending[i].handle != handle) {
            g_q.pending[out++] = g_q.pending[i];
        }
    }
    g_q.pending_count = out;
}

int voice_is_playing(uint8_t handle)
{
    return (g_q.logical_view_bits & ((uint64_t)1u << (uint64_t)handle)) != 0;
}

void voice_end_apply_pending(void)
{
    /* ADR-0106 § handle-order application: walk handles in ascending
     * order, applying each end event.  In our small fixed FIFO it's
     * cheaper to walk the FIFO once and clear bits — order doesn't
     * matter because each event clears its own bit independently. */
    for (uint8_t i = 0; i < g_q.pending_count; i++) {
        uint8_t h = g_q.pending[i].handle;
        g_q.logical_view_bits &= ~((uint64_t)1u << (uint64_t)h);
    }
    g_q.pending_count = 0;
}

void voice_end_record(uint32_t frame, uint8_t handle, uint8_t kind)
{
    if (g_q.pending_count >= VOICE_END_PENDING_MAX) return;
    g_q.pending[g_q.pending_count].frame  = frame;
    g_q.pending[g_q.pending_count].handle = handle;
    g_q.pending[g_q.pending_count].kind   = kind;
    g_q.pending_count++;
}

/* ── tracked region callbacks ────────────────────────────────────────────── */

static void describe(string_sink_t *out)
{
    string_sink_puts(out, "logical_view_bits:u64@"); string_sink_putu(out, offsetof(runtime_voice_end_queue_t, logical_view_bits)); string_sink_putc(out, ',');
    string_sink_puts(out, "pending_count:u8@");      string_sink_putu(out, offsetof(runtime_voice_end_queue_t, pending_count));      string_sink_putc(out, ',');
    string_sink_puts(out, "pending[8]={frame:u32,handle:u8,kind:u8}@");
    string_sink_putu(out, offsetof(runtime_voice_end_queue_t, pending));
}

static void save(uint8_t *dst)
{
    const uint8_t *src = (const uint8_t *)&g_q;
    for (size_t i = 0; i < sizeof(g_q); i++) dst[i] = src[i];
}

static int load(const uint8_t *src, uint32_t n)
{
    if (n != sizeof(g_q)) return 0;
    uint8_t *dst = (uint8_t *)&g_q;
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    /* Sanity: pending_count must be in range.  A buffer that claims
     * pending_count > VOICE_END_PENDING_MAX is corrupt; reject. */
    if (g_q.pending_count > VOICE_END_PENDING_MAX) return 0;
    return 1;
}

const runtime_tracked_region_t region_voice_end_queue = {
    .name     = "voice_end_queue",
    .size     = (uint32_t)sizeof(runtime_voice_end_queue_t),
    .describe = describe,
    .save     = save,
    .load     = load,
};

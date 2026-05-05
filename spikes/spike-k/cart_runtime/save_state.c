/* Spike K — save_state.c.
 *
 * Walks the runtime_tracked region registry to emit and consume the wire
 * format described in save_state.h.  The header carries SAVE_STATE_MAGIC,
 * SAVE_STATE_VERSION, the cached layout hash, the frame the save was taken
 * at, and the total buffer size.  Body is the concatenation of region
 * blocks in registry declaration order; each region writes/reads its own
 * bytes via its tracked-region callbacks.
 */

#include <stdint.h>
#include <stddef.h>

#include "save_state.h"
#include "runtime_tracked.h"

extern int printf(const char *, ...);

/* FNV-1a-64 — same constants as Spike D's frame digest, intentionally
 * reused so the same emitter can hash arbitrary byte spans (header
 * description text, full save buffer for SHA-equivalent identity, etc.). */
#define FNV1A_64_OFFSET  0xcbf29ce484222325ULL
#define FNV1A_64_PRIME   0x00000100000001b3ULL

static uint64_t fnv1a_64_bytes(const void *p, size_t n)
{
    uint64_t h = FNV1A_64_OFFSET;
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= FNV1A_64_PRIME; }
    return h;
}

/* Cached on the first call to save_state_init() and embedded into every
 * subsequent save header.  Loads recompute and compare. */
static uint64_t g_layout_hash;
static int      g_initialized;

void save_state_init(void)
{
    /* 4 KiB scratch is plenty for the spike's region inventory; if a
     * future stage adds enough fields to exceed this, the overflow flag
     * makes the failure loud rather than silent. */
    static char scratch[4096];
    string_sink_t sink = { scratch, sizeof(scratch), 0, 0 };
    runtime_tracked_describe(&sink);
    if (sink.overflowed) {
        /* Treat as fatal — a truncated description would silently
         * accept divergent layouts.  The cart aborts, the harness sees
         * a non-zero exit, and the spike fails loudly. */
        printf("PANIC save_state: layout description overflow (cap=%u)\n",
               (unsigned)sink.cap);
        for (;;) { /* spin; _exit unavailable here without dragging stdlib. */ }
    }
    g_layout_hash = fnv1a_64_bytes(sink.buf, sink.pos);
    g_initialized = 1;
}

uint64_t save_state_layout_hash(void)
{
    return g_layout_hash;
}

size_t save_state_buffer_capacity(void)
{
    return sizeof(save_state_header_t) + runtime_tracked_total_size();
}

uint32_t save_state_save(uint8_t *buf, uint32_t cap, uint32_t frame)
{
    if (!g_initialized) save_state_init();

    uint32_t need = (uint32_t)save_state_buffer_capacity();
    if (cap < need) return 0;

    save_state_header_t *h = (save_state_header_t *)buf;
    h->magic       = SAVE_STATE_MAGIC;
    h->version     = SAVE_STATE_VERSION;
    h->layout_hash = g_layout_hash;
    h->frame       = frame;
    h->total_size  = need;

    uint8_t *p = buf + sizeof(*h);
    for (uint32_t i = 0; i < runtime_tracked_region_count; i++) {
        const runtime_tracked_region_t *r = runtime_tracked_regions[i];
        r->save(p);
        p += r->size;
    }
    return need;
}

int save_state_load(const uint8_t *buf, uint32_t size)
{
    if (size < sizeof(save_state_header_t)) return 0;
    const save_state_header_t *h = (const save_state_header_t *)buf;
    if (h->magic   != SAVE_STATE_MAGIC)   return 0;
    if (h->version != SAVE_STATE_VERSION) return 0;
    if (!g_initialized) save_state_init();
    if (h->layout_hash != g_layout_hash)  return 0;
    if (h->total_size > size)             return 0;
    if (h->total_size != save_state_buffer_capacity()) return 0;

    const uint8_t *p   = buf + sizeof(*h);
    const uint8_t *end = buf + h->total_size;
    for (uint32_t i = 0; i < runtime_tracked_region_count; i++) {
        const runtime_tracked_region_t *r = runtime_tracked_regions[i];
        if (p + r->size > end) return 0;
        if (!r->load(p, r->size)) return 0;
        p += r->size;
    }
    return p == end;
}

/* Hex emission.  Uses the same printf surface that Spike D's digest
 * emitter uses; the harness greps for `^BUFFER ` lines exactly.  No
 * whitespace inside the hex string — a single `[0-9a-f]+` token. */
void save_state_emit_hex(const uint8_t *buf, uint32_t size)
{
    static const char hex[] = "0123456789abcdef";
    /* lua_runtime.c's vprintf uses a 1 KiB internal buffer and truncates
     * to (sizeof - 1) chars, silently dropping the final character of any
     * printf("%s") that produces ≥1024 chars.  We chunk 256 source bytes
     * (512 hex chars + NUL) per printf to stay well under the cap. */
    char chunk[513];
    uint32_t emitted_bytes = 0;
    const save_state_header_t *h = (const save_state_header_t *)buf;
    printf("BUFFER %u ", (unsigned)h->frame);
    while (emitted_bytes < size) {
        uint32_t n = size - emitted_bytes;
        if (n > 256) n = 256;
        for (uint32_t i = 0; i < n; i++) {
            uint8_t b = buf[emitted_bytes + i];
            chunk[i * 2 + 0] = hex[(b >> 4) & 0xf];
            chunk[i * 2 + 1] = hex[b & 0xf];
        }
        chunk[n * 2] = '\0';
        printf("%s", chunk);
        emitted_bytes += n;
    }
    printf("\n");
}

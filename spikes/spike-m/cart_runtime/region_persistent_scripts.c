/* Spike M — persistent-script slot table region implementation. */

#include <stdint.h>
#include <stddef.h>

#include "region_persistent_scripts.h"

static runtime_persistent_scripts_t g_region;

runtime_persistent_scripts_t *region_persistent_scripts_get(void) { return &g_region; }

void persistent_scripts_reset(void)
{
    uint8_t *p = (uint8_t *)&g_region;
    for (size_t i = 0; i < sizeof(g_region); i++) p[i] = 0;
}

void persistent_scripts_unmark_all(void)
{
    g_region.active_bits[0] = 0;
    g_region.active_bits[1] = 0;
}

static inline int slot_in_range(int slot)
{
    return (slot >= 0 && slot < MAX_PERSISTENT_SCRIPTS);
}

static inline int bit_get(int slot)
{
    return (g_region.active_bits[slot >> 5] >> (slot & 31)) & 1u;
}

static inline void bit_set(int slot)
{
    g_region.active_bits[slot >> 5] |= (1u << (slot & 31));
}

static inline void bit_clear(int slot)
{
    g_region.active_bits[slot >> 5] &= ~(1u << (slot & 31));
}

int persistent_scripts_alloc(void)
{
    /* Dense allocation — first free slot wins.  Stable across save/restore
     * because the cart's `create` calls execute in the same order on a
     * fresh run and on a load (the load deserializer fills the bitmap
     * before the cart's first `create`, but `alloc` skips occupied slots,
     * so the cart's calls match up by index). */
    for (int s = 0; s < MAX_PERSISTENT_SCRIPTS; s++) {
        if (!bit_get(s)) {
            bit_set(s);
            /* Don't touch slot_lens or slot bytes — on a load resume,
             * the deserialized blob's length is preserved here so the
             * cart's first script_read_blob(s) returns it.  On a fresh
             * allocation, persistent_scripts_reset() / _free() already
             * zeroed both, so this leaves the slot in the same state. */
            return s;
        }
    }
    return -1;
}

void persistent_scripts_free(int slot)
{
    if (!slot_in_range(slot)) return;
    if (!bit_get(slot)) return;
    bit_clear(slot);
    g_region.slot_lens[slot] = 0;
    for (uint32_t i = 0; i < PERSISTENT_SCRIPT_SLOT_BYTES; i++)
        g_region.slots[slot][i] = 0;
}

int persistent_scripts_write(int slot, const uint8_t *src, uint32_t n)
{
    if (!slot_in_range(slot)) return 0;
    if (!bit_get(slot)) return 0;
    if (n > PERSISTENT_SCRIPT_SLOT_BYTES) return 0;
    for (uint32_t i = 0; i < n; i++) g_region.slots[slot][i] = src[i];
    /* Zero-fill the unused tail so the saved blob is deterministic
     * regardless of stale bytes from a previous, longer write. */
    for (uint32_t i = n; i < PERSISTENT_SCRIPT_SLOT_BYTES; i++)
        g_region.slots[slot][i] = 0;
    g_region.slot_lens[slot] = (uint16_t)n;
    return 1;
}

int persistent_scripts_read(int slot, uint8_t *dst, uint32_t cap)
{
    if (!slot_in_range(slot)) return -1;
    if (!bit_get(slot)) return -1;
    uint16_t n = g_region.slot_lens[slot];
    if (n > cap) return -1;
    for (uint16_t i = 0; i < n; i++) dst[i] = g_region.slots[slot][i];
    return (int)n;
}

int persistent_scripts_is_active(int slot)
{
    if (!slot_in_range(slot)) return 0;
    return bit_get(slot);
}

uint32_t persistent_scripts_active_count(void)
{
    uint32_t c = 0;
    for (int s = 0; s < MAX_PERSISTENT_SCRIPTS; s++) c += (uint32_t)bit_get(s);
    return c;
}

uint16_t persistent_scripts_blob_len(int slot)
{
    if (!slot_in_range(slot)) return 0;
    if (!bit_get(slot)) return 0;
    return g_region.slot_lens[slot];
}

/* ── tracked region callbacks ────────────────────────────────────────────── */

static void describe(string_sink_t *out)
{
    /* Layout:
     *   active_bits[2]:u32@<offset>,
     *   slot_lens[N]:u16@<offset>,
     *   slots[N][SZ]:u8@<offset>
     */
    string_sink_puts(out, "active_bits[2]:u32@");
    string_sink_putu(out, offsetof(runtime_persistent_scripts_t, active_bits));
    string_sink_putc(out, ',');
    string_sink_puts(out, "slot_lens[");
    string_sink_putu(out, MAX_PERSISTENT_SCRIPTS);
    string_sink_puts(out, "]:u16@");
    string_sink_putu(out, offsetof(runtime_persistent_scripts_t, slot_lens));
    string_sink_putc(out, ',');
    string_sink_puts(out, "slots[");
    string_sink_putu(out, MAX_PERSISTENT_SCRIPTS);
    string_sink_puts(out, "][");
    string_sink_putu(out, PERSISTENT_SCRIPT_SLOT_BYTES);
    string_sink_puts(out, "]:u8@");
    string_sink_putu(out, offsetof(runtime_persistent_scripts_t, slots));
}

static void save(uint8_t *dst)
{
    /* No NaN canonicalization — payload is opaque bytes from the
     * flattener, which already canonicalizes f64 fields at flatten
     * time.  Just memcpy. */
    const uint8_t *src = (const uint8_t *)&g_region;
    for (size_t i = 0; i < sizeof(g_region); i++) dst[i] = src[i];
}

static int load(const uint8_t *src, uint32_t n)
{
    if (n != sizeof(g_region)) return 0;
    uint8_t *dst = (uint8_t *)&g_region;
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    return 1;
}

const runtime_tracked_region_t region_persistent_scripts = {
    .name     = "persistent_scripts",
    .size     = (uint32_t)sizeof(runtime_persistent_scripts_t),
    .describe = describe,
    .save     = save,
    .load     = load,
};

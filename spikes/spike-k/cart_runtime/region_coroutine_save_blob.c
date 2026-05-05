/* Spike K — coroutine save-blob region implementation. */

#include <stdint.h>
#include <stddef.h>

#include "region_coroutine_save_blob.h"

static runtime_coroutine_save_blob_t g_blob;

runtime_coroutine_save_blob_t *region_coroutine_save_blob_get(void) { return &g_blob; }

void coroutine_blob_activate(uint8_t slot)
{
    if (slot >= COROUTINE_SAVE_BLOB_MAX_SLOTS) return;
    g_blob.active_slots_bits |= (1u << slot);
}

void coroutine_blob_write(uint8_t slot, const void *src, uint32_t n)
{
    if (slot >= COROUTINE_SAVE_BLOB_MAX_SLOTS) return;
    if (n > COROUTINE_SAVE_BLOB_SLOT_SIZE) n = COROUTINE_SAVE_BLOB_SLOT_SIZE;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) g_blob.slots[slot][i] = s[i];
}

int coroutine_blob_read(uint8_t slot, void *dst, uint32_t n)
{
    if (slot >= COROUTINE_SAVE_BLOB_MAX_SLOTS) return 0;
    if (!(g_blob.active_slots_bits & (1u << slot))) return 0;
    if (n > COROUTINE_SAVE_BLOB_SLOT_SIZE) n = COROUTINE_SAVE_BLOB_SLOT_SIZE;
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = g_blob.slots[slot][i];
    return 1;
}

int coroutine_blob_is_active(uint8_t slot)
{
    if (slot >= COROUTINE_SAVE_BLOB_MAX_SLOTS) return 0;
    return (g_blob.active_slots_bits & (1u << slot)) ? 1 : 0;
}

/* ── tracked region callbacks ────────────────────────────────────────────── */

static void describe(string_sink_t *out)
{
    string_sink_puts(out, "active_slots_bits:u32@");
    string_sink_putu(out, offsetof(runtime_coroutine_save_blob_t, active_slots_bits));
    string_sink_putc(out, ',');
    string_sink_puts(out, "slots[");
    string_sink_putu(out, COROUTINE_SAVE_BLOB_MAX_SLOTS);
    string_sink_puts(out, "][");
    string_sink_putu(out, COROUTINE_SAVE_BLOB_SLOT_SIZE);
    string_sink_puts(out, "]:u8@");
    string_sink_putu(out, offsetof(runtime_coroutine_save_blob_t, slots));
}

static void save(uint8_t *dst)
{
    const uint8_t *src = (const uint8_t *)&g_blob;
    for (size_t i = 0; i < sizeof(g_blob); i++) dst[i] = src[i];
}

static int load(const uint8_t *src, uint32_t n)
{
    if (n != sizeof(g_blob)) return 0;
    uint8_t *dst = (uint8_t *)&g_blob;
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    return 1;
}

const runtime_tracked_region_t region_coroutine_save_blob = {
    .name     = "coroutine_save_blob",
    .size     = (uint32_t)sizeof(runtime_coroutine_save_blob_t),
    .describe = describe,
    .save     = save,
    .load     = load,
};

/* Spike M — persistent-script slot table region.
 *
 * Backs `blyt32.coroutine.create(function(ctx) ... end, seed?)`.  Each
 * occupied slot stores the flattened bytes of one persistent script's
 * `ctx` table.  The Lua wrapper writes those bytes via
 * `console.script_write_blob(slot, bytes)` after every resume; on save,
 * the runtime serializes the region byte-for-byte; on load, the bytes
 * are deserialized back into the slot table; the cart's first call to
 * `blyt32.coroutine.create(body, seed)` for each script then reads the
 * slot's bytes via `console.script_read_blob(slot)` and uses them as
 * the deserialized `ctx` (overriding the seed).
 *
 * Sizing decision: `MAX_PERSISTENT_SCRIPTS = 64` and slot blobs of 256
 * bytes each.  PLAN.md specifies 256 / 4096; the spike's workloads use
 * `ctx` tables under 64 bytes, so 64 × 256 is generous while keeping
 * the save buffer footprint to ~16 KiB.  TASKS.md flags the deviation
 * as a follow-up — production should scale via manifest (ADR-0009).
 *
 * The region's describe() output (used to compute the layout hash)
 * pins the slot count, slot-size, and field order — any change makes
 * existing buffers unloadable, exactly as for the other regions.
 */

#ifndef CART_RUNTIME_REGION_PERSISTENT_SCRIPTS_H
#define CART_RUNTIME_REGION_PERSISTENT_SCRIPTS_H

#include <stdint.h>
#include "runtime_tracked.h"

#define MAX_PERSISTENT_SCRIPTS    64
#define PERSISTENT_SCRIPT_SLOT_BYTES 256

typedef struct __attribute__((packed)) {
    /* Bit i ⇔ slot i is in use.  Two u32s give 64 bits. */
    uint32_t active_bits[2];
    /* Per-slot count of valid bytes in slots[i] (0 ≤ len ≤ 256). */
    uint16_t slot_lens[MAX_PERSISTENT_SCRIPTS];
    /* Per-slot opaque bytes — the flattened-`ctx` payload. */
    uint8_t  slots[MAX_PERSISTENT_SCRIPTS][PERSISTENT_SCRIPT_SLOT_BYTES];
} runtime_persistent_scripts_t;

extern const runtime_tracked_region_t region_persistent_scripts;

runtime_persistent_scripts_t *region_persistent_scripts_get(void);

/* Cart-side helpers — exposed to Lua via the `console.script_*` bindings.
 *
 * Return-value conventions:
 *   script_alloc()   slot index in [0, MAX_PERSISTENT_SCRIPTS), or -1 if
 *                    every slot is occupied (BLYT_ERR_SLOT_EXHAUSTED).
 *   script_free(s)   no-op if `s` is out of range or already free.
 *   script_write     truncates if `n > PERSISTENT_SCRIPT_SLOT_BYTES` and
 *                    returns 0; otherwise stores and returns 1.
 *   script_read      returns the stored byte count (0 ≤ len ≤ 256), or
 *                    a negative error if `s` is out of range / inactive.
 *   script_is_active 0 / 1.
 *   script_active_count walks the bitmap; cheap for 64 slots.
 */
int      persistent_scripts_alloc(void);
void     persistent_scripts_free(int slot);
int      persistent_scripts_write(int slot, const uint8_t *src, uint32_t n);
int      persistent_scripts_read(int slot, uint8_t *dst, uint32_t cap);
int      persistent_scripts_is_active(int slot);
uint32_t persistent_scripts_active_count(void);
uint16_t persistent_scripts_blob_len(int slot);

/* Reset the region to all-empty.  Called at cart init before any allocation. */
void     persistent_scripts_reset(void);

/* Clear active_bits, *preserving* slot_lens and slot bytes.  Called by the
 * load-side driver after `save_state_load` so the cart's `create` calls
 * allocate slots starting from 0 again — picking up the saved bytes via
 * `script_read_blob` as each call hits its corresponding slot. */
void     persistent_scripts_unmark_all(void);

#endif /* CART_RUNTIME_REGION_PERSISTENT_SCRIPTS_H */

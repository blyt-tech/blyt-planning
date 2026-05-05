/* Spike K — coroutine save-blob region (ADR-0012, simplified).
 *
 * Production ADR-0012 specifies that
 * `blyt32.coroutine.create{save = function(ctx) return {...} end}`
 * returns an arbitrary Lua table that the runtime is responsible for
 * serializing.  Recursive Lua-table serialization with a strict POD
 * type set is engineering on top of the byte-image round-trip property
 * this spike validates — explicitly out of spike scope per PLAN.md
 * § "Coroutine save-hook output is treated as opaque bytes".
 *
 * Spike K's simplification: each persistent coroutine is given a
 * fixed-size POD save struct (declared per-cart at compile time, not
 * derived from a Lua table return value).  The save callback writes
 * into the slot's bytes; the restore callback reads from them.  The
 * cross-host save mechanism is identical to any other tracked region.
 *
 * The fixed slot table holds up to MAX coroutines × N bytes per slot.
 * Carts that use the region declare which slots are active and what
 * shape their POD struct has — encoded in the layout descriptor.
 */

#ifndef CART_RUNTIME_REGION_COROUTINE_SAVE_BLOB_H
#define CART_RUNTIME_REGION_COROUTINE_SAVE_BLOB_H

#include <stdint.h>
#include "runtime_tracked.h"

#define COROUTINE_SAVE_BLOB_MAX_SLOTS 4
#define COROUTINE_SAVE_BLOB_SLOT_SIZE 32   /* bytes per slot */

typedef struct __attribute__((packed)) {
    uint32_t active_slots_bits;     /* bit i ⇔ slot i is in use */
    uint8_t  slots[COROUTINE_SAVE_BLOB_MAX_SLOTS][COROUTINE_SAVE_BLOB_SLOT_SIZE];
} runtime_coroutine_save_blob_t;

extern const runtime_tracked_region_t region_coroutine_save_blob;

runtime_coroutine_save_blob_t *region_coroutine_save_blob_get(void);

/* Cart-side helpers. */
void   coroutine_blob_activate(uint8_t slot);
void   coroutine_blob_write(uint8_t slot, const void *src, uint32_t n);
int    coroutine_blob_read(uint8_t slot, void *dst, uint32_t n);
int    coroutine_blob_is_active(uint8_t slot);

#endif /* CART_RUNTIME_REGION_COROUTINE_SAVE_BLOB_H */

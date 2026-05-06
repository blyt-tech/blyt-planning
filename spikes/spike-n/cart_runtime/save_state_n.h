/* Spike N — migration-aware save-state load extension.
 *
 * Spike K's save_state_load() rejects on layout_hash mismatch —
 * that is the correct safe default.  Spike N adds a migration-aware
 * variant that, on a hash mismatch, consults the per-region migration
 * table declared below and calls migrate_region_bytes() instead of
 * failing hard.
 *
 * Usage pattern:
 *   1. Declare migrate_region_t entries (one per region that may change).
 *   2. Point the global migrate_regions[] array at them.
 *   3. Call save_state_load_migrate() exactly where the load driver
 *      would call save_state_load().
 *
 * If NO region changed (layout_hash match), save_state_load_migrate()
 * falls through to the ordinary save_state_load() path — no overhead,
 * no different semantics.
 */

#ifndef CART_RUNTIME_SAVE_STATE_N_H
#define CART_RUNTIME_SAVE_STATE_N_H

#include <stdint.h>
#include <stddef.h>

#include "migrate.h"

/* Per-region migration descriptor.  Registered in the cart's link unit
 * alongside the region_<name>.c file.  The `name` must match the string
 * in the runtime_tracked_region_t so the migration walk can correlate
 * them. */
typedef struct {
    const char       *name;        /* must match runtime_tracked_region.name */
    uint32_t          old_size;    /* pre-edit region byte size */
    const mlayout_t  *old_layout;  /* pre-edit field descriptors */
    const mlayout_t  *new_layout;  /* post-edit field descriptors */
    mretype_fn_t      on_retype;   /* NULL → drop retyped fields */
} migrate_region_t;

/* Cart link unit provides these; defaults to empty (NULL / 0) if not
 * declared, in which case save_state_load_migrate falls through to the
 * ordinary load path. */
extern const migrate_region_t *migrate_regions[];
extern uint32_t                migrate_region_count;

/* Like save_state_load() from spike-k, but:
 *   - On a layout_hash MATCH: behaves identically to save_state_load().
 *   - On a layout_hash MISMATCH: walks each region that has a migration
 *     descriptor in migrate_regions[] and calls migrate_region_bytes()
 *     for it.  Regions without a descriptor use the raw bytes from the
 *     old buffer (which works as long as their layout is unchanged).
 *     Returns 1 on success, 0 on any failure (magic/version mismatch,
 *     truncated buffer, migration walk error).
 *
 * The load is atomic: on any failure, no tracked region has been
 * mutated (the walk first validates all sizes, then applies). */
int save_state_load_migrate(const uint8_t *buf, uint32_t size);

/* Expose the raw old-layout buffer pointer + per-region-size for the
 * migration walk without re-parsing the wire format.  Used by the
 * native driver's BUFFER emission step. */
uint32_t save_state_get_frame(const uint8_t *buf);

#endif /* CART_RUNTIME_SAVE_STATE_N_H */

/* Spike N Stage 2 — native edit cart region registry.
 *
 * Compile with -DEDIT_NUM=N -DEDIT_SIDE=0/1 -DNATIVE_STATE_VERSION=V.
 *
 * The POST (load) ELF also declares the migration descriptors so the
 * save_state_load_migrate() call in n_native_driver_load.c can walk them.
 *
 * Migration descriptor declarations for each edit:
 *   n3 POST: v0 → v3 (add last_score, zero-init)
 *   n4 POST: v3 → v4 (remove unused_count)
 *   n5 POST: v4 → v5 (retype frame_count i32→i64)
 *   n6 POST: v5 → v6 (reorder — name-based match handles it without callback)
 *
 * n1 and n2 POST: layout_hash matches (no struct change), so
 * save_state_load_migrate falls through to save_state_load.  No migration
 * descriptor needed; the weak-sentinel defaults in save_state_n.c apply.
 */

#include "../../cart_runtime/runtime_tracked.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_native_state.h"
#include "../../cart_runtime/save_state_n.h"

const runtime_tracked_region_t *runtime_tracked_regions[] = {
    &region_frame_state,
    &region_native_state,
};

const uint32_t runtime_tracked_region_count =
    sizeof(runtime_tracked_regions) / sizeof(runtime_tracked_regions[0]);

/* ── Migration descriptors (POST side only for schema-changing edits) */

#if defined(EDIT_SIDE) && EDIT_SIDE == 1

#if EDIT_NUM == 3
/* n3 POST: v0 (12 bytes) → v3 (16 bytes, add last_score zero-init) */
static const migrate_region_t migrate_native_n3 = {
    "native_state",
    12,   /* old_size = sizeof(v0) */
    &native_state_layout_v0,
    &native_state_layout_v3,
    0     /* on_retype = NULL (no retyped fields) */
};
const migrate_region_t *migrate_regions[] = { &migrate_native_n3 };
uint32_t                migrate_region_count = 1;

#elif EDIT_NUM == 4
/* n4 POST: v3 (16 bytes) → v4 (12 bytes, drop unused_count) */
static const migrate_region_t migrate_native_n4 = {
    "native_state",
    16,   /* old_size = sizeof(v3) */
    &native_state_layout_v3,
    &native_state_layout_v4,
    0
};
const migrate_region_t *migrate_regions[] = { &migrate_native_n4 };
uint32_t                migrate_region_count = 1;

#elif EDIT_NUM == 5
/* n5 POST: v4 (12 bytes) → v5 (16 bytes, retype frame_count i32→i64) */
static const migrate_region_t migrate_native_n5 = {
    "native_state",
    12,   /* old_size = sizeof(v4) */
    &native_state_layout_v4,
    &native_state_layout_v5,
    native_state_retype_i32_to_i64
};
const migrate_region_t *migrate_regions[] = { &migrate_native_n5 };
uint32_t                migrate_region_count = 1;

#elif EDIT_NUM == 6
/* n6 POST: v5 (16 bytes) → v6 (16 bytes, reorder fields).
 * Name-based matching in migrate_region_bytes() handles this — no
 * on_retype callback needed since types are unchanged. */
static const migrate_region_t migrate_native_n6 = {
    "native_state",
    16,   /* old_size = sizeof(v5) */
    &native_state_layout_v5,
    &native_state_layout_v6,
    0
};
const migrate_region_t *migrate_regions[] = { &migrate_native_n6 };
uint32_t                migrate_region_count = 1;

#endif  /* EDIT_NUM switch for POST migration descriptors */

#endif  /* EDIT_SIDE == 1 */

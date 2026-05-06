/* Spike N — native cart POD state region (all versions).
 *
 * The native cart's tracked state evolves through six struct versions
 * as the edit suite applies n1-n6.  The NATIVE_STATE_VERSION compile-
 * time flag selects which version is compiled into a given ELF.
 *
 * Version history:
 *   v0  baseline (n1, n2):   { i32 frame_count; i32 some_value; i32 unused_count; }
 *   v3  after n3 (add):      { i32 frame_count; i32 some_value; i32 unused_count; i32 last_score; }
 *   v4  after n4 (remove):   { i32 frame_count; i32 some_value; i32 last_score; }
 *   v5  after n5 (retype):   { i64 frame_count; i32 some_value; i32 last_score; }
 *   v6  after n6 (reorder):  { i32 some_value; i32 last_score; i64 frame_count; }
 *
 * (v1 and v2 reuse v0's layout — pure code changes, no struct change.)
 *
 * The migration descriptors for each transition are also declared here
 * and implemented in region_native_state.c; the load driver links the
 * appropriate pair via EDIT_NUM / EDIT_SIDE flags.
 */

#ifndef CART_RUNTIME_REGION_NATIVE_STATE_H
#define CART_RUNTIME_REGION_NATIVE_STATE_H

#include <stdint.h>
#include "runtime_tracked.h"
#include "migrate.h"

/* ── Struct definitions ────────────────────────────────────────────── */

#ifndef NATIVE_STATE_VERSION
#  define NATIVE_STATE_VERSION 0
#endif

#if NATIVE_STATE_VERSION == 0
typedef struct __attribute__((packed)) {
    int32_t  frame_count;
    int32_t  some_value;
    int32_t  unused_count;
} native_cart_state_t;

#elif NATIVE_STATE_VERSION == 3
typedef struct __attribute__((packed)) {
    int32_t  frame_count;
    int32_t  some_value;
    int32_t  unused_count;
    int32_t  last_score;
} native_cart_state_t;

#elif NATIVE_STATE_VERSION == 4
typedef struct __attribute__((packed)) {
    int32_t  frame_count;
    int32_t  some_value;
    int32_t  last_score;
} native_cart_state_t;

#elif NATIVE_STATE_VERSION == 5
typedef struct __attribute__((packed)) {
    int64_t  frame_count;
    int32_t  some_value;
    int32_t  last_score;
} native_cart_state_t;

#elif NATIVE_STATE_VERSION == 6
typedef struct __attribute__((packed)) {
    int32_t  some_value;
    int32_t  last_score;
    int64_t  frame_count;
} native_cart_state_t;

#else
#  error "Unknown NATIVE_STATE_VERSION — must be 0, 3, 4, 5, or 6"
#endif

/* Runtime-tracked region for the native cart state. */
extern const runtime_tracked_region_t region_native_state;

/* Get a pointer to the live native cart state. */
native_cart_state_t *region_native_state_get(void);

/* Per-transition mlayout_t descriptors (used by the load driver to
 * declare its migration_region_t entries). */
extern const mlayout_t native_state_layout_v0;
extern const mlayout_t native_state_layout_v3;
extern const mlayout_t native_state_layout_v4;
extern const mlayout_t native_state_layout_v5;
extern const mlayout_t native_state_layout_v6;

/* on_retype callback for n5 (i32 → i64 widen). */
int native_state_retype_i32_to_i64(const uint8_t *old_field, mfield_type_t old_type,
                                     uint8_t *new_field, mfield_type_t new_type);

#endif /* CART_RUNTIME_REGION_NATIVE_STATE_H */

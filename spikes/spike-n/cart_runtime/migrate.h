/* Spike N — field-level migration API (ADR-0045).
 *
 * When a tracked region's layout_hash changes between save and load
 * (because the cart code was edited and reloaded), the runtime walks
 * the migration policy defined here instead of failing hard.
 *
 * ADR-0045 names four sub-cases for each field:
 *   Matching field (same name, same type)  → copy bytes verbatim
 *   Added field   (in new, not in old)     → zero-initialise
 *   Removed field (in old, not in new)     → drop silently
 *   Retyped field (same name, diff type)   → invoke on_retype callback;
 *                                             if callback is NULL the field
 *                                             is dropped (and a warning is
 *                                             printed via printf).
 *
 * The migration walk is driven by the mlayout_t descriptors that each
 * region registers alongside its ordinary save/load callbacks.  The
 * descriptors mirror the struct's fields in declaration order.
 */

#ifndef CART_RUNTIME_MIGRATE_H
#define CART_RUNTIME_MIGRATE_H

#include <stdint.h>
#include <stddef.h>

/* Primitive field types supported by the migration walk.  These match
 * the types Spike K's layout-hash description emits ("u32", "i64",
 * "f32", "f64").  The walk uses the enum for type-compatibility checks
 * and for passing to the on_retype callback. */
typedef enum {
    MTYPE_U8  = 0,
    MTYPE_U32 = 1,
    MTYPE_I32 = 2,
    MTYPE_U64 = 3,
    MTYPE_I64 = 4,
    MTYPE_F32 = 5,
    MTYPE_F64 = 6,
} mfield_type_t;

/* Descriptor for a single struct field.  Mirrors ADR-0010's typed-buffer
 * layout: name is stable across edits (it is the matching key), type and
 * offset are the post-edit values, size is sizeof for the type (1/4/4/8/8
 * for u8/u32/i32/u64/i64 and 4/8 for f32/f64). */
typedef struct {
    const char   *name;
    mfield_type_t type;
    uint32_t      offset;   /* byte offset within the struct */
    uint32_t      size;     /* sizeof(field) in bytes */
} mfield_desc_t;

/* Descriptor for a complete struct layout (one region's worth). */
typedef struct {
    const mfield_desc_t *fields;
    int                  count;
    uint32_t             total_size;   /* sizeof the whole struct */
} mlayout_t;

/* Callback invoked when a field with the same name exists in both old
 * and new layouts but with different types.  The callback may convert
 * the old value into the new type (writing into new_field) and return 1,
 * or return 0 to drop the field (new_field remains zero).
 *
 * old_field  points at exactly old_size bytes of old data.
 * new_field  points at exactly new_size bytes (pre-zeroed) for the output.
 */
typedef int (*mretype_fn_t)(const uint8_t *old_field, mfield_type_t old_type,
                             uint8_t *new_field, mfield_type_t new_type);

/* Migrate region bytes from old layout to new layout.
 *
 * new_bytes  is zeroed on entry; the walk writes each surviving field.
 * old_bytes  is the pre-edit region bytes (read-only).
 * on_retype  may be NULL; if so, retyped fields are dropped.
 *
 * Returns 1 on success, 0 on internal error (e.g. descriptor offset
 * out of range — indicates a malformed mlayout_t).
 *
 * Does NOT validate total_size against new_layout->total_size; the
 * caller is responsible for sizing new_bytes accordingly. */
int migrate_region_bytes(uint8_t *new_bytes, const mlayout_t *new_layout,
                          const uint8_t *old_bytes, const mlayout_t *old_layout,
                          mretype_fn_t on_retype);

/* Helper: return a stable ASCII name for a field type.  Used in warning
 * diagnostics emitted by the migration walk when on_retype is NULL. */
const char *mfield_type_name(mfield_type_t t);

#endif /* CART_RUNTIME_MIGRATE_H */

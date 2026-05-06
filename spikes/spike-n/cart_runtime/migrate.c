/* Spike N — ADR-0045 migration walk implementation.
 *
 * See migrate.h for the API and the four sub-cases.
 *
 * The walk is O(new_count × old_count) — acceptable because the
 * spike's structs have at most ~8 fields.  A production implementation
 * would sort by name and binary-search; that optimisation is flagged
 * in the result write-up as a recommended follow-up.
 *
 * No stdlib headers beyond stdint/stddef — same rule as all spike-n
 * freestanding C.
 */

#include <stdint.h>
#include <stddef.h>

#include "migrate.h"

extern int printf(const char *, ...);

/* Compare two C strings without including <string.h>. */
static int str_eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == *b);
}

/* Zero-fill new_bytes — called at the top of migrate_region_bytes so
 * added fields start at zero per ADR-0045's "zero-initialise" rule. */
static void zero_fill(uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = 0;
}

/* Copy n bytes from src to dst — avoids memcpy dependency. */
static void copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

const char *mfield_type_name(mfield_type_t t)
{
    switch (t) {
    case MTYPE_U8:  return "u8";
    case MTYPE_U32: return "u32";
    case MTYPE_I32: return "i32";
    case MTYPE_U64: return "u64";
    case MTYPE_I64: return "i64";
    case MTYPE_F32: return "f32";
    case MTYPE_F64: return "f64";
    default:        return "unknown";
    }
}

int migrate_region_bytes(uint8_t *new_bytes, const mlayout_t *new_layout,
                          const uint8_t *old_bytes, const mlayout_t *old_layout,
                          mretype_fn_t on_retype)
{
    /* Step 0: zero-init the output (added fields land here). */
    zero_fill(new_bytes, new_layout->total_size);

    /* Step 1: walk every field in the NEW layout and look it up in the
     * old layout.  This covers:
     *   - Matching field (found in old, same type) → copy.
     *   - Retyped field  (found in old, diff type) → on_retype / drop.
     *   - Added field    (not found in old)        → already zeroed.
     * Removed fields (in old, not in new) are implicitly dropped — we
     * simply never write them into new_bytes. */
    for (int ni = 0; ni < new_layout->count; ni++) {
        const mfield_desc_t *nf = &new_layout->fields[ni];

        /* Sanity: new field offset + size must fit within new struct. */
        if (nf->offset + nf->size > new_layout->total_size) {
            printf("migrate: new field '%s' offset+size overflows struct\n",
                   nf->name);
            return 0;
        }

        /* Look up this field's name in the old layout. */
        const mfield_desc_t *of = 0;
        for (int oi = 0; oi < old_layout->count; oi++) {
            if (str_eq(new_layout->fields[ni].name, old_layout->fields[oi].name)) {
                of = &old_layout->fields[oi];
                break;
            }
        }

        if (!of) {
            /* Added field — already zeroed, nothing to do. */
            continue;
        }

        /* Sanity: old field offset + size must fit within old struct. */
        if (of->offset + of->size > old_layout->total_size) {
            printf("migrate: old field '%s' offset+size overflows old struct\n",
                   of->name);
            return 0;
        }

        if (of->type == nf->type && of->size == nf->size) {
            /* Matching field — copy bytes verbatim. */
            copy_bytes(new_bytes + nf->offset, old_bytes + of->offset, nf->size);
        } else {
            /* Retyped field — invoke on_retype or drop with warning. */
            if (on_retype) {
                int ok = on_retype(old_bytes + of->offset, of->type,
                                   new_bytes + nf->offset, nf->type);
                if (!ok) {
                    printf("migrate: on_retype dropped field '%s' (%s -> %s)\n",
                           nf->name, mfield_type_name(of->type),
                           mfield_type_name(nf->type));
                }
            } else {
                printf("migrate: dropping retyped field '%s' (%s -> %s)"
                       " — no on_retype callback\n",
                       nf->name, mfield_type_name(of->type),
                       mfield_type_name(nf->type));
            }
        }
    }

    return 1;
}

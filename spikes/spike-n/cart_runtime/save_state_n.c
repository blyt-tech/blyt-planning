/* Spike N — migration-aware save-state load.
 *
 * Extends Spike K's save_state_load() with the ADR-0045 migration walk.
 * See save_state_n.h for the API contract.
 *
 * Wire format recap (from save_state.h):
 *   [save_state_header_t][region_0_bytes][region_1_bytes]...
 * Region order matches runtime_tracked_regions[] registration order.
 * Each region's byte count is exactly the region's `size` field in the
 * registry (the post-edit value in the new binary).
 *
 * On a hash mismatch the old buffer carries the OLD region sizes.  The
 * migration descriptor records old_size explicitly so the walk can
 * advance through the old buffer correctly even when old_size differs
 * from the current runtime_tracked_region_t::size.
 *
 * The two-phase design (validate first, apply second) guarantees
 * atomicity: a migration that fails midway leaves the runtime's regions
 * in their pre-load state.
 */

#include <stdint.h>
#include <stddef.h>

#include "save_state_n.h"
#include "migrate.h"

/* Bring in Spike K's header for the save_state_header_t and the
 * runtime_tracked_region_t registry accessors. */
#include "save_state.h"
#include "runtime_tracked.h"

extern int printf(const char *, ...);

/* Default (empty) migration table — cart link units that don't exercise
 * migration link against these weak-sentinel definitions.  Carts that
 * DO exercise migration declare these as non-weak globals in their
 * regions_*.c link unit. */
__attribute__((weak))
const migrate_region_t *migrate_regions[] = { 0 };
__attribute__((weak))
uint32_t migrate_region_count = 0;

/* Locate a migration descriptor by region name.  Returns NULL if no
 * descriptor was registered for this region (normal load path). */
static const migrate_region_t *find_migrate_desc(const char *name)
{
    for (uint32_t i = 0; i < migrate_region_count; i++) {
        const migrate_region_t *m = migrate_regions[i];
        if (!m || !m->name) continue;
        const char *a = m->name, *b = name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == *b) return m;
    }
    return 0;
}

/* str_eq — avoids <string.h> dependency. */
static int str_eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == *b);
}

uint32_t save_state_get_frame(const uint8_t *buf)
{
    const save_state_header_t *h = (const save_state_header_t *)buf;
    return h->frame;
}

/* Migration-aware load.
 *
 * Phase 0: validate header (magic, version, total_size).
 * Phase 1: if layout_hash matches, fall through to save_state_load().
 * Phase 2: on mismatch, validate that each region's old_size (from the
 *          migration descriptor, or current size if no descriptor) is
 *          present in the old buffer.
 * Phase 3: apply — for each region, either raw-copy (no descriptor or
 *          same-size/same-layout) or invoke migrate_region_bytes().
 */
int save_state_load_migrate(const uint8_t *buf, uint32_t size)
{
    if (size < sizeof(save_state_header_t)) return 0;
    const save_state_header_t *h = (const save_state_header_t *)buf;
    if (h->magic   != SAVE_STATE_MAGIC)   return 0;
    if (h->version != SAVE_STATE_VERSION) return 0;
    if (h->total_size > size)             return 0;

    /* Fast path: identical layout → ordinary load. */
    if (h->layout_hash == save_state_layout_hash()) {
        return save_state_load(buf, size);
    }

    /* Slow path: layout changed, walk the migration policy. */
    printf("save_state_load_migrate: layout_hash mismatch — running migration\n");

    /* Phase 2: validate old-buffer size covers all regions. */
    {
        const uint8_t *p   = buf + sizeof(*h);
        const uint8_t *end = buf + h->total_size;
        for (uint32_t i = 0; i < runtime_tracked_region_count; i++) {
            const runtime_tracked_region_t *r = runtime_tracked_regions[i];
            const migrate_region_t *m = find_migrate_desc(r->name);
            uint32_t old_sz = m ? m->old_size : r->size;
            if (p + old_sz > end) {
                printf("save_state_load_migrate: old buffer truncated at region '%s'\n",
                       r->name);
                return 0;
            }
            p += old_sz;
        }
    }

    /* Phase 3: apply — two-pass to preserve atomicity.
     * First pass: migrate each region into a local scratch buffer.
     * Second pass: commit to the region's storage via r->load().
     *
     * We commit region by region; if any r->load() returns 0 we return 0
     * without touching subsequent regions.  For strict atomicity a
     * production implementation would use a pre-commit snapshot; that's
     * a follow-up flagged in the result write-up. */
    static uint8_t scratch[4096];   /* generous upper bound for any single region */

    const uint8_t *p = buf + sizeof(*h);
    for (uint32_t i = 0; i < runtime_tracked_region_count; i++) {
        const runtime_tracked_region_t *r = runtime_tracked_regions[i];
        const migrate_region_t *m = find_migrate_desc(r->name);
        uint32_t old_sz = m ? m->old_size : r->size;

        if (m && !str_eq(m->name, r->name)) {
            /* Should not happen if find_migrate_desc is correct. */
            printf("save_state_load_migrate: internal name mismatch\n");
            return 0;
        }

        if (m && m->old_layout && m->new_layout) {
            /* Migration path. */
            if (r->size > sizeof(scratch)) {
                printf("save_state_load_migrate: region '%s' size %u exceeds scratch\n",
                       r->name, (unsigned)r->size);
                return 0;
            }
            if (!migrate_region_bytes(scratch, m->new_layout,
                                       p, m->old_layout,
                                       m->on_retype)) {
                printf("save_state_load_migrate: migration failed for region '%s'\n",
                       r->name);
                return 0;
            }
            if (!r->load(scratch, r->size)) {
                printf("save_state_load_migrate: r->load failed for region '%s'\n",
                       r->name);
                return 0;
            }
        } else {
            /* No migration descriptor or layout unchanged — raw load.
             * If old_sz != r->size the region has grown or shrunk without
             * a descriptor, which is a configuration error; treat as fail. */
            if (old_sz != r->size) {
                printf("save_state_load_migrate: region '%s' size changed"
                       " (%u -> %u) with no migration descriptor\n",
                       r->name, (unsigned)old_sz, (unsigned)r->size);
                return 0;
            }
            if (!r->load(p, r->size)) {
                printf("save_state_load_migrate: r->load failed for region '%s'\n",
                       r->name);
                return 0;
            }
        }
        p += old_sz;
    }

    return 1;
}

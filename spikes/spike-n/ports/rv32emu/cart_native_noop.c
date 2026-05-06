/* Spike N Stage 1 — no-op native cart shim.
 *
 * The simplest possible native cart: just counts frames in a POD state
 * struct.  Used for the Stage 1 floor test (no-op reload: rebuild but
 * no source change; buffer must be byte-equal pre/post).
 *
 * This is NOT an edit target — the "edit" for Stage 1 is a rebuild with
 * no source change, which should produce an identical ELF (same code,
 * same layout_hash, same migration path = none).
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_native_state.h"
#include "../../cart_runtime/digest.h"

void native_cart_init(void)
{
    /* Nothing — regions_native_noop.c registers the region list. */
}

void native_cart_update(int frame)
{
    native_cart_state_t *s = region_native_state_get();
    s->frame_count++;
    s->some_value    = frame * 3;
    s->unused_count  = frame * 7;

    /* Fold into the frame digest. */
    frame_state_t *fs = region_frame_state_get();
    fs->accum_misc += (float)s->some_value;
}

void native_cart_emit_digest(void)
{
    frame_state_t *fs = region_frame_state_get();
    frame_state_emit_digest(fs);
}

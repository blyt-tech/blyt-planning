/* Spike N Stage 2 — native edit shim.
 *
 * Compile with:
 *   -DEDIT_NUM=N    (1..6)
 *   -DEDIT_SIDE=0   (PRE = save side)
 *   -DEDIT_SIDE=1   (POST = load side)
 *   -DNATIVE_STATE_VERSION=<v>   (matching the version for this edit+side)
 *
 * The native cart logic differs between PRE and POST sides:
 *
 *   n1: PRE  some_value = frame*3     POST some_value = frame*3+1
 *   n2: PRE  no mylib_helper         POST calls mylib_helper (noop impl here)
 *   n3: PRE  v0 struct               POST v3 struct (add last_score)
 *   n4: PRE  v3 struct               POST v4 struct (remove unused_count)
 *   n5: PRE  v4 struct               POST v5 struct (retype frame_count i32→i64)
 *   n6: PRE  v5 struct               POST v6 struct (reorder fields)
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/region_native_state.h"
#include "../../cart_runtime/digest.h"

/* ── Optional helper (n2 POST only) ─────────────────────────────── */

#if EDIT_NUM == 2 && EDIT_SIDE == 1
static int mylib_helper(int frame)
{
    /* New helper added in n2 POST. */
    return frame % 5;
}
#endif

/* ── cart_init ────────────────────────────────────────────────────── */

void native_cart_init(void)
{
    /* Region registration is in regions_native_edit.c */
}

/* ── cart_update ──────────────────────────────────────────────────── */

void native_cart_update(int frame)
{
    native_cart_state_t *s = region_native_state_get();
    frame_state_t       *fs = region_frame_state_get();

    s->frame_count++;

#if EDIT_NUM == 1
    /* n1: body change to some_value formula. */
#  if EDIT_SIDE == 0
    s->some_value = frame * 3;
#  else
    s->some_value = frame * 3 + 1;
#  endif
    s->unused_count = frame * 7;

#elif EDIT_NUM == 2
    /* n2: add mylib_helper call on POST side. */
    s->some_value   = frame * 3;
    s->unused_count = frame * 7;
#  if EDIT_SIDE == 1
    s->unused_count += mylib_helper(frame);
#  endif

#elif EDIT_NUM == 3
    /* n3: PRE=v0, POST=v3.  POST initialises last_score. */
    s->some_value   = frame * 3;
#  if EDIT_SIDE == 0
    /* v0: has unused_count */
    s->unused_count = frame * 7;
#  else
    /* v3: has unused_count + last_score */
    s->unused_count = frame * 7;
    s->last_score   = frame * 2;
#  endif

#elif EDIT_NUM == 4
    /* n4: PRE=v3, POST=v4 (remove unused_count). */
    s->some_value = frame * 3;
#  if EDIT_SIDE == 0
    /* v3 */
    s->unused_count = frame * 7;
    s->last_score   = frame * 2;
#  else
    /* v4: no unused_count */
    s->last_score   = frame * 2;
#  endif

#elif EDIT_NUM == 5
    /* n5: PRE=v4, POST=v5 (retype frame_count i32→i64). */
    s->some_value = frame * 3;
    s->last_score = frame * 2;
    /* frame_count is incremented above regardless of type. */

#elif EDIT_NUM == 6
    /* n6: PRE=v5, POST=v6 (reorder fields). */
    s->some_value = frame * 3;
    s->last_score = frame * 2;
    /* frame_count is incremented above regardless of position. */

#else
#  error "Unknown EDIT_NUM"
#endif

    /* Fold into digest. */
    fs->accum_misc += (float)s->some_value;
}

/* ── cart_emit_digest ─────────────────────────────────────────────── */

void native_cart_emit_digest(void)
{
    frame_state_t *fs = region_frame_state_get();
    frame_state_emit_digest(fs);
}

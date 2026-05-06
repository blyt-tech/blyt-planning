/* Spike N — native cart region (all versions + migration descriptors).
 *
 * The NATIVE_STATE_VERSION flag at compile time selects the struct shape.
 * All six mlayout_t descriptors are defined here (regardless of version)
 * so the load driver can reference the old_layout for the pre-edit version
 * and the new_layout for the post-edit version in a single TU.
 *
 * describe() emits the field layout in the same name:type@offset format
 * as Spike K's region_frame_state.c, feeding the FNV-1a-64 layout_hash.
 * save() canonicalizes NaN for any f32/f64 fields (none in this struct,
 * but the pattern is kept for consistency).
 * load() bounds-checks then memcpys.
 */

#include <stdint.h>
#include <stddef.h>

#include "region_native_state.h"
#include "runtime_tracked.h"
#include "migrate.h"

/* The live state instance (BSS, zero-initialised). */
static native_cart_state_t g_state;

native_cart_state_t *region_native_state_get(void) { return &g_state; }

/* ── describe callback — emits field layout into string_sink ────────── */

static void describe(string_sink_t *out)
{
    /* Each compiled-in version describes its own fields.  The format
     * is name:type@offset, comma-separated — same as Spike K's
     * frame_state describe().  offsetof() is used so the hash captures
     * the binary's actual layout. */
#if NATIVE_STATE_VERSION == 0
    string_sink_puts(out, "frame_count:i32@");  string_sink_putu(out, offsetof(native_cart_state_t, frame_count));  string_sink_putc(out, ',');
    string_sink_puts(out, "some_value:i32@");   string_sink_putu(out, offsetof(native_cart_state_t, some_value));   string_sink_putc(out, ',');
    string_sink_puts(out, "unused_count:i32@"); string_sink_putu(out, offsetof(native_cart_state_t, unused_count)); string_sink_putc(out, ',');
#elif NATIVE_STATE_VERSION == 3
    string_sink_puts(out, "frame_count:i32@");  string_sink_putu(out, offsetof(native_cart_state_t, frame_count));  string_sink_putc(out, ',');
    string_sink_puts(out, "some_value:i32@");   string_sink_putu(out, offsetof(native_cart_state_t, some_value));   string_sink_putc(out, ',');
    string_sink_puts(out, "unused_count:i32@"); string_sink_putu(out, offsetof(native_cart_state_t, unused_count)); string_sink_putc(out, ',');
    string_sink_puts(out, "last_score:i32@");   string_sink_putu(out, offsetof(native_cart_state_t, last_score));   string_sink_putc(out, ',');
#elif NATIVE_STATE_VERSION == 4
    string_sink_puts(out, "frame_count:i32@");  string_sink_putu(out, offsetof(native_cart_state_t, frame_count));  string_sink_putc(out, ',');
    string_sink_puts(out, "some_value:i32@");   string_sink_putu(out, offsetof(native_cart_state_t, some_value));   string_sink_putc(out, ',');
    string_sink_puts(out, "last_score:i32@");   string_sink_putu(out, offsetof(native_cart_state_t, last_score));   string_sink_putc(out, ',');
#elif NATIVE_STATE_VERSION == 5
    string_sink_puts(out, "frame_count:i64@");  string_sink_putu(out, offsetof(native_cart_state_t, frame_count));  string_sink_putc(out, ',');
    string_sink_puts(out, "some_value:i32@");   string_sink_putu(out, offsetof(native_cart_state_t, some_value));   string_sink_putc(out, ',');
    string_sink_puts(out, "last_score:i32@");   string_sink_putu(out, offsetof(native_cart_state_t, last_score));   string_sink_putc(out, ',');
#elif NATIVE_STATE_VERSION == 6
    string_sink_puts(out, "some_value:i32@");   string_sink_putu(out, offsetof(native_cart_state_t, some_value));   string_sink_putc(out, ',');
    string_sink_puts(out, "last_score:i32@");   string_sink_putu(out, offsetof(native_cart_state_t, last_score));   string_sink_putc(out, ',');
    string_sink_puts(out, "frame_count:i64@");  string_sink_putu(out, offsetof(native_cart_state_t, frame_count));  string_sink_putc(out, ',');
#endif
}

/* ── save callback ─────────────────────────────────────────────────── */

static void save(uint8_t *dst)
{
    /* No f32 fields in this struct; pattern preserved for consistency. */
    const uint8_t *src = (const uint8_t *)&g_state;
    for (size_t i = 0; i < sizeof(g_state); i++) dst[i] = src[i];
}

/* ── load callback ─────────────────────────────────────────────────── */

static int load(const uint8_t *src, uint32_t n)
{
    if (n != sizeof(g_state)) return 0;
    uint8_t *dst = (uint8_t *)&g_state;
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    return 1;
}

const runtime_tracked_region_t region_native_state = {
    .name     = "native_state",
    .size     = (uint32_t)sizeof(native_cart_state_t),
    .describe = describe,
    .save     = save,
    .load     = load,
};

/* ── mlayout_t descriptors for all versions ─────────────────────────
 *
 * These are all defined unconditionally so the migration declarations
 * in the load driver can reference old_layout / new_layout regardless
 * of which version was compiled in.  The load ELF is always the
 * POST-edit version; it needs both old and new descriptors. */

static const mfield_desc_t fields_v0[] = {
    { "frame_count",  MTYPE_I32, 0,  4 },
    { "some_value",   MTYPE_I32, 4,  4 },
    { "unused_count", MTYPE_I32, 8,  4 },
};
const mlayout_t native_state_layout_v0 = {
    fields_v0, 3, 12
};

static const mfield_desc_t fields_v3[] = {
    { "frame_count",  MTYPE_I32, 0,  4 },
    { "some_value",   MTYPE_I32, 4,  4 },
    { "unused_count", MTYPE_I32, 8,  4 },
    { "last_score",   MTYPE_I32, 12, 4 },
};
const mlayout_t native_state_layout_v3 = {
    fields_v3, 4, 16
};

static const mfield_desc_t fields_v4[] = {
    { "frame_count",  MTYPE_I32, 0,  4 },
    { "some_value",   MTYPE_I32, 4,  4 },
    { "last_score",   MTYPE_I32, 8,  4 },
};
const mlayout_t native_state_layout_v4 = {
    fields_v4, 3, 12
};

static const mfield_desc_t fields_v5[] = {
    { "frame_count",  MTYPE_I64, 0,  8 },
    { "some_value",   MTYPE_I32, 8,  4 },
    { "last_score",   MTYPE_I32, 12, 4 },
};
const mlayout_t native_state_layout_v5 = {
    fields_v5, 3, 16
};

static const mfield_desc_t fields_v6[] = {
    { "some_value",   MTYPE_I32, 0,  4 },
    { "last_score",   MTYPE_I32, 4,  4 },
    { "frame_count",  MTYPE_I64, 8,  8 },
};
const mlayout_t native_state_layout_v6 = {
    fields_v6, 3, 16
};

/* ── on_retype callback for n5: i32 → i64 sign-extend ─────────────── */

int native_state_retype_i32_to_i64(const uint8_t *old_field, mfield_type_t old_type,
                                     uint8_t *new_field, mfield_type_t new_type)
{
    if (old_type != MTYPE_I32 || new_type != MTYPE_I64) return 0;
    /* Read 4-byte LE i32 from old_field, sign-extend to 8-byte LE i64. */
    int32_t v32;
    uint8_t *vp = (uint8_t *)&v32;
    vp[0] = old_field[0]; vp[1] = old_field[1];
    vp[2] = old_field[2]; vp[3] = old_field[3];
    int64_t v64 = (int64_t)v32;
    uint8_t *op = (uint8_t *)&v64;
    for (int i = 0; i < 8; i++) new_field[i] = op[i];
    return 1;
}

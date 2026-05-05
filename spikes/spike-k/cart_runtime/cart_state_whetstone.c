/* Spike K — whetstone's cart_state_t implementation.
 *
 * The describe callback emits per-field name:type@offset, NUL-separated,
 * in declaration order — same format as region_frame_state.c.  Both feed
 * into save_state's layout_hash so any field reorder, type change, or
 * offset shift makes a saved buffer unloadable on a binary that disagrees
 * about the layout (which is the safety property — see PLAN.md § safety).
 */

#include <stdint.h>
#include <stddef.h>

#include "cart_state_whetstone.h"
#include "nan_canon.h"

static cart_state_whetstone_t g_state = {
    /* The fresh-init values mirror Spike D's whetstone.c locals.  At
     * cart entry the save side calls cart_state_whetstone_get() to fetch
     * the struct and proceeds.  After save_state_load() the load side
     * sees the post-save snapshot in this struct; before any load it sees
     * these defaults (so a load failure doesn't leave the cart in
     * undefined state). */
    .a = 1.0f, .b = -1.0f, .c = -1.0f, .d = -1.0f,
    .e1 = { 1.0f, -1.0f, -1.0f, -1.0f },
    .t = 0.5f,
    .u = 0.75f,
};

cart_state_whetstone_t *cart_state_whetstone_get(void) { return &g_state; }

static void describe(string_sink_t *out)
{
    string_sink_puts(out, "a:f32@");      string_sink_putu(out, offsetof(cart_state_whetstone_t, a));     string_sink_putc(out, ',');
    string_sink_puts(out, "b:f32@");      string_sink_putu(out, offsetof(cart_state_whetstone_t, b));     string_sink_putc(out, ',');
    string_sink_puts(out, "c:f32@");      string_sink_putu(out, offsetof(cart_state_whetstone_t, c));     string_sink_putc(out, ',');
    string_sink_puts(out, "d:f32@");      string_sink_putu(out, offsetof(cart_state_whetstone_t, d));     string_sink_putc(out, ',');
    string_sink_puts(out, "e1[4]:f32@");  string_sink_putu(out, offsetof(cart_state_whetstone_t, e1));    string_sink_putc(out, ',');
    string_sink_puts(out, "t:f32@");      string_sink_putu(out, offsetof(cart_state_whetstone_t, t));     string_sink_putc(out, ',');
    string_sink_puts(out, "u:f32@");      string_sink_putu(out, offsetof(cart_state_whetstone_t, u));
}

static void save(uint8_t *dst)
{
    /* Canonicalize NaN on every f32 at write time — see region_frame_state.c
     * for why this is stricter than spike-D's digest-time canonicalization. */
    g_state.a  = canonicalize_nanf(g_state.a);
    g_state.b  = canonicalize_nanf(g_state.b);
    g_state.c  = canonicalize_nanf(g_state.c);
    g_state.d  = canonicalize_nanf(g_state.d);
    for (int i = 0; i < 4; i++) g_state.e1[i] = canonicalize_nanf(g_state.e1[i]);
    g_state.t  = canonicalize_nanf(g_state.t);
    g_state.u  = canonicalize_nanf(g_state.u);
    const uint8_t *src = (const uint8_t *)&g_state;
    for (size_t i = 0; i < sizeof(g_state); i++) dst[i] = src[i];
}

static int load(const uint8_t *src, uint32_t n)
{
    if (n != sizeof(g_state)) return 0;
    uint8_t *dst = (uint8_t *)&g_state;
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    return 1;
}

const runtime_tracked_region_t region_cart_state_whetstone = {
    .name     = "cart_state_whetstone",
    .size     = (uint32_t)sizeof(cart_state_whetstone_t),
    .describe = describe,
    .save     = save,
    .load     = load,
};

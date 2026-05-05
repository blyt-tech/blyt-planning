/* Spike K Stage 2 — cart_state_lua_simple region implementation. */

#include <stdint.h>
#include <stddef.h>

#include "cart_state_lua_simple.h"
#include "nan_canon.h"

static cart_state_lua_simple_t g_state;

cart_state_lua_simple_t *cart_state_lua_simple_get(void) { return &g_state; }

static void describe(string_sink_t *out)
{
    /* Emits the 8 entities × (x, y, a) shape inline.  The exact byte
     * sequence becomes the layout_hash input — any reorder, type
     * change, or count change makes existing buffers unloadable. */
    string_sink_puts(out, "entities[");
    string_sink_putu(out, LUA_SIMPLE_NUM_ENTITIES);
    string_sink_puts(out, "]={x:f32@0,y:f32@4,a:f32@8}@");
    string_sink_putu(out, offsetof(cart_state_lua_simple_t, entities));
}

static void save(uint8_t *dst)
{
    for (int i = 0; i < LUA_SIMPLE_NUM_ENTITIES; i++) {
        g_state.entities[i].x = canonicalize_nanf(g_state.entities[i].x);
        g_state.entities[i].y = canonicalize_nanf(g_state.entities[i].y);
        g_state.entities[i].a = canonicalize_nanf(g_state.entities[i].a);
    }
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

const runtime_tracked_region_t region_cart_state_lua_simple = {
    .name     = "cart_state_lua_simple",
    .size     = (uint32_t)sizeof(cart_state_lua_simple_t),
    .describe = describe,
    .save     = save,
    .load     = load,
};

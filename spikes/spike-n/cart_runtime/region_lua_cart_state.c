/* Spike N — Lua cart POD state region implementation.
 *
 * See region_lua_cart_state.h for version history.
 * Pattern mirrors region_frame_state.c from Spike K.
 */

#include <stdint.h>
#include <stddef.h>

#include "region_lua_cart_state.h"
#include "runtime_tracked.h"

static lua_cart_state_t g_lua_state;

lua_cart_state_t *region_lua_cart_state_get(void) { return &g_lua_state; }

/* ── describe ─────────────────────────────────────────────────────── */

static void describe(string_sink_t *out)
{
#if LUA_STATE_VERSION == 0
    string_sink_puts(out, "score:i32@");      string_sink_putu(out, offsetof(lua_cart_state_t, score));      string_sink_putc(out, ',');
    string_sink_puts(out, "step:i32@");       string_sink_putu(out, offsetof(lua_cart_state_t, step));       string_sink_putc(out, ',');
    string_sink_puts(out, "combo:i32@");      string_sink_putu(out, offsetof(lua_cart_state_t, combo));      string_sink_putc(out, ',');
#elif LUA_STATE_VERSION == 2
    string_sink_puts(out, "score:i32@");      string_sink_putu(out, offsetof(lua_cart_state_t, score));      string_sink_putc(out, ',');
    string_sink_puts(out, "step:i32@");       string_sink_putu(out, offsetof(lua_cart_state_t, step));       string_sink_putc(out, ',');
    string_sink_puts(out, "combo:i32@");      string_sink_putu(out, offsetof(lua_cart_state_t, combo));      string_sink_putc(out, ',');
    string_sink_puts(out, "bonus:i32@");      string_sink_putu(out, offsetof(lua_cart_state_t, bonus));      string_sink_putc(out, ',');
#elif LUA_STATE_VERSION == 3
    string_sink_puts(out, "score:i32@");      string_sink_putu(out, offsetof(lua_cart_state_t, score));      string_sink_putc(out, ',');
    string_sink_puts(out, "step:i32@");       string_sink_putu(out, offsetof(lua_cart_state_t, step));       string_sink_putc(out, ',');
    string_sink_puts(out, "combo:i32@");      string_sink_putu(out, offsetof(lua_cart_state_t, combo));      string_sink_putc(out, ',');
#elif LUA_STATE_VERSION == 4
    string_sink_puts(out, "score:i32@");      string_sink_putu(out, offsetof(lua_cart_state_t, score));      string_sink_putc(out, ',');
    string_sink_puts(out, "step:i32@");       string_sink_putu(out, offsetof(lua_cart_state_t, step));       string_sink_putc(out, ',');
    string_sink_puts(out, "combo_mult:f32@"); string_sink_putu(out, offsetof(lua_cart_state_t, combo_mult)); string_sink_putc(out, ',');
#endif
}

/* ── save ─────────────────────────────────────────────────────────── */

static void save(uint8_t *dst)
{
    const uint8_t *src = (const uint8_t *)&g_lua_state;
    for (size_t i = 0; i < sizeof(g_lua_state); i++) dst[i] = src[i];
}

/* ── load ─────────────────────────────────────────────────────────── */

static int load(const uint8_t *src, uint32_t n)
{
    if (n != sizeof(g_lua_state)) return 0;
    uint8_t *dst = (uint8_t *)&g_lua_state;
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    return 1;
}

const runtime_tracked_region_t region_lua_cart_state = {
    .name     = "lua_cart_state",
    .size     = (uint32_t)sizeof(lua_cart_state_t),
    .describe = describe,
    .save     = save,
    .load     = load,
};

/* ── All-version layout descriptors ─────────────────────────────── */

static const mfield_desc_t fields_lv0[] = {
    { "score", MTYPE_I32, 0,  4 },
    { "step",  MTYPE_I32, 4,  4 },
    { "combo", MTYPE_I32, 8,  4 },
};
const mlayout_t lua_cart_state_layout_v0 = { fields_lv0, 3, 12 };

static const mfield_desc_t fields_lv2[] = {
    { "score", MTYPE_I32, 0,  4 },
    { "step",  MTYPE_I32, 4,  4 },
    { "combo", MTYPE_I32, 8,  4 },
    { "bonus", MTYPE_I32, 12, 4 },
};
const mlayout_t lua_cart_state_layout_v2 = { fields_lv2, 4, 16 };

static const mfield_desc_t fields_lv3[] = {
    { "score", MTYPE_I32, 0,  4 },
    { "step",  MTYPE_I32, 4,  4 },
    { "combo", MTYPE_I32, 8,  4 },
};
const mlayout_t lua_cart_state_layout_v3 = { fields_lv3, 3, 12 };

static const mfield_desc_t fields_lv4[] = {
    { "score",      MTYPE_I32, 0,  4 },
    { "step",       MTYPE_I32, 4,  4 },
    { "combo_mult", MTYPE_F32, 8,  4 },
};
const mlayout_t lua_cart_state_layout_v4 = { fields_lv4, 3, 12 };

/* ── on_retype: i32 combo → f32 combo_mult ────────────────────────
 *
 * The cart changes the field from an integer combo counter to a float
 * multiplier.  The migration maps old_combo / 10.0f as the initial
 * multiplier — a documented cart-level convention.
 */
int lua_cart_state_retype_combo_to_mult(const uint8_t *old_field, mfield_type_t old_type,
                                         uint8_t *new_field, mfield_type_t new_type)
{
    if (old_type != MTYPE_I32 || new_type != MTYPE_F32) return 0;
    int32_t v32;
    uint8_t *vp = (uint8_t *)&v32;
    vp[0] = old_field[0]; vp[1] = old_field[1];
    vp[2] = old_field[2]; vp[3] = old_field[3];
    /* Map integer combo to float multiplier: combo * 0.1f. */
    float mult = (float)v32 * 0.1f;
    uint8_t *fp = (uint8_t *)&mult;
    new_field[0] = fp[0]; new_field[1] = fp[1];
    new_field[2] = fp[2]; new_field[3] = fp[3];
    return 1;
}

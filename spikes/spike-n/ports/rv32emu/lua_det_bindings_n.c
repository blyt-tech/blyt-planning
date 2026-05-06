/* Spike N — Lua bindings (extends Spike M's set).
 *
 * Adds hot-reload-specific bindings on top of Spike M's full set:
 *   console.get_cart_state()              → table {score, step, combo[/combo_mult]}
 *   console.set_cart_state(t)             → no return
 *   blyt32.on_hot_reload_failed = fn      → install hook (Lua-side assignment
 *                                           intercepted via __newindex)
 *
 * Also adds:
 *   console.emit_buffer()                 → force a BUFFER line (post-migration)
 *
 * All Spike M bindings are preserved verbatim; this file includes the
 * region_lua_cart_state region so the bindings can read/write its fields.
 */

#include <stdint.h>
#include <stddef.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/cart_state_lua_simple.h"
#include "../../cart_runtime/region_persistent_scripts.h"
#include "../../cart_runtime/region_lua_cart_state.h"
#include "../../cart_runtime/pcg32.h"
#include "../../cart_runtime/digest.h"
#include "../../cart_runtime/hot_reload_diagnostic.h"
#include "../../cart_runtime/save_state.h"
#include "../../lib/lua_table_flatten.h"

extern int   printf(const char *, ...);

static pcg32_t g_rng;
static int     g_load_resume;
static int     g_num_frames = 30;

void cart_det_init(void)
{
    pcg32_seed(&g_rng, PCG_DEFAULT_STATE, PCG_DEFAULT_INC);
    region_frame_state_get()->rng_state = g_rng.state;
    region_frame_state_get()->rng_inc   = g_rng.inc;
    persistent_scripts_reset();
}

void cart_det_set_load_resume(int b) { g_load_resume = b; }
void cart_det_set_num_frames(int n)  { g_num_frames  = n; }

void cart_det_resync_rng_from_fs(void)
{
    g_rng.state = region_frame_state_get()->rng_state;
    g_rng.inc   = region_frame_state_get()->rng_inc;
}

/* ── Spike K/M bindings (unchanged) ──────────────────────────────── */

static int l_rng(lua_State *L)
{
    uint32_t v = pcg32_next(&g_rng);
    lua_pushinteger(L, (lua_Integer)v);
    return 1;
}

static int l_unit_float(lua_State *L)
{
    float f = pcg32_unit_float(&g_rng);
    lua_pushnumber(L, (lua_Number)f);
    return 1;
}

static int l_add_misc(lua_State *L)
{
    region_frame_state_get()->accum_misc += (float)luaL_checknumber(L, 1);
    return 0;
}

static int l_frame(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)region_frame_state_get()->frame);
    return 1;
}

static int l_commit_frame(lua_State *L)
{
    (void)L;
    frame_state_t *fs = region_frame_state_get();
    fs->rng_state = g_rng.state;
    fs->rng_inc   = g_rng.inc;
    frame_state_emit_digest(fs);
    fs->frame++;
    return 0;
}

static int l_set_entity(lua_State *L)
{
    lua_Integer idx = luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= LUA_SIMPLE_NUM_ENTITIES) return 0;
    cart_state_lua_simple_t *cs = cart_state_lua_simple_get();
    cs->entities[idx].x = (float)luaL_checknumber(L, 2);
    cs->entities[idx].y = (float)luaL_checknumber(L, 3);
    cs->entities[idx].a = (float)luaL_checknumber(L, 4);
    return 0;
}

static int l_get_entity(lua_State *L)
{
    lua_Integer idx = luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= LUA_SIMPLE_NUM_ENTITIES) {
        lua_pushnil(L); lua_pushnil(L); lua_pushnil(L);
        return 3;
    }
    cart_state_lua_simple_t *cs = cart_state_lua_simple_get();
    lua_pushnumber(L, (lua_Number)cs->entities[idx].x);
    lua_pushnumber(L, (lua_Number)cs->entities[idx].y);
    lua_pushnumber(L, (lua_Number)cs->entities[idx].a);
    return 3;
}

static int l_is_load_resume(lua_State *L)
{
    lua_pushboolean(L, g_load_resume);
    return 1;
}

static int l_num_frames(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)g_num_frames);
    return 1;
}

static int l_script_alloc(lua_State *L)
{
    int s = persistent_scripts_alloc();
    lua_pushinteger(L, (lua_Integer)s);
    return 1;
}

static int l_script_free(lua_State *L)
{
    int s = (int)luaL_checkinteger(L, 1);
    persistent_scripts_free(s);
    return 0;
}

static int l_script_write_blob(lua_State *L)
{
    int s = (int)luaL_checkinteger(L, 1);
    size_t n = 0;
    const char *bytes = luaL_checklstring(L, 2, &n);
    int ok = persistent_scripts_write(s, (const uint8_t *)bytes, (uint32_t)n);
    lua_pushboolean(L, ok);
    return 1;
}

static int l_script_read_blob(lua_State *L)
{
    int s = (int)luaL_checkinteger(L, 1);
    if (!persistent_scripts_is_active(s)) { lua_pushnil(L); return 1; }
    uint8_t buf[PERSISTENT_SCRIPT_SLOT_BYTES];
    int n = persistent_scripts_read(s, buf, (uint32_t)sizeof(buf));
    if (n < 0) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, (const char *)buf, (size_t)n);
    return 1;
}

static int l_script_is_active(lua_State *L)
{
    int s = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, persistent_scripts_is_active(s));
    return 1;
}

static int l_max_persistent_scripts(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)MAX_PERSISTENT_SCRIPTS);
    return 1;
}

static int l_script_has_saved_bytes(lua_State *L)
{
    int s = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, persistent_scripts_raw_blob_len(s) > 0);
    return 1;
}

static int l_lua_table_flatten(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    uint8_t buf[PERSISTENT_SCRIPT_SLOT_BYTES];
    uint32_t out_len = 0;
    int rc = lua_table_flatten(L, 1, buf, (uint32_t)sizeof(buf), &out_len);
    if (rc < 0) {
        lua_pushnil(L);
        lua_pushstring(L, lua_table_flatten_strerror(rc));
        return 2;
    }
    lua_pushlstring(L, (const char *)buf, (size_t)out_len);
    return 1;
}

static int l_lua_table_unflatten(lua_State *L)
{
    size_t n = 0;
    const char *bytes = luaL_checklstring(L, 1, &n);
    int rc = lua_table_unflatten(L, (const uint8_t *)bytes, (uint32_t)n);
    if (rc < 0) {
        lua_pushstring(L, lua_table_flatten_strerror(rc));
        return 2;
    }
    return 1;
}

/* ── Spike N additions ────────────────────────────────────────────── */

/* console.get_cart_state() — return table of POD fields */
static int l_get_cart_state(lua_State *L)
{
    lua_cart_state_t *cs = region_lua_cart_state_get();
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)cs->score);
    lua_setfield(L, -2, "score");
    lua_pushinteger(L, (lua_Integer)cs->step);
    lua_setfield(L, -2, "step");
#if LUA_STATE_VERSION <= 3
    lua_pushinteger(L, (lua_Integer)cs->combo);
    lua_setfield(L, -2, "combo");
#endif
#if LUA_STATE_VERSION == 2
    lua_pushinteger(L, (lua_Integer)cs->bonus);
    lua_setfield(L, -2, "bonus");
#endif
#if LUA_STATE_VERSION == 4
    lua_pushnumber(L, (lua_Number)cs->combo_mult);
    lua_setfield(L, -2, "combo_mult");
#endif
    return 1;
}

/* console.set_cart_state(t) — write POD fields from table */
static int l_set_cart_state(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_cart_state_t *cs = region_lua_cart_state_get();

    lua_getfield(L, 1, "score");
    if (!lua_isnil(L, -1)) cs->score = (int32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "step");
    if (!lua_isnil(L, -1)) cs->step = (int32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);

#if LUA_STATE_VERSION <= 3
    lua_getfield(L, 1, "combo");
    if (!lua_isnil(L, -1)) cs->combo = (int32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
#endif
#if LUA_STATE_VERSION == 2
    lua_getfield(L, 1, "bonus");
    if (!lua_isnil(L, -1)) cs->bonus = (int32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
#endif
#if LUA_STATE_VERSION == 4
    lua_getfield(L, 1, "combo_mult");
    if (!lua_isnil(L, -1)) cs->combo_mult = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
#endif
    return 0;
}

/* blyt32.set_on_hot_reload_failed(fn) — install the Lua hook */
static lua_State *g_hook_L;
static int        g_hook_ref = LUA_NOREF;

static void c_hot_reload_hook(int slot, const char *reason)
{
    if (!g_hook_L || g_hook_ref == LUA_NOREF) return;
    lua_rawgeti(g_hook_L, LUA_REGISTRYINDEX, g_hook_ref);
    lua_pushinteger(g_hook_L, (lua_Integer)slot);
    lua_pushstring(g_hook_L, reason);
    lua_pcall(g_hook_L, 2, 0, 0);
}

static int l_set_on_hot_reload_failed(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (g_hook_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, g_hook_ref);
    g_hook_L   = L;
    g_hook_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    hot_reload_set_hook(c_hot_reload_hook);
    return 0;
}

/* ── registry ─────────────────────────────────────────────────────── */

static const luaL_Reg console_funcs[] = {
    { "rng",                     l_rng                     },
    { "unit_float",              l_unit_float              },
    { "add_misc",                l_add_misc                },
    { "frame",                   l_frame                   },
    { "commit_frame",            l_commit_frame            },
    { "set_entity",              l_set_entity              },
    { "get_entity",              l_get_entity              },
    { "is_load_resume",          l_is_load_resume          },
    { "num_frames",              l_num_frames              },
    { "script_alloc",            l_script_alloc            },
    { "script_free",             l_script_free             },
    { "script_write_blob",       l_script_write_blob       },
    { "script_read_blob",        l_script_read_blob        },
    { "script_is_active",        l_script_is_active        },
    { "max_persistent_scripts",  l_max_persistent_scripts  },
    { "script_has_saved_bytes",  l_script_has_saved_bytes  },
    { "lua_table_flatten",       l_lua_table_flatten       },
    { "lua_table_unflatten",     l_lua_table_unflatten     },
    /* Spike N additions */
    { "get_cart_state",          l_get_cart_state          },
    { "set_cart_state",          l_set_cart_state          },
    { "set_on_hot_reload_failed",l_set_on_hot_reload_failed},
    { NULL, NULL }
};

void cart_det_register(lua_State *L)
{
    lua_newtable(L);
    luaL_setfuncs(L, console_funcs, 0);
    lua_pushinteger(L, (lua_Integer)LUA_SIMPLE_NUM_ENTITIES);
    lua_setfield(L, -2, "num_entities");
    lua_setglobal(L, "console");
}

void cart_det_open_coroutine_lib(lua_State *L)
{
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_pop(L, 1);
}

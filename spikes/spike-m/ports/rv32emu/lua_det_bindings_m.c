/* Spike M — Lua bindings layered on top of Spike K's lua_det_bindings_k.c.
 *
 * Adds the persistent-script slot table API and the table flattener
 * bindings that the Lua-side `blyt32.coroutine` wrapper drives.  Reuses
 * Spike K's frame_state + cart_state_lua_simple bindings verbatim
 * (the digest fold, the entity row accessors, etc.).
 *
 * New bindings on the `console` table:
 *   console.script_alloc()             → slot index, or -1
 *   console.script_free(slot)          → no return
 *   console.script_write_blob(slot, s) → bool (false on overflow / inactive)
 *   console.script_read_blob(slot)     → string or nil if slot inactive
 *   console.script_is_active(slot)     → bool
 *   console.max_persistent_scripts()   → integer
 *   console.lua_table_flatten(tbl)     → string, or (nil, error_string)
 *   console.lua_table_unflatten(s)     → table, or (nil, error_string)
 *
 * Existing Spike K bindings retained:
 *   console.rng / unit_float / add_misc / frame / commit_frame /
 *   set_entity / get_entity / is_load_resume / num_frames /
 *   num_entities (table field, not callable).
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
#include "../../cart_runtime/pcg32.h"
#include "../../cart_runtime/digest.h"
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

/* ── Spike K bindings (reproduced; symbol-private to this TU) ───────────── */

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

/* ── Spike M additions ──────────────────────────────────────────────────── */

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
        /* unflatten already pushed nil on failure. */
        lua_pushstring(L, lua_table_flatten_strerror(rc));
        return 2;
    }
    return 1;
}

/* ── registry ───────────────────────────────────────────────────────────── */

static const luaL_Reg console_funcs[] = {
    /* Spike K */
    { "rng",                     l_rng                     },
    { "unit_float",              l_unit_float              },
    { "add_misc",                l_add_misc                },
    { "frame",                   l_frame                   },
    { "commit_frame",            l_commit_frame            },
    { "set_entity",              l_set_entity              },
    { "get_entity",              l_get_entity              },
    { "is_load_resume",          l_is_load_resume          },
    { "num_frames",              l_num_frames              },
    /* Spike M */
    { "script_alloc",            l_script_alloc            },
    { "script_free",             l_script_free             },
    { "script_write_blob",       l_script_write_blob       },
    { "script_read_blob",        l_script_read_blob        },
    { "script_is_active",        l_script_is_active        },
    { "max_persistent_scripts",  l_max_persistent_scripts  },
    { "lua_table_flatten",       l_lua_table_flatten       },
    { "lua_table_unflatten",     l_lua_table_unflatten     },
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

/* Spike B's lua_init_libs omits the coroutine library because B's
 * benchmarks don't use it; spike M's wrapper relies on
 * `coroutine.create / resume / status / yield`.  Open it explicitly
 * here, after luaL_openlibs, so the global `coroutine` table exists. */
void cart_det_open_coroutine_lib(lua_State *L)
{
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_pop(L, 1);
}

/* Spike K Stage 2 — Lua bindings for the lua_simple cart family.
 *
 * Authored from scratch (rather than extending spike-D's
 * lua_det_bindings.c) so that the C-side state ownership is unambiguous:
 * frame_state lives in region_frame_state, cart_state_lua_simple lives
 * in its own region, and the PCG32 RNG state lives in region_frame_state
 * (mirrored on every commit).
 *
 * Bindings on the `console` table:
 *   console.rng()                   → uint32 (PCG32 next())
 *   console.unit_float()            → f32 in [0,1)
 *   console.add_misc(f)             → fs.accum_misc += f
 *   console.frame()                 → uint32 (current fs.frame; load uses this
 *                                     to know where to resume the Lua loop)
 *   console.commit_frame()          → emit DIGEST and bump fs.frame
 *   console.set_entity(i, x, y, a)  → write cart_state.entities[i]
 *   console.get_entity(i)           → x, y, a   (3 returns)
 *   console.is_load_resume()        → true iff main() called save_state_load()
 *                                     before luaL_dofile (script uses this to
 *                                     skip its init phase)
 */

#include <stdint.h>
#include <stddef.h>

#include "lua.h"
#include "lauxlib.h"

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/cart_state_lua_simple.h"
#include "../../cart_runtime/pcg32.h"
#include "../../cart_runtime/digest.h"

extern int   printf(const char *, ...);

static pcg32_t g_rng;
static int     g_load_resume;     /* set by main() before lua_dofile if a save was loaded */
static int     g_num_frames = 30; /* loop upper bound — set by main() per cart variant */

void cart_det_init(void)
{
    /* frame_state and cart_state_lua_simple have their own zero-init
     * (BSS / declaration default).  Seed the RNG to the spike's
     * canonical default. */
    pcg32_seed(&g_rng, PCG_DEFAULT_STATE, PCG_DEFAULT_INC);
    region_frame_state_get()->rng_state = g_rng.state;
    region_frame_state_get()->rng_inc   = g_rng.inc;
}

void cart_det_set_load_resume(int b) { g_load_resume = b; }
void cart_det_set_num_frames(int n) { g_num_frames = n; }

void cart_det_resync_rng_from_fs(void)
{
    /* On load: frame_state was just deserialized, so its rng_state /
     * rng_inc reflect the post-frame-N state.  Mirror it back into the
     * local pcg32_t so the next pcg32_next() call continues seamlessly. */
    g_rng.state = region_frame_state_get()->rng_state;
    g_rng.inc   = region_frame_state_get()->rng_inc;
}

/* ── Lua bindings ────────────────────────────────────────────────────────── */

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

static const luaL_Reg console_funcs[] = {
    { "rng",            l_rng            },
    { "unit_float",     l_unit_float     },
    { "add_misc",       l_add_misc       },
    { "frame",          l_frame          },
    { "commit_frame",   l_commit_frame   },
    { "set_entity",     l_set_entity     },
    { "get_entity",     l_get_entity     },
    { "is_load_resume", l_is_load_resume },
    { "num_frames",     l_num_frames     },
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

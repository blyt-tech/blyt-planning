/* Lua bindings that expose Spike D's deterministic cart_runtime to the
 * embedded Lua workload.
 *
 * The single "console" global is registered here.  The bindings are
 * intentionally narrow: just RNG + frame_state writes + commit.  No I/O,
 * no time, no other side channels — anything that could leak host
 * behaviour is absent.
 *
 *   console.rng()          → uint32 (PCG32 next())
 *   console.unit_float()   → f32 in [0,1) (PCG32 → 24-bit / 2^24)
 *   console.add_sin(f)     → fs.accum_sin += f
 *   console.add_cos(f)     → fs.accum_cos += f
 *   console.add_sqrt(f)    → fs.accum_sqrt += f
 *   console.add_misc(f)    → fs.accum_misc += f
 *   console.set_mob(i,x,y,vx,vy,state)
 *   console.commit_frame() → emit DIGEST and bump frame counter
 *
 * The frame_state and PCG32 singletons live here too; the cart's main()
 * calls cart_det_init() at startup.
 */

#include <stdint.h>
#include <stddef.h>

#include "lua.h"
#include "lauxlib.h"

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/pcg32.h"
#include "../../cart_runtime/digest.h"

static frame_state_t g_fs;
static pcg32_t       g_rng;

/* Called from the cart's main() before the script runs. */
void cart_det_init(void)
{
    /* Zero frame_state explicitly; carts that re-emit DIGEST without
     * touching every field rely on a fixed initial value for the
     * untouched bytes. */
    unsigned char *p = (unsigned char *)&g_fs;
    for (size_t i = 0; i < sizeof(g_fs); i++) p[i] = 0;
    pcg32_seed(&g_rng, PCG_DEFAULT_STATE, PCG_DEFAULT_INC);
    /* Mirror the seeded RNG into frame_state so the digest covers it. */
    g_fs.rng_state = g_rng.state;
    g_fs.rng_inc   = g_rng.inc;
}

/* ── lua bindings ────────────────────────────────────────────────────────── */

static int l_rng(lua_State *L)
{
    uint32_t v = pcg32_next(&g_rng);
    /* Lua 5.4 with LUA_32BITS: lua_Integer is 32-bit signed, so we
     * push as unsigned via lua_pushinteger of the bit pattern. */
    lua_pushinteger(L, (lua_Integer)v);
    return 1;
}

static int l_unit_float(lua_State *L)
{
    float f = pcg32_unit_float(&g_rng);
    lua_pushnumber(L, (lua_Number)f);
    return 1;
}

static int l_add_sin(lua_State *L)
{
    g_fs.accum_sin += (float)luaL_checknumber(L, 1);
    return 0;
}
static int l_add_cos(lua_State *L)
{
    g_fs.accum_cos += (float)luaL_checknumber(L, 1);
    return 0;
}
static int l_add_sqrt(lua_State *L)
{
    g_fs.accum_sqrt += (float)luaL_checknumber(L, 1);
    return 0;
}
static int l_add_misc(lua_State *L)
{
    g_fs.accum_misc += (float)luaL_checknumber(L, 1);
    return 0;
}

static int l_set_mob(lua_State *L)
{
    lua_Integer idx = luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= FRAME_STATE_MAX_MOBS) return 0;  /* silently drop */
    frame_state_mob *m = &g_fs.mobs[idx];
    m->x     = (float)luaL_checknumber(L, 2);
    m->y     = (float)luaL_checknumber(L, 3);
    m->vx    = (float)luaL_checknumber(L, 4);
    m->vy    = (float)luaL_checknumber(L, 5);
    m->state = (uint32_t)luaL_checkinteger(L, 6);
    return 0;
}

static int l_commit_frame(lua_State *L)
{
    (void)L;
    /* Sync RNG back into frame_state before the digest snapshot. */
    g_fs.rng_state = g_rng.state;
    g_fs.rng_inc   = g_rng.inc;
    frame_state_emit_digest(&g_fs);
    g_fs.frame++;
    return 0;
}

static const luaL_Reg console_funcs[] = {
    { "rng",          l_rng          },
    { "unit_float",   l_unit_float   },
    { "add_sin",      l_add_sin      },
    { "add_cos",      l_add_cos      },
    { "add_sqrt",     l_add_sqrt     },
    { "add_misc",     l_add_misc     },
    { "set_mob",      l_set_mob      },
    { "commit_frame", l_commit_frame },
    { NULL, NULL }
};

void cart_det_register(lua_State *L)
{
    lua_newtable(L);
    luaL_setfuncs(L, console_funcs, 0);
    lua_setglobal(L, "console");
}

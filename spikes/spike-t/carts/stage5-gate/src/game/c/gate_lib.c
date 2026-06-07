/* Spike T stage 5: combined determinism gate (Spike Q methodology, extended).
 * One bridged wrapper exercising strings, table reads/writes, lua_next
 * iteration, multiple returns, and (every 3rd frame) a raised error —
 * called once per frame for 10 frames with evolving state.  The debug-log
 * stream must be byte-identical between the rv32 and WASM paths. */

#include <blyt.h>
#include <stdint.h>

/* step(state, frame) -> digest:i32, label:string
 * Mutates state: state.acc, state[frame], state.last; raises on frame%3==0. */
BLYT_LUA_MODULE_EXPORT_RAW(spike, step) {
    if (!lua_istable(L, 1))
        return luaL_error(L, "step: expected state table");
    lua_Integer frame = luaL_checkinteger(L, 2);

    if (frame % 3 == 0)
        return luaL_error(L, "step: deliberate error on frame %d", (int)frame);

    /* digest := FNV-1a over existing state (next order); unsigned arithmetic
     * only — signed overflow would be UB and a determinism hazard. */
    uint32_t digest = 2166136261u;
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        int vt = lua_type(L, -1);
        digest = (digest ^ (uint32_t)vt) * 16777619u;
        if (vt == LUA_TNUMBER)
            digest = (digest ^ (uint32_t)luaL_checkinteger(L, -1)) * 16777619u;
        if (vt == LUA_TSTRING) {
            size_t_blyt sl = 0;
            const char *sv = lua_tolstring(L, -1, &sl);
            for (size_t_blyt i = 0; i < sl; i++)
                digest = (digest ^ (uint32_t)(unsigned char)sv[i]) * 16777619u;
        }
        lua_pop(L, 1);
        /* key digesting: string keys only */
        if (lua_type(L, -1) == LUA_TSTRING) {
            size_t_blyt kl = 0;
            const char *k = lua_tolstring(L, -1, &kl);
            for (size_t_blyt i = 0; i < kl; i++)
                digest = (digest ^ (uint32_t)(unsigned char)k[i]) * 16777619u;
        }
    }

    /* mutate state */
    lua_getfield(L, 1, "acc");
    lua_Integer acc = luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    lua_pushinteger(L, acc + frame * frame);
    lua_setfield(L, 1, "acc");
    lua_pushinteger(L, (lua_Integer)digest);
    lua_seti(L, 1, frame);
    lua_pushstring(L, frame % 2 ? "odd" : "even");
    lua_setfield(L, 1, "last");

    lua_pushinteger(L, (lua_Integer)digest);
    lua_pushstring(L, frame % 2 ? "ODD" : "EVEN");
    return 2;
}

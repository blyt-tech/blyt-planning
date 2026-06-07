/* Spike T stage 2: scalar ops through the bridged (raw) export path.
 * sum5 exercises what the typed path cannot do: >4 arguments, lua_gettop,
 * and two return values — using only scalar bridge ops. */

#include <blyt.h>

BLYT_LUA_MODULE_EXPORT_RAW(spike, sum5) {
    int n = lua_gettop(L);
    int32_t sum = 0;
    for (int i = 1; i <= n; i++)
        sum += (int32_t)luaL_checkinteger(L, i);
    lua_pushinteger(L, sum);
    lua_pushinteger(L, n);
    return 2;
}

/* Regression: the typed fast path must keep working alongside. */
BLYT_LUA_MODULE_EXPORT_I32(spike, twice, int32_t x) {
    return x * 2;
}

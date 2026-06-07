/* Spike T (f): bridged benchmark wrapper — exactly 10 bridge ECALLs per call:
 * gettop, 4× tointegerx (checkinteger), 3× type, 2× pushinteger. */

#include <blyt.h>

BLYT_LUA_MODULE_EXPORT_RAW(spike, bench) {
    int n = lua_gettop(L); /* 1 */
    lua_Integer a = luaL_checkinteger(L, 1); /* 2 */
    lua_Integer b = luaL_checkinteger(L, 2); /* 3 */
    lua_Integer c = luaL_checkinteger(L, 3); /* 4 */
    lua_Integer d = luaL_checkinteger(L, 4); /* 5 */
    int t1 = lua_type(L, 1); /* 6 */
    int t2 = lua_type(L, 2); /* 7 */
    int t3 = lua_type(L, 3); /* 8 */
    lua_pushinteger(L, a + b + c + d + t1 + t2 + t3); /* 9 */
    lua_pushinteger(L, n); /* 10 */
    return 2;
}

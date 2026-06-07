/* Spike T stage 3: string marshalling + error model through the bridge.
 * - echo_upper: string arg in (any size — exercises the tolstring arena
 *   retry path past 4 KiB), uppercased string + original length out.
 * - nul_roundtrip: returns a string with embedded NULs (length-delimited
 *   pushlstring).
 * - fail: luaL_error with a formatted message — must surface as a
 *   catchable Lua error on both targets (ADR-0084), coroutine resumable.
 */

#include <blyt.h>
#include <stdlib.h>

BLYT_LUA_MODULE_EXPORT_RAW(spike, echo_upper) {
    size_t_blyt len = 0;
    const char *s = luaL_checklstring(L, 1, &len);
    char *buf = malloc(len ? len : 1);
    if (!buf)
        return luaL_error(L, "echo_upper: out of memory");
    for (size_t_blyt i = 0; i < len; i++) {
        char c = s[i];
        buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    lua_pushlstring(L, buf, len);
    lua_pushinteger(L, (lua_Integer)len);
    free(buf);
    return 2;
}

BLYT_LUA_MODULE_EXPORT_RAW(spike, nul_roundtrip) {
    static const char raw[] = {'a', '\0', 'b', '\0', 'c'};
    lua_pushlstring(L, raw, 5);
    return 1;
}

BLYT_LUA_MODULE_EXPORT_RAW(spike, fail) {
    const char *msg = luaL_checkstring(L, 1);
    return luaL_error(L, "boom: %s", msg);
}

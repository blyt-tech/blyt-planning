/* Spike T stage 4: table access through the bridge.
 * - summarize(cfg): reads cfg.name / cfg.scale (getfield), sums cfg.items
 *   (rawlen + geti), iterates cfg with lua_next (fixed-seed order must be
 *   identical across paths), and returns a NEW result table (createtable +
 *   setfield) — config-in / structured-result-out, the ADR-0130 motivating
 *   case.
 * - fill(t): mutates a caller-owned table in place (setfield + seti).
 */

#include <blyt.h>

BLYT_LUA_MODULE_EXPORT_RAW(spike, summarize) {
    if (!lua_istable(L, 1))
        return luaL_error(L, "summarize: expected table, got %s",
                          lua_typename(L, lua_type(L, 1)));

    /* cfg.name (string field) */
    lua_getfield(L, 1, "name");
    size_t_blyt nlen = 0;
    const char *name = luaL_checklstring(L, -1, &nlen);

    /* cfg.scale (integer field) */
    lua_getfield(L, 1, "scale");
    lua_Integer scale = luaL_checkinteger(L, -1);

    /* sum(cfg.items[i] * scale) via rawlen + geti */
    lua_getfield(L, 1, "items");
    if (!lua_istable(L, -1))
        return luaL_error(L, "summarize: items must be a table");
    int items = (int)lua_rawlen(L, -1);
    lua_Integer total = 0;
    for (int i = 1; i <= items; i++) {
        lua_geti(L, -1, i);
        total += luaL_checkinteger(L, -1) * scale;
        lua_pop(L, 1);
    }
    lua_pop(L, 3); /* items, scale, name — stack back to [cfg] */

    /* Iterate cfg with lua_next: key order must be identical across paths
     * (fixed hash seed).  Concatenate string keys in iteration order. */
    char keys[256];
    int kpos = 0, kcount = 0;
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        lua_pop(L, 1); /* drop value, keep key for the next iteration */
        if (lua_type(L, -1) == LUA_TSTRING) {
            size_t_blyt kl = 0;
            const char *k = lua_tolstring(L, -1, &kl);
            for (size_t_blyt j = 0; j < kl && kpos < 250; j++)
                keys[kpos++] = k[j];
            keys[kpos++] = ';';
        }
        kcount++;
    }
    keys[kpos] = '\0';

    /* Build the result table. */
    lua_newtable(L);
    lua_pushlstring(L, name, nlen);
    lua_setfield(L, -2, "name");
    lua_pushinteger(L, total);
    lua_setfield(L, -2, "total");
    lua_pushstring(L, keys);
    lua_setfield(L, -2, "keys");
    lua_pushinteger(L, kcount);
    lua_setfield(L, -2, "n");
    return 1;
}

BLYT_LUA_MODULE_EXPORT_RAW(spike, fill) {
    if (!lua_istable(L, 1))
        return luaL_error(L, "fill: expected table");
    lua_pushinteger(L, 42);
    lua_setfield(L, 1, "answer");
    lua_pushstring(L, "first");
    lua_seti(L, 1, 1);
    return 0;
}

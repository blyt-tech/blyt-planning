#include "lua.h"
#include "lauxlib.h"
#include <stdint.h>

extern void fc_console_print(const char *s);

static int l_add(lua_State *L) {
    int a = (int)lua_tointeger(L, 1);
    int b = (int)lua_tointeger(L, 2);
    lua_pushinteger(L, a + b);
    return 1;
}

/* Loader for require("mylib") — Lua calls this once and caches the result. */
static int luaopen_mylib(lua_State *L) {
    lua_newtable(L);
    lua_pushcfunction(L, l_add);
    lua_setfield(L, -2, "add");
    return 1;
}

__attribute__((section(".text.mylib")))
void cart_lua_modules(lua_State *L) {
    fc_console_print("cart-side init from C\n");

    luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
    lua_pushcfunction(L, luaopen_mylib);
    lua_setfield(L, -2, "mylib");
    lua_pop(L, 1);
}

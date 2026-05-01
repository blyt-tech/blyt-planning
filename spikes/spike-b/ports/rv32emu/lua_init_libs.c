/* Replacement for Lua's linit.c that omits libraries we cannot serve in this
 * spike: no `io` (no filesystem), no `os` (no clock/time/date binding —
 * benchmarks use our own ECALL timer), no `package`/`loadlib` (no dynamic
 * loading), no `coroutine`/`debug`/`utf8` (not exercised by the benchmarks).
 *
 * This file is compiled into liblua54.a in place of upstream linit.c. */

#include "lprefix.h"
#include <stddef.h>
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

static const luaL_Reg loadedlibs[] = {
    {LUA_GNAME,        luaopen_base},
    {LUA_TABLIBNAME,   luaopen_table},
    {LUA_STRLIBNAME,   luaopen_string},
    {LUA_MATHLIBNAME,  luaopen_math},
    {NULL, NULL}
};

LUALIB_API void luaL_openlibs (lua_State *L) {
    const luaL_Reg *lib;
    for (lib = loadedlibs; lib->func; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
}

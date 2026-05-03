/* Spike H — Stage 4: Lua linit replacement (native Linux RV32 build).
 *
 * Mirrors spike-b's lua_init_libs.c: omit io / os / package / coroutine /
 * debug / utf8 because the carts run in a sealed environment with no
 * filesystem, no clocks beyond clock_gettime, and no dynamic loading.
 * For Stage 4 we still want them omitted so the Lua image is comparable
 * to the spike-b benchmark binaries.
 */

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

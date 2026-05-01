#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include <stddef.h>

extern long write(int, const void *, size_t);
extern void *realloc(void *, size_t);
extern void free(void *);

static void *l_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud; (void)osize;
    if (nsize == 0) { free(ptr); return (void *)0; }
    return realloc(ptr, nsize);
}

int main(void)
{
    lua_State *L = lua_newstate(l_alloc, (void *)0);
    if (!L) { write(1, "FAIL: lua_newstate\n", 19); return 1; }

    int rc = luaL_dostring(L, "return 1 + 1");
    if (rc != LUA_OK) { write(1, "FAIL: luaL_dostring\n", 20); return 1; }

    lua_Integer result = lua_tointeger(L, -1);
    if (result != 2) { write(1, "FAIL: wrong result\n", 19); return 1; }
    lua_pop(L, 1);

    /* Force a Lua error so longjmp-across-PLT actually runs.  luaL_dostring
     * is a `||` macro that flattens the error code to 1, so call the
     * underlying pieces directly to read the real LUA_ERRRUN. */
    if (luaL_loadstring(L, "error('boom')") != LUA_OK) {
        write(1, "FAIL: load\n", 11); return 1;
    }
    rc = lua_pcall(L, 0, 0, 0);
    if (rc != LUA_ERRRUN) { write(1, "FAIL: pcall\n", 12); return 1; }
    lua_pop(L, 1);  /* error message */

    lua_close(L);
    write(1, "OK\n", 3);
    return 0;
}

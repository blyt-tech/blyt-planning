/* Spike I Stage 5 — Lua-direct WASM variant of libconsolelua_rv32.c.
 *
 * Same C source structure as the RV32 build, but compiled into a single
 * static WASM module alongside libconsole_wasm.c, the Lua VM sources, and
 * (for case d) cart_lua_modules.c + mylib.c. The Lua C API is portable
 * across the RV32 and WASM builds; only the host runtime primitives differ.
 */

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

extern void fc_console_print(const char *s);

/* Weak — resolved to cart's `cart_lua_modules` for case d, NULL otherwise. */
__attribute__((weak)) extern void cart_lua_modules(lua_State *L);

static const uint8_t  *s_bytecode      = (void *)0;
static uint32_t        s_bytecode_size = 0;
static lua_State      *s_L             = (void *)0;

void fc_consolelua_set_bytecode(const uint8_t *data, uint32_t size) {
    s_bytecode      = data;
    s_bytecode_size = size;
}

static int l_console_print(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    fc_console_print(s);
    return 0;
}

/* Sandbox-only require — same shape as the RV32 build, no luaopen_package. */
static int l_require(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    luaL_getsubtable(L, LUA_REGISTRYINDEX, "_LOADED");
    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);
    luaL_getsubtable(L, LUA_REGISTRYINDEX, "_PRELOAD");
    lua_getfield(L, -1, name);
    if (lua_isnil(L, -1)) return luaL_error(L, "module '%s' not found", name);
    lua_pushstring(L, name);
    lua_call(L, 1, 1);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 1); }
    lua_pushvalue(L, -1);
    lua_setfield(L, -4, name);
    return 1;
}

static void *l_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud; (void)osize;
    if (nsize == 0) { free(ptr); return (void *)0; }
    return realloc(ptr, nsize);
}

static void ensure_state(void) {
    if (s_L) return;
    s_L = lua_newstate(l_alloc, (void *)0);
    if (!s_L) { fc_console_print("FAIL: lua_newstate\n"); return; }

    luaL_requiref(s_L, LUA_GNAME,      luaopen_base,   1); lua_pop(s_L, 1);
    luaL_requiref(s_L, LUA_TABLIBNAME, luaopen_table,  1); lua_pop(s_L, 1);
    luaL_requiref(s_L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(s_L, 1);
    luaL_requiref(s_L, LUA_MATHLIBNAME,luaopen_math,   1); lua_pop(s_L, 1);

    lua_pushcfunction(s_L, l_console_print);
    lua_setglobal(s_L, "console_print");

    lua_pushcfunction(s_L, l_require);
    lua_setglobal(s_L, "require");

    if (cart_lua_modules) cart_lua_modules(s_L);

    if (s_bytecode && s_bytecode_size) {
        int rc = luaL_loadbuffer(s_L, (const char *)s_bytecode,
                                 (size_t)s_bytecode_size, "cart");
        if (rc != LUA_OK) { fc_console_print("FAIL: luaL_loadbuffer\n"); return; }
        rc = lua_pcall(s_L, 0, 0, 0);
        if (rc != LUA_OK) { fc_console_print("FAIL: lua pcall chunk\n"); return; }
    }
}

void fc_cart_init(void) {
    ensure_state();
    if (!s_L) return;
    lua_getglobal(s_L, "init");
    if (lua_isfunction(s_L, -1)) lua_pcall(s_L, 0, 0, 0);
    else lua_pop(s_L, 1);
}

void fc_cart_update(void) {
    if (!s_L) return;
    lua_getglobal(s_L, "update");
    if (lua_isfunction(s_L, -1)) lua_pcall(s_L, 0, 0, 0);
    else lua_pop(s_L, 1);
}

void fc_cart_draw(void) {
    if (!s_L) return;
    lua_getglobal(s_L, "draw");
    if (lua_isfunction(s_L, -1)) lua_pcall(s_L, 0, 0, 0);
    else lua_pop(s_L, 1);
}

/* Spike J — libconsolelua.so with master_hook composition wired in.
 *
 * Extends Spike I's libconsolelua_rv32.c with:
 *  - master_hook installation at lua_State creation time
 *  - per-tic rearm at the top of each fc_cart_{init,update,draw}
 *  - synthetic_reload entry point for ADR-0045 / Spike N validation
 *    (was Spike M before the renumber that inserted the
 *    managed-coroutine spike at M)
 *
 * Compile-time variants drive Stage 1 step 4 overhead measurement:
 *  -DMASTER_OFF              : all three flags off (no-hook baseline)
 *  -DMASTER_BUDGET           : budget on (parity with standalone Spike G)
 *  -DMASTER_BUDGET_DAP_IDLE  : budget + dap on, no client connected
 *  -DMASTER_THROTTLE         : throttle on (parity with standalone Spike G.3)
 *  default                   : all three on (production dev-mode)
 */

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include <stdint.h>
#include <stddef.h>

#include "master_hook.h"

extern void fc_console_print(const char *s);
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

extern void *malloc(size_t);
extern void  free(void *);
extern void *realloc(void *, size_t);

static void *l_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud; (void)osize;
    if (nsize == 0) { free(ptr); return (void *)0; }
    return realloc(ptr, nsize);
}

/* Apply the compile-time variant. The default is "all three on", which is
 * the production dev-mode configuration. */
static void apply_master_config(void) {
#if defined(MASTER_OFF)
    fc_master_hook_cfg.budget_enabled   = 0;
    fc_master_hook_cfg.throttle_enabled = 0;
    fc_master_hook_cfg.dap_enabled      = 0;
#elif defined(MASTER_BUDGET)
    fc_master_hook_cfg.budget_enabled   = 1;
    fc_master_hook_cfg.throttle_enabled = 0;
    fc_master_hook_cfg.dap_enabled      = 0;
#elif defined(MASTER_BUDGET_DAP_IDLE)
    fc_master_hook_cfg.budget_enabled   = 1;
    fc_master_hook_cfg.throttle_enabled = 0;
    fc_master_hook_cfg.dap_enabled      = 1;
#elif defined(MASTER_THROTTLE)
    fc_master_hook_cfg.budget_enabled   = 0;
    fc_master_hook_cfg.throttle_enabled = 1;
    fc_master_hook_cfg.dap_enabled      = 0;
#else
    fc_master_hook_cfg.budget_enabled   = 1;
    fc_master_hook_cfg.throttle_enabled = 1;
    fc_master_hook_cfg.dap_enabled      = 1;
#endif
    fc_master_hook_cfg.budget_ns        = 16667000ULL; /* 16.67 ms / 60 fps */
    fc_master_hook_cfg.throttle_delay_ns = 0; /* calibrated per-bench at runtime */
}

static void configure_sandbox(lua_State *L) {
    luaL_requiref(L, LUA_GNAME,      luaopen_base,   1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table,  1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME,luaopen_math,   1); lua_pop(L, 1);
    lua_pushcfunction(L, l_console_print);
    lua_setglobal(L, "console_print");
    lua_pushcfunction(L, l_require);
    lua_setglobal(L, "require");
    if (cart_lua_modules) cart_lua_modules(L);
}

static void ensure_state(void) {
    if (s_L) return;
    s_L = lua_newstate(l_alloc, (void *)0);
    if (!s_L) { fc_console_print("FAIL: lua_newstate\n"); return; }
    configure_sandbox(s_L);
    apply_master_config();
    fc_consolelua_master_hook_install(s_L);
    if (s_bytecode && s_bytecode_size) {
        int rc = luaL_loadbuffer(s_L, (const char *)s_bytecode,
                                 (size_t)s_bytecode_size, "@cart");
        if (rc != LUA_OK) { fc_console_print("FAIL: luaL_loadbuffer\n"); return; }
        rc = lua_pcall(s_L, 0, 0, 0);
        if (rc != LUA_OK) { fc_console_print("FAIL: lua pcall chunk\n"); return; }
    }
}

void fc_cart_init(void) {
    ensure_state();
    if (!s_L) return;
    fc_consolelua_master_hook_rearm();
    lua_getglobal(s_L, "init");
    if (lua_isfunction(s_L, -1)) lua_pcall(s_L, 0, 0, 0);
    else lua_pop(s_L, 1);
}

void fc_cart_update(void) {
    if (!s_L) return;
    fc_consolelua_master_hook_rearm();
    lua_getglobal(s_L, "update");
    if (lua_isfunction(s_L, -1)) lua_pcall(s_L, 0, 0, 0);
    else lua_pop(s_L, 1);
}

void fc_cart_draw(void) {
    if (!s_L) return;
    fc_consolelua_master_hook_rearm();
    lua_getglobal(s_L, "draw");
    if (lua_isfunction(s_L, -1)) lua_pcall(s_L, 0, 0, 0);
    else lua_pop(s_L, 1);
}

/* Synthetic reload — Stage 4. Tears down the lua_State and re-creates it
 * from new bytecode. State migration is explicitly skipped (Spike N's
 * scope; was Spike M before the renumber). The DAP server's
 * loadedSource emit happens after this returns,
 * sequenced *after* the new state is fully prepared and the master hook
 * is re-installed (avoids the race documented in the plan's risk notes). */
void fc_consolelua_synthetic_reload(const uint8_t *new_bytecode,
                                    uint32_t new_size) {
    if (s_L) { lua_close(s_L); s_L = (void *)0; }
    s_bytecode      = new_bytecode;
    s_bytecode_size = new_size;
    ensure_state();
}

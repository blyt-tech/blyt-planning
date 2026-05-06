/* Spike M — generic full (straight-through) driver.
 *
 * Reference run: no save, no load.  Runs the wrapper + workload from
 * frame 0 to NFRAMES-1; the harness extracts same-host suffixes from
 * any frame for comparison against the load continuations.
 */

#include <stdint.h>
#include <stddef.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "../../cart_runtime/save_state.h"

extern int  printf(const char *, ...);
extern void cart_det_init(void);
extern void cart_det_register(lua_State *L);
extern void cart_det_open_coroutine_lib(lua_State *L);
extern void cart_det_set_load_resume(int b);
extern void cart_det_set_num_frames(int n);

extern const unsigned char *cart_wrapper_lua;
extern unsigned int         cart_wrapper_lua_len;
extern const unsigned char *cart_workload_lua;
extern unsigned int         cart_workload_lua_len;
extern const char          *cart_workload_name;
extern void                 cart_workload_init(void);

#ifndef NFRAMES
#  define NFRAMES 30
#endif

static int panic(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);
    printf("PANIC m_full: %s\n", msg ? msg : "(no msg)");
    return 0;
}

static int run_lua_module(lua_State *L,
                          const char *name,
                          const unsigned char *src, unsigned int len)
{
    if (luaL_loadbufferx(L, (const char *)src, (size_t)len, name, "t") != LUA_OK) {
        printf("PANIC m_full load %s: %s\n", name, lua_tostring(L, -1));
        return 0;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        printf("PANIC m_full pcall %s: %s\n", name, lua_tostring(L, -1));
        return 0;
    }
    return 1;
}

int main(void)
{
    save_state_init();
    cart_det_init();
    cart_workload_init();
    cart_det_set_load_resume(0);
    cart_det_set_num_frames(NFRAMES);

    lua_State *L = luaL_newstate();
    if (!L) { printf("PANIC m_full: luaL_newstate\n"); return 1; }
    lua_atpanic(L, panic);
    luaL_openlibs(L);
    cart_det_open_coroutine_lib(L);
    cart_det_register(L);

    if (!run_lua_module(L, "blyt32_coroutine",
                        cart_wrapper_lua, cart_wrapper_lua_len)) return 1;
    if (!run_lua_module(L, cart_workload_name,
                        cart_workload_lua, cart_workload_lua_len)) return 1;

    lua_close(L);
    return 0;
}

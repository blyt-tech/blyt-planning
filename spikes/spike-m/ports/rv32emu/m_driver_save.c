/* Spike M — generic save-side driver.
 *
 * Used by every Stage 1+ cart: the cart-specific shim (cart_workload_*.c)
 * defines the workload byte array and name, the regions_<cart>.c file
 * declares the region registry, and this file provides the save-side
 * `main()` that loads the wrapper + workload, runs to frame N, snapshots,
 * and emits the BUFFER hex line.
 *
 * Save frame is argv[1] (default 15).
 */

#include <stdint.h>
#include <stddef.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "../../cart_runtime/save_state.h"

extern int  printf(const char *, ...);
extern int  atoi(const char *);
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

#ifndef SAVE_FRAME_DEFAULT
#  define SAVE_FRAME_DEFAULT 15
#endif
#ifndef SAVE_BUF_CAP
#  define SAVE_BUF_CAP 32768
#endif

static int panic(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);
    printf("PANIC m_save: %s\n", msg ? msg : "(no msg)");
    return 0;
}

static int run_lua_module(lua_State *L,
                          const char *name,
                          const unsigned char *src, unsigned int len)
{
    if (luaL_loadbufferx(L, (const char *)src, (size_t)len, name, "t") != LUA_OK) {
        printf("PANIC m_save load %s: %s\n", name, lua_tostring(L, -1));
        return 0;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        printf("PANIC m_save pcall %s: %s\n", name, lua_tostring(L, -1));
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    uint32_t save_frame = SAVE_FRAME_DEFAULT;
    if (argc >= 2) save_frame = (uint32_t)atoi(argv[1]);

    save_state_init();
    cart_det_init();
    cart_workload_init();
    cart_det_set_load_resume(0);
    cart_det_set_num_frames((int)save_frame + 1);

    lua_State *L = luaL_newstate();
    if (!L) { printf("PANIC m_save: luaL_newstate\n"); return 1; }
    lua_atpanic(L, panic);
    luaL_openlibs(L);
    cart_det_open_coroutine_lib(L);
    cart_det_register(L);

    if (!run_lua_module(L, "blyt32_coroutine",
                        cart_wrapper_lua, cart_wrapper_lua_len)) return 1;
    if (!run_lua_module(L, cart_workload_name,
                        cart_workload_lua, cart_workload_lua_len)) return 1;

    static uint8_t buf[SAVE_BUF_CAP];
    uint32_t n = save_state_save(buf, sizeof(buf), save_frame);
    if (!n) {
        printf("PANIC m_save: save_state_save returned 0\n");
        return 1;
    }
    save_state_emit_hex(buf, n);

    lua_close(L);
    return 0;
}

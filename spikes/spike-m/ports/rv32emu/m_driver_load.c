/* Spike M — generic load-side driver.
 *
 * Reads a saved buffer (argv[1]), restores frame_state + cart_state +
 * the persistent_scripts slot table, clears the active bitmap (so the
 * cart's `create` calls allocate slots starting at 0 and pick up the
 * saved blob bytes via `script_read_blob`), then runs the wrapper +
 * workload to NFRAMES.
 */

#include <stdint.h>
#include <stddef.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "../../cart_runtime/save_state.h"
#include "../../cart_runtime/save_io.h"
#include "../../cart_runtime/region_persistent_scripts.h"

extern int  printf(const char *, ...);
extern void cart_det_init(void);
extern void cart_det_register(lua_State *L);
extern void cart_det_open_coroutine_lib(lua_State *L);
extern void cart_det_set_load_resume(int b);
extern void cart_det_set_num_frames(int n);
extern void cart_det_resync_rng_from_fs(void);

extern const unsigned char *cart_wrapper_lua;
extern unsigned int         cart_wrapper_lua_len;
extern const unsigned char *cart_workload_lua;
extern unsigned int         cart_workload_lua_len;
extern const char          *cart_workload_name;
extern void                 cart_workload_init(void);

#ifndef NFRAMES
#  define NFRAMES 30
#endif
#ifndef SAVE_BUF_CAP
#  define SAVE_BUF_CAP 32768
#endif
#ifndef HEX_BUF_CAP
#  define HEX_BUF_CAP 131072
#endif

static int panic(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);
    printf("PANIC m_load: %s\n", msg ? msg : "(no msg)");
    return 0;
}

static int run_lua_module(lua_State *L,
                          const char *name,
                          const unsigned char *src, unsigned int len)
{
    if (luaL_loadbufferx(L, (const char *)src, (size_t)len, name, "t") != LUA_OK) {
        printf("PANIC m_load load %s: %s\n", name, lua_tostring(L, -1));
        return 0;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        printf("PANIC m_load pcall %s: %s\n", name, lua_tostring(L, -1));
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("PANIC m_load: usage: <buffer-hex-file>\n");
        return 1;
    }

    save_state_init();
    cart_det_init();
    cart_workload_init();

    static char hexbuf[HEX_BUF_CAP];
    long n = save_io_read_file(argv[1], hexbuf, sizeof(hexbuf) - 1);
    if (n < 0) { printf("PANIC m_load: cannot read buffer\n"); return 1; }
    hexbuf[n] = '\0';

    static uint8_t bin[SAVE_BUF_CAP];
    long m = save_io_parse_hex(hexbuf, (size_t)n, bin, sizeof(bin));
    if (m < 0) { printf("PANIC m_load: hex parse failed\n"); return 1; }
    if (!save_state_load(bin, (uint32_t)m)) {
        printf("PANIC m_load: save_state_load rejected buffer\n");
        return 1;
    }

    persistent_scripts_unmark_all();
    cart_det_resync_rng_from_fs();
    cart_det_set_load_resume(1);
    cart_det_set_num_frames(NFRAMES);

    lua_State *L = luaL_newstate();
    if (!L) { printf("PANIC m_load: luaL_newstate\n"); return 1; }
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

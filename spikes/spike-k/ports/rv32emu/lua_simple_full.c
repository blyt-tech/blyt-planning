/* Spike K Stage 2 — lua_simple_full.elf — straight-through baseline.
 *
 * Same Lua workload, no save / no load, run for all NFRAMES frames.
 * The continuation digest stream produced by lua_simple_load.elf must
 * equal the suffix of this stream from frame N+1 onward.
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
extern void cart_det_set_load_resume(int b);
extern void cart_det_set_num_frames(int n);

#include "embed/det_lua_simple.h"
extern unsigned char det_lua_simple_lua[];
extern unsigned int  det_lua_simple_lua_len;

#ifndef NFRAMES
#  define NFRAMES 30
#endif

static int panic(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);
    printf("PANIC lua_simple_full: %s\n", msg ? msg : "(no msg)");
    return 0;
}

int main(void)
{
    save_state_init();
    cart_det_init();
    cart_det_set_load_resume(0);
    cart_det_set_num_frames(NFRAMES);

    lua_State *L = luaL_newstate();
    if (!L) { printf("PANIC lua_simple_full: luaL_newstate failed\n"); return 1; }
    lua_atpanic(L, panic);
    luaL_openlibs(L);
    cart_det_register(L);

    if (luaL_loadbufferx(L, (const char *)det_lua_simple_lua,
                            (size_t)det_lua_simple_lua_len,
                            "det_lua_simple", "t") != LUA_OK) {
        printf("PANIC lua_simple_full load: %s\n", lua_tostring(L, -1));
        return 1;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        printf("PANIC lua_simple_full pcall: %s\n", lua_tostring(L, -1));
        return 1;
    }

    lua_close(L);
    return 0;
}

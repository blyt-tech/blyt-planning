/* Spike K Stage 2 — lua_simple_load.elf.
 *
 * Reads the saved buffer (argv[1]), restores frame_state and
 * cart_state_lua_simple, then loads + runs the embedded
 * det_lua_simple.lua workload.  The cart's `console.is_load_resume()`
 * binding returns true so the script skips its init phase; the loop
 * picks up at frame_state.frame (which the deserializer just set to
 * save_frame + 1).
 */

#include <stdint.h>
#include <stddef.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/save_state.h"
#include "../../cart_runtime/save_io.h"

extern int  printf(const char *, ...);
extern void cart_det_init(void);
extern void cart_det_register(lua_State *L);
extern void cart_det_set_load_resume(int b);
extern void cart_det_set_num_frames(int n);
extern void cart_det_resync_rng_from_fs(void);

#include "embed/det_lua_simple.h"
extern unsigned char det_lua_simple_lua[];
extern unsigned int  det_lua_simple_lua_len;

#ifndef NFRAMES
#  define NFRAMES 30
#endif

static int panic(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);
    printf("PANIC lua_simple_load: %s\n", msg ? msg : "(no msg)");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("PANIC lua_simple_load: usage: <buffer-hex-file>\n");
        return 1;
    }

    save_state_init();
    cart_det_init();

    static char hexbuf[16384];
    long n = save_io_read_file(argv[1], hexbuf, sizeof(hexbuf) - 1);
    if (n < 0) { printf("PANIC lua_simple_load: cannot read buffer\n"); return 1; }
    hexbuf[n] = '\0';

    static uint8_t bin[8192];
    long m = save_io_parse_hex(hexbuf, (size_t)n, bin, sizeof(bin));
    if (m < 0) { printf("PANIC lua_simple_load: hex parse failed\n"); return 1; }
    if (!save_state_load(bin, (uint32_t)m)) {
        printf("PANIC lua_simple_load: save_state_load rejected buffer\n");
        return 1;
    }

    /* The deserializer just overwrote frame_state.rng_state/inc; mirror
     * back into the local g_rng so subsequent console.unit_float() calls
     * pick up where the save left off. */
    cart_det_resync_rng_from_fs();
    cart_det_set_load_resume(1);
    cart_det_set_num_frames(NFRAMES);

    lua_State *L = luaL_newstate();
    if (!L) { printf("PANIC lua_simple_load: luaL_newstate failed\n"); return 1; }
    lua_atpanic(L, panic);
    luaL_openlibs(L);
    cart_det_register(L);

    if (luaL_loadbufferx(L, (const char *)det_lua_simple_lua,
                            (size_t)det_lua_simple_lua_len,
                            "det_lua_simple", "t") != LUA_OK) {
        printf("PANIC lua_simple_load load: %s\n", lua_tostring(L, -1));
        return 1;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        printf("PANIC lua_simple_load pcall: %s\n", lua_tostring(L, -1));
        return 1;
    }

    lua_close(L);
    return 0;
}

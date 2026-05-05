/* Spike K Stage 2 — lua_simple_save.elf.
 *
 * Loads the embedded det_lua_simple.lua workload, runs it through
 * frame N (configured via num_frames = N+1 so the script's loop
 * terminates after that frame's commit_frame), then takes a save and
 * emits the buffer hex on stdout.
 *
 * The workload is unchanged save-side vs straight-through — same
 * source, embedded the same way (xxd -i header).  The only thing the
 * driver controls is `num_frames` (loop upper bound) and the
 * `is_load_resume` flag (false here).
 */

#include <stdint.h>
#include <stddef.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/cart_state_lua_simple.h"
#include "../../cart_runtime/save_state.h"

extern int  printf(const char *, ...);
extern int  atoi(const char *);
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
#ifndef SAVE_FRAME_DEFAULT
#  define SAVE_FRAME_DEFAULT 15
#endif

static int panic(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);
    printf("PANIC lua_simple_save: %s\n", msg ? msg : "(no msg)");
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t save_frame = SAVE_FRAME_DEFAULT;
    if (argc >= 2) save_frame = (uint32_t)atoi(argv[1]);

    save_state_init();
    cart_det_init();
    cart_det_set_load_resume(0);
    /* The Lua loop runs from console.frame() (= 0) to num_frames-1.
     * To stop after save_frame's commit_frame, set num_frames = save_frame+1. */
    cart_det_set_num_frames((int)save_frame + 1);

    lua_State *L = luaL_newstate();
    if (!L) { printf("PANIC lua_simple_save: luaL_newstate failed\n"); return 1; }
    lua_atpanic(L, panic);
    luaL_openlibs(L);
    cart_det_register(L);

    if (luaL_loadbufferx(L, (const char *)det_lua_simple_lua,
                            (size_t)det_lua_simple_lua_len,
                            "det_lua_simple", "t") != LUA_OK) {
        printf("PANIC lua_simple_save load: %s\n", lua_tostring(L, -1));
        return 1;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        printf("PANIC lua_simple_save pcall: %s\n", lua_tostring(L, -1));
        return 1;
    }

    /* fs.frame was bumped past save_frame by the last commit_frame.
     * The save header should record the save_frame itself, not the
     * post-commit value — pass it explicitly. */
    static uint8_t buf[8192];
    uint32_t n = save_state_save(buf, sizeof(buf), save_frame);
    if (!n) {
        printf("PANIC lua_simple_save: save_state_save returned 0\n");
        return 1;
    }
    save_state_emit_hex(buf, n);

    lua_close(L);
    return 0;
}

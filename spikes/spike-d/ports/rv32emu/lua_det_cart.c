/* Spike D Lua cart driver.
 *
 * Embeds one Lua workload (xxd-encoded), registers the `console` global
 * via cart_det_register(), runs the script once.  The script is
 * responsible for its own frame loop and for calling
 * `console.commit_frame()` at the end of each frame.  The cart prints
 * a single `=== <name> ===` header before the script runs and nothing
 * else around the digest stream.
 *
 * Compile-time knobs (-D):
 *   WORKLOAD_NAME    human-readable label (string literal)
 *   WORKLOAD_HEADER  quoted xxd-generated header path
 *   WORKLOAD_BUFSYM  byte-array symbol xxd produced
 *   WORKLOAD_LENSYM  length symbol xxd produced
 */

#include <stdint.h>
#include <stddef.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

extern int  printf(const char *, ...);
extern void cart_det_init(void);
extern void cart_det_register(lua_State *L);

#ifndef WORKLOAD_NAME
#  define WORKLOAD_NAME "unknown"
#endif
#ifndef WORKLOAD_HEADER
#  error "WORKLOAD_HEADER must be set"
#endif
#ifndef WORKLOAD_BUFSYM
#  error "WORKLOAD_BUFSYM must be set"
#endif
#ifndef WORKLOAD_LENSYM
#  error "WORKLOAD_LENSYM must be set"
#endif

#include WORKLOAD_HEADER

extern unsigned char WORKLOAD_BUFSYM[];
extern unsigned int  WORKLOAD_LENSYM;

static int panic(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);
    printf("PANIC %s: %s\n", WORKLOAD_NAME, msg ? msg : "(no msg)");
    return 0;
}

int main(void)
{
    cart_det_init();

    lua_State *L = luaL_newstate();
    if (!L) {
        printf("PANIC %s: luaL_newstate failed\n", WORKLOAD_NAME);
        return 1;
    }
    lua_atpanic(L, panic);
    luaL_openlibs(L);
    cart_det_register(L);

    printf("=== %s ===\n", WORKLOAD_NAME);

    if (luaL_loadbufferx(L,
                         (const char *)WORKLOAD_BUFSYM,
                         (size_t)WORKLOAD_LENSYM,
                         WORKLOAD_NAME, "t") != LUA_OK) {
        printf("PANIC %s load: %s\n", WORKLOAD_NAME, lua_tostring(L, -1));
        return 1;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        printf("PANIC %s pcall: %s\n", WORKLOAD_NAME, lua_tostring(L, -1));
        return 1;
    }

    lua_close(L);
    return 0;
}

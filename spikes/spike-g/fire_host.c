/* Spike G fire-test host — confirms the hook fires and the error is recovered.
 *
 * Built with a tiny HOOK_BUDGET_NS (e.g. 100000 = 100 µs) so that
 * any real benchmark immediately exceeds the budget.
 *
 * Exit codes: 0 = HOOK FIRE TEST PASS, 1 = HOOK FIRE TEST FAIL.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#ifndef HOOK_COUNT
#define HOOK_COUNT 100
#endif

#ifndef HOOK_BUDGET_NS
#define HOOK_BUDGET_NS 100000ULL   /* 100 µs — guaranteed to fire */
#endif

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t tic_start_ns;
static const uint64_t budget_ns = (uint64_t)HOOK_BUDGET_NS;

static void hook_fn(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    if (now_ns() - tic_start_ns > budget_ns)
        luaL_error(L, "budget exceeded");
}

static int panic_cb(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);
    fprintf(stderr, "PANIC: %s\n", msg ? msg : "(no msg)");
    return 0;
}

int main(int argc, char **argv)
{
    const char *bench_name = (argc >= 2 && argv[1] && *argv[1]) ? argv[1] : "doom_tick";

    char path[256];
    snprintf(path, sizeof(path), "/%s.lua", bench_name);

    lua_State *L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "HOOK FIRE TEST FAIL: luaL_newstate\n");
        return 1;
    }
    lua_atpanic(L, panic_cb);
    luaL_openlibs(L);

    if (luaL_loadfile(L, path) != LUA_OK) {
        fprintf(stderr, "HOOK FIRE TEST FAIL: load %s: %s\n",
                path, lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }
    int chunk_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(L, LUA_REGISTRYINDEX, chunk_ref);
    tic_start_ns = now_ns();
    lua_sethook(L, hook_fn, LUA_MASKCOUNT, HOOK_COUNT);
    int rc = lua_pcall(L, 0, 0, 0);
    lua_sethook(L, NULL, 0, 0);

    if (rc == LUA_ERRRUN) {
        const char *msg = lua_tostring(L, -1);
        if (msg && strstr(msg, "budget exceeded")) {
            printf("HOOK FIRE TEST PASS\n");
            lua_close(L);
            return 0;
        }
        fprintf(stderr, "HOOK FIRE TEST FAIL: LUA_ERRRUN but wrong message: %s\n",
                msg ? msg : "(null)");
        lua_close(L);
        return 1;
    }

    if (rc == LUA_OK) {
        fprintf(stderr, "HOOK FIRE TEST FAIL: pcall returned LUA_OK — hook did not fire\n");
    } else {
        fprintf(stderr, "HOOK FIRE TEST FAIL: pcall returned %d\n", rc);
    }
    lua_close(L);
    return 1;
}

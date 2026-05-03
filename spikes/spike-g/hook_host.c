/* Spike G host shim — Spike F's Lua-direct-WASM host extended with a
 * lua_sethook Tier-1 per-tic CPU budget enforcer.
 *
 * Build-time parameters (all overridable via -D):
 *
 *   HOOK_ENABLED    1 = install lua_sethook before each tic (default).
 *                   0 = compile out the sethook call entirely; binary is
 *                       functionally equivalent to spike-f/host.c but built
 *                       through this path (no-hook baseline for the sweep).
 *
 *   HOOK_COUNT      N instruction count passed to lua_sethook (default 1000).
 *                   The hook fires every N Lua VM instructions.
 *
 *   HOOK_BUDGET_NS  Wall-clock nanoseconds per tic before the hook fires
 *                   lua_error() (default 16667000 = 16.67 ms, one 60 fps
 *                   frame).  Set to a tiny value in correctness tests to
 *                   force early termination.
 *
 * The hook re-arms every tic (lua_sethook before each lua_pcall) so that
 * tic_start_ns is always fresh.  A single lua_sethook at newstate time would
 * leave tic_start_ns stale after the first pcall returns.
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

#ifndef HOOK_ENABLED
#define HOOK_ENABLED 1
#endif

#ifndef HOOK_COUNT
#define HOOK_COUNT 1000
#endif

#ifndef HOOK_BUDGET_NS
#define HOOK_BUDGET_NS 16667000ULL
#endif

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

#ifdef STUB_CLOCK
/* Diagnostic: stub hook clock always returns 0 so the hook never fires but
 * is still installed and dispatched every HOOK_COUNT instructions.  This
 * isolates the hook dispatch cost from the clock_gettime cost.  The outer
 * tic timing still uses the real now_ns(). */
static uint64_t hook_now_ns(void) { return 0; }
#else
static uint64_t hook_now_ns(void) { return now_ns(); }
#endif

#if HOOK_ENABLED
static uint64_t tic_start_ns;
static const uint64_t budget_ns = (uint64_t)HOOK_BUDGET_NS;

static void hook_fn(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    if (hook_now_ns() - tic_start_ns > budget_ns)
        luaL_error(L, "budget exceeded");
}
#endif

static int frames_for(const char *name)
{
    if (strcmp(name, "doom_tick") == 0)     return 30;
    if (strcmp(name, "doom_tick_gc") == 0)  return 30;
    if (strcmp(name, "entity_update") == 0) return 50;
    return 20;
}

static int panic(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);
    fprintf(stderr, "PANIC: %s\n", msg ? msg : "(no msg)");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || !argv[1] || !*argv[1]) {
        fprintf(stderr, "usage: lua_host <bench>\n");
        return 2;
    }
    const char *bench_name = argv[1];

    char path[256];
    snprintf(path, sizeof(path), "/%s.lua", bench_name);

    lua_State *L = luaL_newstate();
    if (!L) {
        printf("PANIC %s: luaL_newstate failed\n", bench_name);
        return 1;
    }
    lua_atpanic(L, panic);
    luaL_openlibs(L);

    if (luaL_loadfile(L, path) != LUA_OK) {
        printf("PANIC %s load: %s\n", bench_name, lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }
    int chunk_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    int frames = frames_for(bench_name);
    uint64_t min_us = (uint64_t)-1;
    uint64_t max_us = 0;
    uint64_t sum_us = 0;
    int      ran    = 0;

    for (int i = 0; i < frames; i++) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, chunk_ref);
#if HOOK_ENABLED
        tic_start_ns = hook_now_ns();
        lua_sethook(L, hook_fn, LUA_MASKCOUNT, HOOK_COUNT);
#endif
        uint64_t t0 = now_ns();
        int rc = lua_pcall(L, 0, 0, 0);
#if HOOK_ENABLED
        lua_sethook(L, NULL, 0, 0);
#endif
        uint64_t t1 = now_ns();

        if (rc != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            printf("PANIC %s pcall %d: %s\n",
                   bench_name, i, err ? err : "(no msg)");
            lua_close(L);
            return 1;
        }

        uint64_t us = (t1 - t0) / 1000ULL;
        printf("FRAME %s %d %lu\n", bench_name, i, (unsigned long)us);
        if (us < min_us) min_us = us;
        if (us > max_us) max_us = us;
        sum_us += us;
        ran++;
    }

    luaL_unref(L, LUA_REGISTRYINDEX, chunk_ref);
    lua_close(L);

    if (ran == 0) {
        printf("SUMMARY %s frames=0\n", bench_name);
        return 0;
    }
    uint64_t mean = sum_us / (uint64_t)ran;
    printf("SUMMARY %s frames=%d min=%lu max=%lu mean=%lu\n",
           bench_name, ran,
           (unsigned long)min_us, (unsigned long)max_us, (unsigned long)mean);
    return 0;
}

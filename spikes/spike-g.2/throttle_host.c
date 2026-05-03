/* Spike G.2 host shim — LUA_MASKLINE dev-mode throttle.
 *
 * Build-time parameters (all overridable via -D):
 *
 *   THROTTLE_ENABLED   1 = install LUA_MASKLINE hook at startup (default).
 *                      0 = compile out entirely; binary is the no-hook baseline.
 *
 *   THROTTLE_DELAY_NS  Nanoseconds to busy-wait on each line event (default 0).
 *                      0 = hook installed but body is empty (measures raw
 *                          LUA_MASKLINE dispatch overhead without injecting delay).
 *
 *   LINECOUNT_ENABLED  1 = increment a counter on each hook fire and print total
 *                      to stderr as "LINE_TOTAL <bench> <count>".
 *                      0 = no counting (default).
 *
 * The hook is installed once at luaL_newstate time, not per tic.  Unlike
 * spike-g's budget-check hook (which needs a fresh tic_start_ns each tic),
 * the throttle hook carries no per-tic state.
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

#ifndef THROTTLE_ENABLED
#define THROTTLE_ENABLED 1
#endif

#ifndef THROTTLE_DELAY_NS
#define THROTTLE_DELAY_NS 0
#endif

#ifndef LINECOUNT_ENABLED
#define LINECOUNT_ENABLED 0
#endif

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

#if THROTTLE_ENABLED

#if LINECOUNT_ENABLED
static uint64_t line_count = 0;
#endif

static void throttle_hook(lua_State *L, lua_Debug *ar)
{
#if LINECOUNT_ENABLED
    line_count++;
#endif
#if THROTTLE_DELAY_NS > 0
    uint64_t until = now_ns() + (uint64_t)THROTTLE_DELAY_NS;
    while (now_ns() < until) {}
#endif
    (void)L; (void)ar;
}

#endif /* THROTTLE_ENABLED */

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

#if THROTTLE_ENABLED
    lua_sethook(L, throttle_hook, LUA_MASKLINE, 0);
#endif

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
        uint64_t t0 = now_ns();
        int rc = lua_pcall(L, 0, 0, 0);
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

#if LINECOUNT_ENABLED
    fprintf(stderr, "LINE_TOTAL %s %llu\n", bench_name, (unsigned long long)line_count);
#endif

    return 0;
}

/* Spike H — Stage 4: native-Linux RV32 Lua benchmark cart.
 *
 * Adapted from spike-b/ports/rv32emu/lua_cart.c.  The shape is identical:
 *   - one Lua benchmark script is xxd-embedded
 *   - the cart loads it once, then re-runs it BENCH_FRAMES times
 *   - per-iteration wall time and a SUMMARY line are printed
 *
 * Differences from the rv32emu port:
 *   - we link against musl libc (printf, clock_gettime, malloc, math come
 *     from there — no freestanding runtime)
 *   - no softfloat/ECALL shims — RV32 musl-cross provides whatever the
 *     toolchain's libc supports
 *
 * Compile-time knobs (passed via -D from Makefile):
 *   BENCH_NAME, BENCH_HEADER, BENCH_BUFSYM, BENCH_LENSYM, BENCH_FRAMES
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#ifndef BENCH_NAME
#  define BENCH_NAME "unknown"
#endif
#ifndef BENCH_FRAMES
#  define BENCH_FRAMES 100
#endif
#ifndef BENCH_HEADER
#  error "BENCH_HEADER must be set"
#endif
#ifndef BENCH_BUFSYM
#  error "BENCH_BUFSYM must be set"
#endif
#ifndef BENCH_LENSYM
#  error "BENCH_LENSYM must be set"
#endif

#include BENCH_HEADER

extern unsigned char BENCH_BUFSYM[];
extern unsigned int  BENCH_LENSYM;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int panic(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);
    fprintf(stderr, "PANIC %s: %s\n", BENCH_NAME, msg ? msg : "(no msg)");
    return 0;
}

int main(void)
{
    lua_State *L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "PANIC %s: luaL_newstate failed\n", BENCH_NAME);
        return 1;
    }
    lua_atpanic(L, panic);
    luaL_openlibs(L);

    if (luaL_loadbufferx(L,
                         (const char *)BENCH_BUFSYM,
                         (size_t)BENCH_LENSYM,
                         BENCH_NAME, "t") != LUA_OK) {
        fprintf(stderr, "PANIC %s load: %s\n", BENCH_NAME, lua_tostring(L, -1));
        return 1;
    }
    int chunk_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    uint64_t min_us = (uint64_t)-1;
    uint64_t max_us = 0;
    uint64_t sum_us = 0;
    int      ran    = 0;

    for (int i = 0; i < BENCH_FRAMES; i++) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, chunk_ref);
        uint64_t t0 = now_ns();
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            fprintf(stderr, "PANIC %s pcall %d: %s\n",
                    BENCH_NAME, i, lua_tostring(L, -1));
            return 1;
        }
        uint64_t t1 = now_ns();
        uint64_t us = (t1 - t0) / 1000ULL;
        printf("FRAME %s %d %lu\n", BENCH_NAME, i, (unsigned long)us);
        if (us < min_us) min_us = us;
        if (us > max_us) max_us = us;
        sum_us += us;
        ran++;
    }

    luaL_unref(L, LUA_REGISTRYINDEX, chunk_ref);
    lua_close(L);

    if (ran == 0) {
        printf("SUMMARY %s frames=0\n", BENCH_NAME);
        return 0;
    }
    uint64_t mean = sum_us / (uint64_t)ran;
    printf("SUMMARY %s frames=%d min=%lu max=%lu mean=%lu\n",
           BENCH_NAME, ran,
           (unsigned long)min_us, (unsigned long)max_us, (unsigned long)mean);
    return 0;
}

/* Spike F host shim — Lua 5.4 compiled directly to WASM, runs Spike B's
 * benchmark .lua scripts in a hot loop and prints per-tick wall time.
 *
 * Mirrors spike-b/ports/rv32emu/lua_cart.c structurally so the FRAME and
 * SUMMARY printf lines are byte-identical (modulo timing values).  The
 * difference is the layer cake: spike-b runs Lua-in-RV32IMFC under
 * rv32emu; spike-f runs Lua compiled straight to WebAssembly with no
 * intermediate emulator.
 *
 * Each benchmark .lua file is embedded into the WASM module's MEMFS via
 * --embed-file at /<name>.lua.  We re-execute the chunk by stashing it
 * in the registry and `lua_rawgeti`-ing it into a fresh stack each tick
 * — same shape as spike-b.
 *
 * The benchmarks under spike-b/benchmarks/ are pure CPU workloads.  None
 * of them reference console-API primitives (draw, input, audio), so we
 * register no stubs.  PLAN.md flags this as a possible follow-up if a
 * future cart needs them.
 *
 * `run_user(name)` is exposed to JS via `--pre-js user-pre.js`, which
 * sets `Module.noInitialRun = true` and wraps `callMain([name])`.  Same
 * shape as spike-e's user-pre.js so the harness only needs the WASM
 * module URL changed.
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

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Frame counts mirror spike-b's BENCH_FRAMES_<bench> defaults so the
 * FRAME / SUMMARY line counts agree.  Anything not listed here falls
 * back to 20, which is what spike-b uses for the long-running CPU
 * benchmarks (binarytrees, fannkuch, fasta, mandelbrot, nbody,
 * spectral-norm). */
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
        uint64_t t0 = now_ns();
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            printf("PANIC %s pcall %d: %s\n",
                   bench_name, i, lua_tostring(L, -1));
            lua_close(L);
            return 1;
        }
        uint64_t t1 = now_ns();
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

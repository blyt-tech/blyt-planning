/* Lua benchmark cart for the rv32emu spike-b harness.
 *
 * Each cart ELF embeds one benchmark script (xxd-encoded into a header).
 * The cart loads the script once, then re-executes it in a hot loop and
 * reports per-iteration wall time.  The "frame" budget for fc32 is 16.7 ms;
 * the harness prints both per-iteration time in microseconds and a pass/fail
 * marker so the result document can be assembled by grep.
 *
 * Compile-time knobs:
 *   BENCH_NAME     - human-readable label (string literal)
 *   BENCH_HEADER   - quoted xxd-generated header path (string literal)
 *   BENCH_BUFSYM   - the byte-array symbol xxd produces (identifier)
 *   BENCH_LENSYM   - the length symbol xxd produces (identifier)
 *   BENCH_FRAMES   - how many iterations to run (default 100)
 *
 * Output (one line per iteration):
 *   FRAME <name> <i> <us>
 *   SUMMARY <name> frames=<N> min=<us> max=<us> mean=<us>
 */

#include <stdint.h>
#include <stddef.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

uint64_t now_ns(void);   /* in lua_runtime.c */
int      printf(const char *, ...);

#ifndef BENCH_NAME
#  define BENCH_NAME "unknown"
#endif
#ifndef BENCH_FRAMES
#  define BENCH_FRAMES 100
#endif
#ifndef BENCH_HEADER
#  error "BENCH_HEADER must be set to the xxd-generated header path"
#endif
#ifndef BENCH_BUFSYM
#  error "BENCH_BUFSYM must be set"
#endif
#ifndef BENCH_LENSYM
#  error "BENCH_LENSYM must be set"
#endif

#include BENCH_HEADER

/* xxd -i emits an `unsigned char NAME[]` (sometimes `static const ...`).
 * We accept whatever it gave us and grab a pointer + length. */
extern unsigned char BENCH_BUFSYM[];
extern unsigned int  BENCH_LENSYM;

static int panic(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);
    printf("PANIC %s: %s\n", BENCH_NAME, msg ? msg : "(no msg)");
    return 0;
}

int main(void)
{
    lua_State *L = luaL_newstate();
    if (!L) {
        printf("PANIC %s: luaL_newstate failed\n", BENCH_NAME);
        return 1;
    }
    lua_atpanic(L, panic);
    luaL_openlibs(L);

    /* Load + compile the script once.  This isolates parse cost from the
     * benchmark loop and matches how a real game would call into Lua: load
     * once at startup, then call the resulting closure each frame. */
    if (luaL_loadbufferx(L,
                        (const char *)BENCH_BUFSYM,
                        (size_t)BENCH_LENSYM,
                        BENCH_NAME, "t") != LUA_OK) {
        printf("PANIC %s load: %s\n", BENCH_NAME, lua_tostring(L, -1));
        return 1;
    }

    /* Stash the compiled chunk in the registry so we can re-call it from a
     * fresh stack each iteration without re-parsing. */
    int chunk_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    uint64_t min_us = (uint64_t)-1;
    uint64_t max_us = 0;
    uint64_t sum_us = 0;
    int      ran    = 0;

    for (int i = 0; i < BENCH_FRAMES; i++) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, chunk_ref);
        uint64_t t0 = now_ns();
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            printf("PANIC %s pcall %d: %s\n", BENCH_NAME, i, lua_tostring(L, -1));
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

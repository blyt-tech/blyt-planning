/* Spike F determinism cross-check host — Lua 5.4 direct-to-WASM.
 *
 * Runs one of the Spike D determinism workloads (det_doom_tick or
 * det_entity_update) and emits the same DIGEST stream that Spike D's
 * RV32IMFC build produces.  The test criterion: DIGEST lines are
 * byte-identical to spikes/spike-d/digests/digests.arm64.txt for the two
 * Lua workloads.
 *
 * All determinism machinery (PCG32, frame_state, FNV-1a-64, NaN canon,
 * console bindings) is inlined here to keep the WASM build self-contained —
 * no spike-d header copies needed on the Emscripten include path.
 *
 * The `console` global matches spike-d/ports/rv32emu/lua_det_bindings.c
 * exactly: same function names, same PCG32 seed, same FNV-1a implementation,
 * same DIGEST printf format ("%08x%08x" hi/lo split).
 *
 * strtof override: Spike D's freestanding RV32 build uses the naive strtof
 * from spike-b/ports/rv32emu/lua_runtime.c (iterative digit accumulation
 * with float scale *= 0.1f), which is NOT correctly-rounded for all decimal
 * literals.  It parses "6.2831853" as 0x40c90fda instead of the correctly-
 * rounded 0x40c90fdb (1 ULP difference), causing cascading divergence in all
 * angle computations.  We provide the same naive implementation here so that
 * Lua's lua_str2number (which calls strtof) produces bit-identical constants,
 * making the WASM digest stream match the RV32 reference.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

/* ── naive strtof / strtod (matches spike-b/ports/rv32emu/lua_runtime.c) ── */
/* Emscripten wraps calls to these via -Wl,--wrap so our versions intercept
 * every strtof call made by Lua's number-parsing path. */

float __wrap_strtof(const char *s, char **end)
{
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    float v = 0.0f;
    while (*s >= '0' && *s <= '9') { v = v * 10.0f + (float)(*s++ - '0'); }
    if (*s == '.') {
        s++;
        float scale = 0.1f;
        while (*s >= '0' && *s <= '9') {
            v += (float)(*s++ - '0') * scale;
            scale *= 0.1f;
        }
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        int eneg = 0, e = 0;
        if (*s == '-') { eneg = 1; s++; } else if (*s == '+') s++;
        while (*s >= '0' && *s <= '9') e = e * 10 + (*s++ - '0');
        float f = 1.0f;
        for (int i = 0; i < e; i++) f *= 10.0f;
        if (eneg) v /= f; else v *= f;
    }
    if (end) *end = (char *)s;
    return neg ? -v : v;
}

double __wrap_strtod(const char *s, char **end)
{
    return (double)__wrap_strtof(s, end);
}

/* ── PCG32 ──────────────────────────────────────────────────────────────── */
/* Matches cart_runtime/pcg32.h verbatim. */

#define PCG_DEFAULT_STATE  0xfc320001ULL
#define PCG_DEFAULT_INC    0x14057b7ef767814fULL

typedef struct { uint64_t state, inc; } pcg32_t;

static void pcg32_seed(pcg32_t *r, uint64_t st, uint64_t inc)
{
    r->state = 0u;
    r->inc   = (inc << 1u) | 1u;
    r->state = r->state * 6364136223846793005ULL + r->inc;
    r->state += st;
    r->state = r->state * 6364136223846793005ULL + r->inc;
}

static uint32_t pcg32_next(pcg32_t *r)
{
    uint64_t old = r->state;
    r->state = old * 6364136223846793005ULL + r->inc;
    uint32_t xs  = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xs >> rot) | (xs << ((-rot) & 31u));
}

/* Uses upper 24 bits / 2^24 — same formula as pcg32.h. */
static float pcg32_unit_float(pcg32_t *r)
{
    uint32_t u = pcg32_next(r) >> 8;
    return (float)u * (1.0f / 16777216.0f);
}

/* ── frame_state ────────────────────────────────────────────────────────── */
/* Layout matches cart_runtime/frame_state.h; packed so the digest byte
 * stream is host-independent (no compiler-inserted padding). */

#define FRAME_STATE_MAX_MOBS 64

typedef struct __attribute__((packed)) {
    float    x, y, vx, vy;
    uint32_t state;
} frame_state_mob;

typedef struct __attribute__((packed)) {
    uint32_t        frame;
    uint64_t        rng_state;
    uint64_t        rng_inc;
    float           accum_sin;
    float           accum_cos;
    float           accum_sqrt;
    float           accum_misc;
    frame_state_mob mobs[FRAME_STATE_MAX_MOBS];
} frame_state_t;

/* ── NaN canonicalization ───────────────────────────────────────────────── */
/* Matches cart_runtime/nan_canon.h: any NaN → 0x7fc00000 (quiet NaN,
 * payload zero). */

static float canonicalize_nanf(float x)
{
    union { float f; uint32_t u; } v = { .f = x };
    uint32_t exp_bits = (v.u >> 23) & 0xffu;
    uint32_t frac     =  v.u        & 0x7fffffu;
    if (exp_bits == 0xffu && frac != 0u)
        v.u = 0x7fc00000u;
    return v.f;
}

/* ── FNV-1a-64 digest ───────────────────────────────────────────────────── */
/* Matches cart_runtime/digest.c: canonicalize floats, hash whole struct,
 * emit "DIGEST <frame> %08x%08x\n" (hi/lo split avoids %016llx portability
 * concerns — same reasoning as spike-d even though we have full 64-bit
 * printf here, keeping format identical is the point). */

#define FNV1A_OFFSET 0xcbf29ce484222325ULL
#define FNV1A_PRIME  0x00000100000001b3ULL

static void emit_digest(frame_state_t *s)
{
    s->accum_sin  = canonicalize_nanf(s->accum_sin);
    s->accum_cos  = canonicalize_nanf(s->accum_cos);
    s->accum_sqrt = canonicalize_nanf(s->accum_sqrt);
    s->accum_misc = canonicalize_nanf(s->accum_misc);
    for (int i = 0; i < FRAME_STATE_MAX_MOBS; i++) {
        s->mobs[i].x  = canonicalize_nanf(s->mobs[i].x);
        s->mobs[i].y  = canonicalize_nanf(s->mobs[i].y);
        s->mobs[i].vx = canonicalize_nanf(s->mobs[i].vx);
        s->mobs[i].vy = canonicalize_nanf(s->mobs[i].vy);
    }

    uint64_t h = FNV1A_OFFSET;
    const unsigned char *p = (const unsigned char *)s;
    for (size_t i = 0; i < sizeof(*s); i++) {
        h ^= (uint64_t)p[i];
        h *= FNV1A_PRIME;
    }

    uint32_t hi = (uint32_t)(h >> 32);
    uint32_t lo = (uint32_t)(h & 0xffffffffu);
    printf("DIGEST %u %08x%08x\n", (unsigned)s->frame, hi, lo);
}

/* ── singletons ─────────────────────────────────────────────────────────── */

static frame_state_t g_fs;
static pcg32_t       g_rng;

static void cart_det_init(void)
{
    memset(&g_fs, 0, sizeof(g_fs));
    pcg32_seed(&g_rng, PCG_DEFAULT_STATE, PCG_DEFAULT_INC);
    g_fs.rng_state = g_rng.state;
    g_fs.rng_inc   = g_rng.inc;
}

/* ── Lua console bindings ───────────────────────────────────────────────── */
/* Matches spike-d/ports/rv32emu/lua_det_bindings.c exactly. */

static int l_rng(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)pcg32_next(&g_rng));
    return 1;
}

static int l_unit_float(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)pcg32_unit_float(&g_rng));
    return 1;
}

static int l_add_sin(lua_State *L)
{
    g_fs.accum_sin  += (float)luaL_checknumber(L, 1);
    return 0;
}

static int l_add_cos(lua_State *L)
{
    g_fs.accum_cos  += (float)luaL_checknumber(L, 1);
    return 0;
}

static int l_add_sqrt(lua_State *L)
{
    g_fs.accum_sqrt += (float)luaL_checknumber(L, 1);
    return 0;
}

static int l_add_misc(lua_State *L)
{
    g_fs.accum_misc += (float)luaL_checknumber(L, 1);
    return 0;
}

static int l_set_mob(lua_State *L)
{
    lua_Integer idx = luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= FRAME_STATE_MAX_MOBS) return 0;
    frame_state_mob *m = &g_fs.mobs[(int)idx];
    m->x     = (float)luaL_checknumber(L,  2);
    m->y     = (float)luaL_checknumber(L,  3);
    m->vx    = (float)luaL_checknumber(L,  4);
    m->vy    = (float)luaL_checknumber(L,  5);
    m->state = (uint32_t)luaL_checkinteger(L, 6);
    return 0;
}

static int l_commit_frame(lua_State *L)
{
    (void)L;
    g_fs.rng_state = g_rng.state;
    g_fs.rng_inc   = g_rng.inc;
    emit_digest(&g_fs);
    g_fs.frame++;
    return 0;
}

static const luaL_Reg console_funcs[] = {
    { "rng",          l_rng          },
    { "unit_float",   l_unit_float   },
    { "add_sin",      l_add_sin      },
    { "add_cos",      l_add_cos      },
    { "add_sqrt",     l_add_sqrt     },
    { "add_misc",     l_add_misc     },
    { "set_mob",      l_set_mob      },
    { "commit_frame", l_commit_frame },
    { NULL, NULL }
};

static void cart_det_register(lua_State *L)
{
    lua_newtable(L);
    luaL_setfuncs(L, console_funcs, 0);
    lua_setglobal(L, "console");
}

/* ── main ───────────────────────────────────────────────────────────────── */

static int lua_panic_handler(lua_State *L)
{
    const char *msg = lua_tostring(L, -1);
    fprintf(stderr, "PANIC: %s\n", msg ? msg : "(no msg)");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || !argv[1] || !*argv[1]) {
        fprintf(stderr, "usage: lua_det_host <workload>\n");
        fprintf(stderr, "  workload: det_doom_tick | det_entity_update\n");
        return 2;
    }
    const char *name = argv[1];

    cart_det_init();

    lua_State *L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "luaL_newstate failed\n");
        return 1;
    }
    lua_atpanic(L, lua_panic_handler);
    luaL_openlibs(L);
    cart_det_register(L);

    char path[256];
    snprintf(path, sizeof(path), "/%s.lua", name);

    printf("=== %s ===\n", name);

    if (luaL_loadfile(L, path) != LUA_OK) {
        fprintf(stderr, "load error (%s): %s\n", path, lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "runtime error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }

    lua_close(L);
    fflush(stdout);
    return 0;
}

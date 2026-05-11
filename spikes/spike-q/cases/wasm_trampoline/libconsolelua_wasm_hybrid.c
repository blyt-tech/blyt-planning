/* libconsolelua_wasm_hybrid.c — Spike Q Stage 3: Lua-direct WASM host with
 * rv32emu call-on-demand trampoline.
 *
 * Extends Spike I's libconsolelua_wasm.c with:
 *   blyt_hybrid_init()      — loads the Rust cart ELF into rv32emu, reads
 *                             the .lua_exports section, and registers a
 *                             typed trampoline for each exported function.
 *   trampoline functions    — C functions called by the Lua VM; each one reads
 *                             Lua stack args, calls rv32emu_call_fn, and pushes
 *                             the result.
 *   per-call overhead       — wrap each rv32emu_call_fn call in timestamps and
 *                             collect stats for the informational OVERHEAD line.
 *
 * WASM build: compiled by emcc alongside Lua VM sources, libconsole_wasm.c,
 * and rv32emu sources (with apply_call_fn.py patch applied).
 * The Rust cart ELF is embedded via --preload-file rust_cart.elf.
 */

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <emscripten.h>

extern void fc_console_print(const char *s);

/* ── .lua_exports section binary layout (80 bytes per entry) ─────────────── */

#define EXPORT_MODULE_LEN  32
#define EXPORT_NAME_LEN    32
#define EXPORT_ENTRY_SIZE  80

typedef struct {
    char     module[EXPORT_MODULE_LEN];
    char     name[EXPORT_NAME_LEN];
    uint32_t sym_addr;
    uint8_t  arg_types[4];  /* 0x00=end, 0x01=i32/u32, 0x02=f32 */
    uint8_t  ret_type;
    uint8_t  _pad[7];
} lua_export_entry_t;

#define ARG_TYPE_END  0x00
#define ARG_TYPE_I32  0x01
#define ARG_TYPE_F32  0x02

/* ── rv32emu declarations ────────────────────────────────────────────────────
 * Include rv32emu's public header. This file is compiled WITH -include
 * common.h (rv32emu group), NOT together with the Lua VM sources (which
 * define UNUSED differently). The emcc build splits compilation into:
 *   group A: Lua VM sources (lapi.c etc.) — no -include common.h
 *   group B: rv32emu + this file — with -include common.h
 */
#include "riscv.h"

typedef struct {
    int      is_float;
    uint32_t bits;
} rv32emu_arg_t;

extern void rv32emu_call_fn_setup(riscv_t *rv);
extern int  rv32emu_call_fn(riscv_t *rv, uint32_t sym_addr,
                             const rv32emu_arg_t args[], int nargs,
                             uint32_t *ret, int ret_is_float);

/* ── Trampoline registry ──────────────────────────────────────────────────── */

#define MAX_TRAMPOLINES  32

typedef struct {
    char     module[EXPORT_MODULE_LEN];
    char     name[EXPORT_NAME_LEN];
    uint32_t sym_addr;
    uint8_t  arg_types[4];
    uint8_t  ret_type;
} trampoline_t;

static riscv_t    *s_rv          = NULL;
static trampoline_t s_trampolines[MAX_TRAMPOLINES];
static int          s_ntramp      = 0;

/* Per-call timing for OVERHEAD measurement (fast_add only) */
static double s_overhead_sum_us  = 0.0;
static double s_overhead_sq_us   = 0.0;
static int    s_overhead_count   = 0;
static double s_overhead_p99_us  = 0.0;
#define OVERHEAD_SAMPLES  1000

/* ── Trampoline C function (registered as Lua C function) ─────────────────── */

/* Each trampoline is statically registered with its index as an upvalue. */
static int trampoline_call(lua_State *L, int tidx)
{
    const trampoline_t *tr = &s_trampolines[tidx];
    rv32emu_arg_t args[4];
    int nargs = 0;

    for (int i = 0; i < 4 && tr->arg_types[i] != ARG_TYPE_END; i++) {
        if (tr->arg_types[i] == ARG_TYPE_F32) {
            union { float f; uint32_t u; } fv;
            fv.f = (float)lua_tonumber(L, i + 1);
            args[nargs++] = (rv32emu_arg_t){ .is_float = 1, .bits = fv.u };
        } else {
            args[nargs++] = (rv32emu_arg_t){ .is_float = 0,
                                              .bits = (uint32_t)lua_tointeger(L, i + 1) };
        }
    }

    double t0 = emscripten_get_now();
    uint32_t ret = 0;
    int ret_is_float = (tr->ret_type == ARG_TYPE_F32);
    rv32emu_call_fn(s_rv, tr->sym_addr, args, nargs, &ret, ret_is_float);
    double t1 = emscripten_get_now();
    double us = (t1 - t0) * 1000.0;

    /* Collect timing for the first fast_add trampoline (index 0). */
    if (tidx == 0 && s_overhead_count < OVERHEAD_SAMPLES) {
        s_overhead_sum_us += us;
        s_overhead_sq_us  += us * us;
        if (us > s_overhead_p99_us) s_overhead_p99_us = us;
        s_overhead_count++;
    }

    /* Push as integer regardless of ret_type for determinism with rv32 path.
     * The rv32 path's lua_fast_add also uses lua_pushinteger (because
     * libconsolelua.so's __extendsfdf2 stub returns 0.0 for any float,
     * making lua_pushnumber unusable). Matching format ensures byte-equal
     * Stage 4 digest across rv32/arm64, rv32/amd64, and WASM/Node. */
    if (ret_is_float) {
        union { float f; uint32_t u; } fv; fv.u = ret;
        lua_pushinteger(L, (lua_Integer)(int32_t)fv.f);  /* truncate to int */
    } else {
        lua_pushinteger(L, (lua_Integer)(int32_t)ret);
    }
    return 1;
}

/* Individual trampoline wrappers — one per export slot (up to MAX_TRAMPOLINES).
 * This avoids closures and keeps the C function signature simple.
 * Each trampoline_N simply calls trampoline_call(L, N). */

#define TRAMP(N) \
static int trampoline_##N(lua_State *L) { return trampoline_call(L, N); }

TRAMP(0) TRAMP(1) TRAMP(2) TRAMP(3) TRAMP(4) TRAMP(5) TRAMP(6) TRAMP(7)
TRAMP(8) TRAMP(9) TRAMP(10) TRAMP(11) TRAMP(12) TRAMP(13) TRAMP(14) TRAMP(15)

typedef int (*lua_cfn_t)(lua_State *);
static const lua_cfn_t s_tramp_fns[MAX_TRAMPOLINES] = {
    trampoline_0,  trampoline_1,  trampoline_2,  trampoline_3,
    trampoline_4,  trampoline_5,  trampoline_6,  trampoline_7,
    trampoline_8,  trampoline_9,  trampoline_10, trampoline_11,
    trampoline_12, trampoline_13, trampoline_14, trampoline_15,
};

/* ── Minimal 32-bit ELF reader (section locator + symtab walker) ─────────── */

typedef struct { uint8_t ident[16]; uint16_t type; uint16_t machine;
                 uint32_t version, entry, phoff, shoff, flags;
                 uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} Elf32_Ehdr;
typedef struct { uint32_t name, type, flags, addr, offset, size,
                          link, info, addralign, entsize; } Elf32_Shdr;
typedef struct { uint32_t name, value, size; uint8_t info, other;
                 uint16_t shndx; } Elf32_Sym;

#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define STT_FUNC     2
#define ELF32_ST_TYPE(i) ((i) & 0xf)

/* Locate a section by name in the ELF file; return data pointer + size. */
static const uint8_t *elf_find_section(const uint8_t *d, size_t sz,
                                        const char *secname, uint32_t *out_sz)
{
    if (sz < sizeof(Elf32_Ehdr)) return NULL;
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)d;
    if (eh->shoff == 0 || eh->shstrndx >= eh->shnum) return NULL;
    const Elf32_Shdr *shdrs = (const Elf32_Shdr *)(d + eh->shoff);
    const char *shstr = (const char *)(d + shdrs[eh->shstrndx].offset);
    for (uint16_t i = 0; i < eh->shnum; i++) {
        if (strcmp(shstr + shdrs[i].name, secname) == 0) {
            if (out_sz) *out_sz = shdrs[i].size;
            return d + shdrs[i].offset;
        }
    }
    return NULL;
}

/* Find a function symbol's value by name. */
static uint32_t elf_sym_addr(const uint8_t *d, size_t sz, const char *symname)
{
    if (sz < sizeof(Elf32_Ehdr)) return 0;
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)d;
    const Elf32_Shdr *shdrs = (const Elf32_Shdr *)(d + eh->shoff);
    for (uint16_t i = 0; i < eh->shnum; i++) {
        if (shdrs[i].type != SHT_SYMTAB) continue;
        uint32_t strtab_off = shdrs[shdrs[i].link].offset;
        uint32_t ent = shdrs[i].entsize ? shdrs[i].entsize : sizeof(Elf32_Sym);
        uint32_t nsym = shdrs[i].size / ent;
        const char *strtab = (const char *)(d + strtab_off);
        for (uint32_t si = 0; si < nsym; si++) {
            const Elf32_Sym *sym = (const Elf32_Sym *)(d + shdrs[i].offset + si * ent);
            if (sym->value == 0) continue;
            if (ELF32_ST_TYPE(sym->info) != STT_FUNC) continue;
            if (strcmp(strtab + sym->name, symname) == 0) return sym->value;
        }
    }
    return 0;
}

/* ── blyt_hybrid_init — called before Lua state creation ─────────────────── */

void blyt_hybrid_init(lua_State *L)
{
    /* --- Step 1: read rust_cart.elf from MEMFS --- */
    FILE *f = fopen("/rust_cart.elf", "rb");
    if (!f) {
        fc_console_print("FAIL: blyt_hybrid_init: cannot open /rust_cart.elf\n");
        return;
    }
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *elf_data = (uint8_t *)malloc((size_t)fsz);
    if (!elf_data || fread(elf_data, 1, (size_t)fsz, f) != (size_t)fsz) {
        fc_console_print("FAIL: blyt_hybrid_init: short read\n");
        fclose(f); return;
    }
    fclose(f);

    /* --- Step 2: create rv32emu and load the ELF --- */
    static char elf_path[] = "/rust_cart.elf";
    static char *argv_arr[] = { elf_path };
    static vm_attr_t attr;   /* vm_attr_t from riscv.h — exact layout match */
    memset(&attr, 0, sizeof attr);
    attr.data.user.elf_program = elf_path;
    attr.mem_size        = 256ULL * 1024 * 1024;
    attr.stack_size      = 0x1000;
    attr.cycle_per_step  = 100;  /* must be non-zero; 0 causes rv_run infinite loop */
    attr.argc = 1;
    attr.argv = argv_arr;

    s_rv = rv_create(&attr);
    if (!s_rv) {
        fc_console_print("FAIL: blyt_hybrid_init: rv_create failed\n");
        free(elf_data); return;
    }

    rv32emu_call_fn_setup(s_rv);

    /* --- Step 3: read .lua_exports section and resolve sym_addrs --- */
    uint32_t sec_sz = 0;
    const lua_export_entry_t *sec =
        (const lua_export_entry_t *)elf_find_section(elf_data, (size_t)fsz,
                                                       ".lua_exports", &sec_sz);
    if (!sec) {
        fc_console_print("FAIL: blyt_hybrid_init: .lua_exports section not found\n");
        free(elf_data); return;
    }

    uint32_t nentries = sec_sz / EXPORT_ENTRY_SIZE;
    for (uint32_t i = 0; i < nentries && s_ntramp < MAX_TRAMPOLINES; i++) {
        const lua_export_entry_t *e = &sec[i];
        if (e->sym_addr == 0 && e->module[0] == '\0') break; /* terminator */
        if (e->sym_addr == 0) continue;                        /* skip placeholders */

        /* Resolve actual guest address from ELF symtab (overrides placeholder). */
        uint32_t addr = elf_sym_addr(elf_data, (size_t)fsz, e->name);
        if (!addr) addr = e->sym_addr; /* fallback to section value */

        /* Register the trampoline in the global trampoline table. */
        trampoline_t *tr = &s_trampolines[s_ntramp];
        strncpy(tr->module, e->module, EXPORT_MODULE_LEN - 1);
        strncpy(tr->name,   e->name,   EXPORT_NAME_LEN  - 1);
        tr->sym_addr = addr;
        memcpy(tr->arg_types, e->arg_types, 4);
        tr->ret_type = e->ret_type;
        s_ntramp++;
    }

    /* --- Step 4: register trampolines into Lua's _PRELOAD ---
     *
     * For each unique module, build a module table and register a loader in
     * _PRELOAD.  For simplicity (spike quality), we assume a single module
     * "mylib" containing both fast_add and fast_mul.
     */
    luaL_getsubtable(L, LUA_REGISTRYINDEX, "_PRELOAD");

    for (int m = 0; m < s_ntramp; ) {
        /* Find the range of trampolines with the same module name. */
        const char *mod = s_trampolines[m].module;
        int mend = m + 1;
        while (mend < s_ntramp &&
               strcmp(s_trampolines[mend].module, mod) == 0) mend++;

        /* Create a module table with all functions pre-registered. */
        lua_newtable(L);
        for (int j = m; j < mend; j++) {
            lua_pushcfunction(L, s_tramp_fns[j]);
            lua_setfield(L, -2, s_trampolines[j].name);
        }

        /* Store the already-built table as the loader result in _LOADED.
         * Then set _PRELOAD[mod] = function() return _LOADED[mod] end
         * via the simpler path: just store in _LOADED so require() finds it. */
        luaL_getsubtable(L, LUA_REGISTRYINDEX, "_LOADED");
        lua_pushvalue(L, -2); /* copy the module table */
        lua_setfield(L, -2, mod); /* _LOADED[mod] = table */
        lua_pop(L, 1); /* pop _LOADED */

        lua_pop(L, 1); /* pop the module table */
        m = mend;
    }

    lua_pop(L, 1); /* pop _PRELOAD */
    free(elf_data);
}

/* ── Module state (same as spike-i Stage 5) ─────────────────────────────── */

__attribute__((weak)) extern void cart_lua_modules(lua_State *L);

static const uint8_t  *s_bytecode      = NULL;
static uint32_t        s_bytecode_size = 0;
static lua_State      *s_L             = NULL;

void fc_consolelua_set_bytecode(const uint8_t *data, uint32_t size)
{
    s_bytecode      = data;
    s_bytecode_size = size;
}

static int l_console_print(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    fc_console_print(s);
    return 0;
}

static int l_require(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    luaL_getsubtable(L, LUA_REGISTRYINDEX, "_LOADED");
    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);
    luaL_getsubtable(L, LUA_REGISTRYINDEX, "_PRELOAD");
    lua_getfield(L, -1, name);
    if (lua_isnil(L, -1)) return luaL_error(L, "module '%s' not found", name);
    lua_pushstring(L, name);
    lua_call(L, 1, 1);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 1); }
    lua_pushvalue(L, -1);
    lua_setfield(L, -4, name);
    return 1;
}

static void *l_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud; (void)osize;
    if (nsize == 0) { free(ptr); return NULL; }
    return realloc(ptr, nsize);
}

static void ensure_state(void)
{
    if (s_L) return;
    s_L = lua_newstate(l_alloc, NULL);
    if (!s_L) { fc_console_print("FAIL: lua_newstate\n"); return; }

    luaL_requiref(s_L, LUA_GNAME,       luaopen_base,   1); lua_pop(s_L, 1);
    luaL_requiref(s_L, LUA_TABLIBNAME,  luaopen_table,  1); lua_pop(s_L, 1);
    luaL_requiref(s_L, LUA_STRLIBNAME,  luaopen_string, 1); lua_pop(s_L, 1);
    luaL_requiref(s_L, LUA_MATHLIBNAME, luaopen_math,   1); lua_pop(s_L, 1);

    lua_pushcfunction(s_L, l_console_print);
    lua_setglobal(s_L, "console_print");

    lua_pushcfunction(s_L, l_require);
    lua_setglobal(s_L, "require");

    /* Load the Rust cart via rv32emu (Stage 3 hybrid path). */
    blyt_hybrid_init(s_L);

    /* Also call cart_lua_modules if the cart provides one (not needed for Stage 3,
     * since the rv32 Rust functions are already registered via trampolines). */
    if (cart_lua_modules) cart_lua_modules(s_L);

    if (s_bytecode && s_bytecode_size) {
        int rc = luaL_loadbuffer(s_L, (const char *)s_bytecode,
                                 (size_t)s_bytecode_size, "cart");
        if (rc != LUA_OK) { fc_console_print("FAIL: luaL_loadbuffer\n"); return; }
        rc = lua_pcall(s_L, 0, 0, 0);
        if (rc != LUA_OK) { fc_console_print("FAIL: lua pcall chunk\n"); return; }
    }
}

void fc_cart_init(void)
{
    ensure_state();
    if (!s_L) return;
    lua_getglobal(s_L, "init");
    if (lua_isfunction(s_L, -1)) lua_pcall(s_L, 0, 0, 0);
    else lua_pop(s_L, 1);
}

void fc_cart_update(void)
{
    if (!s_L) return;
    lua_getglobal(s_L, "update");
    if (lua_isfunction(s_L, -1)) lua_pcall(s_L, 0, 0, 0);
    else lua_pop(s_L, 1);
}

void fc_cart_draw(void)
{
    if (!s_L) return;
    lua_getglobal(s_L, "draw");
    if (lua_isfunction(s_L, -1)) lua_pcall(s_L, 0, 0, 0);
    else lua_pop(s_L, 1);
}

/* emit_overhead — called after the frame loop; prints timing measurement. */
void blyt_hybrid_emit_overhead(void)
{
    if (s_overhead_count == 0) {
        fc_console_print("OVERHEAD no data\n");
        return;
    }
    double mean = s_overhead_sum_us / s_overhead_count;
    char buf[128];
    /* Format: "OVERHEAD mean=<Xus> p99=<Yus>\n" */
    snprintf(buf, sizeof buf, "OVERHEAD mean=%.2fus p99=%.2fus\n",
             mean, s_overhead_p99_us);
    fc_console_print(buf);
}

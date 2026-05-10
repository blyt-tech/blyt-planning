/* test_call_on_demand.c — Spike Q Stage 2: rv32emu_call_fn validation harness.
 *
 * Loads test_funcs.elf into rv32emu and calls add32(3, 4) and addf(1.0f, 2.0f)
 * via rv32emu_call_fn.  Emits RESULT lines for the Makefile gate:
 *
 *   RESULT add32 00000007     (3 + 4 = 7)
 *   RESULT addf  40400000     (1.0f + 2.0f = 3.0f as IEEE 754 hex)
 *
 * Compiled as a host binary (amd64 or arm64) and linked against rv32emu objects.
 * Provides its own main() — rv32emu's main.o is excluded from the link command.
 *
 * Build (in Dockerfile, from /spike-a/rv32emu/):
 *   ar rcs build/librv32emu.a $(ls build/*.o | grep -v main.o)
 *   gcc -O2 -include src/common.h -Isrc/ \
 *       test_call_on_demand.c -Lbuild -lrv32emu \
 *       $(ls build/softfloat/*.o) -lm \
 *       -o test_call_on_demand
 */

/* rv32emu public headers — available via -Isrc/ at build time */
#include "riscv.h"
#include "elf.h"

/* fc32_libpath: defined in main.c (multi-dynload patch), excluded when main.o
 * is not linked.  Set to NULL — the test ELF is statically linked so no
 * dynamic library loading is needed. */
char *fc32_libpath = NULL;

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── rv32emu_call_fn interface (defined in src/call_fn.c) ─────────────────── */

typedef struct {
    int      is_float;
    uint32_t bits;
} rv32emu_arg_t;

int  rv32emu_call_fn(riscv_t *rv, uint32_t sym_addr,
                     const rv32emu_arg_t args[], int nargs,
                     uint32_t *ret, int ret_is_float);
void rv32emu_call_fn_setup(riscv_t *rv);

/* ── helpers ────────────────────────────────────────────────────────────────── */

static void print_hex8(uint32_t v)
{
    static const char hex[] = "0123456789abcdef";
    char buf[9];
    buf[8] = '\0';
    for (int i = 7; i >= 0; i--) { buf[i] = hex[v & 0xf]; v >>= 4; }
    fputs(buf, stdout);
}

/* ── main ──────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    const char *elf_path = (argc > 1) ? argv[1] : "test_funcs.elf";

    /* Create rv32emu instance.
     * vm_attr_t is defined in riscv.h and contains all config fields. */
    static vm_attr_t attr;
    memset(&attr, 0, sizeof attr);
    attr.data.user.elf_program = (char *)elf_path;
    attr.mem_size   = 256ULL * 1024 * 1024;  /* 256 MiB */
    attr.stack_size = 0x1000;                 /* 4 KiB */
    attr.argc = 1;
    attr.argv = (char *[]){ (char *)elf_path };
    attr.fd_stdin       = 0;
    attr.fd_stdout      = 1;
    attr.fd_stderr      = 2;
    attr.cycle_per_step = 100;  /* default from rv32emu main.c; 0 would spin */

    riscv_t *rv = rv_create(&attr);
    if (!rv) {
        fprintf(stderr, "rv_create failed for %s\n", elf_path);
        return 1;
    }

    /* Resolve symbol addresses from the loaded ELF.
     * elf_get_symbol is rv32emu's built-in symbol lookup. */
    elf_t *elf = elf_new();
    if (!elf || !elf_open(elf, elf_path)) {
        fprintf(stderr, "elf_open failed for %s\n", elf_path);
        rv_delete(rv);
        return 1;
    }
    const struct Elf32_Sym *s_add32 = elf_get_symbol(elf, "add32");
    const struct Elf32_Sym *s_addf  = elf_get_symbol(elf, "addf");
    if (!s_add32 || !s_addf) {
        fprintf(stderr, "Symbols add32/addf not found in %s\n", elf_path);
        rv_delete(rv);
        return 1;
    }
    uint32_t add32_addr = s_add32->st_value;
    uint32_t addf_addr  = s_addf->st_value;

    /* Install sentinel. */
    rv32emu_call_fn_setup(rv);

    /* ── Test 1: add32(3, 4) — integer args and return ── */
    rv32emu_arg_t args_int[2] = {
        {.is_float = 0, .bits = 3},
        {.is_float = 0, .bits = 4},
    };
    uint32_t ret_add32 = 0;
    rv32emu_call_fn(rv, add32_addr, args_int, 2, &ret_add32, 0);
    fputs("RESULT add32 ", stdout);
    print_hex8(ret_add32);
    fputc('\n', stdout);

    /* ── Test 2: addf(1.0f, 2.0f) — float args and return ── */
    union { float f; uint32_t u; } f1 = { .f = 1.0f };  /* 0x3F800000 */
    union { float f; uint32_t u; } f2 = { .f = 2.0f };  /* 0x40000000 */
    rv32emu_arg_t args_flt[2] = {
        {.is_float = 1, .bits = f1.u},
        {.is_float = 1, .bits = f2.u},
    };
    uint32_t ret_addf = 0;
    rv32emu_call_fn(rv, addf_addr, args_flt, 2, &ret_addf, 1);
    fputs("RESULT addf  ", stdout);
    print_hex8(ret_addf);
    fputc('\n', stdout);

    rv_delete(rv);
    return 0;
}

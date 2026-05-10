#!/usr/bin/env python3
"""
Patch rv32emu with the Spike Q rv32emu_call_fn call-on-demand API.

Run from the rv32emu/ directory:
    python3 /spike-q/patches/apply_call_fn.py

Applies all changes in one pass (idempotent):
  src/syscall.c  -- adds sentinel ECALL (0xDEAD) check before the switch
  src/call_fn.c  -- new file: rv32emu_call_fn_setup + rv32emu_call_fn
  Makefile       -- adds call_fn.o to OBJS

With FC32_CALL_FN sentinel check removed, unknown ECALLs still hit rv_log_fatal.
The sentinel (ECALL 57005 = 0xDEAD) is routed to rv_halt() before the switch.
"""

import sys
import os

# ---------------------------------------------------------------------------
# src/syscall.c  — add sentinel check before the switch
# ---------------------------------------------------------------------------

SYSCALL_C = "src/syscall.c"
with open(SYSCALL_C) as f:
    syscall_src = f.read()

SENTINEL_MARKER = "/* fc32 Spike Q: sentinel */"

if SENTINEL_MARKER not in syscall_src:
    OLD = "    switch (syscall) { /* dispatch system call */"
    NEW = """\
    /* fc32 Spike Q: sentinel for rv32emu_call_fn return detection.
     * ECALL 0xDEAD (57005) is fired by the sentinel stub at FC32_SENTINEL_ADDR.
     * It is above the fc32 ECALL range (0-999) and above all rv32emu syscalls. */
    /* fc32 Spike Q: sentinel */
    if (syscall == 57005u) { rv_halt(rv); return; }

    switch (syscall) { /* dispatch system call */"""
    if OLD not in syscall_src:
        sys.exit("ERROR: switch(syscall) not found in src/syscall.c")
    syscall_src = syscall_src.replace(OLD, NEW, 1)
    with open(SYSCALL_C, "w") as f:
        f.write(syscall_src)
    print("src/syscall.c patched.")
else:
    print("src/syscall.c already patched, skipping.")

# ---------------------------------------------------------------------------
# src/call_fn.c  — new file implementing rv32emu_call_fn
# ---------------------------------------------------------------------------

CALL_FN_C = "src/call_fn.c"

CALL_FN_IMPL = r"""/*
 * rv32emu_call_fn — Spike Q call-on-demand API for the Lua+Rust hybrid.
 *
 * Allows the host to invoke a single guest function at a given address,
 * passing i32 or f32 arguments and reading back a single return value,
 * without running the guest from _start to exit.
 *
 * The sentinel mechanism:
 *   - A 12-byte sentinel stub is written to FC32_SENTINEL_ADDR in guest memory.
 *   - The stub executes: lui a7, 14; addi a7, a7, -339; ecall
 *     which loads a7 = 57005 (0xDEAD) and fires ECALL.
 *   - syscall_handler checks for ECALL 57005 BEFORE the switch and calls rv_halt.
 *   - rv_run() returns, and rv32emu_call_fn reads the return registers.
 *
 * ilp32f register ABI:
 *   Integer args:  a0..a7  (rv->X[10..17])
 *   Float args:    fa0..fa7 (rv->F[10..17] — softfloat_float32_t .v field)
 *   Integer ret:  a0  (rv->X[10])
 *   Float ret:    fa0 (rv->F[10].v)
 */

#include "riscv.h"
#include "riscv_private.h"
#include "io.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* Sentinel ECALL number — must match the check in src/syscall.c */
#define FC32_SENTINEL_ECALL  57005u   /* 0xDEAD */

/* Sentinel page guest address — in the gap between cart load region and
 * the library load base (0x08000000).  Safely within the default 256 MB
 * guest address space and not used by the cart or its libraries. */
#define FC32_SENTINEL_ADDR   0x04000000u

/* fa0..fa7 are float registers 10..17 in rv->F[] */
#define RV_FREG_FA0   10

typedef struct {
    int      is_float; /* 0 = integer arg in a-reg, 1 = float arg in fa-reg */
    uint32_t bits;     /* raw bit pattern: IEEE 754 for float, value for int */
} rv32emu_arg_t;

static int s_sentinel_installed = 0;

/*
 * rv32emu_call_fn_setup — write the sentinel stub into guest memory.
 * Must be called after rv_create() + elf_load().  Idempotent.
 *
 * Sentinel code (12 bytes, little-endian RISC-V):
 *   LUI  a7, 14           0x0000E8B7   a7 = 14 << 12 = 0xE000
 *   ADDI a7, a7, -339     0xEAD88893   a7 += -339  → a7 = 0xDEAD = 57005
 *   ECALL                 0x00000073
 */
void rv32emu_call_fn_setup(riscv_t *rv)
{
    if (s_sentinel_installed) return;

    vm_attr_t *attr = PRIV(rv);
    memory_t  *mem  = attr->mem;

    static const uint8_t sentinel[] = {
        0xB7, 0xE8, 0x00, 0x00,  /* lui  a7, 14           */
        0x93, 0x88, 0xD8, 0xEA,  /* addi a7, a7, -339     */
        0x73, 0x00, 0x00, 0x00,  /* ecall                  */
    };
    if (!memory_write(mem, FC32_SENTINEL_ADDR, sentinel, sizeof sentinel)) {
        fprintf(stderr, "rv32emu_call_fn_setup: cannot write sentinel to 0x%08X\n",
                FC32_SENTINEL_ADDR);
        return;
    }
    s_sentinel_installed = 1;
}

/*
 * rv32emu_call_fn — call a guest function and return its result.
 *
 * sym_addr      : guest address of the function to call.
 * args[0..nargs-1]: argument descriptors (is_float + bits).
 * nargs         : number of arguments (max 6; excess are ignored).
 * ret           : output — receives a0 (int) or fa0.v (float) on return.
 * ret_is_float  : 0 = read a0 as integer, 1 = read fa0 as float.
 *
 * Returns 0 on success, 1 if the sentinel was not reached (iteration limit
 * or fault — the caller should treat this as a guest error).
 *
 * The function is NOT re-entrant and is intended for single-threaded use.
 */
int rv32emu_call_fn(riscv_t *rv,
                    uint32_t sym_addr,
                    const rv32emu_arg_t args[],
                    int nargs,
                    uint32_t *ret,
                    int ret_is_float)
{
    rv32emu_call_fn_setup(rv);

    /* Reset halt flag so rv_run() can enter the loop. */
    rv->halt = false;

    /* Set ra = sentinel address.  When the guest function returns (jalr ra),
     * execution jumps to the sentinel stub, which fires ECALL 0xDEAD and halts. */
    rv_set_reg(rv, rv_reg_ra, FC32_SENTINEL_ADDR);

    /* Set PC to the target function. */
    rv_set_pc(rv, sym_addr);

    /* Place arguments in the correct register class.
     * ilp32f ABI: integer args in a0..a5, float args in fa0..fa5.
     * We track separate counters for each class. */
    int iarg = 0, farg = 0;
    for (int i = 0; i < nargs && i < 6; i++) {
        if (args[i].is_float) {
            if (farg < 8) {
                struct riscv_internal *priv = (struct riscv_internal *)rv;
                priv->F[RV_FREG_FA0 + farg].v = args[i].bits;
                farg++;
            }
        } else {
            if (iarg < 8) {
                rv_set_reg(rv, rv_reg_a0 + iarg, args[i].bits);
                iarg++;
            }
        }
    }

    /* Run the interpreter until halt (sentinel ECALL fires or guest exits). */
    rv_run(rv);

    /* Read the return value from the appropriate register. */
    if (ret) {
        if (ret_is_float) {
            struct riscv_internal *priv = (struct riscv_internal *)rv;
            *ret = priv->F[RV_FREG_FA0].v;
        } else {
            *ret = rv_get_reg(rv, rv_reg_a0);
        }
    }

    return 0;
}
"""

if not os.path.exists(CALL_FN_C):
    with open(CALL_FN_C, "w") as f:
        f.write(CALL_FN_IMPL)
    print("src/call_fn.c written.")
else:
    # Check if it's our version
    with open(CALL_FN_C) as f:
        existing = f.read()
    if "rv32emu_call_fn" not in existing:
        with open(CALL_FN_C, "w") as f:
            f.write(CALL_FN_IMPL)
        print("src/call_fn.c replaced.")
    else:
        print("src/call_fn.c already present, skipping.")

# ---------------------------------------------------------------------------
# Makefile — add call_fn.o to OBJS
# ---------------------------------------------------------------------------

MK = "Makefile"
with open(MK) as f:
    mk_src = f.read()

if "call_fn.o" not in mk_src:
    OLD_OBJS = "OBJS := map.o utils.o decode.o io.o syscall.o"
    NEW_OBJS = "OBJS := map.o utils.o decode.o io.o syscall.o call_fn.o"
    if OLD_OBJS not in mk_src:
        sys.exit("ERROR: OBJS line not found in Makefile")
    mk_src = mk_src.replace(OLD_OBJS, NEW_OBJS, 1)
    with open(MK, "w") as f:
        f.write(mk_src)
    print("Makefile patched (call_fn.o added to OBJS).")
else:
    print("Makefile already patched, skipping.")

print("Done.  Build with: make OUT=build -j$(nproc)")

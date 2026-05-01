#!/usr/bin/env python3
"""
Patch rv32emu to support the ADR-0082 MIPS cap.

Run from the rv32emu/ directory:
    python3 ../patches/apply-mips-cap.py

Effects:
  Makefile   -- adds FC32_EXTRA_CFLAGS ?= and CFLAGS += $(FC32_EXTRA_CFLAGS)
                so the cap value can be injected without overriding CFLAGS.
  src/riscv.c -- adds <time.h> and wraps rv_run()'s inner loop with a
                nanosleep throttle guarded by #if FC32_MIPS_CAP > 0.

With FC32_MIPS_CAP == 0 (default) the binary behaves identically to upstream.
"""

import re
import sys
import textwrap

# ---------------------------------------------------------------------------
# Makefile: inject FC32_EXTRA_CFLAGS hook after the initial CFLAGS = line
# ---------------------------------------------------------------------------

MK_OLD = "CFLAGS = -std=gnu11 $(KCONFIG_CFLAGS) -Wall -Wextra -Werror"
MK_NEW = """\
CFLAGS = -std=gnu11 $(KCONFIG_CFLAGS) -Wall -Wextra -Werror
# fc32 spike-a: allow injecting extra flags (e.g. -DFC32_MIPS_CAP=N) without
# overriding the CFLAGS line.  Pass FC32_EXTRA_CFLAGS=... on the make command
# line to enable the ADR-0082 MIPS throttle.
FC32_EXTRA_CFLAGS ?=
CFLAGS += $(FC32_EXTRA_CFLAGS)"""

with open("Makefile") as f:
    mk = f.read()

if MK_OLD not in mk:
    sys.exit("ERROR: expected CFLAGS line not found in Makefile -- patch already applied?")

mk = mk.replace(MK_OLD, MK_NEW, 1)

with open("Makefile", "w") as f:
    f.write(mk)

print("Makefile patched.")

# ---------------------------------------------------------------------------
# src/riscv.c: add <time.h> and throttle loop
# ---------------------------------------------------------------------------

RISCV_C = "src/riscv.c"

with open(RISCV_C) as f:
    src = f.read()

# 1. Add <time.h> after <errno.h>
TIME_OLD = "#include <errno.h>"
TIME_NEW = "#include <errno.h>\n#include <time.h>  /* fc32: MIPS cap uses clock_gettime/nanosleep */"

if TIME_OLD not in src:
    sys.exit("ERROR: <errno.h> include not found in riscv.c -- patch already applied?")

src = src.replace(TIME_OLD, TIME_NEW, 1)

# 2. Replace the inner default run loop with the throttled version.
LOOP_OLD = """\
        /* default main loop */
        for (; !rv_has_halted(rv);) /* run until the flag is done */
            rv_step(rv);            /* step instructions */"""

LOOP_NEW = """\
        /* default main loop */
#if defined(FC32_MIPS_CAP) && FC32_MIPS_CAP > 0
        /* ADR-0082: throttle guest instruction throughput to FC32_MIPS_CAP MIPS.
         * After each step, sleep if wall-clock time is less than the time those
         * cycles would have taken at the target rate:
         *   emulated_ns = cycles * 1e9 / (MIPS * 1e6) = cycles * 1000 / MIPS
         */
        {
            uint64_t _fc32_prev = rv->csr_cycle;
            struct timespec _fc32_t0, _fc32_t1;
            for (; !rv_has_halted(rv);) {
                clock_gettime(CLOCK_MONOTONIC, &_fc32_t0);
                rv_step(rv);
                clock_gettime(CLOCK_MONOTONIC, &_fc32_t1);
                uint64_t _fc32_done = rv->csr_cycle - _fc32_prev;
                _fc32_prev = rv->csr_cycle;
                uint64_t _fc32_emu_ns = _fc32_done * 1000ULL / (uint64_t)(FC32_MIPS_CAP);
                uint64_t _fc32_wall_ns =
                    (uint64_t)(_fc32_t1.tv_sec - _fc32_t0.tv_sec) * 1000000000ULL
                    + (uint64_t)(_fc32_t1.tv_nsec - _fc32_t0.tv_nsec);
                if (_fc32_emu_ns > _fc32_wall_ns) {
                    uint64_t _fc32_sleep_ns = _fc32_emu_ns - _fc32_wall_ns;
                    struct timespec _fc32_ts = {
                        .tv_sec  = (time_t)(_fc32_sleep_ns / 1000000000ULL),
                        .tv_nsec = (long)(_fc32_sleep_ns % 1000000000ULL),
                    };
                    nanosleep(&_fc32_ts, NULL);
                }
            }
        }
#else
        for (; !rv_has_halted(rv);) /* run until the flag is done */
            rv_step(rv);            /* step instructions */
#endif"""

if LOOP_OLD not in src:
    sys.exit("ERROR: target loop not found in riscv.c -- patch already applied or upstream changed?")

src = src.replace(LOOP_OLD, LOOP_NEW, 1)

with open(RISCV_C, "w") as f:
    f.write(src)

print("src/riscv.c patched.")
print("Done. Build with: make FC32_EXTRA_CFLAGS=-DFC32_MIPS_CAP=<N>")

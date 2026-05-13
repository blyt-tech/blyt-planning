/* libblyt32_stub_s.c — ILP32 stub for libblyt32.so (Spike S).
 *
 * Compiled as an ILP32 shared library:
 *   riscv32-linux-musl-gcc -fPIC -shared -Wl,-soname,libblyt32.so
 *       -o libblyt32_stub.so libblyt32_stub_s.c
 *
 * Provides:
 *   1. Stub no-op symbols that the test cart binary imports.
 *   2. A constructor that installs the phase-2 RISCV32 seccomp filter.
 *
 * The constructor runs after ld.so has resolved all DT_NEEDED libraries
 * (including this one) and before the cart's main().  It installs the
 * restrictive phase-2 filter over the more-permissive phase-1 filter
 * installed by the LP64 launcher.  LIFO seccomp semantics mean that
 * phase-2 (installed second, evaluated first) can further restrict what
 * phase-1 allowed.
 *
 * Constructor ordering guarantee (ELF spec):
 *   A library's constructors run before the main executable's constructors,
 *   in DT_NEEDED order.  Since the cart DT_NEEDs libblyt32.so, this
 *   constructor runs before any cart-code constructors.
 *
 * Phase-2 filter note:
 *   The filter is built from seccomp_phase2_riscv32_nrs[] (a PLACEHOLDER
 *   list; see seccomp_allowlist_s.h).  After Stage 3 strace derivation,
 *   update that list and rebuild.
 *
 * seccomp(2) is called here while phase-1 is active.  Phase-1 RISCV32
 * allowlist includes seccomp(277) specifically for this constructor.
 * PR_SET_NO_NEW_PRIVS was set by the LP64 launcher before execve and is
 * inherited across exec; no prctl call is needed here.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Include the filter builder.  The seccomp_allowlist_s.h is shared between
 * the LP64 launcher and this ILP32 shared library.  Both build_arch_dispatch_filter
 * and build_riscv32_only_filter are static inline functions defined there. */
#include "seccomp_allowlist_s.h"

/* ─────────────────────────────────────────────────────────────────────────
 * Phase-2 constructor
 * ───────────────────────────────────────────────────────────────────────── */

__attribute__((constructor))
static void libblyt32_phase2_constructor(void)
{
    /* install_phase2_filter() is defined in seccomp_allowlist_s.h.
     * It builds and installs the restrictive RISCV32-only phase-2 filter.
     * Failure is fatal: if we cannot install the phase-2 filter, the cart
     * would run with only the phase-1 filter (which is too permissive). */
    if (install_phase2_filter() != 0) {
        /* write(2, ...) is in phase-1 RISCV32 allowlist. */
        const char msg[] = "libblyt32: FATAL: phase-2 seccomp install failed\n";
        syscall(SYS_write, 2, msg, sizeof(msg) - 1);
        syscall(SYS_exit_group, 127);
        __builtin_unreachable();
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Stub symbols — no-ops for the test cart to link against.
 * ───────────────────────────────────────────────────────────────────────── */

/* blyt_init: called by the cart binary at startup (optional in the stub). */
void blyt_init(void)
{
    /* No-op stub.  Production libblyt32.so provides API initialisation. */
}

/* blyt_update: placeholder for the per-frame update hook. */
void blyt_update(void)
{
    /* No-op stub. */
}

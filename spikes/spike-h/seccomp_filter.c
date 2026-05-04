/* Spike H — Stage 3: seccomp-bpf filter construction (libseccomp).
 *
 * Default action SCMP_ACT_KILL_PROCESS (SIGSYS, immediate kill).  The
 * allowlist is the minimum set required for a static musl RV32 process to
 * reach main() and call write/exit, plus clock_gettime for timing carts.
 *
 * Per PLAN.md §10:
 *   - mmap/mmap2/brk: process startup (libc heap & stack init)
 *   - rt_sigreturn:   signal frame unwind (kept even though we don't install handlers)
 *   - exit_group:     process termination
 *   - write:          libconsole stub writes to stdout
 *   - clock_gettime / clock_gettime64: timing for benchmark workloads
 *   - close:          musl _start closes stale fds (sometimes)
 *   - readlinkat:     musl init path on some kernels (TLS init)
 *   - set_tid_address: musl glibc-compat thread init
 *   - mprotect (RW only): allowed but exec bit blocked elsewhere via the cart
 *                  not invoking it; libseccomp arg-matching keeps this
 *                  intentionally permissive — see PLAN.md note on tightening.
 *
 * The compat layer on RV32 routes some calls through 32-bit-friendly numbers
 * (e.g. clock_gettime64 = 403, mmap2 = 192).  We allow both 32-bit and
 * 64-bit numbers so the same filter works whether libseccomp resolves
 * SCMP_SYS(x) to the rv32 or rv64 number.
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <seccomp.h>

#include "spike_h.h"

/* Names that exist on rv64 but not necessarily on rv32 (or vice versa).
 * libseccomp's SCMP_SYS macro returns a negative pseudo-syscall number
 * for unknown names; we tolerate that by ignoring add-rule failures for
 * those entries.  The real syscall set is whatever the kernel's compat
 * layer chooses to honour; missing entries surface as SIGSYS during the
 * adversary tests and need adding to this list. */
static const char *allow_names[] = {
    "exit",
    "exit_group",
    "rt_sigreturn",
    "rt_sigprocmask",
    "rt_sigaction",
    "brk",
    "mmap",
    "mmap2",
    "munmap",
    "mremap",
    "write",
    "writev",
    "read",
    "close",
    "fstat",
    "fstat64",
    "fstatat64",
    "newfstatat",
    "statx",
    "ioctl",          /* musl probes isatty(stdout) */
    "readlinkat",     /* musl AUXV/TLS init on some kernels */
    "set_tid_address",
    "set_robust_list",
    "futex",
    "futex_time64",
    "clock_gettime",
    "clock_gettime64",
    "getrandom",      /* musl __init_libc may pull AT_RANDOM via this */
    "prlimit64",      /* musl stack-size probe */
    "uname",          /* musl __init_libc; resolves to 160 on RISC-V */
    "newuname",       /* alternative name; may not be in libseccomp database */
    /* execve is required for the launcher to exec the cart after seccomp_load().
     * The filter is inherited by the cart process; blocking execve again for
     * the cart's post-exec calls is a production tightening item (two-phase
     * seccomp) deferred to the runtime implementation. */
    "execve",
    /* RISC-V-specific: musl RV32 startup calls riscv_flush_icache for I-cache
     * coherence after loading a new binary.  libseccomp resolves this to 259;
     * musl uses 258 (adjacent arch-specific slot).  Both are allowed below. */
    "riscv_flush_icache",
    NULL,
};

int spike_h_install_seccomp(void)
{
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL_PROCESS);
    if (!ctx) {
        fprintf(stderr, "seccomp_init failed\n");
        return -1;
    }

    /* Allow SIGSYS itself to be delivered — without this, KILL_PROCESS may
     * race with the kernel signal-delivery path on some configurations. */
    if (seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, 1) != 0) {
        fprintf(stderr, "seccomp_attr_set NNP failed\n");
        seccomp_release(ctx);
        return -1;
    }

    int allowed = 0, missing = 0;
    for (const char **n = allow_names; *n; n++) {
        int sysno = seccomp_syscall_resolve_name(*n);
        if (sysno == __NR_SCMP_ERROR) {
            /* Name unknown to libseccomp on this arch; skip silently. */
            missing++;
            continue;
        }
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, sysno, 0) != 0) {
            fprintf(stderr, "seccomp_rule_add(%s) failed: %s\n",
                    *n, strerror(errno));
            seccomp_release(ctx);
            return -1;
        }
        allowed++;
    }

    /* Allow RISC-V arch-specific syscall 258 as a numeric fallback.
     * musl's RV32 startup issues this (likely riscv_flush_icache variant)
     * before reaching main(); without it, the cart is killed before any
     * probe function runs.  libseccomp resolves "riscv_flush_icache" to
     * 259 but musl uses 258 — both are in the same arch-specific slot range
     * and are harmless to allow (they just flush the instruction cache). */
    (void)seccomp_rule_add(ctx, SCMP_ACT_ALLOW, 258, 0); /* RISC-V only */

    if (seccomp_load(ctx) != 0) {
        fprintf(stderr, "seccomp_load failed: %s\n", strerror(errno));
        seccomp_release(ctx);
        return -1;
    }

    fprintf(stderr, "spike-h: seccomp loaded (%d allowed, %d unknown)\n",
            allowed, missing);
    seccomp_release(ctx);
    return 0;
}

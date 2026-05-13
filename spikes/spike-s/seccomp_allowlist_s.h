/* seccomp_allowlist_s.h — arch-dispatch BPF filter for Spike S.
 *
 * Defines three allowlists and filter-builder functions for the hardware
 * trusted native-exec path (ADR-0119):
 *
 *   seccomp_phase1_riscv64_nrs[] — LP64 launcher syscalls (post-filter-install)
 *   seccomp_phase1_riscv32_nrs[] — ILP32 ld.so startup + seccomp call
 *   seccomp_phase2_riscv32_nrs[] — ILP32 cart-code syscalls (post-constructor)
 *
 * STATUS: phase1 RISCV32 list empirically validated in Stage 2 (2026-05-13)
 * by strace over adversary_s_dynamic on the c-sky 6.5-rc1 ILP32 kernel.
 * New entries confirmed: fcntl(25), statx(291), writev(66).
 * phase2 RISCV32 list remains placeholder; Stage 3 strace will refine it.
 *
 * KEY PROPERTY: RISC-V uses a unified syscall table.  ILP32 binaries use
 * the same syscall NRs as LP64 binaries (no compat NRs like mmap2 or
 * clock_gettime64 from ARM/x86).  Verify empirically in Stage 3 — if any
 * ILP32-specific NR appears in strace output, it must be added explicitly.
 *
 * AUDIT_ARCH values:
 *   RISCV64: EM_RISCV(0xF3) | LE(0x40000000) | 64BIT(0x80000000) = 0xC00000F3
 *   RISCV32: EM_RISCV(0xF3) | LE(0x40000000)                     = 0x400000F3
 */

#ifndef SECCOMP_ALLOWLIST_S_H
#define SECCOMP_ALLOWLIST_S_H

#include <errno.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#ifndef AUDIT_ARCH_RISCV64
#  define AUDIT_ARCH_RISCV64  0xC00000F3U
#endif
#ifndef AUDIT_ARCH_RISCV32
#  define AUDIT_ARCH_RISCV32  0x400000F3U
#endif

/* Offsets within struct seccomp_data. */
#define SDAT_NR   0
#define SDAT_ARCH 4

#define SECCOMP_MAX_INSNS 512

/* ─────────────────────────────────────────────────────────────────────────
 * Phase-1 RISCV64 allowlist — LP64 launcher syscalls, post-filter-install.
 *
 * After installing phase-1, the launcher only calls execve, possibly write
 * for error output, and exit_group on failure.  All other launcher activity
 * (prctl, seccomp install, setenv) happens BEFORE phase-1 is active.
 * ───────────────────────────────────────────────────────────────────────── */
static const unsigned int seccomp_phase1_riscv64_nrs[] = {
    221, /* execve:      launcher → cart binary                               */
     64, /* write:       error messages before exec                           */
     94, /* exit_group:  launcher exits on error                              */
};
#define SECCOMP_PHASE1_RISCV64_N \
    ((int)(sizeof(seccomp_phase1_riscv64_nrs)/sizeof(seccomp_phase1_riscv64_nrs[0])))

/* ─────────────────────────────────────────────────────────────────────────
 * Phase-1 RISCV32 allowlist — ILP32 ld.so startup + phase-2 install.
 *
 * Empirically derived in Stage 2 (2026-05-13) by strace over
 * adversary_s_dynamic (ilp32d, DT_NEEDED libblyt32.so, musl ld.so).
 * Kernel: c-sky 6.5-rc1.  19 strace lines total.
 *
 * Confirmed by strace: set_tid_address, brk, mmap, openat, fcntl, statx,
 *   read, close, mprotect, writev, seccomp (+ execve for trace start).
 * Not seen in Stage 2 (kept for larger carts / other musl paths):
 *   lseek, fstat, munmap, faccessat, set_robust_list, rseq, getrandom,
 *   prlimit64, rt_sigaction, rt_sigreturn.
 * ───────────────────────────────────────────────────────────────────────── */
static const unsigned int seccomp_phase1_riscv32_nrs[] = {
    /* ── ELF loading / ld.so library resolution (all confirmed by Stage 2 strace) ── */
     56, /* openat:          open libblyt32.so per DT_NEEDED                  */
     57, /* close:           close fd after ELF load                         */
     63, /* read:            read ELF header + section data                  */
     25, /* fcntl:           F_SETFD(FD_CLOEXEC) on lib fd after openat      */
    291, /* statx:           musl uses statx (not fstat) for file info       */
    222, /* mmap:            ELF PT_LOAD segment mapping + guard page         */
    226, /* mprotect:        set segment permissions after mmap + relocation  */
    214, /* brk:             heap init during ld.so startup                  */
     96, /* set_tid_address: musl TLS self-pointer init                       */

    /* ── musl stdio (used by libblyt32.so constructor fprintf) ── */
     66, /* writev:          musl uses writev (not write) for stdio output    */

    /* ── seccomp(2) called by libblyt32.so constructor ── */
    277, /* seccomp:         install phase-2 RISCV32 filter                   */

    /* ── conservative extras: not seen with simple binary but expected for
     *    larger carts or alternative musl code paths ── */
     62, /* lseek:           ELF section seeking (not seen with musl/mmap)   */
     80, /* fstat:           older musl versions use fstat, not statx         */
     79, /* newfstatat:      older musl versions                              */
    215, /* munmap:          unmap temporaries when loading many libs         */
     48, /* faccessat:       ld.so /etc/ld.so.preload check                  */
     99, /* set_robust_list: musl futex list (larger carts with threads)      */
    293, /* rseq:            restartable sequences (some musl builds)         */
    278, /* getrandom:       stack-canary entropy (some musl paths)           */
    261, /* prlimit64:       libc stack-size probe                            */
    134, /* rt_sigaction:    musl default signal handlers (when signals used) */
    139, /* rt_sigreturn:    return from signal handler                       */

    /* ── cart code (needed post-constructor; must match phase-2 set) ── */
     29, /* ioctl:           isatty probe on first write (terminal check)     */
     64, /* write:           cart output (raw write syscall via SYS_write)    */
     93, /* exit:            single-thread exit                               */
     94, /* exit_group:      cart exits                                       */
};
#define SECCOMP_PHASE1_RISCV32_N \
    ((int)(sizeof(seccomp_phase1_riscv32_nrs)/sizeof(seccomp_phase1_riscv32_nrs[0])))

/* ─────────────────────────────────────────────────────────────────────────
 * Phase-2 RISCV32 allowlist — cart-code syscalls post-constructor.
 *
 * Empirically derived from Stage 3 strace (2026-05-13).
 *
 * Note: Stage 3 used LP64 `env` as a launcher wrapper, which contaminated
 * the strace union with glibc LP64 syscalls (faccessat, fstat, futex,
 * prlimit64, rseq, set_robust_list, munmap, getrandom).  These are RISCV64
 * syscalls from the env process — NOT from the ILP32 cart.  The ILP32
 * cart phase (after second execve) confirmed the same minimal set as
 * Stage 2.  The list below is the empirically minimal set.
 *
 * After libblyt32.so's constructor installs phase-2, these are the ONLY
 * syscalls the cart code may make.  Any syscall not in this list that the
 * cart calls will produce SIGSYS.
 *
 * Confirmed needed: write(64) for output; exit(93) and exit_group(94) for
 * termination; ioctl(29) for isatty probe in stdio; read(63) for cart input.
 * The ld.so startup syscalls (openat, mmap, etc.) must NOT be in this list
 * — they are only needed before the constructor runs.
 * ───────────────────────────────────────────────────────────────────────── */
static const unsigned int seccomp_phase2_riscv32_nrs[] = {
     29, /* ioctl:           isatty probe                                     */
     63, /* read:            cart input                                       */
     64, /* write:           cart output                                      */
     93, /* exit:            single-thread exit                               */
     94, /* exit_group:      cart exits                                       */
};
#define SECCOMP_PHASE2_RISCV32_N \
    ((int)(sizeof(seccomp_phase2_riscv32_nrs)/sizeof(seccomp_phase2_riscv32_nrs[0])))


/* ─────────────────────────────────────────────────────────────────────────
 * BPF filter builders
 * ───────────────────────────────────────────────────────────────────────── */

/*
 * build_arch_dispatch_filter — arch-dispatch BPF for the phase-1 launcher.
 *
 * Layout (N64 = rv64_n, N32 = rv32_n):
 *
 *   [0]               LD arch
 *   [1]               JEQ RISCV64, jt=0, jf=(2+2*N64)   → RV64 block or past it
 *   [2]               LD nr                               ┐ RV64 block
 *   [3..2+2*N64]      N64 × (JEQ nr, jt=0,jf=1 / ALLOW)  │ (2+2*N64 insns)
 *   [3+2*N64]         RET KILL_PROCESS (default deny RV64) ┘
 *   [4+2*N64]         JEQ RISCV32, jt=1, jf=0
 *   [5+2*N64]         RET KILL_PROCESS (unknown arch)
 *   [6+2*N64]         LD nr                               ┐ RV32 block
 *   [7+2*N64..6+2*(N64+N32)] N32 × (JEQ nr / ALLOW)       │
 *   [7+2*(N64+N32)]   RET KILL_PROCESS (default deny RV32) ┘
 *
 * Total: 8 + 2*(N64 + N32) instructions.
 * Returns instruction count, or -1 on error (too large or overflow).
 */
static int build_arch_dispatch_filter(
    struct sock_filter *buf, int bufsz,
    const unsigned int *rv64_nrs, int rv64_n,
    const unsigned int *rv32_nrs, int rv32_n)
{
    int total = 8 + 2 * (rv64_n + rv32_n);
    if (total > bufsz) {
        fprintf(stderr, "seccomp_allowlist_s: filter too large: %d > %d\n",
                total, bufsz);
        return -1;
    }
    if ((2 + 2 * rv64_n) > 255) {
        fprintf(stderr, "seccomp_allowlist_s: rv64 block too large for jf offset\n");
        return -1;
    }

    int i = 0;

#define FS(code_, k_) do { \
    buf[i].code = (unsigned short)(code_); \
    buf[i].jt = 0; buf[i].jf = 0; buf[i].k = (k_); i++; } while (0)
#define FJ(code_, k_, jt_, jf_) do { \
    buf[i].code = (unsigned short)(code_); \
    buf[i].jt = (__u8)(jt_); buf[i].jf = (__u8)(jf_); buf[i].k = (k_); i++; } while (0)

    /* Arch dispatch: RISCV64 → RV64 block; else → RV32 check. */
    FS(BPF_LD  | BPF_W | BPF_ABS, SDAT_ARCH);
    FJ(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_RISCV64, 0, 2 + 2 * rv64_n);

    /* RV64 block. */
    FS(BPF_LD  | BPF_W | BPF_ABS, SDAT_NR);
    for (int j = 0; j < rv64_n; j++) {
        FJ(BPF_JMP | BPF_JEQ | BPF_K, rv64_nrs[j], 0, 1);
        FS(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    }
    FS(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);

    /* RV32 check (reached only if not RISCV64). */
    FJ(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_RISCV32, 1, 0);
    FS(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);  /* unknown arch */

    /* RV32 block. */
    FS(BPF_LD  | BPF_W | BPF_ABS, SDAT_NR);
    for (int j = 0; j < rv32_n; j++) {
        FJ(BPF_JMP | BPF_JEQ | BPF_K, rv32_nrs[j], 0, 1);
        FS(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    }
    FS(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);

#undef FS
#undef FJ

    return i;
}

/*
 * build_riscv32_only_filter — restrictive RISCV32-only filter for phase-2.
 *
 * Layout (N = n):
 *   [0]        LD arch
 *   [1]        JEQ RISCV32, jt=1, jf=0   → NR check (skip KILL); else KILL
 *   [2]        RET KILL_PROCESS (wrong arch — should never happen)
 *   [3]        LD nr
 *   [4..3+2*N] N × (JEQ nr, jt=0,jf=1 / ALLOW)
 *   [4+2*N]    RET KILL_PROCESS (default deny)
 *
 * Total: 4 + 2*N instructions.
 * Returns instruction count, or -1 on error.
 */
static int build_riscv32_only_filter(
    struct sock_filter *buf, int bufsz,
    const unsigned int *nrs, int n)
{
    int total = 4 + 2 * n;
    if (total > bufsz) {
        fprintf(stderr, "seccomp_allowlist_s: riscv32 filter too large: %d > %d\n",
                total, bufsz);
        return -1;
    }

    int i = 0;

#define FS(code_, k_) do { \
    buf[i].code = (unsigned short)(code_); \
    buf[i].jt = 0; buf[i].jf = 0; buf[i].k = (k_); i++; } while (0)
#define FJ(code_, k_, jt_, jf_) do { \
    buf[i].code = (unsigned short)(code_); \
    buf[i].jt = (__u8)(jt_); buf[i].jf = (__u8)(jf_); buf[i].k = (k_); i++; } while (0)

    FS(BPF_LD  | BPF_W | BPF_ABS, SDAT_ARCH);
    FJ(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_RISCV32, 1, 0);
    FS(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);  /* wrong arch */

    FS(BPF_LD  | BPF_W | BPF_ABS, SDAT_NR);
    for (int j = 0; j < n; j++) {
        FJ(BPF_JMP | BPF_JEQ | BPF_K, nrs[j], 0, 1);
        FS(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    }
    FS(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);

#undef FS
#undef FJ

    return i;
}

/* Install a BPF filter via seccomp(2).  Caller must hold PR_SET_NO_NEW_PRIVS. */
static int seccomp_install_filter(const struct sock_filter *prog, int len)
{
    struct sock_fprog fp = {
        .len    = (unsigned short)len,
        .filter = (struct sock_filter *)prog,
    };
    if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &fp) != 0) {
        perror("seccomp SECCOMP_SET_MODE_FILTER");
        return -1;
    }
    return 0;
}

/* install_phase1_filter — build and install the arch-dispatch phase-1 filter.
 * Called by the LP64 launcher after prctl(NO_NEW_PRIVS), before execve.
 * Returns 0 on success, -1 on failure. */
static int install_phase1_filter(void)
{
    struct sock_filter prog[SECCOMP_MAX_INSNS];
    int len = build_arch_dispatch_filter(
        prog, SECCOMP_MAX_INSNS,
        seccomp_phase1_riscv64_nrs, SECCOMP_PHASE1_RISCV64_N,
        seccomp_phase1_riscv32_nrs, SECCOMP_PHASE1_RISCV32_N);
    if (len < 0) return -1;
    fprintf(stderr, "spike-s: phase-1 arch-dispatch filter: %d instructions "
            "(rv64=%d, rv32=%d)\n", len,
            SECCOMP_PHASE1_RISCV64_N, SECCOMP_PHASE1_RISCV32_N);
    return seccomp_install_filter(prog, len);
}

/* install_phase2_filter — build and install the RISCV32-only phase-2 filter.
 * Called by libblyt32.so constructor in the ILP32 cart process, after ld.so.
 * PR_SET_NO_NEW_PRIVS inherited from launcher; no prctl needed here.
 * Returns 0 on success, -1 on failure. */
static int install_phase2_filter(void)
{
    struct sock_filter prog[SECCOMP_MAX_INSNS];
    int len = build_riscv32_only_filter(
        prog, SECCOMP_MAX_INSNS,
        seccomp_phase2_riscv32_nrs, SECCOMP_PHASE2_RISCV32_N);
    if (len < 0) return -1;
    /* write is in phase-1's phase1_riscv32 allowlist, so this fprintf is safe
     * even though phase-2 is not yet installed. */
    fprintf(stderr, "spike-s: phase-2 RISCV32 filter: %d instructions (%d syscalls)\n",
            len, SECCOMP_PHASE2_RISCV32_N);
    return seccomp_install_filter(prog, len);
}

#endif /* SECCOMP_ALLOWLIST_S_H */

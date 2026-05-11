/* seccomp_allowlist.h — unified RISCV64 BPF filter for Spike R.
 *
 * KEY FINDING (Stage 1 empirical result, Fedora 42 kernel 6.16.4):
 *   The kernel reports AUDIT_ARCH_RISCV64 in seccomp_data.arch for ILP32
 *   compat processes — not AUDIT_ARCH_RISCV32.  Arch-dispatch between LP64
 *   and ILP32 via seccomp_data.arch is NOT possible on this kernel.
 *
 *   Consequence: a single unified RISCV64 filter is used for both the LP64
 *   launcher and the ILP32 cart.  Syscalls appear identical to the filter
 *   regardless of which process (LP64 or ILP32) makes them.
 *
 *   Consequence for execve: execve cannot be selectively blocked for the
 *   ILP32 cart via arch-dispatch.  execve remains in the allowlist (needed
 *   for the launcher→cart exec).  Blocking execve for the cart requires
 *   Option B (two-phase): the cart installs a restrictive phase-2 filter at
 *   startup that shadows the phase-1 ALLOW with a KILL for execve.  This
 *   is a follow-on implementation item; see docs/design/spike-r-results.md.
 *
 * Stage 3 update (2026-05-11, Fedora 42 kernel 6.16.4):
 *   Allowlist rebuilt from strace of rv32emu (spike-a source, CONFIG_EXT_A,
 *   multi-dynload patch, interpreter-only) over spike-i (C, Lua), spike-q
 *   (Rust), and spike-h adversary cart workloads.  22 syscalls observed.
 *
 *   Previous qemu-riscv32-static-derived entries removed:
 *     clone3(435), clock_nanosleep(115), nanosleep(101), sysinfo(179),
 *     madvise(233), futex(98), futex_time64(422), clock_gettime(113),
 *     clock_gettime64(403), mmap2(192), fstat64(325), fstatat64(327),
 *     riscv_flush_icache(258/259), uname(160), mremap(216), statx(291),
 *     fcntl(25), readlinkat(78), writev(66), pread64(67), rt_sigprocmask(135),
 *     getuid(174), geteuid(175), getgid(176), getegid(177), gettid(178).
 *   All were qemu-riscv32-static JIT/TCG or ILP32-musl-specific syscalls
 *   not needed by rv32emu's interpreter path.
 *
 *   New entry added: faccessat(48) — rv32emu's ld.so checks /etc/ld.so.preload.
 */

#ifndef SECCOMP_ALLOWLIST_H
#define SECCOMP_ALLOWLIST_H

#include <errno.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

/* AUDIT_ARCH_RISCV64: EM_RISCV (0xF3) | __AUDIT_ARCH_LE (0x40000000)
 *                              | AUDIT_ARCH_64BIT (0x80000000)           */
#ifndef AUDIT_ARCH_RISCV64
#  define AUDIT_ARCH_RISCV64  0xC00000F3U
#endif

/* AUDIT_ARCH_RISCV32: EM_RISCV (0xF3) | __AUDIT_ARCH_LE (0x40000000)
 * NOTE: The Fedora 42 kernel (6.16.4) does NOT report this value in
 * seccomp_data.arch for ILP32 compat processes.  Defined here for
 * documentation and potential future kernel support only. */
#ifndef AUDIT_ARCH_RISCV32
#  define AUDIT_ARCH_RISCV32  0x400000F3U
#endif

/* Offsets within struct seccomp_data. */
#define SDAT_NR   0   /* __s32 nr:   syscall number  */
#define SDAT_ARCH 4   /* __u32 arch: AUDIT_ARCH_*    */

/* Maximum BPF instruction count (kernel limit: 4096). */
#define SECCOMP_MAX_INSNS 512

/* --------------------------------------------------------------------------
 * Unified allowlist: syscalls made by rv32emu (interpreter-only, no JIT)
 * running RV32 cart workloads on Fedora 42 kernel 6.16.4.
 *
 * Derived from strace of rv32emu over:
 *   - spike-h adversary (static RV32, baseline)
 *   - spike-i case_a (C cart + libconsole.so)
 *   - spike-i case_b (Lua cart + libconsole.so + libconsolelua.so)
 *   - spike-q rust_cart.elf (Rust cart)
 * All using spike-a rv32emu source with CONFIG_EXT_A=y and multi-dynload
 * patch; -L flag for shared library path.
 *
 * execve (221) is in this list for the launcher→cart exec.  The cart
 * (rv32emu) can also call execve; use Option B two-phase filter or
 * mount-namespace isolation to prevent arbitrary cart exec.
 *
 * openat (56) allows rv32emu to open the cart ELF and shared libs.
 * The mount namespace limits which paths are accessible in production.
 * -------------------------------------------------------------------------- */
static const unsigned int seccomp_unified_nrs[] = {
    /* ── Startup / ELF loading (rv32emu + its ld.so) ── */
    221, /* execve:      launcher→cart exec (rv32emu process start)        */
     48, /* faccessat:   ld.so checks /etc/ld.so.preload on startup        */
     56, /* openat:      rv32emu opens cart ELF, shared libs, ld.so.cache  */
     57, /* close:       after ELF/lib load                                */
     63, /* read:        ELF header and section reads                      */
     62, /* lseek:       ELF file seeking during cart/lib load             */
     80, /* fstat:       ELF file size before mmap                         */
     79, /* newfstatat:  ld.so stat checks for lib paths                   */
    222, /* mmap:        ELF PT_LOAD segment mapping + heap                */
    226, /* mprotect:    ELF segment permissions after mmap                */
    215, /* munmap:      unmap ld.so.cache and temp mappings               */
    214, /* brk:         rv32emu and libm/libc heap growth                 */

    /* ── rv32emu process init (libc/musl via rv32emu's own ld.so) ── */
     96, /* set_tid_address: libc thread init (TLS self-pointer)           */
     99, /* set_robust_list: libc futex list init (even without threading) */
    293, /* rseq:        restartable sequences (libc TLS init)             */
    278, /* getrandom:   libc random init (AT_RANDOM seed)                 */
    261, /* prlimit64:   libc stack-size probe                             */
    134, /* rt_sigaction: rv32emu installs SIGFPE/SIGILL handlers          */
    139, /* rt_sigreturn: return from signal handler                       */

    /* ── Normal execution / output ── */
     29, /* ioctl:       libc probes isatty(stdout) on first write         */
     64, /* write:       rv32emu cart output to stdout/stderr              */
     94, /* exit_group:  normal cart exit                                  */
     93, /* exit:        single-thread exit (e.g. atexit handler threads)  */
};
#define SECCOMP_UNIFIED_N ((int)(sizeof(seccomp_unified_nrs)/sizeof(seccomp_unified_nrs[0])))

/* --------------------------------------------------------------------------
 * build_unified_filter: construct a RISCV64-only BPF filter into buf[bufsz].
 * Returns instruction count, or -1 on error.
 *
 * Layout:
 *   [0]  LD arch
 *   [1]  JEQ RISCV64, jt=1, jf=0  → RISCV64: skip 1 (→LD nr@3); else next (→KILL@2)
 *   [2]  RET KILL_PROCESS          (unknown arch)
 *   [3]  LD nr
 *   [4..3+n*2]  for each nr: JEQ nr,jt=0,jf=1 / RET ALLOW
 *   [4+n*2]     RET KILL_PROCESS   (default deny)
 * -------------------------------------------------------------------------- */
static int build_unified_filter_with_default(
    struct sock_filter *buf, int bufsz,
    const unsigned int *nrs, int n,
    unsigned int default_action);

static int build_unified_filter(
    struct sock_filter *buf, int bufsz,
    const unsigned int *nrs, int n)
{
    return build_unified_filter_with_default(
        buf, bufsz, nrs, n, SECCOMP_RET_KILL_PROCESS);
}

static int build_unified_filter_with_default(
    struct sock_filter *buf, int bufsz,
    const unsigned int *nrs, int n,
    unsigned int default_action)
{
    int total = 5 + n * 2;
    if (total > bufsz) {
        fprintf(stderr, "seccomp filter too large: %d > %d\n", total, bufsz);
        return -1;
    }

    int i = 0;

#define FI_STMT(code_, k_) do { \
        buf[i].code = (unsigned short)(code_); \
        buf[i].jt = 0; buf[i].jf = 0; buf[i].k = (k_); i++; \
    } while (0)
#define FI_JUMP(code_, k_, jt_, jf_) do { \
        buf[i].code = (unsigned short)(code_); \
        buf[i].jt = (jt_); buf[i].jf = (jf_); buf[i].k = (k_); i++; \
    } while (0)

    FI_STMT(BPF_LD  | BPF_W | BPF_ABS, SDAT_ARCH);
    FI_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_RISCV64, 1, 0);
    FI_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);   /* unknown arch */
    FI_STMT(BPF_LD  | BPF_W | BPF_ABS, SDAT_NR);

    for (int j = 0; j < n; j++) {
        FI_JUMP(BPF_JMP | BPF_JEQ | BPF_K, nrs[j], 0, 1);
        FI_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    }
    FI_STMT(BPF_RET | BPF_K, default_action);   /* default deny */

#undef FI_STMT
#undef FI_JUMP

    return i;
}

/* install a BPF filter via seccomp(2).  Caller must have PR_SET_NO_NEW_PRIVS. */
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

/* install_raw_bpf_filter: build and install the unified RISCV64 filter.
 * Call after prctl NO_NEW_PRIVS, immediately before execv.
 * Returns 0 on success, -1 on failure. */
static int install_raw_bpf_filter(void)
{
    struct sock_filter prog[SECCOMP_MAX_INSNS];
    int len = build_unified_filter(prog, SECCOMP_MAX_INSNS,
                                   seccomp_unified_nrs, SECCOMP_UNIFIED_N);
    if (len < 0) return -1;
    fprintf(stderr, "spike-r: unified BPF filter: %d instructions "
            "(%d syscalls)\n", len, SECCOMP_UNIFIED_N);
    return seccomp_install_filter(prog, len);
}

#endif /* SECCOMP_ALLOWLIST_H */

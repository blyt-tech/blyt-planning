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
 * rv32emu commit: (to be filled after Stage 3 strace run)
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
 * Unified allowlist: syscalls needed by both the RV64 launcher (post-filter)
 * and the RV32 cart (ILP32 compat process).
 *
 * Because seccomp_data.arch = RISCV64 for all processes on this kernel,
 * a single RISCV64 filter covers everything.
 *
 * Numbers annotated [compat] are 32-bit-only compat wrapper syscall numbers
 * that appear under the same asm-generic table on RISC-V but are handled
 * via compat wrappers internally.
 *
 * execve (221) is in this list for the launcher→cart exec.  The ILP32 cart
 * can also call execve; use mount-namespace isolation (empty rootfs) or the
 * two-phase Option B approach to prevent arbitrary cart exec.
 *
 * Stage 3 update: add any additional syscalls that strace reveals from
 * rv32emu running real cart workloads.
 * -------------------------------------------------------------------------- */
static const unsigned int seccomp_unified_nrs[] = {
    221, /* execve:           launcher→cart exec; also used by cart (see note above) */
     94, /* exit_group        */
     93, /* exit              */
     64, /* write             */
    139, /* rt_sigreturn      */
    135, /* rt_sigprocmask    */
    134, /* rt_sigaction      */
    214, /* brk               */
    222, /* mmap              */
    192, /* mmap2             [compat: 32-bit mmap with page-offset] */
    215, /* munmap            */
    216, /* mremap            */
     66, /* writev            */
     63, /* read              */
     57, /* close             */
     80, /* fstat             */
     79, /* newfstatat        */
    325, /* fstat64           [compat] */
    327, /* fstatat64         [compat] */
    291, /* statx             */
     25, /* fcntl: file control ops (F_GETFL on /proc/self/maps) */
     29, /* ioctl             (musl probes isatty(stdout)) */
     78, /* readlinkat        (musl TLS/AUXV init)         */
     96, /* set_tid_address   (musl thread init)           */
     99, /* set_robust_list   (musl thread init)           */
     98, /* futex             */
    422, /* futex_time64      [compat] */
    113, /* clock_gettime     */
    403, /* clock_gettime64   [compat] */
    278, /* getrandom         (musl __init_libc via AT_RANDOM) */
    261, /* prlimit64         (musl stack-size probe)          */
    160, /* uname             (musl __init_libc)               */
    226, /* mprotect          (PROT_EXEC blocked by other policy) */
    174, /* getuid            (musl __init_libc privilege check)    */
    175, /* geteuid           (musl __init_libc privilege check)    */
    176, /* getgid            (musl __init_libc privilege check)    */
    177, /* getegid           (musl __init_libc privilege check)    */
    178, /* gettid            (musl thread self-identification)     */
    258, /* riscv_flush_icache (musl RV32 startup; Spike H finding) */
    259, /* riscv_flush_icache (libseccomp resolves to 259; both needed) */

    /* Syscalls needed by qemu-riscv32-static (binfmt_misc ELF32 handler)
     * and expected to also be needed by rv32emu (the production cart runner).
     *
     * NOTE: openat (56) is needed for the ELF loader to open the cart binary.
     * In production, the mount namespace limits which paths are accessible;
     * seccomp alone cannot distinguish "rv32emu opening the cart ELF" from
     * "cart code opening /etc/passwd" (both appear as host openat).  The mount
     * namespace provides the filesystem isolation; seccomp blocks syscalls that
     * rv32emu genuinely doesn't need (socket, etc.).
     */
     56, /* openat: cart ELF loader (rv32emu open of cart binary)   */
     62, /* lseek: ELF file seeking during adversary/cart ELF load */
     67, /* pread64: ELF segment reading (mmap offset reads)        */
    101, /* nanosleep: thread sleep / scheduler yield               */
    115, /* clock_nanosleep: TCG worker thread scheduling sleep     */
    179, /* sysinfo: qemu-riscv32-static memory sizing              */
    233, /* madvise: memory hints (MADV_FREE/DONTNEED for JIT pages) */
    293, /* rseq: restartable sequences (musl/glibc TLS)            */
    435, /* clone3: qemu TCG worker thread creation                  */
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

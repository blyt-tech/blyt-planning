/* seccomp_raw_test_s.c — Stage 1: arch-dispatch filter across the exec boundary.
 *
 * LP64 (RV64) binary.  Validates that:
 *   1. AUDIT_ARCH_RISCV64 rules apply to the LP64 launcher side.
 *   2. AUDIT_ARCH_RISCV32 rules apply to a natively-exec'd ILP32 process.
 *   3. The same filter (installed by the LP64 parent, before exec) governs
 *      the ILP32 child after exec.
 *
 * PREREQUISITE: a kernel with native RISC-V ILP32 ELF loader support.
 * On Fedora 42 / kernel 6.16.4 this fails (returns ENOEXEC) — use the
 * ILP32-capable kernel built per PLAN.md Stage 0.
 *
 * Usage:
 *   seccomp_raw_test_s <adversary_s_path>
 *   (adversary_s must be a *static* ILP32 ELF; no PT_INTERP needed)
 *
 * Tests:
 *   A  LP64 side  — write(64) allowed under RV64 rules            expect rc=0
 *   B  LP64 side  — socket(198) blocked under RV64 rules          expect rc=159
 *   C  ILP32 side — write probe allowed under RV32 rules          expect rc=0
 *   D  ILP32 side — socket probe blocked under RV32 rules         expect rc=159
 *   E  ILP32 side — execve probe blocked under RV32 rules         expect rc=159
 *   F  Arch check — RISCV64-only filter kills unknown arch        expect ILP32 rc=159
 *
 * Filter used in Tests A–E (arch-dispatch):
 *   arch == RISCV64: allow write(64) + exit_group(94); kill socket(198) + execve(221)
 *   arch == RISCV32: allow write(64) + exit_group(94); kill socket(198) + execve(221)
 *   else:            KILL_PROCESS
 *
 * Filter used in Test F (RISCV64-only):
 *   arch == RISCV64: allow all
 *   else:            KILL_PROCESS
 * Under this filter, an ILP32 process (arch == RISCV32) has no matching arch
 * branch → hits KILL_PROCESS on its first syscall.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include "seccomp_allowlist_s.h"

/* ─────────────────────────────────────────────────────────────────────────
 * Test filter construction
 * ───────────────────────────────────────────────────────────────────────── */

/* RV64 allowlist: needs execve so LP64 child can exec the ILP32 adversary.
 * write + exit_group for LP64 tests A and B. */
static const unsigned int test_rv64_nrs[] = {
    221, /* execve: LP64 child must exec the ILP32 adversary */
     64, /* write */
     94, /* exit_group */
};
#define TEST_RV64_N 3

/* RV32 allowlist: musl startup + write + exit_group for ILP32 adversary.
 * Demonstrates different rules per arch — the whole point of arch-dispatch. */
static const unsigned int test_rv32_nrs[] = {
     96, /* set_tid_address: first ILP32 syscall (musl startup) */
     64, /* write */
     94, /* exit_group */
};
#define TEST_RV32_N 3

/* Build the arch-dispatch filter for Tests A-E.
 * RV64 and RV32 have DIFFERENT rules — this is what we're testing. */
static int build_test_dispatch_filter(struct sock_filter *buf, int bufsz)
{
    return build_arch_dispatch_filter(
        buf, bufsz,
        test_rv64_nrs, TEST_RV64_N,   /* RV64: execve+write+exit_group */
        test_rv32_nrs, TEST_RV32_N);  /* RV32: set_tid_addr+write+exit_group */
}

/* Build a "RISCV32 + write → ERRNO(42), else ALLOW" filter for Test G.
 * Non-destructive: startup syscalls are allowed; only write in an ILP32
 * process returns errno=42.  Distinguishes arch reporting from kill semantics.
 * Layout:
 * [0] LD arch
 * [1] JEQ RISCV32, jt=0, jf=3   RISCV32: [2]; else: [5] ALLOW
 * [2] LD nr
 * [3] JEQ write(64), jt=0, jf=1  write: [4] ERRNO(42); else: [5] ALLOW
 * [4] RET ERRNO(42)
 * [5] RET ALLOW
 */
static int build_riscv32_write_errno_filter(struct sock_filter *buf, int bufsz)
{
    if (bufsz < 6) return -1;
    int i = 0;
#define FS(code_, k_) do { \
    buf[i].code=(unsigned short)(code_); buf[i].jt=0; buf[i].jf=0; buf[i].k=(k_); i++; } while(0)
#define FJ(code_, k_, jt_, jf_) do { \
    buf[i].code=(unsigned short)(code_); buf[i].jt=(__u8)(jt_); \
    buf[i].jf=(__u8)(jf_); buf[i].k=(k_); i++; } while(0)
    FS(BPF_LD  | BPF_W | BPF_ABS, SDAT_ARCH);
    FJ(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_RISCV32, 0, 3);
    FS(BPF_LD  | BPF_W | BPF_ABS, SDAT_NR);
    FJ(BPF_JMP | BPF_JEQ | BPF_K, 64 /*SYS_write*/, 0, 1);
    FS(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | 42);
    FS(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
#undef FS
#undef FJ
    return i;
}

/* Build a RISCV64-only ALLOW-all filter for Test F.
 * Format: check arch==RISCV64; if not: KILL; if yes: ALLOW.
 * [0] LD arch
 * [1] JEQ RISCV64, jt=0, jf=1  → RISCV64: fall to ALLOW; else: KILL
 * [2] RET KILL_PROCESS
 * [3] RET ALLOW
 */
static int build_riscv64_only_allowall(struct sock_filter *buf, int bufsz)
{
    if (bufsz < 4) return -1;
    int i = 0;
#define FS(code_, k_) do { \
    buf[i].code=(unsigned short)(code_); buf[i].jt=0; buf[i].jf=0; buf[i].k=(k_); i++; } while(0)
#define FJ(code_, k_, jt_, jf_) do { \
    buf[i].code=(unsigned short)(code_); buf[i].jt=(__u8)(jt_); \
    buf[i].jf=(__u8)(jf_); buf[i].k=(k_); i++; } while(0)
    FS(BPF_LD  | BPF_W | BPF_ABS, SDAT_ARCH);
    FJ(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_RISCV64, 1, 0);
    FS(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
    FS(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
#undef FS
#undef FJ
    return i;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Test harness
 * ───────────────────────────────────────────────────────────────────────── */

/* Run the ILP32 adversary with the given probe under the given filter(s).
 * Returns the child's exit code, or 159 if it exited with SIGSYS. */
static int run_ilp32_probe(const char *adversary_path, const char *probe,
                           struct sock_filter **filters, int *filter_lens, int nfilters)
{
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) {
        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
            perror("prctl NO_NEW_PRIVS"); _exit(127);
        }
        for (int fi = 0; fi < nfilters; fi++) {
            if (seccomp_install_filter(filters[fi], filter_lens[fi]) != 0)
                _exit(127);
        }
        char *av[] = { (char *)adversary_path, (char *)probe, NULL };
        execv(adversary_path, av);
        /* If we reach here, execv failed.  This should mean ENOEXEC (no ILP32
         * ELF loader) or ENOENT.  Exit 126 to distinguish from seccomp kills. */
        fprintf(stderr, "execv %s: %s\n", adversary_path, strerror(errno));
        _exit(126);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); return -1; }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        if (WTERMSIG(status) == SIGSYS) return 159;
        return 128 + WTERMSIG(status);
    }
    return -1;
}

/* Run an LP64 inline probe (no exec): installs the filter in a fork'd child,
 * calls the syscall inline, and reports the result. */
static int run_lp64_probe_write(struct sock_filter *filt, int flen)
{
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) {
        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) _exit(127);
        if (seccomp_install_filter(filt, flen) != 0) _exit(127);
        long rc = syscall(SYS_write, 2, "", 0);
        _exit(rc >= 0 ? 0 : 1);
    }
    int st = 0; waitpid(pid, &st, 0);
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st) && WTERMSIG(st) == SIGSYS) return 159;
    return -1;
}

static int run_lp64_probe_socket(struct sock_filter *filt, int flen)
{
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) {
        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) _exit(127);
        if (seccomp_install_filter(filt, flen) != 0) _exit(127);
        long rc = syscall(SYS_socket, 2 /*AF_INET*/, 1 /*SOCK_STREAM*/, 0);
        _exit(rc >= 0 ? 0 : 1);
    }
    int st = 0; waitpid(pid, &st, 0);
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st) && WTERMSIG(st) == SIGSYS) return 159;
    return -1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: seccomp_raw_test_s <adversary_s_path>\n");
        fprintf(stderr, "  adversary_s must be a static ILP32 RISC-V ELF\n");
        return 2;
    }
    const char *adv = argv[1];

    static struct sock_filter dispatch_filt[SECCOMP_MAX_INSNS];
    int dispatch_len = build_test_dispatch_filter(dispatch_filt, SECCOMP_MAX_INSNS);
    if (dispatch_len < 0) return 1;
    printf("Arch-dispatch filter: %d instructions\n", dispatch_len);

    static struct sock_filter rv64only_filt[8];
    int rv64only_len = build_riscv64_only_allowall(rv64only_filt, 8);
    if (rv64only_len < 0) return 1;

    int pass = 0, fail = 0;

    /* ── Test A: LP64 write allowed under RV64 rules ── */
    printf("\n=== Test A: LP64 write → allowed (expect rc=0) ===\n");
    printf("  arch=RISCV64 process; RV64 rules allow write(64)\n");
    {
        int rc = run_lp64_probe_write(dispatch_filt, dispatch_len);
        if (rc == 0) {
            printf("  PASS A: LP64 write rc=0 (RV64 rules allow write)\n");
            pass++;
        } else {
            printf("  FAIL A: LP64 write rc=%d (expected 0)\n", rc);
            fail++;
        }
    }

    /* ── Test B: LP64 socket blocked under RV64 rules ── */
    printf("\n=== Test B: LP64 socket → SIGSYS (expect rc=159) ===\n");
    printf("  arch=RISCV64 process; RV64 rules do not allow socket(198)\n");
    {
        int rc = run_lp64_probe_socket(dispatch_filt, dispatch_len);
        if (rc == 159) {
            printf("  PASS B: LP64 socket rc=159 SIGSYS (RV64 rules block socket)\n");
            pass++;
        } else {
            printf("  FAIL B: LP64 socket rc=%d (expected 159)\n", rc);
            fail++;
        }
    }

    /* ── Test C: ILP32 write probe allowed under RV32 rules ── */
    printf("\n=== Test C: ILP32 write → allowed (expect rc=0) ===\n");
    printf("  arch=RISCV32 process (via native ILP32 exec); RV32 rules allow write(64)\n");
    printf("  REQUIRES: kernel with RISC-V ILP32 ELF loader\n");
    {
        struct sock_filter *fs[] = { dispatch_filt };
        int lens[] = { dispatch_len };
        int rc = run_ilp32_probe(adv, "write", fs, lens, 1);
        if (rc == 0) {
            printf("  PASS C: ILP32 write rc=0 (RV32 rules allow write — arch dispatch confirmed)\n");
            pass++;
        } else if (rc == 126) {
            printf("  FAIL C: exec failed (rc=126) — ENOEXEC? Kernel missing ILP32 ELF loader.\n");
            fail++;
        } else {
            printf("  FAIL C: ILP32 write rc=%d (expected 0)\n", rc);
            fail++;
        }
    }

    /* ── Test D: ILP32 socket blocked under RV32 rules ── */
    printf("\n=== Test D: ILP32 socket → SIGSYS (expect rc=159) ===\n");
    printf("  arch=RISCV32 process; RV32 rules do not allow socket(198)\n");
    {
        struct sock_filter *fs[] = { dispatch_filt };
        int lens[] = { dispatch_len };
        int rc = run_ilp32_probe(adv, "socket", fs, lens, 1);
        if (rc == 159) {
            printf("  PASS D: ILP32 socket rc=159 SIGSYS (RV32 rules block socket)\n");
            pass++;
        } else if (rc == 126) {
            printf("  FAIL D: exec failed (rc=126) — ENOEXEC? Kernel missing ILP32 ELF loader.\n");
            fail++;
        } else {
            printf("  FAIL D: ILP32 socket rc=%d (expected 159)\n", rc);
            fail++;
        }
    }

    /* ── Test E: ILP32 execve blocked under RV32 rules ── */
    printf("\n=== Test E: ILP32 execve → SIGSYS (expect rc=159) ===\n");
    printf("  arch=RISCV32 process; RV32 rules do not allow execve(221)\n");
    {
        struct sock_filter *fs[] = { dispatch_filt };
        int lens[] = { dispatch_len };
        int rc = run_ilp32_probe(adv, "execve", fs, lens, 1);
        if (rc == 159) {
            printf("  PASS E: ILP32 execve rc=159 SIGSYS (cart cannot exec)\n");
            pass++;
        } else if (rc == 126) {
            printf("  FAIL E: exec failed (rc=126) — ENOEXEC? Kernel missing ILP32 ELF loader.\n");
            fail++;
        } else {
            printf("  FAIL E: ILP32 execve rc=%d (expected 159)\n", rc);
            fail++;
        }
    }

    /* ── Test F: RISCV64-only filter kills ILP32 process (no RV32 branch) ── */
    printf("\n=== Test F: RISCV64-only filter kills ILP32 (expect rc=159) ===\n");
    printf("  Confirms AUDIT_ARCH_RISCV32 is seen for ILP32; no RV64-only fallthrough.\n");
    printf("  If arch-dispatch is needed, the RV32 branch must be present.\n");
    {
        struct sock_filter *fs[] = { rv64only_filt };
        int lens[] = { rv64only_len };
        int rc = run_ilp32_probe(adv, "write", fs, lens, 1);
        if (rc == 159) {
            printf("  PASS F: ILP32 write rc=159 under RISCV64-only filter\n");
            printf("          → AUDIT_ARCH_RISCV32 confirmed for ILP32 processes\n");
            pass++;
        } else if (rc == 0) {
            printf("  FAIL F: ILP32 write rc=0 — arch is RISCV64 (no native ILP32 exec?)\n");
            printf("          → This kernel does not report RISCV32; Spike S cannot proceed.\n");
            fail++;
        } else if (rc == 126) {
            printf("  FAIL F: exec failed (rc=126) — ENOEXEC? Kernel missing ILP32 ELF loader.\n");
            fail++;
        } else {
            printf("  FAIL F: ILP32 write rc=%d (expected 159)\n", rc);
            fail++;
        }
    }

    /* ── Test G: non-destructive arch probe via SECCOMP_RET_ERRNO ── */
    printf("\n=== Test G: ILP32 arch probe via ERRNO (non-destructive) ===\n");
    printf("  Filter: if arch==RISCV32 AND nr==write → ERRNO(42); else ALLOW\n");
    printf("  Startup syscalls are unaffected; only write is probed.\n");
    printf("  ILP32 write rc=1 (write failed errno=42) → arch IS RISCV32\n");
    printf("  ILP32 write rc=0 (write succeeded)       → arch IS RISCV64\n");
    {
        static struct sock_filter errno_filt[8];
        int errno_len = build_riscv32_write_errno_filter(errno_filt, 8);
        if (errno_len < 0) {
            printf("  SKIP G: filter build failed\n");
        } else {
            struct sock_filter *fs[] = { errno_filt };
            int lens[] = { errno_len };
            int rc = run_ilp32_probe(adv, "write", fs, lens, 1);
            printf("  ILP32 write under ERRNO filter: rc=%d\n", rc);
            if (rc == 1) {
                printf("  PASS G: write returned non-zero (errno=42) → arch IS RISCV32\n");
                printf("          → seccomp_data.arch = AUDIT_ARCH_RISCV32 confirmed\n");
                pass++;
            } else if (rc == 0) {
                printf("  INFO G: write succeeded → arch IS RISCV64\n");
                printf("          → TIF_32BIT not set; seccomp_data.arch = RISCV64\n");
                /* Not a hard failure — informational diagnostic */
            } else if (rc == 126) {
                printf("  SKIP G: exec failed (ENOEXEC)\n");
            } else {
                printf("  INFO G: write rc=%d (unexpected)\n", rc);
            }
        }
    }

    printf("\n==========================================\n");
    printf("Stage 1 results: PASS=%d  FAIL=%d\n", pass, fail);
    if (fail == 0) {
        printf("Stage 1: PASS\n");
        printf("  Arch-dispatch filter works across exec boundary.\n");
        printf("  AUDIT_ARCH_RISCV32 confirmed for ILP32 processes.\n");
        printf("  LP64 and ILP32 rules applied correctly by same filter.\n");
        return 0;
    }
    printf("Stage 1: FAIL (%d tests failed)\n", fail);
    return 1;
}

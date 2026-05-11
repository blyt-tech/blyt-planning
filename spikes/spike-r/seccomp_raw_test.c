/* seccomp_raw_test.c — Stage 1: raw BPF seccomp filter validation.
 *
 * RV64 binary.  Validates the unified RISCV64 filter and verifies multi-filter
 * LIFO semantics against the Fedora 42 RISC-V kernel (6.16.4).
 *
 * Usage:
 *   seccomp_raw_test <adversary_path>
 *
 * KEY FINDINGS (from empirical testing on Fedora 42 kernel 6.16.4):
 *
 *   1. seccomp_data.arch = AUDIT_ARCH_RISCV64 for ALL processes.
 *      AUDIT_ARCH_RISCV32 is never reported.  The kernel uses binfmt_misc +
 *      qemu-riscv32-static to run ILP32 binaries; CONFIG_COMPAT provides
 *      compat_sys_* wrappers but does NOT include the ELF32 loader.
 *      Consequence: arch-dispatch by RISCV32/RISCV64 is not possible;
 *      a unified RISCV64 allowlist is used for both LP64 and ILP32 processes.
 *
 *   2. LIFO semantics confirmed: a later-installed ALLOW does not override an
 *      earlier KILL.  Option B (cart installs restrictive phase-2 at startup
 *      to block execve and other post-exec sensitive calls) is viable.
 *
 * Three sub-tests:
 *
 *   A  Unified RISCV64 filter; ILP32 uname (160) is allowlisted.
 *      → adversary exits 0  (uname allowed; qemu-riscv32-static runs the
 *        adversary which calls uname via LP64 host syscall translation)
 *
 *   B  Unified RISCV64 filter; socket (198) is NOT allowlisted.
 *      → adversary exits 159  (SIGSYS — default-deny works)
 *      NOTE: "open" probe is now also allowlisted (openat=56 needed by
 *      qemu-riscv32-static to load the adversary ELF); filesystem isolation
 *      is provided by mount namespace in production.
 *
 *   C  Multi-filter LIFO: phase-1 kills uname by NR; phase-2 ALLOWs all.
 *      → adversary exits 159  (SIGSYS — LIFO confirmed; phase-2 ALLOW passes
 *        to phase-1 KILL; Option B two-phase approach is viable)
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "seccomp_allowlist.h"

/* ---------- Test C filter definitions ---------- */

/*
 * phase1_filter (installed FIRST, evaluated LAST in LIFO):
 * KILLs uname (nr=160), ALLOWs everything else.
 *
 * [0] LD nr
 * [1] JEQ 160(uname), jt=0, jf=1  → uname: next (→ [2] KILL); else: skip 1 (→ [3] ALLOW)
 * [2] RET KILL_PROCESS
 * [3] RET ALLOW
 */
static struct sock_filter phase1_filter[4];
static int phase1_len;

static void build_phase1(void)
{
    int i = 0;
#define P1S(op_, imm_) do { \
        phase1_filter[i].code=(op_); phase1_filter[i].jt=0; \
        phase1_filter[i].jf=0; phase1_filter[i].k=(imm_); i++; } while(0)
#define P1J(op_, imm_, tru_, fls_) do { \
        phase1_filter[i].code=(op_); phase1_filter[i].jt=(tru_); \
        phase1_filter[i].jf=(fls_); phase1_filter[i].k=(imm_); i++; } while(0)
    P1S(BPF_LD|BPF_W|BPF_ABS, SDAT_NR);
    P1J(BPF_JMP|BPF_JEQ|BPF_K, 160 /* uname */, 0, 1);
    P1S(BPF_RET|BPF_K, SECCOMP_RET_KILL_PROCESS);
    P1S(BPF_RET|BPF_K, SECCOMP_RET_ALLOW);
#undef P1S
#undef P1J
    phase1_len = i;
}

/*
 * phase2_filter (installed SECOND, evaluated FIRST in LIFO):
 * ALLOWs everything (one instruction).  Tests whether its ALLOW result
 * causes the kernel to continue evaluating phase-1 (which KILLs uname).
 */
static struct sock_filter phase2_filter[1];
static int phase2_len;

static void build_phase2(void)
{
    phase2_filter[0].code = BPF_RET | BPF_K;
    phase2_filter[0].jt   = 0;
    phase2_filter[0].jf   = 0;
    phase2_filter[0].k    = SECCOMP_RET_ALLOW;
    phase2_len = 1;
}

/* ---------- test runner ---------- */

static int run_probe(const char *adversary_path, const char *probe,
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
        char *argv[] = { (char *)adversary_path, (char *)probe, NULL };
        execv(adversary_path, argv);
        perror("execv");
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); return -1; }
    if (WIFEXITED(status))  return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        if (WTERMSIG(status) == SIGSYS) return 159;
        return 128 + WTERMSIG(status);
    }
    return -1;
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: seccomp_raw_test <adversary_path>\n");
        return 2;
    }
    const char *adversary = argv[1];

    static struct sock_filter f_unified[SECCOMP_MAX_INSNS];
    int f_unified_len = build_unified_filter(
        f_unified, SECCOMP_MAX_INSNS,
        seccomp_unified_nrs, SECCOMP_UNIFIED_N);
    if (f_unified_len < 0) return 1;

    build_phase1();
    build_phase2();

    int pass = 0, fail = 0;

    /* ---- Test A ---- */
    printf("\n=== Test A: unified RISCV64 filter — ILP32 uname (expect rc=0) ===\n");
    printf("  [arch=RISCV64 for all processes; binfmt_misc+qemu-riscv32-static]\n");
    {
        struct sock_filter *fs[] = { f_unified };
        int lens[] = { f_unified_len };
        int rc = run_probe(adversary, "uname", fs, lens, 1);
        if (rc == 0) {
            printf("  PASS A: uname rc=0 (ILP32 uname(160) allowed by unified filter)\n");
            pass++;
        } else {
            printf("  FAIL A: uname rc=%d (expected 0)\n", rc);
            fail++;
        }
    }

    /* ---- Test B: use socket probe (not open, since openat is in allowlist) ---- */
    printf("\n=== Test B: unified RISCV64 filter — socket (198) not allowlisted (expect rc=159) ===\n");
    printf("  [openat(56) is in allowlist for ELF loading; socket(198) is not]\n");
    {
        struct sock_filter *fs[] = { f_unified };
        int lens[] = { f_unified_len };
        int rc = run_probe(adversary, "socket", fs, lens, 1);
        if (rc == 159) {
            printf("  PASS B: socket rc=159 SIGSYS (socket not in allowlist — default-deny works)\n");
            pass++;
        } else {
            printf("  FAIL B: socket rc=%d (expected 159 SIGSYS)\n", rc);
            fail++;
        }
    }

    /* ---- Test C ---- */
    printf("\n=== Test C: multi-filter LIFO semantics (expect rc=159 SIGSYS) ===\n");
    printf("  phase-1 (installed first, evaluated LAST):  nr==160 → KILL, else ALLOW\n");
    printf("  phase-2 (installed second, evaluated FIRST): ALLOW everything\n");
    printf("  LIFO: phase-2 ALLOW(uname) → continue to phase-1 → KILL(uname)\n");
    {
        struct sock_filter *fs[] = { phase1_filter, phase2_filter };
        int lens[] = { phase1_len, phase2_len };
        int rc = run_probe(adversary, "uname", fs, lens, 2);
        if (rc == 159) {
            printf("  PASS C: uname rc=159 SIGSYS (LIFO confirmed: phase-2 ALLOW "
                   "passes to phase-1 KILL)\n");
            printf("          → Option B two-phase approach is viable.\n");
            pass++;
        } else if (rc == 0) {
            printf("  FAIL C: uname rc=0 (ALLOW won — LIFO semantics unexpected)\n");
            fail++;
        } else {
            printf("  FAIL C: uname rc=%d (unexpected)\n", rc);
            fail++;
        }
    }

    printf("\n==========================================\n");
    printf("Stage 1 results: PASS=%d  FAIL=%d\n", pass, fail);
    if (pass == 3 && fail == 0) {
        printf("Stage 1: PASS\n");
        printf("  Unified RISCV64 filter works for ILP32 compat syscalls.\n");
        printf("  LIFO semantics confirmed; Option B two-phase approach viable.\n");
        return 0;
    }
    printf("Stage 1: FAIL\n");
    return 1;
}

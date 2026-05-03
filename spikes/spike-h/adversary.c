/* Spike H — Stage 3: forbidden-syscall probe.
 *
 * Compiled as a static RV32 musl ELF, executed via launcher with the
 * seccomp filter installed.  Each probe attempts a syscall that should
 * be blocked.  Two outcomes are valid:
 *
 *   1. The kernel kills us with SIGSYS — seccomp did its job.  This is
 *      what the launcher's KILL_PROCESS default action causes.
 *   2. The syscall returns successfully — the filter is broken.
 *
 * If the launcher uses SCMP_ACT_KILL_PROCESS, the first forbidden probe
 * terminates us mid-program and the launcher reports SIGSYS.  The harness
 * must therefore be invoked once per probe (--probe N) and observe a
 * SIGSYS exit each time.
 *
 * The probes are kept self-contained (no libc beyond raw syscall numbers)
 * so the seccomp filter doesn't have to allow whatever helper musl would
 * have used.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

/* ── probes ──────────────────────────────────────────────────────────────── */

static int probe_open(void)
{
    /* openat(AT_FDCWD, "/etc/passwd", O_RDONLY) — should be killed by
     * seccomp (filesystem read access not on allowlist). */
    int fd = (int)syscall(SYS_openat, -100 /*AT_FDCWD*/, "/etc/passwd", 0 /*O_RDONLY*/, 0);
    fprintf(stderr, "probe_open: returned fd=%d errno=%d\n", fd, errno);
    return fd >= 0 ? 0 : 1;
}

static int probe_socket(void)
{
    /* socket(AF_INET, SOCK_STREAM, 0) — should be killed (network not on
     * allowlist).  On RISC-V Linux there is no separate socket() syscall;
     * it is dispatched via SYS_socket. */
    int s = (int)syscall(SYS_socket, 2 /*AF_INET*/, 1 /*SOCK_STREAM*/, 0);
    fprintf(stderr, "probe_socket: returned %d errno=%d\n", s, errno);
    return s >= 0 ? 0 : 1;
}

static int probe_execve(void)
{
    /* execve("/bin/sh", NULL, NULL) — should be killed (no exec allowed
     * from inside a sealed cart). */
    char *const argv[] = { (char *)"/bin/sh", NULL };
    char *const envp[] = { NULL };
    long rc = syscall(SYS_execve, "/bin/sh", argv, envp);
    fprintf(stderr, "probe_execve: returned %ld errno=%d\n", rc, errno);
    return rc == 0 ? 0 : 1;
}

static int probe_mprotect_exec(void)
{
    /* mprotect(addr, 4096, PROT_READ|PROT_EXEC) on a fresh anonymous page —
     * should be killed if the filter blocks exec-bit changes. */
    extern char __executable_start[];   /* a known executable address (linker-defined) */
    /* We don't actually want to flip our own .text to RW; instead use an
     * anonymous mmap.  But mmap PROT_EXEC itself is a candidate to block;
     * for the spike, blocking just mprotect-EXEC catches the W^X bypass. */
    void *p = (void *)((unsigned long)&__executable_start);
    long rc = syscall(SYS_mprotect, p, 4096UL, 5 /*PROT_READ|PROT_EXEC*/);
    fprintf(stderr, "probe_mprotect_exec: returned %ld errno=%d\n", rc, errno);
    return rc == 0 ? 0 : 1;
}

static int probe_uname(void)
{
    /* Sanity check: this one IS on the allowlist; the call should succeed
     * with rc=0.  If the seccomp filter is wrong, it might be killed too. */
    char buf[390]; /* sizeof(struct utsname) on Linux */
    long rc = syscall(SYS_uname, buf);
    fprintf(stderr, "probe_uname: returned %ld errno=%d\n", rc, errno);
    return rc == 0 ? 0 : 1;
}

/* ── dispatch ────────────────────────────────────────────────────────────── */

struct probe { const char *name; int (*fn)(void); };
static const struct probe probes[] = {
    { "open",            probe_open },
    { "socket",          probe_socket },
    { "execve",          probe_execve },
    { "mprotect-exec",   probe_mprotect_exec },
    { "uname",           probe_uname },
    { NULL, NULL },
};

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: adversary <probe>\n");
        fprintf(stderr, "probes:");
        for (const struct probe *p = probes; p->name; p++)
            fprintf(stderr, " %s", p->name);
        fprintf(stderr, "\n");
        return 2;
    }
    for (const struct probe *p = probes; p->name; p++) {
        if (!strcmp(argv[1], p->name)) {
            int rc = p->fn();
            /* If we got here, the kernel did NOT kill us — the filter is
             * broken (or the probe is the allowed `uname`).  Report. */
            fprintf(stderr, "probe %s: NOT killed (rc=%d) — filter leak!\n",
                    p->name, rc);
            return rc;
        }
    }
    fprintf(stderr, "unknown probe: %s\n", argv[1]);
    return 2;
}

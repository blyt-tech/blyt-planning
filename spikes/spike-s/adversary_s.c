/* adversary_s.c — Spike S ILP32 probe binary.
 *
 * Extends spike-h/adversary.c with a `write` probe for Stage 1 arch tests.
 * Compiled as a static (Stage 0/1) or dynamic (Stage 2-4) ILP32 ELF.
 *
 * Usage: adversary_s <probe>
 *   write        — call write(2, ".\n", 2); returns 0 if allowed
 *   socket       — call socket(AF_INET, SOCK_STREAM, 0); should be killed
 *   execve       — call execve("/bin/sh", ...); should be killed
 *   mprotect-exec — call mprotect(PROT_READ|PROT_EXEC); should be killed
 *   uname        — call uname (allowed in some test filters, blocked in others)
 *
 * Each probe exits 0 if the syscall succeeded (not killed by seccomp) or is
 * killed by SIGSYS (rc 159 as reported by the parent waitpid).
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

static int probe_write(void)
{
    /* write(2, ".\n", 2) — should be allowed; returns 0 on success. */
    char msg[] = ".\n";
    long rc = syscall(SYS_write, 2 /*stderr*/, msg, 2UL);
    if (rc == 2) return 0;
    fprintf(stderr, "probe_write: returned %ld errno=%d\n", rc, errno);
    return 1;
}

static int probe_socket(void)
{
    int s = (int)syscall(SYS_socket, 2 /*AF_INET*/, 1 /*SOCK_STREAM*/, 0);
    fprintf(stderr, "probe_socket: returned %d errno=%d\n", s, errno);
    return s >= 0 ? 0 : 1;
}

static int probe_execve(void)
{
    char *const argv[] = { (char *)"/bin/sh", NULL };
    char *const envp[] = { NULL };
    long rc = syscall(SYS_execve, "/bin/sh", argv, envp);
    fprintf(stderr, "probe_execve: returned %ld errno=%d\n", rc, errno);
    return rc == 0 ? 0 : 1;
}

static int probe_mprotect_exec(void)
{
    extern char __executable_start[];
    void *p = (void *)((unsigned long)&__executable_start);
    long rc = syscall(SYS_mprotect, p, 4096UL, 5 /*PROT_READ|PROT_EXEC*/);
    fprintf(stderr, "probe_mprotect_exec: returned %ld errno=%d\n", rc, errno);
    return rc == 0 ? 0 : 1;
}

static int probe_uname(void)
{
    char buf[390];
    long rc = syscall(SYS_uname, buf);
    fprintf(stderr, "probe_uname: returned %ld errno=%d\n", rc, errno);
    return rc == 0 ? 0 : 1;
}

struct probe { const char *name; int (*fn)(void); };
static const struct probe probes[] = {
    { "write",          probe_write },
    { "socket",         probe_socket },
    { "execve",         probe_execve },
    { "mprotect-exec",  probe_mprotect_exec },
    { "uname",          probe_uname },
    { NULL, NULL },
};

/* blyt_init is defined in libblyt32.so.  The weak declaration here means:
 * - static build (no libblyt32): blyt_init resolves to NULL → skipped.
 * - dynamic build (DT_NEEDED libblyt32.so): blyt_init provided by the lib.
 * Calling blyt_init() via this extern forces DT_NEEDED into the dynamic ELF. */
extern void blyt_init(void) __attribute__((weak));

int main(int argc, char **argv)
{
    if (blyt_init) blyt_init();  /* triggers libblyt32 constructor via ld.so */

    if (argc != 2) {
        fprintf(stderr, "usage: adversary_s <probe>\nprobes:");
        for (const struct probe *p = probes; p->name; p++)
            fprintf(stderr, " %s", p->name);
        fprintf(stderr, "\n");
        return 2;
    }
    for (const struct probe *p = probes; p->name; p++) {
        if (!strcmp(argv[1], p->name))
            return p->fn();
    }
    fprintf(stderr, "unknown probe: %s\n", argv[1]);
    return 2;
}

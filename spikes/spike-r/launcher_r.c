/* launcher_r.c — Stage 2: cart-runner launcher with raw BPF seccomp filter.
 *
 * Drop-in replacement for Spike H's launcher.c that uses a hand-written
 * raw BPF arch-dispatch filter (from seccomp_allowlist.h) instead of
 * libseccomp.  This correctly handles AUDIT_ARCH_RISCV32 — the ILP32 cart
 * process's syscalls are matched against the ILP32 allowlist, not treated
 * as unknown-arch (Spike H limitation) and killed indiscriminately.
 *
 * Usage:
 *   launcher_r [--cgroup PATH] [--rootfs DIR]
 *              [--no-seccomp] [--no-netns] [--no-mountns]
 *              -- /path/to/cart [args...]
 *
 * Options match Spike H's launcher.c exactly so existing test scripts work.
 *
 * Filter decision (from Stage 1 seccomp_raw_test):
 *   Option A (single arch-dispatch filter) is used.  A single filter
 *   installed before execv handles both LP64 (launcher → exec) and ILP32
 *   (cart post-exec) syscalls.  The LP64 branch allows execv and exits
 *   only; the ILP32 branch allows the musl runtime + workload allowlist
 *   from seccomp_allowlist.h.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "seccomp_allowlist.h"

struct opts {
    const char *cgroup_dir;
    const char *rootfs;
    int         do_seccomp;
    int         do_netns;
    int         do_mountns;
    char      **cart_argv;
};

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [--cgroup PATH] [--rootfs DIR]\n"
        "       %*s [--no-seccomp] [--no-netns] [--no-mountns]\n"
        "       %*s -- /path/to/cart [args...]\n",
        prog, (int)strlen(prog), "", (int)strlen(prog), "");
}

static int parse_args(int argc, char **argv, struct opts *o)
{
    o->cgroup_dir = NULL;
    o->rootfs     = NULL;
    o->do_seccomp = 1;
    o->do_netns   = 1;
    o->do_mountns = 1;
    o->cart_argv  = NULL;

    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        else if (!strcmp(argv[i], "--cgroup") && i + 1 < argc)
            o->cgroup_dir = argv[++i];
        else if (!strcmp(argv[i], "--rootfs") && i + 1 < argc)
            o->rootfs = argv[++i];
        else if (!strcmp(argv[i], "--no-seccomp")) o->do_seccomp = 0;
        else if (!strcmp(argv[i], "--no-netns"))   o->do_netns   = 0;
        else if (!strcmp(argv[i], "--no-mountns")) o->do_mountns = 0;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]); exit(0);
        } else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            usage(argv[0]);
            return -1;
        }
    }
    if (i >= argc) {
        fprintf(stderr, "missing cart path after --\n");
        usage(argv[0]);
        return -1;
    }
    o->cart_argv = &argv[i];
    return 0;
}

static int join_cgroup(const char *cgroup_dir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/cgroup.procs", cgroup_dir);
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return -1;
    }
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%d\n", (int)getpid());
    if (write(fd, buf, (size_t)n) != n) {
        fprintf(stderr, "write %s: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int do_pivot_root(const char *rootfs)
{
    if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) != 0) {
        fprintf(stderr, "bind-mount %s: %s\n", rootfs, strerror(errno));
        return -1;
    }
    char put_old[512];
    snprintf(put_old, sizeof put_old, "%s/put_old", rootfs);
    if (mkdir(put_old, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir %s: %s\n", put_old, strerror(errno));
        return -1;
    }
    if (syscall(SYS_pivot_root, rootfs, put_old) != 0) {
        fprintf(stderr, "pivot_root: %s\n", strerror(errno));
        return -1;
    }
    if (chdir("/") != 0) {
        fprintf(stderr, "chdir /: %s\n", strerror(errno));
        return -1;
    }
    if (umount2("/put_old", MNT_DETACH) != 0) {
        fprintf(stderr, "umount2 /put_old: %s\n", strerror(errno));
        return -1;
    }
    (void)rmdir("/put_old");
    return 0;
}

static int child_main(struct opts *o)
{
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        fprintf(stderr, "prctl PR_SET_NO_NEW_PRIVS: %s\n", strerror(errno));
        return 127;
    }

    int unshare_flags = 0;
    if (o->do_mountns) unshare_flags |= CLONE_NEWNS;
    if (o->do_netns)   unshare_flags |= CLONE_NEWNET;
    if (unshare_flags && unshare(unshare_flags) != 0) {
        fprintf(stderr, "unshare: %s\n", strerror(errno));
        return 127;
    }

    if (o->do_mountns) {
        if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
            fprintf(stderr, "mount --make-rprivate /: %s\n", strerror(errno));
            return 127;
        }
    }

    if (o->rootfs && do_pivot_root(o->rootfs) != 0) return 127;
    if (o->cgroup_dir && join_cgroup(o->cgroup_dir) != 0) return 127;

    /* Install the raw BPF arch-dispatch filter (Option A from Stage 1).
     * This replaces spike_h_install_seccomp() which used libseccomp and
     * could not express AUDIT_ARCH_RISCV32 rules. */
    if (o->do_seccomp && install_raw_bpf_filter() != 0) return 127;

    execv(o->cart_argv[0], o->cart_argv);
    fprintf(stderr, "execv %s: %s\n", o->cart_argv[0], strerror(errno));
    return 127;
}

int main(int argc, char **argv)
{
    struct opts o;
    if (parse_args(argc, argv, &o) != 0) return 2;

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        _exit(child_main(&o));
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "waitpid: %s\n", strerror(errno));
        return 1;
    }
    if (WIFEXITED(status)) {
        int rc = WEXITSTATUS(status);
        fprintf(stderr, "spike-r: child exited rc=%d\n", rc);
        return rc;
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        fprintf(stderr, "spike-r: child killed by signal %d (%s)\n",
                sig, strsignal(sig));
        if (sig == SIGSYS) return 159;
        return 128 + sig;
    }
    return 1;
}

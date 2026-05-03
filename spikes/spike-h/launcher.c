/* Spike H — Stage 3/4: cart-runner launcher.
 *
 * Forks a child, applies isolation (seccomp-bpf + namespaces), optionally
 * joins a pre-created cgroups v2 cgroup, then execs the cart ELF.  The
 * parent waits and reports the child's exit status / signal.
 *
 * Usage:
 *   launcher [--cgroup PATH] [--no-seccomp] [--no-netns] [--no-mountns]
 *            [--rootfs DIR] -- /path/to/cart [args...]
 *
 *   --cgroup PATH      cgroup-v2 directory; the launcher's child writes its
 *                      pid to cgroup.procs before exec.  The cgroup itself
 *                      (and its cpu.max value) must be created by the caller.
 *   --no-seccomp       skip the seccomp filter (debugging only).
 *   --no-netns         skip CLONE_NEWNET (debugging only).
 *   --no-mountns       skip CLONE_NEWNS  (debugging only).
 *   --rootfs DIR       pivot_root into DIR before exec.  DIR must contain a
 *                      'put_old' subdir (created if missing) and the cart
 *                      ELF + any libraries it needs (e.g. ld-musl).  Without
 *                      this flag the mount namespace is unshared but the
 *                      filesystem is still visible (CLONE_NEWNS alone copies
 *                      the mount tree); see PLAN.md §13.
 *
 * The launcher is a native RV64 process.  The cart it execs is RV32 IMFC
 * and runs under CONFIG_COMPAT.
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

#include "spike_h.h"

struct opts {
    const char *cgroup_dir;     /* /sys/fs/cgroup/<name>           or NULL */
    const char *rootfs;         /* directory to pivot_root into    or NULL */
    int         do_seccomp;
    int         do_netns;
    int         do_mountns;
    char      **cart_argv;      /* cart path + args; argv[0] = cart path */
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
    o->cgroup_dir  = NULL;
    o->rootfs      = NULL;
    o->do_seccomp  = 1;
    o->do_netns    = 1;
    o->do_mountns  = 1;
    o->cart_argv   = NULL;

    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        else if (!strcmp(argv[i], "--cgroup") && i + 1 < argc) {
            o->cgroup_dir = argv[++i];
        }
        else if (!strcmp(argv[i], "--rootfs") && i + 1 < argc) {
            o->rootfs = argv[++i];
        }
        else if (!strcmp(argv[i], "--no-seccomp")) o->do_seccomp = 0;
        else if (!strcmp(argv[i], "--no-netns"))   o->do_netns   = 0;
        else if (!strcmp(argv[i], "--no-mountns")) o->do_mountns = 0;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            exit(0);
        }
        else {
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

/* Write our own pid into <cgroup_dir>/cgroup.procs.  Caller has already
 * created the cgroup and configured cpu.max.  Returns 0 on success. */
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

/* pivot_root sequence per PLAN.md §13:
 *   1. mount the new root onto itself (so it qualifies as a mount point)
 *   2. ensure put_old/ exists inside the new root
 *   3. pivot_root(new_root, put_old)
 *   4. chdir("/") and umount2(put_old, MNT_DETACH)
 * This is RUN AFTER unshare(CLONE_NEWNS) and BEFORE seccomp_load (which
 * blocks mount/umount/pivot_root). */
static int do_pivot_root(const char *rootfs)
{
    /* Ensure rootfs is itself a mount point. */
    if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) != 0) {
        fprintf(stderr, "bind-mount %s on itself: %s\n", rootfs, strerror(errno));
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
    if (rmdir("/put_old") != 0 && errno != ENOENT) {
        /* Not fatal; the directory becomes invisible after the next
         * unshare anyway. */
    }
    return 0;
}

static int child_main(struct opts *o)
{
    /* PR_SET_NO_NEW_PRIVS is required before loading a non-root seccomp
     * filter and is also a defence-in-depth step for cart processes. */
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

    /* Make all mounts in our new namespace private so future mount events
     * don't propagate to the host. */
    if (o->do_mountns) {
        if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
            fprintf(stderr, "mount --make-rprivate /: %s\n", strerror(errno));
            return 127;
        }
    }

    if (o->rootfs && do_pivot_root(o->rootfs) != 0) return 127;

    if (o->cgroup_dir && join_cgroup(o->cgroup_dir) != 0) return 127;

    if (o->do_seccomp && spike_h_install_seccomp() != 0) return 127;

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
        fprintf(stderr, "spike-h: child exited rc=%d\n", rc);
        return rc;
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        fprintf(stderr, "spike-h: child killed by signal %d (%s)\n",
                sig, strsignal(sig));
        /* Encode SIGSYS specially so test harnesses can detect it. */
        if (sig == SIGSYS) return 159;
        return 128 + sig;
    }
    return 1;
}

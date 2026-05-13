/* launcher_s.c — Stage 4: LP64 launcher for hardware native-exec path.
 *
 * Implements the phase-1 side of ADR-0119 Option B:
 *   1. Validates the cart binary exists and is executable.
 *   2. Calls prctl(PR_SET_NO_NEW_PRIVS) — required before seccomp install.
 *   3. Installs the arch-dispatch phase-1 filter (seccomp_allowlist_s.h).
 *   4. Sets LD_LIBRARY_PATH to the runtime library directory.
 *   5. execve's the ILP32 cart binary.
 *
 * The cart binary is a native ILP32 ELF.  After exec, the kernel loads it
 * via the compat ELF loader.  ld.so resolves libblyt32.so (and other
 * DT_NEEDED libs), using only the syscalls permitted by the phase-1 RV32
 * block.  libblyt32.so's constructor then installs the more-restrictive
 * phase-2 filter before the cart's main() runs.
 *
 * Usage:
 *   launcher_s [--lib-dir DIR] [--no-seccomp] -- /path/to/cart [args...]
 *
 * The --lib-dir option sets LD_LIBRARY_PATH for the ILP32 cart.
 * If omitted, an existing LD_LIBRARY_PATH in the environment is preserved.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "seccomp_allowlist_s.h"

struct opts {
    const char *lib_dir;
    int         do_seccomp;
    char      **cart_argv;
};

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [--lib-dir DIR] [--no-seccomp] -- /path/to/cart [args...]\n",
        prog);
}

static int parse_args(int argc, char **argv, struct opts *o)
{
    o->lib_dir    = NULL;
    o->do_seccomp = 1;
    o->cart_argv  = NULL;

    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        else if (!strcmp(argv[i], "--lib-dir") && i + 1 < argc)
            o->lib_dir = argv[++i];
        else if (!strcmp(argv[i], "--no-seccomp"))
            o->do_seccomp = 0;
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
        usage(argv[0]); return -1;
    }
    o->cart_argv = &argv[i];
    return 0;
}

/* Confirm the cart binary exists and is a regular ELF file.
 * We read the first 4 bytes (ELF magic) for a basic sanity check.
 * Returns 0 on success, -1 on error. */
static int validate_cart(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "launcher_s: stat %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "launcher_s: %s: not a regular file\n", path);
        return -1;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "launcher_s: open %s: %s\n", path, strerror(errno));
        return -1;
    }
    unsigned char magic[4];
    ssize_t n = read(fd, magic, 4);
    close(fd);
    if (n != 4 || magic[0] != 0x7f || magic[1] != 'E' ||
        magic[2] != 'L' || magic[3] != 'F') {
        fprintf(stderr, "launcher_s: %s: not an ELF binary\n", path);
        return -1;
    }
    return 0;
}

static int child_main(struct opts *o)
{
    /* prctl(PR_SET_NO_NEW_PRIVS) must precede seccomp filter install. */
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        fprintf(stderr, "launcher_s: prctl NO_NEW_PRIVS: %s\n", strerror(errno));
        return 127;
    }

    /* Validate cart binary before installing the filter (validate uses open/read
     * which may not be in the phase-1 RISCV64 allowlist). */
    if (validate_cart(o->cart_argv[0]) != 0) return 127;

    /* Set LD_LIBRARY_PATH for the ILP32 cart's dynamic linker. */
    if (o->lib_dir) {
        if (setenv("LD_LIBRARY_PATH", o->lib_dir, 1) != 0) {
            fprintf(stderr, "launcher_s: setenv LD_LIBRARY_PATH: %s\n",
                    strerror(errno));
            return 127;
        }
        fprintf(stderr, "launcher_s: LD_LIBRARY_PATH=%s\n", o->lib_dir);
    }

    /* Install arch-dispatch phase-1 filter.  After this point only syscalls
     * in seccomp_phase1_riscv64_nrs[] are allowed for this LP64 process. */
    if (o->do_seccomp && install_phase1_filter() != 0) return 127;

    /* The only syscalls after this point (for the LP64 process) are
     * execve (allowed), write (for any subsequent fprintf — avoid these),
     * and exit_group on failure. */
    execve(o->cart_argv[0], o->cart_argv, environ);
    /* execve only returns on failure.  write is in the phase-1 RV64 allowlist. */
    fprintf(stderr, "launcher_s: execve %s: %s\n",
            o->cart_argv[0], strerror(errno));
    return 127;
}

int main(int argc, char **argv)
{
    struct opts o;
    if (parse_args(argc, argv, &o) != 0) return 2;

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "launcher_s: fork: %s\n", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        _exit(child_main(&o));
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "launcher_s: waitpid: %s\n", strerror(errno));
        return 1;
    }
    if (WIFEXITED(status)) {
        int rc = WEXITSTATUS(status);
        fprintf(stderr, "launcher_s: child exited rc=%d\n", rc);
        return rc;
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        fprintf(stderr, "launcher_s: child killed by signal %d (%s)\n",
                sig, strsignal(sig));
        if (sig == SIGSYS) return 159;
        return 128 + sig;
    }
    return 1;
}

/* Spike H — shared declarations between launcher.c and seccomp_filter.c. */

#ifndef SPIKE_H_H
#define SPIKE_H_H

/* Install the seccomp-bpf filter.  Must be called in the child after any
 * necessary unshare(2)/pivot_root/mount-namespace setup and immediately
 * before execve().  Returns 0 on success, -1 on failure. */
int spike_h_install_seccomp(void);

#endif /* SPIKE_H_H */

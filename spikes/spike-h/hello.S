/* Spike H — Stage 1: minimal RV32IMFC Linux ELF.
 *
 * Writes "OK\n" to stdout and exits.  Built with -nostdlib so the binary
 * has no C runtime dependency: it tests only the kernel's ability to load
 * an RV32 ELF and dispatch RV32 syscalls under CONFIG_COMPAT.
 *
 * Linux RV32 syscall ABI:
 *   a7 = syscall number,  a0..a5 = arguments,  ecall returns in a0.
 *   __NR_write       = 64
 *   __NR_exit_group  = 94
 */

    .section .text
    .globl _start
    .type  _start, @function
_start:
    li      a7, 64                  /* __NR_write */
    li      a0, 1                   /* fd = stdout */
    la      a1, msg
    li      a2, 3                   /* len("OK\n") */
    ecall

    li      a7, 94                  /* __NR_exit_group */
    li      a0, 0                   /* status */
    ecall
1:  j       1b                      /* unreachable */

    .section .rodata
msg:
    .ascii  "OK\n"

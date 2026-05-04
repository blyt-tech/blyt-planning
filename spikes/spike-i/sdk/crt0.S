/* SDK crt0.S — minimal _start for every spike-i cart.
 * Sets up gp, calls fc_console_main() via PLT into libconsole.so,
 * then exits via ECALL 93.
 */
    .section .text._start, "ax"
    .globl _start
    .type  _start, @function
_start:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    call    fc_console_main
    li      a0, 0
    li      a7, 93
    ecall
1:  j       1b

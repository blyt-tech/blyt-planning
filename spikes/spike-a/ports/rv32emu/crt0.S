/* Minimal startup for rv32emu (Linux RV32 ABI).
 * On entry: sp points at argc, followed by argv[], then envp[].
 * _exit is defined in syscalls.c.
 */
    .section .text
    .global _start
    .type   _start, @function
_start:
    /* Initialize gp before any gp-relative loads. .option norelax keeps
     * the linker from relaxing this into a gp-relative form (which would
     * read gp before it's set). */
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    mv      fp, zero        /* clear frame pointer */
    lw      a0, 0(sp)       /* argc */
    addi    a1, sp, 4       /* argv */
    call    main
    call    _exit           /* pass main's return value in a0 */
1:  j       1b              /* unreachable */

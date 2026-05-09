/* SDK crt0.S — minimal _start for spike-p carts.
 * Extends spike-i's crt0.S with heap zero-init and __blyt_heap_init call.
 *
 * Heap zero-init uses 4-byte stores; ALIGN(16) in cart.ld ensures
 * __blyt_heap_end is 16-byte aligned (a multiple of 4), so the loop
 * never writes past the declared section boundary.
 */
    .section .text._start, "ax"
    .globl _start
    .type  _start, @function
_start:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    /* Zero the .blyt_heap section.  The NOLOAD attribute means the ELF
     * loader does not supply zeroed bytes; explicit clearing ensures a
     * deterministic allocator state regardless of loader behaviour. */
    la      a0, __blyt_heap_start
    la      a1, __blyt_heap_end
    j       2f
1:  sw      zero, 0(a0)
    addi    a0, a0, 4
2:  bltu    a0, a1, 1b

    /* Initialise the heap allocator.  __blyt_heap_init is provided by the
     * blyt32 SDK crate (no-op when start == end, i.e. HEAP_SIZE = 0). */
    la      a0, __blyt_heap_start
    la      a1, __blyt_heap_end
    call    __blyt_heap_init

    call    fc_console_main
    li      a0, 0
    li      a7, 93
    ecall
1:  j       1b

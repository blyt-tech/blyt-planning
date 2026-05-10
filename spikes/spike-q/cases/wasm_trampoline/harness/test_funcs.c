/* test_funcs.c — minimal RV32IMAFC functions for Stage 2 call-on-demand test.
 *
 * Compiled to riscv32imafc-unknown-none-elf (no libc, no dynamic linking).
 * The harness (test_call_on_demand.c) loads this ELF into rv32emu and calls
 * add32 and addf via rv32emu_call_fn, validating the ilp32f register ABI.
 *
 * add32: integer args in a0, a1; return in a0.
 * addf:  float  args in fa0, fa1; return in fa0 (ilp32f ABI).
 */

#include <stdint.h>

__attribute__((noinline))
uint32_t add32(uint32_t a, uint32_t b)
{
    return a + b;
}

__attribute__((noinline))
float addf(float a, float b)
{
    return a + b;
}

/* Minimal _start — immediately calls ECALL 93 (SYS_exit) with code 0.
 * This is never reached in the call-on-demand path; it exists so the ELF
 * is a valid executable that rv32emu can load. */
__attribute__((naked, noreturn))
void _start(void)
{
    __asm__ volatile (
        "li a0, 0\n"
        "li a7, 93\n"
        "ecall\n"
        "1: j 1b\n"
    );
}

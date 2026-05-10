#![no_std]
#![no_main]

// Rust cart ELF for the WASM trampoline case (Stage 3, Spike Q).
//
// This is a minimal riscv32imafc-unknown-none-elf static library containing
// two pure-compute functions that the WASM host calls via rv32emu_call_fn.
// There is no Lua C API interaction here; the Lua-direct host on the WASM side
// handles all Lua stack operations and uses rv32emu_call_fn as the trampoline.
//
// The _start sentinel is a two-instruction stub that fires ECALL 0xDEAD
// immediately.  rv32emu_call_fn never calls _start; it jumps directly to the
// target function's address with ra set to the sentinel page.  The stub exists
// so that if _start were somehow entered, it would terminate cleanly rather
// than executing garbage.

mod lua_exports;

use core::arch::global_asm;

// Sentinel _start: li a7, 0xDEAD (= 57005); ecall; loop forever.
// The rv32emu_call_fn sentinel stub at FC32_SENTINEL_ADDR (0x04000000) contains
// the same instruction sequence; this _start stub fires it from _start as a
// belt-and-suspenders measure — rv32emu_call_fn never actually calls _start.
global_asm!(
    ".section .text._start, \"ax\"",
    ".globl _start",
    "_start:",
    "    li   a7, 57005",   // 0xDEAD
    "    ecall",
    "1:  j    1b",
);

// ---------------------------------------------------------------------------
// fast_add — pure f32 addition.
// Called by the WASM host via rv32emu_call_fn with is_float=1 for both args
// and ret_is_float=1.  The ilp32f ABI places f32 args in fa0, fa1 and the
// return value in fa0.
// ---------------------------------------------------------------------------
#[no_mangle]
pub extern "C" fn fast_add(a: f32, b: f32) -> f32 {
    a + b
}

// ---------------------------------------------------------------------------
// fast_mul — pure i32 multiplication.
// Called by the WASM host via rv32emu_call_fn with is_float=0 for both args
// and ret_is_float=0.  The ilp32f ABI places i32 args in a0, a1 and the
// return value in a0.
// ---------------------------------------------------------------------------
#[no_mangle]
pub extern "C" fn fast_mul(a: i32, b: i32) -> i32 {
    a * b
}

// ---------------------------------------------------------------------------
// Panic handler — required by no_std.
// ---------------------------------------------------------------------------
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

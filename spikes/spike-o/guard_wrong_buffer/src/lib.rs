// Compile-fail guard: cross-buffer FieldHandle misuse.
//
// This crate MUST NOT compile.  The Makefile asserts cargo build exits
// non-zero and that the error output contains "FieldHandle".
//
// The bug: S_CTR is a FieldHandle<MainBuffer>, but we pass it to
// MainBuffer.set_u32 via an EnemiesBuffer-typed call path.  Because
// FieldHandle<B> is parameterised on B, the type checker catches the mismatch
// at compile time — no runtime penalty, no unsafe code.
//
// If this compiles, the FieldHandle<B> phantom-type guarantee is not enforced
// and must be redesigned.

#![no_std]
#![no_main]

use blyt32::{Slot};
use blyt32::buffers::{MainBuffer, EnemiesBuffer, FieldHandle};

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! { loop {} }

const S_CTR: FieldHandle<MainBuffer> = FieldHandle::new(0x00010001);

#[no_mangle]
pub extern "C" fn fc_cart_init() {
    let slot = Slot(0);
    // ERROR: S_CTR is FieldHandle<MainBuffer> but EnemiesBuffer.set_u32
    // expects FieldHandle<EnemiesBuffer>.  Type mismatch → compile error.
    EnemiesBuffer.set_u32(slot, S_CTR, 42);
}

#[no_mangle]
pub extern "C" fn fc_cart_update() {}
#[no_mangle]
pub extern "C" fn fc_cart_draw() {}

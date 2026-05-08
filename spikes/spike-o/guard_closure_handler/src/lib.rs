// Compile-fail guard: capturing closure passed where fn pointer required.
//
// This crate MUST NOT compile.  The Makefile asserts cargo build exits
// non-zero and that the error output contains "fn pointer" or "expected fn".
//
// The bug: blyt32::register_handler expects fn(u32) -> u32 (a plain function
// pointer, not a closure).  A capturing closure (one that captures `captured`
// from the enclosing scope) cannot coerce to a fn pointer — Rust requires
// closures to be non-capturing to coerce to fn.  A non-capturing closure
// *would* coerce, so we deliberately capture a local variable to force the
// error.
//
// If this compiles, the fn-typing constraint is not enforced and register_handler
// must be redesigned (e.g. to use a trait object, which would lose the
// compile-time guarantee).

#![no_std]
#![no_main]

use blyt32::HandlerHandle;

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! { loop {} }

const HANDLER_AI: HandlerHandle = HandlerHandle(1);

#[no_mangle]
pub extern "C" fn fc_cart_init() {
    // captured is a local — the closure captures it, making it a non-fn closure.
    let captured: u32 = 42;
    // ERROR: |ctx| { ... } is a closure (captures `captured`), not fn(u32)->u32.
    blyt32::register_handler(HANDLER_AI, |ctx| { ctx + captured });
}

#[no_mangle]
pub extern "C" fn fc_cart_update() {}
#[no_mangle]
pub extern "C" fn fc_cart_draw() {}

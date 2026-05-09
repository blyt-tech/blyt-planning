// Stage 2 — allocator smoke test.
//
// Confirms that __blyt_heap_init ran correctly and that alloc + dealloc work.
// Allocates a Box<u32>, reads the value, drops the box.  Exercises Vec growth
// (realloc path).  Exits after one frame.
//
// Gate: rv32emu exits 0, "OK" in output.

extern crate alloc;
use alloc::boxed::Box;
use alloc::vec::Vec;

static OK_MSG: &[u8] = b"OK\n";

#[no_mangle]
pub extern "C" fn fc_cart_init() {
    // Box alloc + dealloc path
    let boxed = Box::new(0xDEAD_BEEFu32);
    assert_eq!(*boxed, 0xDEAD_BEEF);
    drop(boxed);

    // Vec alloc + growth (realloc) + dealloc path
    let mut v: Vec<u32> = Vec::new();
    for i in 0..8u32 {
        v.push(i * i);
    }
    assert_eq!(v[4], 16);
    drop(v);

    // Emit "OK\n" so the Makefile can grep for success.
    unsafe {
        core::arch::asm!(
            "ecall",
            in("a0") 1usize,
            in("a1") OK_MSG.as_ptr(),
            in("a2") OK_MSG.len(),
            in("a7") 64usize,
            lateout("a0") _,
            options(nostack),
        );
    }
}

#[no_mangle]
pub extern "C" fn fc_cart_update() {
    unsafe { core::arch::asm!("li a0, 0", "li a7, 93", "ecall", options(noreturn)) };
}

#[no_mangle]
pub extern "C" fn fc_cart_draw() {}

#[no_mangle]
pub extern "C" fn fc_cart_on_load() {}

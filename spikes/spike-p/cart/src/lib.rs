#![no_std]
#![no_main]

// Provide global allocator + __blyt_heap_init for carts that use alloc types.
// Rust requires #[global_allocator] to be in the final crate (binary/staticlib),
// not in a dependency rlib.  The atomics cart skips this module; its weak
// no-op __blyt_heap_init is provided by the C stub library.
#[cfg(not(feature = "atomics"))]
mod heap {
    extern crate alloc;
    use core::alloc::{GlobalAlloc, Layout};
    use core::cell::UnsafeCell;
    use linked_list_allocator::Heap;

    struct BlytAllocator(UnsafeCell<Heap>);
    // SAFETY: single-threaded cart execution; no concurrent allocator calls.
    unsafe impl Sync for BlytAllocator {}

    unsafe impl GlobalAlloc for BlytAllocator {
        unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
            (*self.0.get())
                .allocate_first_fit(layout)
                .map_or(core::ptr::null_mut(), |p| p.as_ptr())
        }
        unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
            (*self.0.get()).deallocate(
                core::ptr::NonNull::new_unchecked(ptr),
                layout,
            )
        }
    }

    #[global_allocator]
    static ALLOCATOR: BlytAllocator = BlytAllocator(UnsafeCell::new(Heap::empty()));

    /// Called by crt0.S before fc_cart_init.  start..end is the .blyt_heap region.
    #[no_mangle]
    pub unsafe extern "C" fn __blyt_heap_init(start: *mut u8, end: *mut u8) {
        let size = (end as usize).saturating_sub(start as usize);
        if size > 0 {
            (*ALLOCATOR.0.get()).init(start, size);
        }
    }
}

static PANIC_BYTE: &[u8] = b"!";

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    unsafe {
        core::arch::asm!(
            "ecall",
            in("a0") 1usize,
            in("a1") PANIC_BYTE.as_ptr(),
            in("a2") PANIC_BYTE.len(),
            in("a7") 64usize,
            lateout("a0") _,
            options(nostack),
        );
    }
    loop {}
}

#[cfg(feature = "atomics")]
#[path = "lib_atomics.rs"]
mod cart_impl;

#[cfg(feature = "heap_smoke")]
#[path = "lib_heap_smoke.rs"]
mod cart_impl;

#[cfg(not(any(feature = "atomics", feature = "heap_smoke")))]
#[path = "lib_alloc.rs"]
mod cart_impl;

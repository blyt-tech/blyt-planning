// blyt32 allocator module — documentation and helper types.
//
// DESIGN NOTE: Rust requires #[global_allocator] to be declared in the final
// binary/staticlib crate, not in a dependency rlib.  Therefore this module
// does NOT declare #[global_allocator].
//
// The actual allocator declaration (using linked_list_allocator::Heap) lives
// in cart/src/lib.rs for all carts that use alloc.  Production blytbuild will
// generate this declaration in the cart preamble for carts with heap_size > 0.
//
// The __blyt_heap_init function (called by crt0.S to initialize the heap) is
// also in cart/src/lib.rs so it can initialize the same ALLOCATOR static.
// The C stub provides a weak no-op __blyt_heap_init for carts without alloc.

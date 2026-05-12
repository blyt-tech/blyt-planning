# ADR-0120: Runtime-provided C library (libblytc.so)

## Status

Accepted

## Context

Cart binaries currently statically link their own C runtime. For Rust
carts using `no_std + alloc`, ADR-0108 defined a fixed-size heap region
declared in `cart.build.yaml`; for C carts, a C library is statically
linked per-cart. Both approaches embed libc code in every distributable,
inflating cart size.

The runtime already provides shared libraries to all targets via the same
mechanism: on emulated targets (rv32emu), libraries are pre-mapped into
the guest address space; on hardware trusted exec, ld.so finds them via
LD_LIBRARY_PATH. Adding a C library follows the same pattern.

A runtime-provided C library is variant-independent — it is the same
regardless of which console variant (blyt32, blytty, blyt3d) the cart
targets.

## Decision

### libblytc.so

A trimmed musl-based C library, compiled for RV32IMAFC ILP32, is
provided by the runtime as `libblytc.so`. It is optional: carts that
need it declare it in DT_NEEDED; carts that do not (statically-linked C
carts, Rust carts that avoid alloc) omit it and pay no overhead.

**Included:** `malloc`/`free`/`realloc`/`calloc`, string functions
(`memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcmp`, `strcpy`,
`strncpy`, `strtol`, `strtod`, etc.), `printf`-family formatting
(`snprintf`, `vsnprintf`), `qsort`, `abs`, `math.h` functions.

**Excluded:** `FILE*` I/O (`fopen`, `fread`, `fprintf`, etc.) — carts
access data through the console resource API, not the filesystem;
`pthread_*` — carts are single-threaded; `signal` — signal handling is
the runtime's concern; `dlopen`/`dlclose` — dynamic loading is not
permitted in carts.

### Target availability

`libblytc.so` is available on all targets via the standard runtime
library mechanism:

- **Hardware trusted exec:** present in the runtime library directory;
  ld.so resolves it via LD_LIBRARY_PATH.
- **Hardware custom loader / emulated targets (rv32emu):** mapped into
  the rv32emu guest address space alongside libblyt32.so before execution
  begins. rv32emu's loader accepts the library from an in-memory buffer
  (see "libretro core" below).
- **WASM:** same as rv32emu — the library is loaded into the rv32emu
  guest that runs inside the WASM host. No change to the WASM host layer.

### Cart memory budget

Every cart runs within a hard **16 MB total memory budget**, enforced by
the runtime. The budget is not declared by the cart and cannot be
influenced by any value in the cart binary, including `.cart.info`.
`.cart.info` is cart-supplied and therefore untrusted for
security-relevant limits.

The 16 MB covers **both heap allocations and currently-loaded resources**
combined. A cart that has pinned 14 MB of resources has only 2 MB
available for heap, and vice versa.

**Mechanism:** The runtime provides a 16 MB arena to libblytc.so by
calling `blytc_arena_init(void *base, size_t size)` before cart entry.
libblytc.so's malloc sub-allocates from this arena. The resource
subsystem also allocates resource buffers from this arena (by calling
malloc internally for all resource loads). Because all cart memory flows
through the same arena, the 16 MB cap is automatically enforced at the
allocation source: malloc returns NULL and resource-load ECALLs return an
error code whenever the combined live allocation would exceed the budget.

This call is made on all paths: before the rv32emu guest starts on
emulated targets, and from libblyt32.so's constructor before cart entry
on the hardware trusted-exec path.

The `heap_size` field previously described in ADR-0108 is removed. No
per-cart heap declaration exists; the 16 MB budget is the only limit.

### Rust global_allocator

The `blyt32` SDK crate's `#[global_allocator]` is backed by
`libblytc.so`'s `malloc`/`free`. Rust carts that want `alloc` (Vec,
String, Box, etc.) enable the SDK crate's `alloc` feature, which:
1. Declares a DT_NEEDED dependency on `libblytc.so` in the cart ELF.
2. Registers the `#[global_allocator]` that forwards to libblytc's malloc.

Carts that do not enable the `alloc` feature omit `libblytc.so` from
DT_NEEDED and use no allocator. The `no_std` model is unchanged; `std`
is not available (libblytc.so does not provide the full interface that
Rust's `std` requires, and the riscv32imfc target is not in the upstream
Rust standard library distribution).

### libretro core embedding

For libretro core distribution (desktop, WASM, and the custom frontend),
the ILP32 runtime libraries — `libblyt32.so`, `libblyt32lua.so`,
`libblytc.so` — are embedded as binary data sections inside the libretro
core `.so`. rv32emu's loader is extended to accept library images from
memory buffers in addition to file paths.

This makes the libretro core a single self-contained distributable: no
companion files, no installation step, no path-lookup logic. Frontend
packaging (RetroArch core directory, desktop app bundle) ships one file.

On hardware, these libraries live at known paths in the buildroot image
and are not embedded in any binary.

## Consequences

- Cart distributables are smaller: libc code is shared across all carts
  instead of statically linked per cart.
- The `heap_size` field in `cart.build.yaml` and `.cart.info` is removed.
  The 16 MB runtime budget replaces it; no per-cart declaration exists.
- `libblytc.so` must be built as part of the runtime toolchain alongside
  `libblyt32.so`. It is a build-time dependency of the SDK crate's
  `alloc` feature.
- The libretro core becomes self-contained. rv32emu's loader gains a
  memory-buffer load path (low-complexity addition; the file-path path
  is still used for dev-mode carts and hardware).
- C carts that previously relied on static libc can optionally migrate to
  linking `libblytc.so` instead; backward compatibility with statically-
  linked libc is preserved.
- libblytc.so's excluded surface (`FILE*`, pthreads, dlopen) is not a
  gap for cart authors: filesystem access goes through the resource API,
  threading is not available, and dynamic loading is not permitted.

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

### Heap limit enforcement

Cart memory limits are enforced inside libblytc.so's malloc implementation.
The allocator tracks total live heap bytes and refuses allocations that
would exceed a per-cart limit, returning NULL (C) or triggering
out-of-memory handling (Rust alloc).

The limit value is declared by the cart as `heap_size` in
`cart.build.yaml`; the packer writes it into the `.cart.info` FlatBuffers
section. The runtime reads it at load time and calls a libblytc.so
initialisation symbol (`blytc_heap_init(size_t limit)`) before jumping
to the cart entry point. This call is made on all paths: before the rv32emu
guest starts on emulated targets, and from libblyt32.so's constructor on
the hardware trusted-exec path.

`heap_size` in `cart.build.yaml` is therefore retained (not removed).
What changes from ADR-0108 is the enforcement mechanism: previously a
fixed address-space region was reserved; now the limit is a soft cap
inside the allocator, and the heap grows lazily via brk/mmap up to that
cap. Carts that omit `heap_size` (or set it to zero) get no heap; linking
libblytc.so without declaring a heap_size is a packer error.

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
- `heap_size` in `cart.build.yaml` is retained and written into
  `.cart.info`. The enforcement mechanism changes from address-space
  reservation to a soft cap inside libblytc.so's allocator.
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

# ADR-0129: Debug and release build variants — carts, RV32 libraries, and frontends

## Status

Accepted — implemented; merged to blyt `main`, CI green.

Carts build release `-O2`/stripped vs debug `-O0 -g`/`.dbg.blyt`; the RV32 libs
and libc++ build release + debug variants (`lib/` + `lib/debug/`); frontends
split into release (`blytplay`, `blyt_libretro`) and debug (`blytdebug`) with the
debug transports gated out of release; the devtool exposes `blyt run` (release)
and `blyt debug` (debug); and the VS Code extension drives debugging via
`blyt debug`. Release `-O2` is bit-identical to debug `-O0` on the determinism
path (ADR-0007 invariant verified).

Landed as ADR-0129 phases 1–6 plus follow-ups: removal of libretro-core
debugging (the shipped core is release-only; debugging is via `blytdebug` and the
debug WASM); and `-O2`-exposed fixes (Lua runtime `fputs`/`fputc`/`__floatundisf`,
`-fno-builtin` for the hand-rolled native lib, and `-fsemantic-interposition` so
the emulated↔native symbol-preemption override survives optimization). Verified
across the full integration suite locally, in the Linux Docker CI-mirror, and on
GitHub CI (including the native RISC-V QEMU trusted-exec gate).

Builds on ADR-0073/ADR-0065 (the long-intended cart `debug` metadata field),
ADR-0120 (runtime libc), ADR-0121 and ADR-0127 (C++ / libc++ and SDK
distribution), and ADR-0112 (load-time security). Relates to ADR-0007
(structural determinism).

## Context

Cart authors need real source-level debugging — stepping into their own code,
the statically-linked libc++, and the runtime libc with accurate source and
unoptimised values — without players ever receiving debug machinery or bloated,
symbol-bearing binaries.

Today there is effectively a single, middling build of everything:

- The shipped RV32 libraries (`libblytc.so`, `libblyt32.so`, `libblyt32lua.so`,
  static `libc++.a`) carry no DWARF. Only the Rust path debugs well, because
  `-Z build-std` rebuilds `core`/`alloc` with `-g -O0` for debug carts.
- The debugger transport is compile-gated (`BLYT_GDB`/`BLYT_DAP`) but lives in
  the same frontend binaries that ship to players.
- Carts are not stripped, and `blyt run --debug` bolts a debugger onto the
  normal runtime rather than being a distinct mode.

The C++ work (ADR-0121/ADR-0127) made the gap concrete: stepping into
`std::string` showed optimised code and no source. This ADR establishes a clean
debug/release split across three layers — **carts, the RV32 libraries, and the
runtime frontends** — so that distributables contain zero debug machinery and a
parallel, SDK-only debug toolchain provides full source-level debugging.

## Decision

### 1. Two build variants of all RV32-targeted code

Every RV32-targeted library is built in two variants that differ **only** in
optimisation and debug info — never in semantics:

- **Debug:** `-O0 -g` (DWARF), unstripped.
- **Release:** optimised; libc++ as fat LTO objects (ADR-0127); fully stripped
  (DWARF **and** `.symtab`/`.strtab`).

This applies to the static libraries (`libc++.a`, `libc++abi.a`) and the dynamic
libraries (`libblytc.so`, `libblyt32.so`, `libblyt32lua.so`).

**Determinism invariant (load-bearing).** Both variants carry the *same* strict
IEEE / determinism flags (ADR-0007: `-ffp-contract=off`, `-fno-fast-math`,
`-frounding-math`, `-fsignaling-nans`, `-fwrapv`). Only `-O`/`-g`/LTO differ.
Consequently a debug run and a release run produce **bit-identical results**,
and the debug and release libraries are **ABI-identical** — unlike, say, MSVC's
debug vs release CRTs. This is what makes mixing variants (below) safe, and it
must be preserved: optimisation level may never change numerical behaviour.

### 2. Carts: naming, DWARF, metadata, verification

- A **debug cart** retains DWARF and is named `foo.dbg.blyt`. A **release cart**
  is fully stripped and named `foo.blyt`.
- The `.dbg` suffix is a **tooling/human convenience only**. The runtime must
  **not** infer build type from the filename.
- Build type is recorded in cart metadata: add a `debug: bool` field to
  `cart.info` (`schemas/cart_info.fbs`). This is the field ADR-0073 and ADR-0065
  already describe but never landed; this ADR is its authoritative specification.
  The packer sets it.
- **Debug verification.** A debug frontend (§4) treats a cart as debuggable only
  if the declared `debug` flag is set **and** DWARF (`.debug_*`) is actually
  present, and **aborts** with a clear message if a debug session targets a cart
  missing either. (A declared-only check would let a mislabelled release cart
  attach and then offer no symbols; requiring real DWARF avoids that.)
- Debug carts are **dev-only artifacts** — larger and symbol-bearing — and are
  not distributed. The release cart is the distributable.
- DWARF sections are non-`SHF_ALLOC`: they enlarge the cart *file* but are not
  mapped at runtime, so the 16 MB per-cart memory budget (ADR-0120) is
  unaffected. The load-time section allowlist already permits `.debug_*` and
  `.symtab`/`.strtab` (ADR-0112; `runtime/host/src/libblyt/cart_load.c`), so
  debug carts pass validation unchanged.

### 3. Library selection is path/registry based

The host dynamic linker (`open_lib`/`dynlink` in
`runtime/host/src/libblyt/cart_run.c`) resolves a DT_NEEDED soname from the
embedded library registry first, then from `BLYT_LIB_DIR`, taking the first
match with no version or hash checks. Debug and release variants share the same
sonames, so selection is purely a function of **which registry/directory the
frontend uses** — the cart never encodes the choice.

Because the variants are ABI- and result-identical (§1), **running a debug cart
against release libraries is fully supported.** A non-SDK runtime, which ships
only release libraries, runs a debug cart correctly; the author simply cannot
step into the runtime libraries there.

Note libc++ is **static**, so a cart's build type is intrinsic to its static
portion: a debug cart embeds debug libc++ (its DWARF travels inside the cart,
exactly as Rust std does via build-std), a release cart embeds the fat-LTO,
stripped libc++. Only the **dynamic** libraries are swappable at run time.

### 4. Split runtime frontends — release vs debug as hard artifacts

Debug capability is moved out of the shipped runtimes entirely:

- **Release frontends (distributable, zero debug machinery):** `blytplay`,
  `blytplay.wasm`, `blyt_libretro.so`. No GDB stub, no DAP relay, no debug-lib
  loading path. Debugging cannot be enabled in a shipped runtime — consistent
  with the security posture of ADR-0112.
- **Debug frontends (SDK-only, always debug):** `blytdebug`, `blytdebug.wasm`.
  These always start the GDB/DAP transport, load the debug libraries, and
  enforce the debug-cart verification of §2. They behave roughly as
  `blyt run --debug` does today.

The frontend's identity *is* the mode, mirroring the cart side: **debug cart ⇄
debug frontend, release cart ⇄ release frontend.** Debug and release frontends
are built from the **same source with two CMake build configurations**; the
debug-only code stays behind the existing `BLYT_GDB`/`BLYT_DAP` compile guards,
and shared logic is refactored into a common module only if `#ifdef` density
warrants it. Because the WASM frontend embeds its libraries at compile time and
has no `BLYT_LIB_DIR`, `blytdebug.wasm` **embeds the debug libraries**.

### 5. Distribution

- **Debug dynamic libraries ship in the SDK only.**
- **Release dynamic libraries ship with the runtime** (and in the SDK, for
  building/running release carts locally).
- **Both libc++ variants ship in the SDK** (static → chosen at cart link time;
  libc++ is never part of the runtime).
- **Debug frontends ship in the SDK only**; release frontends are the
  distributables.

### 6. Libretro is release-only

There is one libretro core, `blyt_libretro.so` — release, embedding release
libraries, with no debug machinery. The `_libretro` filename suffix and the
`retro_get_system_info().library_name` stay single, so there is no core-identity
collision in RetroArch.

Debugging is **never hosted inside RetroArch**: it lives in the dev frontends
(`blytdebug`/`blytdebug.wasm`), which own a GDB/DAP transport that the libretro
core lifecycle does not provide (RetroArch drives the core; there is no clean
place for the core to stand up a debug server). A debug libretro core
(`blytdebug_libretro.so` with a distinct `library_name` and a core-hosted GDB
transport) is explicitly **out of scope** and would be a future ADR if a
debug-under-RetroArch use case ever materialises.

`blyt_libretro.c` remains the shared core logic compiled into `blytplay`,
`blytdebug`, and the standalone core; the libretro distribution target simply
never enables the debug compile flags.

### 7. CLI: `blyt debug`

Rename `blyt run --debug` to **`blyt debug`**. `blyt run` serves the release
frontend; `blyt debug` serves the debug frontend (`blytdebug.wasm`), starts the
DAP/GDB relays, and enforces debug-cart verification. The VS Code debug
configurations switch from `blyt run --debug` to `blyt debug`.

## Consequences

- Players receive runtimes with no debugger surface at all; the debug toolchain
  is an SDK-only concern. This is a security improvement (ADR-0112) and shrinks
  the distributables.
- A consistent, symmetric mental model: debug builds (carts, libs, frontends) on
  one side; release on the other; mixing across the boundary is safe by the
  determinism invariant.
- The SDK roughly doubles its RV32 library footprint (debug `-O0 -g` libc++ is
  substantially larger). Debug libraries are dev-time only and may be packaged
  as an optional/secondary download.
- Implementation surface (separate follow-up work):
  - `schemas/cart_info.fbs`: add `debug`; regenerate readers; the packer sets it
    (`devtool/src/build.rs` `CART_INFO`/`finalise_cart`); `cart_load.c` reads it.
  - `devtool/src/build.rs`: `.dbg.blyt` output naming for debug; conditional
    stripping (release strips DWARF + `.symtab`/`.strtab`); fat-LTO-objects
    libc++ in release (ADR-0127).
  - `cmake/blyt_sdk.cmake`: build debug + release variants of the RV32 libraries
    into separate SDK locations.
  - Frontends: add `blytdebug` / `blytdebug.wasm` CMake targets (same source,
    debug flags on; debug WASM embeds debug libs); keep `blyt_libretro.so`
    release-only.
  - `devtool/src/{main.rs,run.rs}`: add the `blyt debug` subcommand; update the
    VS Code extension's debug configurations.
- Until implemented, the status quo holds: a single library build, carts carry
  whatever `--debug` produced, and `blyt run --debug` provides debugging.
</content>

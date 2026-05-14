# ADR-0126: Implementation repository structure

## Status

Accepted.

## Context

The runtime, SDK, tooling, and test infrastructure must be developed
together. ADR-0109 established the source set model for cart author
projects; this ADR establishes the layout of the implementation
repository — the monorepo that contains the runtime libraries,
frontends, packer, SDK, and tests.

ADR-0105 identified the library components (`libblyt`, `libblytcommon`,
`libblytcommonlua`, `libblyt32`, `libblyt32lua`). ADR-0120 established
`libblytc.so` as a runtime-provided shared library. ADR-0121 established
`libc++.a` as an SDK artifact for C++ cart authors. ADR-0109 established
`blytbuild` as the cart build tool and driver.

The `fc32` repository (where this ADR lives) is an ADR and design archive.
Production code lives in a separate implementation repository.

Key constraints:
- The libretro buildbot is a strategic distribution target; it expects a
  Makefile entry point (ADR-0033 / ADR-0034).
- The runtime is C for libretro ABI compatibility.
- The packer (`blytbuild`) is Rust/Cargo.
- FlatBuffers schemas for the cart binary format are shared between the C
  runtime (reader) and the Rust packer (writer).
- Modified upstream dependencies (musl for `libblytc`, LLVM libc++ for
  C++ carts) must be maintainable against upstream without patch-file
  workflows that create checkout friction.

## Decision

### Repository identity

A repository named `blyt` hosts all implementation. The `fc32` repository
remains as an ADR and design archive; no production code lives there.
The name `blyt` is the project identity established in ADR-0105.

### Top-level directory layout

```
blyt/
├── CMakeLists.txt             # C runtime build root
├── Makefile.libretro          # libretro buildbot entry point (thin shim)
├── schemas/                   # FlatBuffers schemas (shared, C and Rust)
├── include/                   # public headers
├── src/                       # C runtime library source
├── frontends/                 # platform frontends
├── sdk/                       # non-code SDK artifacts (toolchain, linker scripts)
├── blytbuild/                 # Rust Cargo workspace (packer, asset pipeline)
├── tests/                     # runtime unit and integration tests
└── third_party/               # vendored and forked upstream dependencies
```

### Source libraries (`src/`)

Library source directories are named for their output library:

```
src/
  libblyt/           # → libblyt  (host runtime; frontend-facing API in blyt_runtime.h)
  libblytcommon/     # → libblytcommon.so  (audio, state, RNG, resources, lifecycle)
  libblytcommonlua/  # → libblytcommonlua.so  (Lua VM, sandbox, bytecode loading)
  libblyt32/         # → libblyt32.so  (gfx, input, stage — Blyt32-specific)
  libblyt32lua/      # → libblyt32lua.so  (registers blyt32.* Lua tables)
  libblytc/          # → libblytc.so  (runtime-provided C library — ADR-0120)
```

Each directory name is the soname of its output artifact; the mapping is
unambiguous with no mental translation step.

Adding a new variant (e.g. BlyTTY, ADR-0105) adds `src/libblytty/` and
`src/libblyttylua/`. The `libblytcommon/` and `libblytcommonlua/` directories
are unaffected.

### Public headers (`include/`)

```
include/
  blyt.h             # shared cart-facing umbrella
  blyt32.h           # Blyt32 variant umbrella
  blyt_runtime.h     # host/frontend-facing API (not shipped in cart SDK)
  blyt/              # per-subsystem headers
    audio.h
    state.h
    gfx.h
    input.h
    stage.h
    ...
```

All public headers live under `include/`. Internal headers live within
their respective `src/<lib>/` directory and are not installed.

### Frontends (`frontends/`)

```
frontends/
  sdl/       # SDL2 frontend → blyt binary (native runner)
  libretro/  # libretro adapter → blyt_libretro.so
  wasm/      # Emscripten frontend
```

### SDK non-code artifacts (`sdk/`)

```
sdk/
  toolchain/   # CMake toolchain files (RV32IMAFC cross-compilation, K230D, etc.)
  linker/      # linker scripts for cart ELFs
```

`sdk/` contains only non-code artifacts — configuration and scripts that
ship with the SDK but are not compiled C source. All C source lives under
`src/` without exception. `libblytc` source is in `src/libblytc/` (not
under `sdk/`) because it is C source built by the same CMake toolchain as
all other runtime libraries.

### Packer toolchain (`blytbuild/`)

`blytbuild/` is a top-level Cargo workspace. It is not nested under a
`tools/` directory: the packer is a primary product component comparable
in scope to the runtime, not a development utility. It is the cart build
driver (ADR-0109) and asset pipeline (ADR-0088).

### Tests (`tests/`)

```
tests/
  unit/          # C unit tests for runtime components
  integration/   # end-to-end: blytbuild + runtime
```

**Integration tests** are written in Rust. The `assert_cmd` crate
provides a clean API for invoking and asserting on CLI binary outputs;
the `tempfile` crate provides temporary filesystem isolation for test
cart projects. A typical integration test: write a minimal cart source
tree into a `tempdir`, invoke `blytbuild pack` on it, invoke the runtime
against the resulting `.blyt`, assert on output and exit code.

The integration tests live as a Cargo test binary — either as a crate
within the `blytbuild/` workspace or as a separate top-level crate at
`tests/integration/`. The monorepo makes both `blytbuild` and the
compiled runtime available in the same checkout without cross-repository
coordination.

### Third-party dependencies (`third_party/`)

```
third_party/
  rv32emu/          # RISC-V interpreter (git submodule — fork likely)
  lua/              # Lua 5.4 source
  libopenmpt/       # XM/IT tracker decoder
  zstd/             # per-resource compression
  libretro-common/  # MIT libretro utilities (git submodule)
  libcxx/           # LLVM libc++ fork (git submodule — see ADR-0127)
```

**rv32emu is used in two independent build contexts.** It is statically
linked into `blytbuild` as a C library (for running the embedded RV32IMAFC
`luac` binary during Lua compilation), and compiled separately by CMake into
the runtime (for cart execution on emulated targets). The RV32IMAFC `luac`
binary produced by the CMake build is embedded into `blytbuild` at Cargo
compile time via `include_bytes!()`; the CMake build must complete before
`blytbuild` can be compiled. See ADR-0109 for the Lua compilation mechanism.

**Modified dependencies are maintained as forks, referenced via git
submodule.** Patch-file workflows (checking in `.patch` files applied at
build time) are not used: they require a patch-application step after
checkout and create friction for contributors. A fork is always in a
buildable state after `git submodule update --init`. Upstream updates are
pulled via `git fetch upstream` and rebased in the fork; the submodule
pointer is bumped in a single commit to the `blyt` repository.

Dependencies used without modification are referenced as submodules
pointing to upstream directly.

### Build systems

**C runtime: CMake.** Two reasons:

1. The libretro buildbot expects a Makefile entry point (`Makefile.libretro`).
   A thin shim invokes cmake then make, translating buildbot environment
   variables (`CC`, `CROSS_COMPILE`, `platform=`, etc.) to cmake arguments
   and a toolchain file. CMake's pattern for this shim is established in the
   libretro ecosystem; a Meson shim is less common and less tested in that
   environment.
2. CMake toolchain files are the standard cross-compilation interface in the
   embedded and console game development world. The K230D Buildroot target
   and the SDK's RV32IMAFC toolchain both express cross-compilation via
   toolchain files; the same files serve both the runtime's build and the
   SDK distributed to cart authors.

`Makefile.libretro` contains no build logic — it is a translation layer
only. All build logic lives in `CMakeLists.txt`.

**Rust tooling: Cargo.** The `blytbuild/` workspace uses Cargo, invoked
independently by CI. There is no meta-build system unifying the two; they
are separate jobs.

### FlatBuffers schemas (`schemas/`)

The cart binary format uses FlatBuffers for `.cart.info` and `.cart.config`
ELF sections. Schemas are shared between the C runtime (which reads them)
and the Rust packer (which writes them). They live at the repository root
in `schemas/`:

```
schemas/
  cart_info.fbs
  cart_config.fbs
```

Generated code (C headers via `flatc`, Rust modules via `flatc`) is
produced at build time and not committed. CMake generates C headers into
`build/`; Cargo generates Rust modules into `OUT_DIR`.

### Non-FlatBuffers shared format definitions

Binary layouts not expressed in FlatBuffers schemas (resource index format,
state buffer layout encoding) have the C header in `include/` as the
canonical definition. The Rust packer maintains parallel `#[repr(C)]`
struct definitions. CI tests assert `std::mem::size_of` and field offsets
match the C layout, catching divergence at compile time without requiring
bindgen at build time.

## Consequences

- All C source lives under `src/`; all non-code SDK artifacts under `sdk/`.
  The rule is consistent with no exceptions.
- The `fc32` ADR repository is preserved as a design archive. Implementation
  history starts fresh in `blyt`.
- Monorepo means format changes that touch both C runtime and Rust packer
  (e.g. a new cart.info field requiring a schema update) are atomic commits.
  No cross-repository release coordination during initial development.
- `Makefile.libretro` is a durable thin interface. It does not contain
  build logic and does not need to change when CMake build targets are
  reorganised.
- Modified upstream dependencies are maintained as forks. Contributors can
  clone the `blyt` repository, run `git submodule update --init`, and build
  without any additional steps.
- Integration tests run on any platform where the CMake and Cargo builds
  succeed; no platform-specific test infrastructure is required.
- Adding BlyTTY or Blyt3D variants adds source directories without touching
  the shared library tree, validating the variant boundary established in
  ADR-0105.

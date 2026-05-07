# ADR-0109: Cart source set model and build directory conventions

## Status

Accepted — amends ADR-0067 (Lua compilation always runs; emulator-based luac);
amends ADR-0073 (default source directory per language).
Amended by ADR-0110 (test frameworks, TAP protocol confirmed, two-tier
host + emulator test execution).

## Context

Cart projects contain source in multiple languages (C, Lua, Rust), vendored
libraries, generated API bindings, and assets. A consistent model is needed
for how source files are grouped, where build artifacts land, and how the
build driver invokes each language's toolchain.

ADR-0088 established the asset pipeline and the `build/` staging directory.
This ADR extends those conventions to compiled source artifacts and generalises
the layout across all artifact types.

## Decision

### Source sets

A **source set** is a named, independently-built collection of source files in
a single language. Each source set has a designated source directory and a
corresponding subdirectory under `build/`. Intermediate files are isolated
per source set — this prevents filename conflicts and enables per-set
incremental up-to-date checking without a global dependency graph.

### Default source sets

`blytbuild new` creates the following source sets, one per declared language:

| Source directory | Build directory | Language |
|---|---|---|
| `src/game/c` | `build/game/c` | C game logic |
| `src/game/lua` | `build/game/lua` | Lua game logic |
| `src/game/rust` | `build/game/rust` | Rust game logic |

This amends the ADR-0073 default: the canonical source location for each
language is `src/game/<lang>`, not the bare `src/` fallback in the original
ADR-0073 text. The `src/` fallback is removed; non-default layouts require an
explicit `sources:` declaration in `cart.build.yaml`.

### Library source sets

Additional source sets for helper libraries or vendored dependencies are
declared in `cart.build.yaml` under `src/lib/<name>`, building to
`build/lib/<name>`:

```yaml
sources:
  lib:
    - name: physics           # src/lib/physics → build/lib/physics
    - name: zlib
      path: vendor/zlib       # explicit path override
      build: external
      command: >
        cmake -S {source} -B {output}
        -DCMAKE_TOOLCHAIN_FILE={toolchain}
```

### Test source sets

Test source sets live under `src/test/<name>` and build to
`build/test/<name>`. Default sets created by `blytbuild new`:

| Source directory | Build directory |
|---|---|
| `src/test/c` | `build/test/c` |
| `src/test/rust` | `build/test/rust` |

Tests are compiled to RV32IMFC ELF and run inside the fc32 emulator (see
§Tests below).

### Generated API bindings

Packer-generated language bindings (resource constants, state field handles,
handler IDs) go to `build/blyt/` and are inputs to the game source sets.
They must be generated before any language's compilation step.

| Generated output | Language |
|---|---|
| `build/blyt/c/` | C headers |
| `build/blyt/rust/` | Rust modules (Cargo `OUT_DIR`) |
| `build/blyt/lua/` | Lua modules |

### Full build directory layout

```
build/
  packer-state.json       ← incremental build tracking (ADR-0088)
  resource-id-index       ← runtime resource lookup (ADR-0088)
  cart.info.fb
  cart.config.fb
  blyt/
    c/                    ← packer-generated C headers
    rust/                 ← packer-generated Rust modules
    lua/                  ← packer-generated Lua modules
  game/
    c/                    ← compiled C objects
    lua/                  ← copied Lua source + compiled bytecode
    rust/                 ← Cargo output (CARGO_TARGET_DIR)
  lib/
    <name>/               ← one subdirectory per declared library source set
  test/
    c/
    rust/
  assets/
    resources/            ← processed asset staging (ADR-0088)
```

### C include paths across source sets

Library source sets export their public headers to all C source sets in the
cart via convention: `src/lib/<name>/include/` is automatically on the
compiler include path if it exists. No declaration is required on the game
source set.

For external builds, the include directory may be under `build/` rather than
`src/`. The lib sourceset declares it explicitly:

```yaml
sources:
  lib:
    - name: zlib
      build: external
      command: cmake -S {source} -B {output} -DCMAKE_TOOLCHAIN_FILE={toolchain}
      export_include: build/lib/zlib   # cmake places headers here
    - name: physics
      # src/lib/physics/include/ auto-detected — no declaration needed
```

All exported include paths from all declared lib source sets are available
to all C source sets in the cart — game, test, and other lib source sets.
There is no per-consumer dependency declaration. This means that internal
helper libraries must be declared as lib source sets under `src/lib/<name>`
(not embedded under `src/game/c/`) to participate in the header export
convention and be consumable by other libs. For v1 this is sufficient: game
carts are small, include paths are cart-scoped, and any edge case (e.g. a
lib whose headers must not be visible to other libs) can be handled via the
source set's `extra_flags`. A proper dependency graph between source sets is
a post-v1 concern.

**Build ordering.** Because external lib source sets write their headers to
`build/` during the build step, lib source sets must be built before game
source sets. The fixed phase order is:

1. Generate `build/blyt/` API bindings
2. Build all lib source sets (simple and external)
3. Build game source sets
4. Build test source sets

No dependency graph is required to derive this ordering — it is a fixed
consequence of the "all lib headers visible to all game code" model.

### C build mechanisms

**Simple** (default for `src/game/c` and `src/lib/<name>`): the builder finds
all `.c` files in the source directory recursively, compiles each to a `.o`
in the corresponding build subdirectory using the RV32IMFC cross-compilation
toolchain, then links into a static archive. Source sets may declare
additional compiler flags; the cross-compilation flags are always present and
cannot be overridden.

**External**: the builder execs a declared command, substituting `{source}`,
`{output}`, and `{toolchain}` template variables. The external command must
write its outputs to the designated build subdirectory. Intended for vendored
libraries with their own build systems (CMake, Meson, plain Make).

Most C library build system "logic" is feature detection and platform
branching — it collapses to a fixed set when the target is known. For the
common case (e.g. zlib, freetype, small game utility libs), the external
mechanism is just cmake/make invoked with a toolchain file and a handful of
`-D` flags. Real code-generation steps (protobuf, some codecs) are the
exception.

**Configure-time generated headers.** For libraries that generate a header
at configure time and compile the rest with plain C, the preferred approach
is: run the configure step once, commit the generated header to source
control, treat the remainder as a simple source set. A comment in the header
names the inputs that would require regeneration.

### Rust: Cargo integration

The builder sets `CARGO_TARGET_DIR` to the source set's build directory and
passes `--target riscv32imfc-unknown-none-elf` plus any required linker env
vars. Packer-generated Rust modules (from `build/blyt/rust/`) are consumed
via `include!` in the cart's `build.rs` (ADR-0108).

Each Rust source set is an independent Cargo workspace. Multiple Rust source
sets are not merged; if a cart declares two, dependencies may be duplicated.
This is acceptable for v1 — game carts almost always have one Rust source set.

### Lua: compilation via emulated luac

Standard `luac` produces bytecode for the architecture it runs on.
Cross-compilation is not supported: a host-native `luac` cannot produce valid
fc32 bytecode (bytecode format encodes endianness and integer sizes).

The packer compiles Lua source by running the fc32-native `luac` binary
inside the fc32 emulator as a build step. This guarantees bytecode
compatibility by construction — the same luac version and architecture that
the runtime uses performs the compilation.

**Amendment to ADR-0067:** luac compilation runs for **both** debug and
release builds, not only release. Debug carts still ship Lua source text
(ADR-0067 unchanged on that point); the compilation step runs anyway to
validate that the source compiles. Compiled bytecode and source text are both
written to `build/game/lua/` — bytecode for potential runtime use, source for
debug tooling (stack traces, DAP step-debugging).

The fc32 emulator is therefore a **build-time dependency** for any cart with
Lua source, not only a dev/run dependency.

### Tests under the emulator

Test binaries are compiled for RV32IMFC using the same cross-compilation
toolchain as game code. The test runner:

1. Builds the test binary.
2. Executes it inside the fc32 emulator.
3. Collects results via TAP output (ADR-0110).

Running tests on the emulator ensures results reflect the actual target
environment. Emulator bugs or ABI divergences surface during development,
not at cart runtime on real hardware.

A host-compiled test mode (`blytbuild test --host`) also exists for fast
iteration; it uses the same source set but stubs the blyt API boundary and
does not require the emulator. See ADR-0110 for framework choices, the blyt
API mock surface, and the full two-tier execution model.

### Debugging via GDB stub

Debugging of emulator-mode tests uses a GDB remote stub exposed by the fc32
emulator. The cross-compilation toolchain includes GDB or LLDB with RV32
support. VS Code attaches via the existing DAP connection (ADR-0044/0045).
Host-mode tests debug via native lldb/gdb with no remote stub required.

## Consequences

- All build artifacts are under `build/` — one directory to inspect,
  gitignore, and clean. This is a superset of the asset staging established
  in ADR-0088; the two layouts are consistent.
- Per-source-set build subdirectories prevent filename collisions and scope
  incremental rebuilds to the changed set.
- The two C mechanisms cover the expected library landscape without requiring
  a full CMake dependency for simple libraries or bypassing it for complex ones.
- The emulator-as-luac approach eliminates cross-compilation patching at the
  cost of making the emulator a build-time dependency — acceptable given the
  emulator is already required for the dev loop.
- Running tests on the emulator catches target-environment issues early; the
  emulator startup overhead is acceptable for the small test suites expected
  under the cart size class constraints.

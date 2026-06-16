# Implementation sequence

This document records the agreed implementation order for the `blyt`
repository. The high-level design's §18 "Suggested Early Work" is the
broader context; this document is the concrete sequence, including the
gates that control when the API surface may expand.

The guiding principle: the novel parts of this project are the cart format,
cross-target execution, and security enforcement. Those are proven first
with a minimal real function before the game API is built out. Debugging
works before the API grows. State buffers are the first real game API
because they underpin every other differentiating feature.

---

## Phase 1 — Repository scaffold

**Deliverables:**
- `blyt` repository created with directory structure per ADR-0126
- `CMakeLists.txt`: skeleton that compiles nothing but has the target
  structure in place (`libblytcommon`, `libblyt32`, `libblytc`, etc. as
  empty targets)
- `devtool/`: Cargo workspace for the `blyt` dev tool — stub binary that prints its version
  and exits
- Submodules registered: `rv32emu`, `lua`, `libopenmpt`, `zstd`,
  `libretro-common`, `libcxx`
- `schemas/cart_info.fbs`, `schemas/cart_config.fbs`: initial FlatBuffers
  schemas; codegen step wired into CMake
- Header stubs: `blyt.h`, `blyt32.h`, `blyt_runtime.h` — present but
  declaring nothing yet
- CI: Linux x86-64 build passes on an empty project

---

## Phase 2 — Cart format foundation

**Deliverables:**
- ELF cart loader in `libblyt`: load a `.blyt` cart file, parse
  `.cart.info` and `.cart.config` sections via the FlatBuffers schemas
- Load-time security checks (ADR-0112): validate `e_machine`, `e_flags`,
  `EI_OSABI = ELFOSABI_NONE`, reject unknown ELF sections, enforce
  `DT_NEEDED` allowlist
- The runtime can load a valid cart and reject malformed or malicious ones;
  no cart code executes yet

---

## Phase 3 — `blyt_console_debug` + SDL2 + emulated execution

**Deliverables:**
- `rv32emu` integrated into the runtime for emulated cart execution
- ECALL mechanism: the cart-to-runtime call interface
- `blyt_console_debug(const char *s)` declared in `blyt.h`, implemented as
  an ECALL handler; output goes to the frontend's log/console
- SDL2 frontend: window, event loop, audio output stub, wired to `libblyt`
- `blyt`: pack a minimal C cart — a single source file that calls
  `blyt_console_debug` and exits; no assets, no state, no other API
- rv32emu security: ecall allowlist enforced; non-permitted ecalls trap

**Gate:** a C cart calls `blyt_console_debug`, output appears in the SDL2
frontend. Malicious ecalls are trapped.

---

## Phase 4 — libretro adapter

**Deliverables:**
- libretro adapter in `frontends/libretro/`: implements `retro_*` interface
  over `libblyt`
- `libretro-common` integrated
- `blyt_console_debug` output routed via `retro_log_printf`
- Tested in RetroArch on desktop

**Gate:** C cart calling `blyt_console_debug` works in RetroArch.

---

## Phase 5 — WASM / Emscripten

**Deliverables:**
- Emscripten build of the runtime in `frontends/wasm/`
- Browser frontend: HTML shell, canvas, `blyt_console_debug` output to
  browser console
- `blyt run` orchestrates the WASM build and serves it locally
- DAP and GDB server transport design settled: WebSocket endpoints exposed
  by the WASM runtime (required for Phase 8's primary debugging path)

**Gate:** C cart calling `blyt_console_debug` works in the browser.

---

## Phase 6 — Trusted native exec on QEMU

**Deliverables:**
- LP64 launcher binary: the hardware frontend that execs the ILP32 cart
  ELF as a native process
- `libblytc.so` built and deployed: runtime-provided restricted musl,
  resolved by `ld.so` alongside `libblyt32.so`
- seccomp phase-1 filter (installed by launcher before exec) and phase-2
  filter (installed by `libblyt32.so` constructor): allowlists from
  Spike S empirical data
- `blyt_console_debug` output via the permitted `write(2)` syscall
- QEMU environment: Linux kernel with ILP32 ELF loader support (as per
  Spike S setup)

**Gate — targets:** all four targets pass:
- SDL2 + emulated (Phase 3)
- libretro (Phase 4)
- WASM (Phase 5)
- Trusted native exec on QEMU (this phase)

**Gate — security:** on the QEMU target, a cart that attempts a blocked
syscall (`socket`, `execve`) is killed by seccomp; `blyt_console_debug`
(via `write`) succeeds.

The targets gate closes here. No game API expansion until all four pass.

---

## Phase 7 — All languages

**Deliverables:**

**Rust:**
- Rust SDK crate in `devtool/`: wraps `blyt_console_debug`, provides
  `#[global_allocator]` backed by `libblytc.so`'s `malloc`
- `blyt` packs a minimal Rust cart
- Rust cart calls `blyt_console_debug` on all four targets

**Lua:**
- Lua 5.4 VM embedded in `libblytcommonlua`
- ECALL-based Lua host API (ADR-0025): create state, load source, call
  functions
- `libblyt32lua`: registers `blyt32.*` and `blyt.*` Lua tables;
  `blyt32.debug.print` wraps `blyt_console_debug`
- Lua cart template: native ELF shim that initialises the Lua state and
  forwards lifecycle callbacks
- `blyt` packs a minimal Lua cart
- Lua cart calls `blyt32.debug.print` on all four targets

**C++:**
- SDK toolchain verified: modified `libc++.a` links cleanly, `-fno-exceptions
  -fno-rtti` enforced, `std::random_device` terminates
- `blyt` packs a minimal C++ cart
- C++ cart calls `blyt_console_debug` on all four targets

**Cross-language calling:**

The hybrid cart model (ADR-0111) allows native code and Lua to coexist in
one cart; the cross-language boundary must be validated before the API
grows, not discovered later.

- **Lua → C**: a Lua cart calls a C function exposed via `extern "C"`,
  which calls `blyt_console_debug`. Validates that C libraries can be
  called from Lua carts.
- **Lua → Rust**: a Lua cart calls a Rust function exposed via
  `#[no_mangle] extern "C"`, which calls `blyt_console_debug`. Validates
  the hybrid binding layer (ADR-0111, Spike Q).
- **Lua → C++**: a Lua cart calls a C++ function exposed via `extern "C"`.
  Validates the C++ boundary convention from ADR-0121 (no C++ types at
  the language boundary).
- **Rust → C**: a Rust cart calls a vendored C function via FFI. Validates
  that Rust carts can consume C libraries, the expected pattern for
  physics engines and similar dependencies.
- **C → Lua** (native shim hosting Lua): the Lua cart template itself
  is this case — a native ELF (C or Rust) that creates and drives a Lua
  state via ECALL. Validated implicitly by the Lua cart deliverable above,
  but should be confirmed explicitly as a distinct case.

Each cross-language combination is exercised as a minimal cart on all four
targets. `blyt` must pack hybrid carts (multiple language source sets
in one cart) for this to work.

**Gate:** C, Rust, Lua, and C++ carts each call the debug output function
on all four targets. All cross-language call directions listed above
produce correct output on all four targets.

---

## Phase 8 — Debugging

Primary dev environment is the WASM target: VS Code cannot embed native
windows, so the browser-hosted runtime is the inner loop. DAP and GDB
servers must work well on WASM before the API grows. Other targets follow.

**Lua debugging — DAP:**
- DAP server in the WASM runtime, exposed via WebSocket
- `lua_sethook` for breakpoints, `lua_getinfo` for stack frames,
  `lua_getlocal` for locals
- Primary test: set a breakpoint in a Lua cart, step through it using a
  standalone DAP client connected to the WASM runtime's WebSocket
- DAP wired on SDL2, libretro, and QEMU targets (stdio or TCP transport
  as appropriate per target)

**Native cart debugging — GDB remote:**
- GDB RSP stub in `rv32emu`, exposed via WebSocket in the WASM build
- Enables stepping through C/Rust cart code running inside the emulator
- Primary test: attach a GDB client to the WASM runtime's GDB WebSocket,
  set a breakpoint in a C cart, step through it
- GDB RSP wired on SDL2, libretro, and QEMU targets (TCP transport)

**`blyt run`:**
- Orchestrates the WASM build, serves the frontend, exposes DAP and GDB
  WebSocket endpoints
- No VS Code extension yet; developer connects a standalone client

**Gate:** Lua cart: breakpoint, step, inspect locals via DAP on WASM.
Native cart: breakpoint, step via GDB on WASM. Both work on all targets.

The debugging gate closes here. Game API expansion begins.

---

## Phase 9 — State buffers and codegen

The first real game API: the state buffer system is the foundation for
save state, rewind, netplay, and deterministic replay — the differentiating
features of the console.

**Deliverables:**
- State buffer system in `libblytcommon`: typed field layouts (ADR-0010),
  SOA storage, field-handle access functions, Lua metatable sugar (ADR-0011)
- `blyt` codegen: reads state buffer declarations from `cart.build.yaml`,
  emits:
  - C headers with typed field handle constants (`build/blyt/c/`)
  - Rust modules with field handle constants (`build/blyt/rust/`)
  - Lua modules with field handle constants (`build/blyt/lua/`)
- `.cart.layouts` ELF section: packer writes the typed layout declarations;
  runtime reads and registers them
- Save state: walk tracked state buffer regions, serialize to a flat byte
  buffer (ADR-0125 save file format)
- Load state: deserialize, restore, verify round-trip

**Gate:** a cart declares a state buffer, writes values into it, saves
state, reloads, and reads back the same values — on all targets, in all
languages.

---

## Phase 10 — SDK packaging and integration testing

The API surface at this point (debug output, state buffers, save/load) is
minimal but real. Before expanding it, the SDK is packaged and an
integration test suite is established. This validates the SDK as a product
— not just the runtime as infrastructure — and provides a regression
baseline that grows alongside the API.

**Deliverables:**

**SDK packaging pipeline:**
- Build pipeline assembles the four platform SDK downloads per ADR-0127:
  per-host binaries (Clang, LLD, `blyt`) combined with shared
  RV32IMAFDC target artefacts (headers, `libblyt32.so`, `libblyt32lua.so`,
  `libblytc.so`, `libc++.a`, `BlytToolchain.cmake`, `BlytConfig.cmake`,
  linker scripts)
- Pipeline runs in CI and produces versioned archives

**Integration test suite (`tests/integration/`):**

Written in Rust using `assert_cmd` and `tempfile` (per ADR-0126). The
test sequence has four steps, run per platform:

1. **Build SDKs.** The main CI build (Linux x64) produces the shared
   RV32IMAFDC target artefacts. Each platform's SDK is assembled and
   archived.

2. **Execute SDKs on each platform to build test carts.** On each host
   platform (Linux x64, Linux arm64, Windows x64, macOS Universal), unpack
   the platform's SDK into a clean directory and use only its tools
   (`blyt`, SDK-bundled Clang) to build the same test cart projects
   from source.

3. **Execute the cart.** Run each platform's SDK-built cart in the runtime
   (emulated target in CI) and assert on output and exit code. Correct
   behaviour across all platforms is the primary gate.

4. **Binary identity check (aspirational).** If Clang reproducibility can
   be achieved cleanly — via `SOURCE_DATE_EPOCH`, suppressing host paths
   from debug info, and consistent flags — compare SDK-built carts
   byte-for-byte against a reference cart built by the main build sequence.
   This is a useful property but not load-bearing: the behavioural test in
   step 3 is the hard requirement. Binary identity is attempted and
   recorded; if it proves fragile across platforms it is dropped without
   affecting the gate.

Carts exercised across all steps:
- C, Rust, Lua, C++ carts: debug output, state buffer declare/write/read,
  save/load round-trip
- Hybrid carts: each cross-language calling direction from Phase 7

Cart execution runs against the emulated target (rv32emu) in CI; libretro
and QEMU native targets run selectively where the environment is available.

**Gate:** SDK archives build cleanly for all host platforms. SDK-built
carts are byte-for-byte identical to reference carts on all platforms.
Executed carts produce correct output on all targets. Integration tests
pass in CI.

---

## After Phase 10 — Game API expansion

With the infrastructure proven and the state system in place, the game API
fills out in roughly the order below. Detailed sequencing within this phase
is left to a future planning document.

1. Graphics subsystem: framebuffer, palette, `blyt_gfx_blit` and friends
   (Blyt32 variant)
2. Input subsystem: gamepad, the 4-player model (ADR-0017)
3. Audio subsystem: software mixer, voice groups, tracker playback (libopenmpt)
4. Resource system: ELF resource loading, decompression (zstd), the pin/unpin
   API (ADR-0027)
5. Stage system: entity slots, scene stack, event pub/sub (ADR-0089–0102)
6. Remaining save/prefs APIs: multi-slot saves, preferences persistence
7. libretro save state integration (`retro_serialize` / `retro_unserialize`)
8. Input recording / replay; speedrun mode
9. LAN netplay

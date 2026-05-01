# Early validation spikes

Before building the full runtime, a small set of focused spikes de-risk the
architectural bets that could actually invalidate the design. Everything else
— ELF loading, the full API surface, the packer, debug tooling — is
well-understood engineering. These five questions are not.

Each spike is the minimum code needed to answer its question. None of them
requires a working cart format, a complete API, or any tooling beyond a
compiler and a target device.

---

## Spike A — Interpreter throughput on minimum emulation hardware

**The question:** Can an RV32IMFC interpreter running on a Pi Zero 2 W
(Cortex-A53 quad-core @ 1 GHz) execute a realistic cart workload within the
16.7 ms frame budget at 60 fps?

**Why this is a risk:** The Pi Zero 2 W is the declared minimum emulation
host. If interpreter overhead on that hardware is too high, the floor hardware
story is broken and the design needs revisiting before any code is written.

**What to build:**
- A minimal RV32IMFC interpreter. Use an existing one (Spike, RVVM, or
  similar) if licensing permits; otherwise write the smallest viable
  interpreter that handles RV32IMFC correctly.
- **CoreMark** and **Embench** compiled to RV32IMFC as the primary workloads.
  CoreMark (Apache 2.0) is the standard embedded CPU benchmark and produces
  a widely-understood score; Embench was developed with RISC-V community
  involvement and is more representative of real embedded workloads. Running
  both gives a cross-check and makes results comparable to published
  interpreter benchmarks.
- A harness that reports CoreMark/MHz and Embench scores alongside wall-clock
  frame time.

**Success criterion:** CoreMark and Embench scores high enough that a
non-trivial retro-era game loop leaves at least half the frame budget (~8 ms)
for subsystem overhead not modelled by the benchmarks. Published RV32 soft-
core scores provide a useful reference baseline.

**Secondary output:** The measured effective MIPS figure from this spike
becomes the emulator MIPS cap baked into all emulator builds (ADR-0082).
Until this spike runs, the cap is unset and emulators run at full host speed.

**Platform:** Raspberry Pi Zero 2 W or equivalent Cortex-A53 device.

---

## Spike B — Lua running inside the interpreter on minimum hardware

**The question:** Can Lua 5.4 (`LUA_32BITS`) compiled to RV32IMFC, running
under the interpreter from Spike A, execute a realistic game-loop workload
at 60 fps on a Pi Zero 2 W?

**Why this is a risk:** This is double interpretation: Lua bytecode runs on a
Lua VM that is itself RV32IMFC guest code running under the interpreter. That
stack could easily be an order of magnitude slower than native execution. If
it cannot sustain 60 fps for retro-era game logic, the Lua authoring story
needs fundamental rethinking — perhaps JIT, perhaps a different VM placement,
perhaps tighter limits on what Lua carts can do.

**What to build:**
- Lua 5.4 compiled to RV32IMFC with `LUA_32BITS` defined. This is a
  straightforward cross-compilation; the interesting question is whether
  it runs fast enough, not whether it compiles.
- The **Lua benchmark suite** (lua.org/benchmarks) as the primary workload.
  These test actual Lua VM behaviour — table iteration, closures, string
  operations, numeric loops — and are more representative of real cart logic
  than a synthetic loop. Complement with a hand-written entity-update script
  (position, velocity, collision checks at steady-state allocation) to model
  the specific `update()` pattern carts will use.
- Load and run these via the Lua C API from within the cart binary, inside
  the interpreter from Spike A.
- Same frame-timing harness as Spike A.

**Success criterion:** The Lua workload representative of a non-trivial
retro-era game's `update()` function completes comfortably within the frame
budget on Pi Zero 2 W, with headroom remaining for draw calls (which in the
full runtime will be native-speed ECALLs, not Lua computation).

**Dependency:** Spike A (the interpreter).

---

## Spike C — Lua as a host-provided shared library in the VM

**The question:** Can Lua 5.4 be built as a versioned RV32IMFC shared
library, pre-loaded into a cart's VM address space, and called from cart code
via direct in-VM function calls (not ECALLs)?

**Why this is a risk:** The architecture requires the runtime to own the Lua
version — shipping Lua as a pre-loaded library rather than statically linking
it into each cart. This is an unusual linker/loader arrangement for a
sandboxed environment. The build toolchain, dynamic linking under RV32IMFC,
and the interpreter's ability to handle a pre-mapped shared library all need
to work together in a way that has not been done in this combination before.

**What to build:**
- Build `liblua54.so` targeting RV32IMFC.
- A minimal cart ELF that declares a dependency on this library and calls
  `lua_newstate()`, `luaL_dostring()`, and `lua_close()`.
- Extend the interpreter from Spike A to pre-map the shared library into
  guest memory before cart execution begins, resolving dynamic symbols.
- Verify the cart can call Lua and get correct results.

**Success criterion:** The cart successfully creates a Lua state, executes
a trivial script, and exits cleanly. No correctness is required beyond basic
function — this spike is about architecture feasibility, not performance.

**Dependency:** Spike A (the interpreter, extended with a minimal dynamic
loader).

---

## Spike D — Cross-platform determinism

**The question:** Does the same cart workload, given the same inputs, produce
bit-identical output on two materially different host platforms without
heroic effort?

**Why this is a risk:** Determinism is a structural property of the design —
save states, rewind, replay, and netplay all depend on it. The theory is
sound but FP strictness across compilers, compiler versions, and host
architectures has real-world failure modes. Better to find them on a two-file
spike than after the full runtime is built.

**What to build:**
- Take the workload from Spike B (Lua-in-interpreter with FP arithmetic
  including transcendentals provided by a deterministic musl libm
  implementation). Supplement with **Whetstone** compiled to RV32IMFC: it is
  almost entirely f32 arithmetic and any platform divergence shows up
  immediately in its output.
- After each simulated frame, write a snapshot of the guest state buffer
  (entity positions, accumulated FP results, RNG state) to stdout as a hex
  digest.
- Run on two platforms: x86-64 Linux and ARM64 Linux (Pi or equivalent).
- Diff the output.

**Success criterion:** Bit-identical output across both platforms for a
workload that includes f32 arithmetic, transcendentals (sin, cos, sqrt via
console API), and RNG. Any divergence must be diagnosed and resolved before
proceeding.

**Dependency:** Spike B.

---

## Spike E — Performance and correctness in a WASM container

**Status:** results in [`spike-e-results.md`](spike-e-results.md). Build and
correctness verified across Docker arm64 / Node-WASM / headless Chrome 147.
Desktop measurement: load-bearing Lua workload (`doom_tick`) runs at
~123 ms / tick on Apple Silicon Chrome — **7.4× over the 16.67 ms budget**.
Native-C cart (`doom_tick_c`) runs at ~2 ms — **fits comfortably**.
Mid-range Android phone measurement is the open follow-up.


**The question:** Does the interpreter and Lua-in-interpreter stack, compiled
to WASM via Emscripten, run at acceptable frame rates in a browser on both a
development desktop and a modern mid-range smartphone?

**Why this is a risk:** WASM is a different constraint profile from the Pi.
Raw throughput on desktop is likely fine; the questions are whether Emscripten
is compatible with the runtime's structure (memory layout, threading
assumptions, audio timing), and whether the double-interpretation stack is
survivable on a mid-range Android device running in a browser tab.

Note: a modest Chromebook or mid-range Android may itself be slower than a
Pi Zero 2 W, meaning the MIPS cap (ADR-0082) is not sufficient to guarantee
correct behaviour on those devices. Finding the true WASM floor requires
broader device testing with a realistic workload; the Doom port is the
intended vehicle for that. This spike does not resolve that question — it
only validates that the WASM target is viable in principle. The Pi-derived
MIPS cap stands as the interim floor.

**What to build:**
- Compile the interpreter + Lua workload from Spike B to WASM using
  Emscripten, with a minimal HTML shell that drives a `requestAnimationFrame`
  loop and reports frame times.
- No audio, no real graphics output — a canvas element that updates a counter
  each frame is sufficient.

**Success criterion:** Consistent 60 fps on a development desktop and on a
representative mid-range Android phone (2–3 year old hardware) in a current
Chrome or Firefox browser, with frame time variance low enough that dropped
frames are rare. If the Android result is marginal, document the headroom
precisely — it informs whether WASM on mobile is a first-class target or a
best-effort one.

**Determinism bonus:** Compare the hex-digest output from Spike D against the
WASM run. If they match, cross-platform determinism including the WASM target
is validated essentially for free.

**Dependency:** Spike B, Spike D (for determinism comparison).

---

## Ordering

A → B → C and D and E (B is the dependency for C, D, and E; C, D, E can
proceed in parallel once B is done).

```
A (interpreter)
└── B (Lua-in-interpreter)
    ├── C (shared lib architecture)
    ├── D (determinism)
    └── E (WASM) ← also uses D for comparison
```

Spike C can begin as soon as A produces a working interpreter, since it is
about build/linker feasibility rather than performance. The performance
question in C is answered by B.

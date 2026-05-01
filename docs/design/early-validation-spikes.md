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

## Spike F — Lua compiled directly to WASM (no rv32emu layer)

**Status:** results in [`spike-f-results.md`](spike-f-results.md). Build
and stack end-to-end complete; correctness verified against the same
Spike B benchmark `.lua` files under Node + WASM and headless Chrome 147;
desktop measurements taken on Apple Silicon Chrome 147 — every Lua
benchmark fits the 16.67 ms frame budget, with the load-bearing
`doom_tick` at 1.71 ms mean / 3.40 ms p99 (a 72× / 37× reduction relative
to Spike E's rv32emu+Lua-in-RV32IMFC stack on the same workload, and 36×
faster than the Spike B Docker arm64 native path on the same workload).
Mid-range Android measurement is the open question and is handed off as
a manual run; the desktop projection (2–4× mid-range-Android factor)
suggests realistic per-frame Lua tics fit the budget on phones at both
ends of the projection band. Spike F's recommendation is to **adopt
Lua-direct-to-WASM for the WASM target only**. The cost is the
*sandbox-model* asymmetry ADR-0025 names — different CPU-cap and
memory-failure surfaces than RV32IMFC. Float-determinism, by contrast,
is reachable with build-config discipline (matched musl version,
no `-ffast-math`); Emscripten links musl libm and stdio into the
.wasm so transcendentals and `printf` are *not* host-dependent.
Spike F revises the earlier "no cross-target byte-deterministic
replay" framing accordingly.

**The question:** Does Lua 5.4 compiled *directly* to WASM (no `rv32emu`
layer, no RV32IMFC step — just the Lua VM as a WASM module) hold 60 fps in
a browser on both a development desktop and a mid-range 2–3-year-old
Android phone, on the same cart workloads Spike E measured?

**Why this is a risk:** Spike E confirmed the current architectural choice
(Lua → RV32IMFC → rv32emu → WASM) is two-layer interpretation on the WASM
target: Lua bytecode dispatched by a Lua VM whose machine code is
RV32IMFC dispatched by `rv32emu`'s interpreter compiled to WASM. The
desktop-Chrome `doom_tick` mean is 123 ms/tick (7.4× over 16.67 ms) and
the Lua-vs-C factor widens from 78× on Docker arm64 to ~130× on WASM,
i.e. Lua carts pay roughly 3× the WASM tax that the native-C cart pays.
A 2× constant-factor improvement in V8 would still leave `doom_tick` at
~3.7× over budget. The only plausible path to viable Lua-on-WASM is to
remove a layer of interpretation.

This question is load-bearing for the platform's WASM target story. If
Lua-direct-to-WASM holds 60 fps on desktop and is at-most-marginal on
mid-range Android, the WASM target gets a real Lua authoring story
(at the cost of a per-target divergence in execution model, which the
determinism story has to absorb — see below). If it does not hold,
Lua-on-WASM is structurally infeasible for non-trivial per-frame work
and the WASM target reduces to native-C carts only.

ADR-0025 names this fallback explicitly: "*If the WASM case fails the
budget, the fallback is the host-embedded Lua architecture, accepting the
asymmetric security and debugging model it entails.*" Spike F is the
measurement that lets us choose between honouring that asymmetry and
restricting Lua-on-WASM. ADR-0007 (structural determinism) is the other
side of the trade — Lua-direct-on-WASM means the WASM target's
deterministic ground truth is the Lua VM's behaviour, not the RV32IMFC
ISA, which re-opens the determinism question for the WASM target
specifically. Spike F does not resolve that — it produces the
performance number that decides whether the determinism conversation
needs to happen at all.

**What to build:**
- Lua 5.4 (vanilla PUC, `LUA_32BITS` per ADR-0066) compiled to WASM
  via Emscripten. This is a standard Emscripten port of upstream Lua —
  no rv32emu, no RV32IMFC cross-toolchain, no shared-library loader.
- A thin host shim (C + JS) that exposes `lua_newstate` / `luaL_dostring`
  / `lua_pcall` to the harness, embeds the cart's Lua source or
  precompiled bytecode via `--embed-file`, and runs N ticks of the cart's
  `update()` body emitting the same `FRAME <name> <i> <us>` and
  `SUMMARY <name> ...` lines Spike E's harness consumes.
- Reuse Spike E's measurement harness end-to-end: `web/spike_e.html`,
  `web/spike_e_worker.js`, `web/run_chrome.cjs`, the rAF jank meter, the
  per-tick distribution + p50/p95/p99 + 32-bin histogram. Only the WASM
  module URL changes. Add a side-by-side comparison view if it is cheap
  to do so.
- The same four workloads Spike E measured: `lua_cart_doom_tick`,
  `lua_cart_entity_update`, `lua_cart_binarytrees`, and a native-C
  control. The Lua sources are byte-identical to Spike B/E's; the
  *runtime that executes them* is what differs. Console-API ECALLs that
  are not load-bearing for timing (draw, audio) become no-op JS stubs;
  this spike is a CPU-bound Lua-VM measurement, not a runtime port.

**Success criterion:**
- **Primary (desktop):** `doom_tick` holds within the 16.67 ms budget at
  p99 on Apple Silicon Chrome 147. Spike E's `doom_tick_c` p99 of 4.2 ms
  is the floor for "the WASM tax alone"; Lua-direct should land within a
  small constant factor of that, not within the 30× factor Spike E saw
  for `doom_tick` over `doom_tick_c`. A pass is desktop p99 ≤ ~8 ms,
  giving the 2–4× mobile projection band a fighting chance.
- **Secondary (mobile):** measured run on a mid-range 2–3-year-old
  Android device — same hand-off procedure as Spike E. A pass is
  p99 ≤ 16.67 ms; "marginal" is 16.67–25 ms; anything above is a fail
  for the load-bearing workload.
- **Tertiary (cross-stack comparison):** add a row to Spike E's
  cross-stack table — Docker arm64 / Node 22 + WASM (rv32emu) /
  Chrome 147 + WASM (rv32emu) / **Chrome 147 + WASM (Lua-direct)**.
  The interesting datum is the Lua-direct/Docker-arm64 ratio: if
  Lua-direct on WASM is at-or-better than the rv32emu+Lua stack on
  native arm64, then WASM-Lua-direct is the best-performing Lua surface
  the platform has, and the rv32emu-on-WASM choice becomes purely a
  determinism decision rather than a throughput one.

**What this spike does and does not decide:**
- Decides whether the *performance* objection to Lua-on-WASM (Spike E's
  7.4×) is removed by the architectural change.
- Does not decide whether the architectural change is worth its
  determinism cost — that is an ADR-level decision informed by Spike D's
  cross-platform digest results plus this spike's numbers.
- Does not measure the security model trade-off (sandbox via RV32IMFC
  ISA vs. sandbox via Lua's `_ENV` plus a stripped standard library).
  ADR-0038 (two-layer sandboxing) and ADR-0079 (Lua standard library
  allowlist) already describe what an asymmetric model would look like.

**Dependency:** Spike B (cart workloads — same Lua sources), Spike E
(measurement harness, baseline numbers, the cross-stack comparison
methodology). Spike D's hex-digest comparison is the determinism
follow-up if and when D produces reference digests.

---

## Ordering

A → B → C and D and E and F (B is the dependency for C, D, and E; F
depends on B and E). C, D, E can proceed in parallel once B is done; F
runs after E because it consumes E's harness and baseline numbers.

```
A (interpreter)
└── B (Lua-in-interpreter)
    ├── C (shared lib architecture)
    ├── D (determinism)
    └── E (WASM, rv32emu+Lua) ← also uses D for comparison
        └── F (WASM, Lua-direct) ← contingent on E's miss; uses E's
                                    harness and cross-stack baseline
```

Spike C can begin as soon as A produces a working interpreter, since it
is about build/linker feasibility rather than performance. The
performance question in C is answered by B. Spike F is contingent on
Spike E missing the desktop-WASM budget — if E had passed, F would not
need to run.

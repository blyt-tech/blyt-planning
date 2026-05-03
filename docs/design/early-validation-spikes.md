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

**Status:** PASS. Implementation in [`spikes/spike-c/`](../../spikes/spike-c/);
results in [`spike-c-results.md`](spike-c-results.md). The cart prints
`OK` and exits 0 — `liblua54.so` builds clean, the rv32emu dynamic-loader
patch maps it at `0x08000000`, and PLT calls into Lua return correctly.

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

**Status:** PASS. Implementation in [`spikes/spike-d/`](../../spikes/spike-d/);
results in [`spike-d-results.md`](spike-d-results.md). Three carts
(`lua_cart_det_doom_tick.elf`, `lua_cart_det_entity_update.elf`,
`whetstone.elf`) produce byte-identical per-frame digest streams across
`linux/arm64` and `linux/amd64` Docker builds on Apple Silicon (the
amd64 side via qemu-user). The cart ELFs themselves are byte-identical
across the two cross-toolchains. Float-precision musl 1.2.5
transcendentals ported clean to freestanding RV32IMFC behind a small
header shim.

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

## Spike G — WASM Lua-direct: per-frame CPU budget enforcement

**Status:** PASS. Implementation in [`spikes/spike-g/`](../../spikes/spike-g/);
results in [`spike-g-results.md`](spike-g-results.md). `lua_sethook` with
`LUA_MASKCOUNT` at N=100 adds < 3% overhead to `doom_tick` p99 on Chrome
desktop (observed +0.1 ms vs threshold of 0.34 ms). All N values in the sweep
pass on Chrome; the Node overhead is 50–80% (Node/Chrome diverge ~27× due to
JIT tier-up — Chrome is authoritative). Production values: N=100, budget=16.67 ms.

**The question:** Can per-frame CPU exhaustion be enforced in the
Lua-direct WASM execution model — recommended by Spike F — with
overhead low enough to preserve Spike F's timing numbers?

**Why this is a risk:** Spike F's recommendation to adopt Lua-direct on
the WASM target removes the `rv32emu` instruction-cycle counter that
ADR-0082's MIPS cap relies on. On the `rv32emu` path, the interpreter
counts every guest instruction and throttles via `nanosleep` to simulate
Pi-class throughput and prevent CPU exhaustion. On Lua-direct there is
no instruction counter. Whether an adequate replacement can be enforced
without unacceptable overhead is the unknown this spike resolves.

### Proposed two-tier enforcement policy

**Tier 1 — per-tic step budget (`lua_sethook`).**
At the start of each tic, the host calls `lua_sethook(L, hook_fn,
LUA_MASKCOUNT, N)`. The hook reads wall-clock time and aborts the tic
via `lua_error()` if the frame budget (16.67 ms) is exceeded. This
catches well-behaved carts that simply do too much work per tic. But
`lua_sethook` is not free: every N instructions requires a conditional
branch into the hook, and tight values of N degrade throughput. The
spike determines whether an acceptable N exists.

**Tier 2 — external hard timeout (~1 s watchdog).**
`LUA_MASKCOUNT` fires on function calls and returns; a cart that spins
in a tight loop with no calls (`while true do end`) will not trigger
it. Rather than adding `LUA_MASKLINE` (which fires every source line
and is more expensive), carts that evade Tier 1 are terminated by an
external watchdog — e.g. a `setTimeout` of ~1 s that kills the Web
Worker. A 1 s termination for a cart that is actively bypassing the
budget mechanism is an acceptable outcome. `LUA_MASKLINE` is therefore
out of scope for this spike.

**What to build:**

- Extend the Spike F host shim (`host.c`) to add `lua_sethook` (Tier 1)
  at the start of each tic, with a hook that checks wall-clock elapsed
  and calls `lua_error()` if the frame budget (16.67 ms) is exceeded.
  Parameterise the step count N at build time.
- Re-run Spike F's full timing suite (at minimum: `doom_tick`,
  `entity_update`, `binarytrees`, `mandelbrot`) with step counting
  enabled at a range of N values. Report p50/p95/p99 and overhead
  relative to Spike F's baseline (with no hook). Identify the minimum N
  at which overhead is < 10% of Spike F's `doom_tick` p99 (i.e.
  < 0.34 ms added to the 3.40 ms baseline).

**Success criterion:**

- *Primary:* `lua_sethook` overhead at the chosen N is < 10% of Spike
  F's `doom_tick` p99 (< 0.34 ms added), with the hook enabled.
- *Secondary:* The chosen N and the two-tier policy are documented as
  the production values for the WASM Lua-direct runtime path.

**What this spike does and does not decide:**

- Decides whether Tier 1 (`lua_sethook`) is viable. If overhead exceeds
  the 10% threshold, an alternative Tier 1 mechanism (e.g.
  `Atomics.waitAsync` watchdog replacing the step hook) becomes the
  candidate; evaluating that alternative would be the spike's follow-up.
  Tier 2 (the ~1 s hard timeout) stands regardless of the Tier 1 result.
- Does not resolve the *development-feedback* asymmetry — a critical deferred
  risk. Native CLI builds run Lua under `rv32emu` with ADR-0082's MIPS cap,
  so developers on that toolchain already see Pi-class throughput. But the
  VS Code extension must use a web view, which runs the Lua-direct WASM
  build. That build is ~36× faster than Pi hardware and has no throttle:
  developers iterating in VS Code see systematically wrong frame times during
  their primary edit-run loop. Code that "feels fine" in the extension will
  miss the 16.67 ms per-frame budget on Pi. This gap is addressed by
  Spike G.2 (see below).
- Does not replace Spike A. The Pi target continues to use `rv32emu`
  with ADR-0082's instruction-cap throttle; this spike applies only to
  the WASM Lua-direct execution path.

**Dependency:** Spike F (timing baseline and `host.c` shim codebase).

---

## Spike G.2 — WASM Lua-direct: dev-mode Pi-parity throttle

**The question:** Can `lua_maskline` be used in the VS Code extension's dev
WASM build to slow Lua-direct execution to approximately Pi-class throughput,
giving developers accurate per-frame budget signals in their primary edit-run
loop?

**Why this is a risk:** Native CLI builds already provide Pi-parity
performance via `rv32emu` + the ADR-0082 MIPS cap — no throttle spike is
needed there. But the VS Code extension must use a web view, which runs the
Lua-direct WASM build. That build is ~36× faster than Pi hardware. Without
a credible in-WASM throttle, developers get systematically wrong performance
signals in the tool they use most. Code that "feels fine" in VS Code will
miss budget on Pi.

**Why `lua_maskline`:** `LUA_MASKCOUNT` fires only on function calls and
returns — too coarse for proportional time injection on a per-line basis.
`LUA_MASKLINE` fires on every source line, which is the right granularity.
Its overhead was judged "too expensive" for production in Spike G's Tier 2
discussion, but that was an assumption — the actual cost has never been
measured on WASM. If it turns out to be low, it could serve as the throttle
mechanism in all builds, not just dev. The spike measures first and decides
scope from the data.

**Proposed approach:**

- Build a dev-mode variant of the Spike G host shim with
  `lua_sethook(L, hook_fn, LUA_MASKLINE, 0)` installed at startup.
- The hook busy-waits for a calibrated `us_per_line` duration. The
  calibration factor is derived by running a known workload (e.g.
  `doom_tick`) without the hook to establish a WASM line-execution rate,
  then comparing to Spike B's Pi-projected frame time to compute the
  per-line delay required to equalise them.
- Re-run the full Spike F benchmark suite with the dev hook active. Report
  achieved frame times vs. Spike B's Pi-projected targets.

**Success criterion:**

- Throttled WASM frame times match Spike B's Pi-projected frame times within
  ±25% on `doom_tick`, `entity_update`, `binarytrees`, and `mandelbrot`.
- The hook is stable: no pathological GC pressure and frame-time variance
  stays low enough to be a useful signal.

**Possible outcomes (the spike decides between them):**

1. *Overhead is low enough for production use* — `LUA_MASKLINE` becomes the
   throttle mechanism in all WASM builds, replacing or supplementing Spike G's
   `LUA_MASKCOUNT` Tier 1.
2. *Overhead is acceptable only with artificial delay removed* — still usable
   as a dev-only throttle; production uses Spike G's `LUA_MASKCOUNT` approach.
3. *Overhead is too high and too jittery* — `LUA_MASKLINE` is ruled out
   entirely; fallback is a `LUA_MASKCOUNT`-based throttle with N=1 and a
   per-call delay derived from Spike B's call-frequency data.

**What this spike does and does not decide:**

- Decides whether `lua_maskline` is viable and, if so, in which builds
  (all, dev-only).
- Does not design the dev-UI around the result.
- Does not affect native CLI builds, which already get Pi-parity via
  `rv32emu` + ADR-0082.

**Dependency:** Spike G (host.c shim shape, `LUA_MASKCOUNT` baseline);
Spike B (Pi-projected frame-time targets for calibration).

---

## Spike G.3 — WASM Lua-direct: accumulated-debt Pi-parity throttle

**The question:** Can `LUA_MASKLINE` be used as the dev-mode Pi-parity
throttle if the hook body tracks an *accumulated debt* against an absolute
deadline, instead of attempting a per-line busy-wait?

**Why this is a risk (now):** Spike G.2 ruled out the per-line-busy-wait
variant of `LUA_MASKLINE` because Chrome web-worker `performance.now()`
clamps to ~100 µs resolution, far above every calibrated `ns_per_line` value
the spike needed (756–7,682 ns). The mechanism failure looked complete.

But the failure mode was specifically "ask for 3 µs of busy-wait, get 100 µs
because that is when the timer next ticks." If the hook instead accumulates
the configured per-line debt (`target_ns += ns_per_line`) and only spins
when the cumulative target already exceeds elapsed wall time, the floor
stops being a noise floor and becomes a *quantum*. A frame that wants 327 ms
of total throttle spends ~3,275 spins of one quantum each (~100 µs); the
configured `ns_per_line` controls how often a quantum is spent, not how long
each individual wait lasts.

**Why this is plausible (and not measured by G.2):** the accumulated-debt
design changes *what is being asked of the timer*. `clock_gettime` only
needs to be accurate at a granularity ≥ the quantum it provides — which it
is, by definition. Predicted Stage 4 accuracy under this design is within
~1 % of Pi target across all five benches; G.2's actual error was 13–131×.
The two-order-of-magnitude difference comes entirely from the hook body, not
the underlying primitive.

**Proposed approach:**

- Adapt `spikes/spike-g.2/throttle_host.c` to use the absolute-deadline form:

  ```c
  static uint64_t target_ns = 0;
  static uint64_t start_ns  = 0;

  static void throttle_hook(lua_State *L, lua_Debug *ar) {
      if (!start_ns) start_ns = now_ns();
      target_ns += THROTTLE_DELAY_NS;
      uint64_t deadline = start_ns + target_ns;
      while (now_ns() < deadline) {}
      (void)L; (void)ar;
  }
  ```

- Reset `start_ns` and `target_ns` per outer frame (so timing inside a tic
  is not contaminated by paused-between-tics wall time).

- Reuse Spike G.2's calibration constants directly — `ns_per_line` is a
  property of the workload and Pi target, not the hook body.

- Re-run the same nine-bench Stage 4 sweep on Chrome at D=2,250 / 3,000 /
  3,750 (the same 4× / 6× / 8× Pi-band points G.2 used).

**Success criterion (the same as G.2's, restated):**

- p50 of throttled `doom_tick` and `entity_update` lands within ±25 % of the
  Pi midpoint at the D_MID variant.
- p99 / p50 ≤ 3× on those same benches (jitter is usable as a dev signal).
- The other three measured benches (`binarytrees`, `mandelbrot`,
  `spectral-norm`) are reported but not gating; a single `ns_per_line`
  calibrated to `doom_tick` is allowed to miss them by more than ±25 %.

**Possible outcomes:**

1. *Pi target met within ±25 %, jitter low* — `LUA_MASKLINE` with
   accumulated-debt is the dev-mode throttle. Spike G.2's Outcome 3 is
   superseded; the design-doc fallback to `LUA_MASKCOUNT` N=1 is not needed.
2. *Pi target met but jitter > 3×* — usable only as a coarse "you're in the
   right ballpark" signal, not for tight per-frame budget feedback. Document
   and decide alongside the dev-UI design.
3. *Pi target missed by > ±25 %* — implies hook overhead on the cheap path
   (no-spin lines) eats the budget G.2's calibration assumes. Fall back to
   the non-throttling alternatives in `docs/design/spike-g.2-results.md`
   ("Other recommendations"): synthetic-cost projection or end-of-frame
   projection.

**What this spike does and does not decide:**

- Decides whether the accumulated-debt design clears Chrome's timer floor
  in practice and lands in the calibration band.
- Does not redesign the dev UI or commit to a specific Pi-parity feature.
- Does not affect production WASM builds (Spike G's `LUA_MASKCOUNT` Tier 1
  is unchanged) or native CLI builds (rv32emu + ADR-0082 cap).
- Does not retest the per-line-busy-wait variant — Spike G.2 already
  measured that conclusively.

**Dependency:** Spike G.2 (mechanism failure analysis, line-count data,
calibration constants, Stage 4 chrome runner).
- The vendored Lua sources, Emscripten toolchain, and bench `.lua` files
  are reused via the same chain G.2 used (spike-f → spike-g → spike-g.2 →
  spike-g.3); no re-vendoring.

---

## Spike H — Native RISC-V cart execution and sandboxing

**The question:** Can a RV32IMFC cart ELF run natively on the Milk-V Duo's
RISC-V64 Linux kernel, communicate with the runtime via a shared-memory IPC
library, be adequately isolated from the host system using OS-level
mechanisms, and have its CPU budget capped to match the performance floor
of the minimum emulation host?

**Why this is a risk:** On every other platform, the console API is
surfaced to cart code as a thin library wrapper over ECALLs, and the
RISC-V interpreter intercepts those ECALLs to enforce the sandbox: it
bounds-checks every memory access and is the sole gateway to host
effects. On native RISC-V hardware there is no interpreter — the cart
is a real Linux process. The `ecall` instruction in a native RISC-V
process is a Linux system call; it goes to the kernel, not to the
runtime. A malicious cart can therefore invoke any syscall the kernel
permits, bypassing the console API entirely.

Four questions compound:

1. *RV32 on RV64 Linux.* Carts are RV32IMFC ELF binaries. The Milk-V
   Duo's C906 is RISC-V64. Running 32-bit RISC-V userspace requires
   `CONFIG_COMPAT` in the Linux kernel. Minimal buildroot configs
   frequently omit this; its availability on the Milk-V Duo kernel is
   unverified.

2. *Console API IPC mechanism.* On native hardware, `libconsole` cannot
   issue ECALLs to reach the runtime. The library must communicate with
   the runtime process via a real IPC channel — the most promising
   candidate is a shared-memory ring buffer with futex synchronisation,
   mapped into the cart process before execution begins. The runtime
   services requests from the ring on the other side. This keeps
   per-API-call overhead low (a futex syscall rather than a context
   switch) while maintaining a clean process boundary.

3. *OS-level sandbox.* Because the cart is a native process, the
   security boundary is whatever the OS enforces. A seccomp-bpf filter
   on the cart process can allowlist the exact syscalls it legitimately
   needs: `futex` (for the ring buffer), `clock_gettime` (if not
   handled by the vDSO), and the small set required for process startup
   and shutdown. Everything else — `open`, `socket`, `mmap` of new
   regions, `execve` — is blocked at the kernel level. Combined with
   a mount namespace (no filesystem visibility), a network namespace
   (no networking), and no Linux capabilities, this should make
   escape from the cart process difficult even without an interpreter
   as a first line.

4. *cgroups CPU quota.* The emulated paths enforce a per-frame CPU
   budget via the MIPS cap (ADR-0082) and step counting (Spike G).
   On native hardware, the equivalent mechanism is a cgroup `cpu.max`
   quota on the cart child process. If the runtime can determine the
   hardware's throughput relative to the minimum emulation host (Pi
   Zero 2 W, measured in Spike A) and express that as a CPU fraction,
   the kernel will throttle the cart to Pi-class throughput without any
   instruction-level counting. Whether cgroups v2 is available and
   configurable in the minimal buildroot environment is unverified.

**What to build:**

- **Stage 1 — RV32 execution.** Confirm that a minimal RV32IMFC ELF
  (a "hello world" that calls `write` via ECALL 64) runs natively on
  the Milk-V Duo's kernel. If `CONFIG_COMPAT` is absent, determine
  whether it can be added to the buildroot config and rebuilt, or
  whether an alternative (e.g. compiling carts as RV64 with RV32
  ABI constraints) is needed.

- **Stage 2 — IPC library round-trip.** Build a minimal `libconsole`
  stub: on the cart side, a ring-buffer writer + futex wait; on the
  runtime side, a reader that processes requests and writes responses.
  A simple API call (`console_get_frame_count()`) exercises the full
  round-trip. Measure round-trip latency for a single call and for a
  batch representative of one frame's worth of draw calls.

- **Stage 3 — seccomp + namespace isolation.** Apply a seccomp-bpf
  allowlist to the cart child process after the shared-memory mapping
  is established. Verify that:
  - Legitimate API calls still work (the ring-buffer path does not
    require any blocked syscall).
  - An adversarial cart attempting `open("/etc/passwd", O_RDONLY)`
    receives `SIGSYS` / `ENOSYS`.
  - An adversarial cart attempting `socket(AF_INET, SOCK_STREAM, 0)`
    is likewise blocked.
  - Mount and network namespace isolation prevents any filesystem or
    network access independent of seccomp.

- **Stage 4 — cgroups CPU quota.** Verify that cgroups v2 is
  mountable and configurable in the buildroot environment. Since the
  kernel config is under control, set the CFS bandwidth throttle
  period (`cpu.max` period component) to a fixed 5000 µs. This is
  short enough to enforce the budget in fine slices at any cart fps
  (carts may run slower than 60 fps but not faster), avoiding the
  problem of a period that spans multiple frames at lower rates. Run
  a calibration benchmark (a fixed CoreMark iteration count,
  completing in under 1 second) to determine the hardware's throughput
  relative to the Pi Zero 2 W's Spike A measurement, then set
  `cpu.max` on the cart child process to `<quota_us> 5000`, where
  `quota_us = 5000 × (Pi_MIPS / native_MIPS)`. Run the Spike B game-loop workloads inside the
  cgroup-throttled cart process and confirm they experience the same
  budget pressure as under the emulated MIPS cap.

  The calibration step has three options in ascending order of
  simplicity; the spike should confirm which is viable:
  - *Per-boot measurement:* run the benchmark on every boot and write
    the result to a tempfile. Adds a small but bounded startup cost.
  - *Per-installation measurement:* run once at install time and
    persist the result. Eliminates boot cost; requires a first-run
    setup step.
  - *Baked into the boot image:* for known hardware targets (Milk-V
    Duo, Milk-V Duo S), the CPU quota is a fixed constant included in
    the image, with no runtime measurement at all. Simplest at runtime;
    requires a new constant when porting to new hardware.

**Success criterion:**

- *Stage 1:* A RV32IMFC ELF executes natively on the Milk-V Duo,
  either via `CONFIG_COMPAT` or a documented alternative.
- *Stage 2:* The IPC round-trip latency for a single API call is
  ≤ 10 µs; a simulated frame's worth of calls (100–200 draw
  primitives) adds ≤ 1 ms of IPC overhead against the 16.67 ms
  budget.
- *Stage 3:* All adversarial syscall attempts are blocked; no
  seccomp, namespace, or capability configuration prevents the
  legitimate IPC path from functioning.
- *Stage 4:* cgroups v2 is available; the cart process experiences
  budget pressure consistent with the emulated MIPS cap; at least one
  of the three calibration options is confirmed practical.

**What this spike does and does not decide:**

- Decides the IPC mechanism for the native hardware console API and
  whether OS-level isolation is sufficient to replace the interpreter
  sandbox on that path. A failure in Stage 3 (isolation insufficient)
  or Stage 2 (IPC overhead too high) requires rethinking the native
  execution model — e.g. running carts through the interpreter even
  on RISC-V hardware, accepting the performance cost.
- Decides whether cgroups-based CPU quota is a viable replacement for
  instruction-level MIPS capping on native hardware. A failure in
  Stage 4 (cgroups unavailable or quota too coarse) leaves the
  runtime-side IPC timeout as the only enforcement, accepting that
  well-behaved carts must be developed under emulation.
- Does not specify the full seccomp allowlist for production — that
  is an implementation task once the complete set of runtime-side
  syscalls is known. The spike establishes that the approach is sound.
- Does not replace Spike A. Spike A measures emulation throughput on
  the Pi; this spike is about native execution on RISC-V hardware.
  They address different deployment paths.

**Platform:** Milk-V Duo or Milk-V Duo S (must be real hardware;
QEMU does not exercise the `CONFIG_COMPAT` and seccomp questions in
a representative way).

---

## Ordering

A → B → C and D and E and F (B is the dependency for C, D, and E; F
depends on B and E). C, D, E can proceed in parallel once B is done; F
runs after E because it consumes E's harness and baseline numbers.
G depends on F.

```
A (interpreter)
└── B (Lua-in-interpreter)
    ├── C (shared lib architecture)
    ├── D (determinism)
    └── E (WASM, rv32emu+Lua) ← also uses D for comparison
        └── F (WASM, Lua-direct) ← contingent on E's miss; uses E's
            │                       harness and cross-stack baseline
            └── G (WASM Lua-direct CPU cap) ← depends on F's host.c
                                               and timing baseline

H (native RISC-V sandbox) ← independent; requires Milk-V Duo hardware
```

Spike C can begin as soon as A produces a working interpreter, since it
is about build/linker feasibility rather than performance. The
performance question in C is answered by B. Spike F is contingent on
Spike E missing the desktop-WASM budget — if E had passed, F would not
need to run. Spike G is contingent on Spike F's adoption recommendation
— if F had not recommended Lua-direct for WASM, G would not be needed.
Spike H is independent of the rest of the series; it requires Milk-V
Duo hardware and can run in parallel with any other spike.

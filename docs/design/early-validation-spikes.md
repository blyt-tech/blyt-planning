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

**Status:** FAIL (Outcome 3) — superseded by Spike G.3. Implementation in
[`spikes/spike-g.2/`](../../spikes/spike-g.2/); results in
[`spike-g.2-results.md`](spike-g.2-results.md). The per-line-busy-wait
variant of `LUA_MASKLINE` cannot serve as the throttle: Chrome web-worker
`performance.now()` clamps to ~100 µs, two orders of magnitude above every
calibrated `ns_per_line` value (756–7,682 ns), so the busy-wait collapses
to "wait until the next 100 µs tick" regardless of the configured delay.
Throttled frame times overshoot Pi targets by 13–131× across the suite.
The failure-mode analysis identified the accumulated-debt design that
becomes Spike G.3.

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

**Status:** PASS (Outcome 1). Implementation in
[`spikes/spike-g.3/`](../../spikes/spike-g.3/); results in
[`spike-g.3-results.md`](spike-g.3-results.md). The accumulated-debt hook
clears Chrome's 100 µs timer floor and lands `doom_tick` at 0.4 % accuracy
vs Pi @ 6× midpoint with jitter (p99/p50) of 1.002. Effective ns/line
tracks configured `THROTTLE_DELAY_NS` within ±2.5 % across a 5-bench × 3-D
matrix; cheap-path overhead is ~157 ns/line. Spike G.2's Outcome 3 is
superseded. The mechanism's settled; the open follow-ups (throttle vs
projection feature; renderer-side runtime architecture) are captured in
[ADR-0103](../adr/dev-experience/0103-dev-mode-pi-parity-feedback.md) and
[ADR-0104](../adr/dev-experience/0104-vscode-dev-shell-runtime-architecture.md),
both Proposed.

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

## Spike I — Cart format end-to-end validation

**The question:** Do all four cart configurations — C-only, C with a user
library, Lua-only, and Lua calling a C library — build, load, and execute
correctly across the emulator, WASM, and native RISC-V (QEMU) targets?

**Why this is a risk:** Every prior spike used a bespoke benchmark harness,
not the cart format specified in ADR-0024 and ADR-0025. The first
production-shape load of a cart ELF — with `libconsole.so`/`libconsolelua.so`
pre-mapped, PLT/GOT resolved in guest space, and `.cart.*` sections parsed —
has never run. Four qualitatively different cart configurations exercise
different parts of the format and loader, and running them across three
targets gives the first complete matrix test of the cart format as designed.

**The four cases:**

**Case a — C-only cart.** A minimal native cart: RV32IMFC ELF, one
`DT_NEEDED: libconsole.so`, direct C implementations of `init`, `update`,
`draw`. The simplest possible cart. Validates the core ELF + dynamic-link
round-trip on all three targets.

**Case b — C cart with a user library.** The same as case a, but game logic
is split: a supporting library is compiled with `-ffunction-sections` into
named `.text.mylib` sections and statically linked into the cart binary.
Cart C code calls into the library directly. Validates that the loader
handles named `.text.*` sections correctly and that the section-level
incremental-build convention works in practice.

**Case c — Lua-only cart.** A pure-data ELF: no `.text`, `DT_NEEDED:
libconsole.so libconsolelua.so`, Lua bytecode in `.cart.resources`.
`libconsolelua.so` exports `init`/`update`/`draw`; the cart binary contains
no executable code. Validates the data-container model end-to-end, including
VM init, resource loading, and Lua callback dispatch.

**Case d — Lua cart with a C user library.** A hybrid cart: `.text.mylib`
sections with C binding code (same convention as case b), `cart_lua_modules`
exported, and Lua bytecode in `.cart.resources`. `libconsolelua.so` detects
and calls `cart_lua_modules` before loading Lua bytecode; the binding code
registers a `mylib` module via `package.preload`. Lua code does
`local mylib = require("mylib")`. The binding code uses the Lua C API symbols
re-exported by `libconsolelua.so` (see ADR-0025). Validates the full hybrid
path and the `cart_lua_modules` hook.

**The three targets:**

1. **Emulator.** `rv32emu` pre-maps `libconsole.so` and `libconsolelua.so` as
   RV32IMFC shared libraries into guest memory and resolves PLT/GOT before the
   cart entry point. This is the path established by Spike C; this spike
   validates all four cases through a proper cart container for the first time.

2. **WASM.** Cases a and b (native C carts) run through `rv32emu` compiled to
   WASM — the interpreter and the cart ELF together form the WASM module. Cases
   c and d (Lua carts) use the Lua-direct path: `libconsolelua.so` compiled to
   WASM owns the Lua VM and loads Lua bytecode from the cart data sections. For
   case d specifically, the `cart_lua_modules` symbol is present in the RV32IMFC
   ELF but the WASM Lua-direct path does not run rv32emu; the spike determines
   whether `cart_lua_modules` is called via Emscripten's exported symbol table
   or whether an alternative registration mechanism is required for this path.

3. **Native RISC-V (QEMU).** QEMU user-mode with an RV64 Linux kernel and
   `CONFIG_COMPAT` for RV32 userspace. `libconsole.so` and `libconsolelua.so`
   are real shared libraries mapped into the cart process. Validates the
   dynamic-link model on the native path. QEMU is sufficient for cart-format
   correctness; sandbox isolation (seccomp, namespaces) and cgroups CPU quota
   are Spike H concerns that require real hardware.

**What to build:**
- Four cart ELF binaries (cases a–d), each with a minimal but end-to-end
  workload: a frame counter incremented each `update` and printed via the
  console API in `draw`. Case d additionally calls a C library function from
  Lua and asserts the return value.
- Stub `libconsole.so` and `libconsolelua.so` for each target: just enough API
  surface to support the workload (counter, print).
- A loader per target: the emulator's pre-map path; an Emscripten HTML harness
  for WASM; a minimal Linux process launcher for QEMU.
- For case d on WASM: resolve and document the `cart_lua_modules` registration
  path on the Lua-direct WASM build.

**Success criterion:** All four cases build, load, and produce correct output
on all three targets. Any case/target combination that does not reach a clean
pass is recorded as a design finding — the spike documents what must change.

**What this spike does and does not decide:**
- Decides whether the cart format (ADR-0024, ADR-0025) is end-to-end viable
  across all three deployment targets.
- Decides the WASM execution model for C carts (rv32emu) vs Lua carts
  (Lua-direct) in the actual cart container, and resolves the case d
  `cart_lua_modules` hook on the WASM Lua-direct path.
- Does not implement the full console API surface — stubs are sufficient.
- Does not address sandbox depth (seccomp, namespaces, cgroups) — those are
  Spike H concerns requiring real hardware.

**Dependency:** ADR-0024 (cart format), ADR-0025 (Lua execution model and C
bindings hook). Spike C (pre-map/PLT mechanism). Spikes F and G (Lua-direct
WASM path). Can run in parallel with Spike H.

---

## Spike J — Debugger composition (DAP + GDB stub) under the existing hook load

**The question:** Can the runtime expose DAP for source-level Lua
debugging and a GDB remote serial protocol for source-level native
debugging — concurrently with the per-frame CPU budget (`LUA_MASKCOUNT`,
Spike G) and the dev-mode Pi-parity throttle (`LUA_MASKLINE`,
Spike G.3) — without those hook consumers interfering with each other,
with DWARF unwinding that walks correctly through PLT/GOT into the
pre-mapped `libconsole.so` / `libconsolelua.so` (Spike I's loader
layout), and with the DAP session surviving an ADR-0045 hot reload
without manual user intervention?

**Why this is a risk:** §21 of the design document treats debugger
support as engineering rather than research, but three assumptions in
that framing have not been tested.

First, `lua_sethook` is now load-bearing for two production features
already (Spikes G and G.3) and the DAP server wants it for stepping and
breakpoints. Lua exposes one hook slot per `lua_State`. Whether DAP can
multiplex with the budget hook and the throttle hook — by chaining,
masking, or some other discipline — and still preserve Spike G's
< 3 % overhead and Spike G.3's ±0.4 % calibration accuracy, is unproven.
A naive "DAP just installs its own hook" implementation breaks both.

Second, the GDB stub side relies on DWARF in the cart ELF unwinding
across the PLT into pre-mapped shared libraries that live at runtime-
chosen guest addresses (`0x08000000` and the next 4 KiB-aligned slot
above libconsole's PT_LOAD extent — see Spike I). GDB needs to know
those addresses to map symbols to source. Whether the standard
`qOffsets` / `vFile:setfs` machinery handles a non-conventional layout
where libraries sit *below* the cart in guest memory is unverified.
Failure mode is silent: stepping appears to work but stack traces stop
at the PLT.

Third, ADR-0045 promises that the DAP session is uninterrupted across
a hot reload — VS Code's stock breakpoint-resync flow is supposed to
just work because the runtime emits `loadedSource(reason: "changed")`
and VS Code re-sends `setBreakpoints` with the editor's current
(post-edit, line-shift-tracked) positions. This is the standard
DAP-with-hot-reload pattern (Flutter, .NET Hot Reload, JVM HotSwap all
use it), but the runtime side has to actually emit the right events at
the right time. Failure modes include: the server forgets to emit
`loadedSource`, so VS Code never re-syncs and breakpoints stay bound
to deleted code; the server emits but at the wrong time (before the
new cart is mapped, so `setBreakpoints` re-binds against the old code
that's still in memory); or the GDB-stub equivalent (`library-loaded`
notification on cart-ELF replacement) is missing so VS Code's GDB
extension never re-applies breakpoints. Any of these silently degrade
the headline "edit-and-debug" workflow.

**What to build:**

- A minimal DAP server inside the runtime, exposing breakpoints, step,
  call-stack, and local-variable inspection against a Lua cart from
  Spike I's case c. Implement hook composition: a single
  `master_hook(L, ar)` dispatches to whichever combination of
  budget / throttle / debugger handlers is active for the current
  build. Verify Spike G's `doom_tick` p99 overhead remains < 0.34 ms
  with the master hook installed at `LUA_MASKCOUNT` N=100 *and* a DAP
  client attached but idle (no breakpoints).
- A minimal GDB remote serial stub inside rv32emu, exposing register
  read/write, memory read, single-step, and breakpoint set/clear
  against a Spike I case b cart (C cart with `.text.mylib`) built with
  `-g -gdwarf-4`. Verify a `bt` from inside `mylib_value()` walks the
  cart's PLT into `libconsole.so`'s `fc_console_print` and reports the
  correct source location on the libconsole side. Document the
  `qOffsets` / shared-library reporting protocol used.
- A reload-while-debugging test for the DAP path. The spike does not
  need full Spike M / K machinery — a *synthetic* reload (tear down
  the cart's `lua_State`, rebuild it from a modified Lua source, do
  not bother preserving game state) is sufficient to exercise the
  protocol. Procedure:
  1. Attach VS Code to the DAP server. Open `case_c.lua`. Set a
     breakpoint at line 47. Run; confirm it hits.
  2. Edit the source: insert five lines above the breakpoint so the
     editor's marker tracks to line 52. Save.
  3. Trigger a synthetic reload via a custom `hot_reload` DAP request.
     The runtime rebuilds the cart and emits
     `loadedSource(reason: "changed")` for `case_c.lua`.
  4. Without any user interaction in VS Code, confirm: VS Code
     re-sends `setBreakpoints` with `lines: [52]`; the server
     responds `verified: true`; running the cart hits the new line.
  5. Repeat with an edit that makes the breakpoint line no longer
     executable (e.g. delete that line entirely). Confirm the server
     responds `verified: false` and VS Code shows the breakpoint
     hollow in the gutter, with no spurious hit.
- A reload-while-debugging test for the GDB stub. The synthetic
  reload here is replacing the cart ELF in rv32emu's address space
  with a rebuilt one (different DWARF, possibly shifted line tables).
  Verify VS Code's GDB extension re-applies breakpoints automatically
  via the standard `library-loaded` / process-replacement notification.
- VS Code launch configurations for both paths (one Lua cart, one
  C cart). The success criterion is "F5 hits a breakpoint and shows the
  right frame in the variables panel" for each.

**Success criterion:**

- DAP: breakpoint hit, step-over, call-stack, locals, and upvalues all
  work on a Spike I case c cart; the master-hook composition adds
  ≤ 10 % to Spike G's `doom_tick` p99 with the debugger attached and
  idle.
- GDB stub: breakpoint hit at a `mylib_value()` source line on a
  Spike I case b cart; `bt` walks across the PLT into `libconsole.so`
  with correct source mapping; memory read returns the cart's
  `.cart.resources` bytes at the right guest addresses.
- DAP reload-while-debugging: after the synthetic reload, VS Code
  re-binds the shifted breakpoint at the correct new line *with no
  manual UI action*; the deleted-line case shows a hollow gutter
  marker; no spurious breakpoint hits at stale line numbers.
- GDB reload-while-debugging: after cart-ELF replacement, VS Code's
  GDB extension re-applies breakpoints automatically; line shifts
  in the new ELF are honoured; stale breakpoints in deleted code
  fail to bind cleanly.
- VS Code: both launch configurations connect on F5, hit breakpoints,
  and surface variables in the standard panel.

**What this spike does and does not decide:**

- Decides whether the three-way hook composition is workable as
  designed, whether GDB's shared-library protocol handles
  rv32emu's pre-mapped layout, and whether the standard
  DAP-hot-reload signalling pattern works against the runtime's
  DAP server without an out-of-protocol custom extension on the
  client side.
- Does not implement the production debugger UI (watchpoints,
  conditional breakpoints with full expression evaluation, reverse
  step). Those are engineering once the composition story is settled.
- Does not address debugger access on the native RISC-V hardware
  path. §21 calls for the GDB stub to run there too, exposed over
  TCP from the dev-mode device image; that's a Spike H follow-up
  on real hardware, not this spike.
- Does not validate the *full* hot-reload mechanics (snapshot,
  state migration, native-cart restart). Those are Spike M's
  scope. J validates only the debugger-side protocol seam — the
  events, the re-binding, the no-UI-action contract — using a
  synthetic reload that throws away game state.

**Dependency:** Spike G (`LUA_MASKCOUNT` budget hook), Spike G.3
(`LUA_MASKLINE` throttle hook), Spike I (cart format, library load
addresses, the case b/c/d binaries). Can run in parallel with
Spikes K, L, M; intersects with M at the hot-reload-while-debugging
seam — J validates the debugger side with a synthetic reload, M
validates the snapshot/restore side without a debugger attached, and
the production combination of "real reload + real debugger" composes
their results.

---

## Spike K — Cross-platform save-state portability end-to-end

**The question:** Does a save state serialized on one host platform
deserialize on a materially different host platform, restore the
same simulation state byte-for-byte, and continue producing the same
per-frame digests as a same-host continuation would have produced?

**Why this is a risk:** Spike D proved that the same cart, replayed
from frame 0 on two host platforms, produces bit-identical per-frame
digests. Save state is structurally different: it captures the
simulation state mid-run, writes it to a portable byte buffer, and
restores it elsewhere. The portability guarantee depends on every
tracked region (POD state buffers, RNG streams, the audio mixer's
voice-end queue per ADR-0106, screen-shake state per ADR-0051,
coroutine save-hook output per ADR-0012) round-tripping correctly,
and on the deserialized state being *exactly* what an in-place
continuation would have held in memory at that frame.

ADR-0007 calls determinism structural; ADRs 0009, 0010, 0011, 0013
specify the by-memcpy-with-typed-layouts mechanism. None of that has
been exercised end-to-end. Padding bytes inside POD layouts, struct
alignment differences across compilers, endianness assumptions in
serialized RNG state, and the audio voice-end-event log are all
candidate failure points that don't surface until a real round-trip
runs.

**What to build:**

- Extend Spike D's harness: continue running a Spike B-style cart
  workload, but at frame N, serialize all tracked state regions to a
  byte buffer (the same bytes a save-state file would contain). Emit
  the buffer as a hex dump alongside the per-frame digest stream.
- A minimal restore path: read the buffer, deserialize into a
  freshly-initialised runtime, advance from frame N+1, emit the same
  per-frame digest stream from there.
- Run the full save-then-continue sequence on linux/arm64 and
  linux/amd64. Cross-load: take the serialized buffer from arm64,
  deserialize on amd64, continue, and compare the resulting digest
  stream against arm64's same-host continuation.
- Include at least one cart that exercises the audio voice-end queue
  (ADR-0106) and one that uses a coroutine with explicit save hooks
  (ADR-0012). Pure-CPU `whetstone` is the floor case.

**Success criterion:**

- The serialized buffer is byte-identical between the two host
  platforms at frame N (same-host save → same bytes).
- The cross-loaded continuation produces a digest stream byte-
  identical to the same-host continuation from frame N+1 onward, on
  both platforms in both directions.
- The audio-voice and coroutine carts round-trip without divergence
  for at least 60 frames after restore.

**What this spike does and does not decide:**

- Decides whether the save-by-memcpy + typed-layouts model is sound
  in practice across host platforms, and whether the audio voice-end
  queue and coroutine save-hook designs are complete enough to round-
  trip without ADR-level changes.
- Does not address libretro's `retro_serialize` / `retro_unserialize`
  wrapping (that's Spike L scope, building on this spike's buffer
  format), and does not test save-state on the WASM target (a follow-
  up if K passes on native). The Spike D result already covers WASM
  for the *replay-from-frame-0* path.
- Does not address rewind. Rewind is N consecutive save states; if
  one round-trip works, rewind is engineering on top of it.

**Dependency:** Spike D (digest harness, the cart workloads, the
two-host build infrastructure). Can run in parallel with Spikes J, L,
M.

---

## Spike L — Libretro core adapter feasibility

**The question:** Can the runtime, built as a library, be wrapped in a
~400-line libretro core such that a Spike I case d cart runs in
RetroArch on desktop with correct video, audio, input, save state,
and rewind — given that libretro's callback-pull model is structurally
inverted from the runtime's frontend-pulls model (ADR-0036)?

**Why this is a risk:** The §18 plan and ADR-0033 both treat the
libretro adapter as thin engineering. Two structural mismatches make
that claim worth checking before the runtime API freezes.

First, ADR-0036 has the *frontend* pull from the runtime: the frontend
calls `runtime_run_frame()` and reads back framebuffer / audio /
state. Libretro inverts this: the frontend calls `retro_run()`, and
the core pushes video/audio out via callbacks (`video_refresh_t`,
`audio_sample_batch_t`) that the frontend installed earlier. Whether
a thin shim can bridge these without forcing API changes — particularly
around audio buffer sizing, where libretro expects a per-frame batch
matched to the host's audio configuration rather than the runtime's
internal mixer cadence — is the question this spike answers.

Second, libretro's `retro_serialize_size` requires a *fixed* upper
bound for save state size, returned before any save happens. The
runtime's tracked-region layout is cart-dependent (manifest-declared
state buffers, ADR-0009). Whether the runtime can compute a tight
upper bound at cart-load time, and whether that bound is small enough
that RetroArch's rewind buffer (default ~10 MB at 60 fps) is usable
for a non-trivial cart, is unverified.

**What to build:**

- A libretro core (`runtime_libretro.so`) wrapping the runtime as it
  exists at the time of the spike. Implements the standard libretro
  entry points: `retro_init`, `retro_load_game`, `retro_run`,
  `retro_get_system_av_info`, `retro_serialize_size`,
  `retro_serialize`, `retro_unserialize`, plus the input/video/audio
  callback plumbing.
- A Spike I case d cart loaded as the test workload (Lua + C user
  library — exercises both Lua VM and native cart paths).
- Run in RetroArch on desktop (linux/amd64). Verify:
  - Video at 60 fps with no tearing, correct palette, no extra
    indirection latency.
  - Audio at the host's sample rate without underruns; voice-end
    events from ADR-0106 still fire in input-frame order.
  - All inputs from ADR-0017's button set route correctly through
    libretro's input descriptors.
  - Save state via RetroArch's UI round-trips correctly (consumes
    Spike K's buffer format).
  - Rewind enabled in RetroArch produces visually-correct backwards
    playback for at least 5 seconds of history.

**Success criterion:**

- All five behaviours above work without runtime API changes. Audio
  buffer cadence is reconciled inside the adapter, not via runtime
  callbacks redesign. `retro_serialize_size` returns a bound that
  RetroArch's default rewind buffer can sustain ≥ 5 s for the case d
  cart.
- Adapter LOC is in the same order of magnitude as the §18 estimate
  (≤ ~1000 lines including comments). A blow-up well beyond that is
  evidence the inversion is harder than ADR-0033 assumes.

**What this spike does and does not decide:**

- Decides whether the runtime's frontend-pulls model and libretro's
  callback model can be reconciled with a thin shim, or whether the
  runtime API needs adjustment for libretro to be a first-class
  target. The latter is structural and has to happen before the API
  freezes.
- Decides whether `retro_serialize_size` is computable cheaply
  enough at load time for the manifest-declared state-buffer model.
- Does not address libretro on retro handhelds, mobile, or browser
  (RetroArch web) — those are platform-deployment validations after
  the core is known to work on desktop.
- Does not address the standalone custom libretro frontend
  (ADR-0034). Both consume the same core; this spike validates the
  core, not the frontends.

**Dependency:** Spike I (cart format, the case d binary used as test
workload), Spike K (save-state buffer format consumed by
`retro_serialize` / `retro_unserialize`). Can run in parallel with
Spikes J, M.

---

## Spike M — Hot-reload via save/restore (Lua and native paths)

**The question:** Can a cart — Lua or native — be edited and reloaded
mid-run such that POD state persists across the reload, the cart's
new code resumes from the prior state without observable break, and
the path covers the kinds of edits authors actually make in their
edit-run loop?

For native carts the mechanism collapses to a clean composition over
Spike K: rebuild the cart ELF, restart the cart process, run `init`,
deserialise the saved state, advance from the next frame. There are
no closures-over-old-source and no live coroutines that depend on
deleted functions, because every cart-side allocation is rebuilt from
scratch — only the POD tracked regions cross the reload boundary.
That makes the native case a *direct* test of Spike K plus the
packer-to-runtime signal protocol of ADR-0045, with no additional
mechanism. It is worth running explicitly because it is the simplest
path the spike can validate end-to-end and it establishes the
floor case.

For Lua carts the same outline applies, but the cart's pre-reload
state can include closures and yielded coroutines whose
representations don't survive a code change. ADR-0045 specifies the
default migration (copy matching fields, zero new ones) and
ADR-0012 specifies the coroutine save-hook contract; whether those
policies together cover the realistic edit distribution — or whether
the "long tail" of edge cases dominates — is the question this spike
exists to answer.

**Why this is a risk:** The save half is Spike K. The restore-into-
modified-code half is where the design's edge cases live, and they
have not been measured against a realistic edit set. The named
failure modes are: a closure that captures an upvalue the new code
no longer declares; a coroutine yielded inside a function that no
longer exists; a state buffer whose typed layout changed shape
(field added, removed, renamed, or retyped); a cart that allocated a
runtime resource (a tilemap, a font) whose declaration moved between
manifest entries.

**What to build:**

- A minimal VS Code-style edit-save loop: runtime hosts a Spike I
  case binary, the packer rebuilds on file save, the runtime applies
  the rebuilt cart via the Spike K save-state buffer.
- A native cart suite (Spike I case a or b): edits that change a C
  function body, add a new function, change a tracked-state struct
  field (matching field, added field, removed field, retyped field),
  and rebuild the ELF. Verify that `init` runs against the new code,
  the saved tracked-state regions deserialise into the new layout
  per ADR-0045's migration rules, and the cart continues from the
  next frame with the matching fields intact.
- A Lua cart suite (Spike I case c or d): in addition to the
  pure-data edits above, (i) a function body changed (no signature
  change); (ii) a function renamed (one call site updated, one not);
  (iii) a closure's captured upvalue removed in the new code; (iv) a
  coroutine yielded in a function whose body changed; (v) a coroutine
  yielded in a function that was deleted.
- For each edit in both suites, record: did the reload succeed, did
  POD state survive correctly, did the cart continue running, did
  any observable glitch occur (a frame of wrong rendering, an audio
  pop, a dropped input). For Lua (iii)–(v), the expected outcome is
  a clean migration error surfaced through the cart's
  `on_hot_reload_failed` hook (or an equivalent ADR-0083 crash
  diagnostic if the design forces a hard restart for that case);
  the spike's job is to confirm which.

**Success criterion:**

- *Native suite:* every edit reloads cleanly. POD state matches what
  the cart had pre-edit on every matching field; new fields are
  zeroed; removed fields are dropped. The packer-to-reload latency
  for a native rebuild meets the §19 risk-table target (< 3 s).
- *Lua pure-data and body-change edits* (matching the native suite
  plus (i)–(ii)): reload in-place with no observable glitch.
- *Lua closure / coroutine edits* (iii)–(v): either migrate cleanly
  via the coroutine save-hook contract, or fail with a diagnostic
  that points at the specific closure / coroutine / function that
  could not migrate (no silent corruption, no segfault, no half-
  restored state).
- The packer-to-reload latency for a Lua-only edit meets the §19
  target (< 500 ms).

**What this spike does and does not decide:**

- Decides whether the native reload path is, as expected, a clean
  composition over Spike K with no additional mechanism, and whether
  the < 3 s latency target is reachable with ADR-0088's packer.
- Decides whether ADR-0045's default migration policy plus
  ADR-0012's coroutine hook contract cover the realistic Lua edit
  distribution, or whether additional migration affordances (named
  author hooks, schema migration DSL, etc.) are required before v1
  ships.
- Decides whether the < 500 ms Lua-only latency target is reachable
  with the packer architecture from ADR-0088 in place.
- Does not address asset-only hot reloads (sprite, palette, audio).
  Those are simpler than code reload; if code reload works, asset-
  only reload is a strict subset.

**Dependency:** Spike K (save-state buffer is the migration vehicle —
non-negotiable for both suites), Spike I (cart format and the case
a/b/c/d binaries that supply the content under edit), ADR-0088
(packer two-phase incremental build, to make the edit-save loop fast
enough to be measurable). Can run in parallel with Spikes J, L; the
native suite can run before the Lua suite, since the native path is
a strict subset of the Lua path's mechanism.

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
    │   └── K (save-state cross-platform portability) ← extends D's
    │       │                                           harness to a
    │       │                                           save-restore
    │       │                                           round-trip
    │       └── L (libretro core adapter) ← consumes K's save-state
    │       │                               buffer for retro_serialize
    │       └── M (hot-reload via save/restore) ← uses K's buffer as
    │                                              the migration vehicle
    └── E (WASM, rv32emu+Lua) ← also uses D for comparison
        └── F (WASM, Lua-direct) ← contingent on E's miss; uses E's
            │                       harness and cross-stack baseline
            └── G (WASM Lua-direct CPU cap) ← depends on F's host.c
                │                              and timing baseline
                └── G.3 (dev-mode Pi-parity throttle)
                    └── J (debugger composition) ← three-way hook
                                                    composition over
                                                    G + G.3 + DAP

H (native RISC-V sandbox) ← independent; requires Milk-V Duo hardware

I (cart format end-to-end) ← depends on ADR-0024/ADR-0025 and Spikes
                              C, F, G; can run in parallel with H; QEMU
                              makes the native-RISC-V dimension accessible
                              without physical hardware
                              ↑
                              Spikes J, K, L, M consume Spike I's case
                              binaries as their cart workloads
```

Spike C can begin as soon as A produces a working interpreter, since it
is about build/linker feasibility rather than performance. The
performance question in C is answered by B. Spike F is contingent on
Spike E missing the desktop-WASM budget — if E had passed, F would not
need to run. Spike G is contingent on Spike F's adoption recommendation
— if F had not recommended Lua-direct for WASM, G would not be needed.
Spike H is independent of the rest of the series; it requires Milk-V
Duo hardware and can run in parallel with any other spike. Spike I
depends on the settled cart format (ADR-0024 Accepted, ADR-0025
Accepted) and on the Lua-direct WASM path established by Spikes F and G;
it can run in parallel with H and is the first end-to-end test of the
cart format as a container rather than as individual harness components.

Spikes J, K, L, M de-risk the architectural questions left after I lands.
J answers whether `lua_sethook` can serve three concurrent consumers
(budget, throttle, debugger) and whether GDB DWARF unwinding works through
Spike I's PLT/GOT layout. K extends D's per-frame digest result to a
serialise-on-A / deserialise-on-B / continue round-trip — the structural
case D did not cover. L composes K's buffer with libretro's callback-pull
inversion of the runtime's frontend-pulls model (ADR-0036). M reuses K's
buffer as the migration vehicle for ADR-0045 hot reload. J, K, L, M can
run in parallel with each other once their listed dependencies are met,
and in parallel with H (which is gated on hardware, not on these).

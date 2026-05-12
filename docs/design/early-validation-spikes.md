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
  need full Spike N / K machinery — a *synthetic* reload (tear down
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
  state migration, native-cart restart). Those are Spike N's
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

## Spike M — Managed Lua coroutine save/restore end-to-end

**Status:** PASS. Implementation in
[`spikes/spike-m/`](../../spikes/spike-m/); results in
[`spike-m-results.md`](spike-m-results.md). All six stages PASS:
eight workloads × 29 save frames × 4 cross-host directions = 928
cross-host runs, byte-identical save buffers and continuation
digest streams matching the same-host straight-through suffix; the
transient negative test additionally produces a 4-way byte-equal
STDERR + DIGEST at S=5; three Stage 6 negative tests confirm the
mechanism error paths (slot overflow, ctx-with-function,
slot-blob bit-flip) all surface canonical `BLYT_ERR_*` strings
without crashing.  ADR-0012 amended (2026-05-06) with the
single-function `create(function(ctx), seed?)` shape, the
constrained `ctx` shape, and the load-resume idiom contract.

**The question:** Can a real algorithm — say a multi-step cutscene, an
AI behaviour tree, or an asynchronous loader — written using
`blyt32.coroutine.create{start, save, restore}` (ADR-0012) be
save-stated mid-yield, restored on a fresh runtime, and continue
producing the same per-frame digest stream as a same-host
straight-through run, with the property holding cross-host
(linux/arm64 ↔ linux/amd64) byte-for-byte?

**Why this is a risk:** Spike K validated the *byte transport* of a
coroutine save blob — fixed POD per slot, written via
`coroutine_blob_write()` and round-tripped through the registered
region. The header of K's `det_cutscene_save.elf` is explicit about
the limit: *"Simulates the production `blyt32.coroutine.create{start,
save, restore}` pattern (ADR-0012) **without a Lua VM**"*. Five
load-bearing questions sit downstream of that simplification, none of
which any current spike has touched:

1. **Is the `start / save / restore` author API actually expressible
   as Lua and ergonomic for realistic patterns?** ADR-0012 specifies
   the shape but does not show a working algorithm using it. What
   does `start` get as its argument? How does the cart-author code
   inside `start` invoke `coroutine.yield()` such that the runtime
   can mark a resume point? What does `restore` need to return so the
   runtime knows where in `start` to continue?
2. **Does the runtime's wrapping of `coroutine.yield` compose with
   branching cart code?** The cutscene example in ADR-0012 is
   linear; real coroutines branch. If the resume marker is just a
   yield count, branching that conditionally yields a different
   number of times across save / restore breaks the model. The
   alternative — capturing call-stack and instruction position — is
   strictly more complex.
3. **Does the `save` callback's table return cross-host round-trip
   byte-equal?** ADR-0012 lets `save = function(ctx) return {...}`
   return any Lua table; the runtime serializes it. Lua iteration
   order, string interning, and hash slot assignment are all
   non-deterministic in stock Lua. If the flatten path is not
   cross-host bit-identical, ADR-0012 may need to constrain the
   `save` return shape (ordered list, flat record, no nested
   tables) before v1 ships — a spec-level revision, not just
   engineering. This is the question Spike K explicitly deferred.
4. **Is transient `coroutine.create()` detectable at save time
   (must-throw rule), without false positives on managed
   coroutines?** ADR-0012 mandates a runtime error when a
   non-persistent coroutine survives save / restore. The Lua VM
   exposes `lua_State` per coroutine but no first-class "this was
   created by my managed wrapper" marker; the discrimination
   mechanism — registry table? upvalue tag? `lua_setuservalue`? —
   is unspecified. The open question is whether the Lua VM exposes
   enough hooks to distinguish the two creation paths at save
   time, or whether ADR-0012 needs to specify the discrimination
   mechanism explicitly.
5. **Do multiple persistent coroutines running concurrently survive
   save / restore independently?** A single cutscene exercises the
   slot-table accounting trivially; a cutscene plus an AI script
   plus an asynchronous loader does not. Slot allocation, slot-
   reuse-after-completion, and per-coroutine save-blob sizing all
   need to round-trip when multiple slots are simultaneously active.

**What to build:**

- A real Lua coroutine workload — a 5-step cutscene with at least one
  conditional branch (`if frame_count > 30 then yield_n_more(2) else
  yield_n_more(5) end`) — wrapped in
  `blyt32.coroutine.create{start, save, restore}`. Each yield encodes
  the current step + a small payload (angle, position) into the
  `save` return table; `restore` rebuilds local state from that
  table.
- A second Lua workload running *two* persistent coroutines
  concurrently in the same cart: a cutscene as above, plus a tiny
  AI behaviour script (alternating between "wander" and "chase"
  states across yields). Different slot indices, different
  save-blob shapes. Verify they round-trip independently.
- Per-frame digest emission identical to Spike K (same FNV-1a-64
  helper). For each workload: run frames 0–N straight through;
  separately run frames 0–S, save, restart on a fresh runtime,
  load, continue frames S+1–N; assert the digest streams match
  byte-for-byte. Repeat for *every* save frame S in `[1, N-1]` so a
  resume marker that only works at certain yield boundaries fails
  loudly.
- Cross-host validation: build the same Lua workloads on
  linux/arm64 and linux/amd64 (Spike D's two-Docker-image setup,
  inherited via Spike K's Dockerfile pattern). Save on host A,
  load on host B; assert the `save` callback's flattened table
  bytes are byte-equal pre-write, and the continuation digest
  streams byte-equal post-load.
- The transient-coroutine negative test: a small workload that
  calls bare `coroutine.create()` (not the `blyt32` wrapper),
  yields, takes a save, restores, and attempts to resume. Confirm
  the runtime throws `RuntimeError: coroutine crossed a save/
  restore boundary` (or whichever string ADR-0012 settles on) —
  string-equality on stderr, same on both hosts.

**Success criterion:**

- The single-coroutine cutscene and the dual-coroutine workload
  both have byte-equal continuation digest streams across every
  save frame `S ∈ [1, N-1]`, on both linux/arm64 and linux/amd64,
  cross-saved and cross-loaded in both directions. Failure at any
  S is a real bug — either in the resume-marker mechanism or in
  the table-flatten determinism.
- The transient-coroutine negative test throws the specified
  error, with byte-equal stderr on both hosts.
- The implemented author API (the actual Lua signatures of
  `start`, `save`, `restore`, and how `coroutine.yield` is used
  inside `start`) is documented as a recommendation back into
  ADR-0012, with any awkwardness or gaps identified.

**What this spike does and does not decide:**

- Decides whether `blyt32.coroutine.create{}` is implementable on
  top of stock Lua's coroutine machinery without a Lua VM patch,
  or whether the runtime needs to substitute its own coroutine
  primitive.
- Decides whether the `save` callback's free-form table return
  cross-host round-trips byte-equal, or whether ADR-0012 needs a
  shape constraint before v1 ships.
- Decides whether the transient-coroutine must-throw rule is
  enforceable with the Lua VM's existing introspection surface.
- Pins down the author-facing API shape with a real working
  algorithm — feeding back into ADR-0012's specification.
- Does not address hot reload of the coroutine's source code
  (function body changes mid-yield). That is Spike N's scope; M
  proves the save/restore mechanism works against unchanged code
  first, so N can isolate the change-related failures cleanly.
- Does not address asset-only coroutine state (e.g. a coroutine
  whose state references a sprite handle whose backing changed
  out from under it). Same reasoning — N's territory once M's
  base case is solid.

**Dependency:** Spike K (save-state buffer + tracked-region
registry — non-negotiable; M's coroutine save blob registers as a
region exactly like K Stage 4's, but with a real Lua VM populating
the bytes), Spike I (Lua cart pipeline — the workloads are Lua
carts in the spike-I sense), Spike B (Lua VM availability inside
the runtime). M can run in parallel with Spike L; N depends on M.

---

## Spike N — Hot-reload via save/restore (Lua and native paths)

**Status:** PASS. Implementation in
[`spikes/spike-n/`](../../spikes/spike-n/); results in
[`spike-n-results.md`](spike-n-results.md). All six stages PASS: 6
native edits (n1–n6) × 2 hosts, 10 Lua edits (l1–l10) × 2 hosts, and
the l8 all-S sweep (29 frames × 4 cross-host directions = 116 runs) —
148 cross-host runs total, all byte-identical save buffers and
continuation digest streams; l6/l9/l10 clean-failure diagnostics
byte-equal cross-host.  ADR-0045 and ADR-0083 both amended (2026-05-07)
with the diagnostic format, rejected-reload-preserves-state rule,
`blyt32.on_hot_reload_failed` hook surface, M↔N composition note, and
`on_retype`-mandatory-for-retypes rule.

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
- Does not re-validate the managed-coroutine save/restore mechanism
  itself — that is Spike M's scope. N depends on M's mechanism
  working against unchanged code so the Lua suite cases (iv) and
  (v) can isolate failures attributable to the *code change*, not
  to a flaw in the underlying coroutine save/restore.
- Does not address asset-only hot reloads (sprite, palette, audio).
  Those are simpler than code reload; if code reload works, asset-
  only reload is a strict subset.

**Dependency:** Spike K (save-state buffer is the migration vehicle —
non-negotiable for both suites), Spike M (managed-coroutine save/
restore mechanism — required for the Lua suite's coroutine edit
cases), Spike I (cart format and the case a/b/c/d binaries that
supply the content under edit), ADR-0088 (packer two-phase
incremental build, to make the edit-save loop fast enough to be
measurable). Can run in parallel with Spikes J, L; the native suite
can run before the Lua suite, since the native path is a strict
subset of the Lua path's mechanism and does not depend on M.

---

## Spike O — Rust cart end-to-end

**Status:** results in [`spike-o-results.md`](spike-o-results.md). All five
stages PASS on both arm64 and amd64 (`make all` exits 0). Eight numbered
findings recorded; none are design flaws (four are confirmations, four are
production follow-ups). Finding O-1 (target name correction) acted on:
ADR-0001 amended to add the A extension (RV32IMFC → RV32IMAFC) and
ADR-0108 amended with the corrected target string, primary-language status,
and A-extension rationale.

**The question:** Can a Rust cart compile to `riscv32imafc-unknown-none-elf`,
link against `libblyt32`, pack through the existing cart pipeline, and run
inside rv32emu producing a per-frame digest stream identical to an equivalent
C cart — and do the ADR-0108 ergonomic guarantees (typed `FieldHandle<B>`,
`fn`-enforced handler purity, two-tier error model) actually surface as
described in practice?

**Why this is a risk:** ADR-0108 is design-only; nothing has been built.
Three distinct risks sit underneath the design:

1. **Toolchain.** The `riscv32imfc-unknown-none-elf` target is supported in
   upstream Rust but bare-metal RISC-V with `no_std` and no allocator is
   an unusual configuration. ISA flag alignment (`+f`, `-d`, soft-float ABI
   vs hard-float) must match `libblyt32`'s compilation flags exactly or
   `extern "C"` calls will corrupt floating-point arguments silently. This
   is the same class of ABI hazard that caused Spike C's shared-library
   positioning requirement; Rust hides it behind Cargo target strings.

2. **Packer–Cargo integration.** The `OUT_DIR` codegen path (packer emits
   `resources.rs`, `state.rs` etc.; cart's `build.rs` invokes packer and
   points Cargo at the output) is the proposed mechanism but has never been
   exercised. Whether packer invocation can be folded into the existing
   `cart.build.yaml`-driven build or requires separate orchestration is
   unknown.

3. **SDK ergonomics vs design.** ADR-0108's typed `FieldHandle<B>` phantom
   type, `#[repr(transparent)]` newtypes, and `fn`-only handler registration
   are well-motivated on paper. A working cart will either confirm them or
   reveal friction the design did not anticipate — places where the Rust
   borrow checker, orphan rules, or `no_std` constraints push back against
   the proposed shape.

If any of these is a fundamental mismatch, it is far cheaper to discover it
before the C API hardens than after. ADR-0108 explicitly calls for future
API decisions to treat Rust as a first-class consumer alongside C and Lua;
that is only possible if real Rust cart code is exercised while the API is
still fluid.

**What to build:**

- **Minimal Rust SDK crate** (`blyt32-sys` + `blyt32`): `extern "C"`
  declarations for the functions the toy cart calls (image load/blit, one
  audio SFX play/stop, state buffer get/set for one field); one
  `#[repr(transparent)]` newtype per used handle type; method wrappers on
  those newtypes; one `FieldHandle<B>` definition with the `PhantomData`
  type parameter; `fn`-typed handler registration stub. No allocation.
  `no_std`.

- **Packer stub** that generates `resources.rs` and `state.rs` into
  `$OUT_DIR`. Content can be hardcoded constants matching the toy cart's
  asset declarations — the goal is to prove the `include!(concat!(
  env!("OUT_DIR"), "/resources.rs"))` integration, not to implement a
  production packer.

- **Toy cart in Rust** that:
  - Loads one image resource and blits it each frame
  - Plays one SFX on a trigger input
  - Reads and writes one state buffer field (`S_COUNTER: FieldHandle<MainBuffer>`)
  - Registers one handler via the `fn`-typed API
  - Emits the same per-frame determinism digest as the Spike D/K harness

- **Equivalent C cart** (can reuse a Spike I case) exercising the same
  operations, so the two digest streams can be compared byte-for-byte.

- **Compile-time guard tests** (not run, just compiled):
  - A version that passes `FieldHandle<EnemiesBuffer>` where
    `FieldHandle<MainBuffer>` is expected — must be a compile error.
  - A version that attempts to register a closure (capturing an upvalue)
    as a handler — must be a compile error.

- **Cart build integration**: `cart.build.yaml` with `language: rust`,
  `build.rs` invoking the packer stub and declaring `cargo:rerun-if-changed`
  correctly, Makefile target wiring.

**Success criterion:**

- Rust cart compiles to RV32IMFC ELF with ISA flags matching `libblyt32`;
  no link errors or ABI warnings.
- Cart packs through the existing packer pipeline and runs to completion
  inside rv32emu.
- Per-frame digest stream matches the equivalent C cart byte-for-byte
  across 29 save frames on the same host.
- Both compile-time guard tests fail to compile with a type error on the
  relevant line (no `unsafe` workaround needed).
- Toolchain setup (rustup target add, Cargo config, cross-compilation) is
  documented in `spikes/spike-o/README.md` and reproduces from scratch in
  a clean Docker environment with the same base image as Spike I.

**Secondary output:** Any API shape that is ergonomically awkward in
practice — handle granularity, output parameter ordering, missing derives —
is recorded as a numbered finding in the results doc, with a proposed C API
amendment where applicable. These findings become the first concrete
Rust-side input to in-flight API ADRs.

**Dependencies:** Spike I (cart format and packing pipeline), Spike C
(shared library ABI and link model), Spike H (native RISC-V execution
substrate). Spike O is independent of M and N and can run in parallel with
any outstanding M/N follow-up work.

---

## Spike P — Rust heap allocator, atomics, and on_load contract

**The question:** Does a manifest-declared heap region wired up as a Rust
`#[global_allocator]` let `Vec`, `Arc`, and other `alloc` types work
correctly in a `no_std` Rust cart? Do `AtomicU32` and `Once` emit
A-extension instructions that rv32emu executes correctly? And does the
`on_load` hook correctly separate resource-derived heap data (which
survives rewind intact) from state-derived heap data (which `on_load`
refreshes)?

**Why this is a risk:** Three distinct unknowns sit under the design
established during Spike O and the subsequent ADR-0108/ADR-0087 updates:

1. **Atomics execution.** Spike O's Rust cart compiled with the `A`
   extension ELF flag (`rv32i2p1_m2p0_a2p1_f2p2_c2p0`) but never actually
   called code that emitted LR/SC or AMO instructions — no `AtomicU32`,
   no `Once`, no `Arc` reference count. rv32emu's A-extension support is
   therefore untested in practice; a silent fall-through or illegal-
   instruction trap would not have surfaced.

2. **Heap allocator.** The ADR-0108 heap allocator design — `heap_size`
   manifest field, SDK `#[global_allocator]` implementation, runtime
   carving out and exposing the heap region via a well-known symbol — is
   fully design-only. Whether a simple embedded allocator works correctly
   in the `no_std` cart context under rv32emu, and whether `Vec`/`Arc`
   built on top of it behave as expected, is unknown.

3. **`on_load` heap contract.** ADR-0087's rewind model divides heap data
   into two categories: resource-derived (set up in `init`, survives
   rewind) and state-derived (rebuilt in `on_load`). This distinction has
   never been demonstrated in a working cart. Whether `on_load` is
   sufficient as the single rebuild hook — or whether there are heap data
   patterns that do not fit neatly into either category — is unknown.

**What to build:**

- **Atomics smoke test.** A Rust cart (reusing the Spike O harness) that
  calls `AtomicU32::fetch_add` in a loop and reads the result via
  `Once::call_once` on first frame, embedding the output in the per-frame
  FNV-1a-64 digest. The point is to force actual LR/SC or AMO opcode
  emission and confirm rv32emu executes them; the digest confirms the
  result is deterministic across arm64 and amd64.

- **SDK `#[global_allocator]`.** A minimal allocator implementation in
  the `blyt32` SDK crate, backed by a static byte array sized by a
  `heap_size` build-time constant derived from `cart.build.yaml`. The
  runtime exposes the region's base address via a well-known linker
  symbol (`__blyt_heap_base`, `__blyt_heap_size`); the allocator
  initialises itself against that region on first use. Carts opt in via:
  ```yaml
  heap_size: 65536
  ```
  Carts that omit `heap_size` (or set it to zero) link the no-op
  allocator stub and get a clear link error if they accidentally import
  `alloc`.

- **`alloc` cart.** A Rust cart (separate from the atomics test) that:
  - Declares `heap_size: 65536` in `cart.build.yaml`
  - Builds a `Vec<u32>` in `init` from resource-derived data (a fixed
    lookup table read from a stub resource — not from a state buffer)
  - Builds an `Arc<u32>` counter and clones it across two notional
    "owners"; increments it each frame
  - Reads and writes a state buffer field alongside the heap allocations
  - Emits a per-frame digest covering the `Vec` length, the `Arc`
    reference count, and the state buffer value

- **Two-category `on_load` demonstration.** Extend the `alloc` cart with
  a simulated save/restore cycle (same mechanism as Spike K's
  save-restore harness). The cart's state buffers are saved, a field is
  mutated, then the save is restored and `on_load` fires:
  - **Resource-derived heap data** (`Vec` built from the stub resource in
    `init`): must be unchanged after restore — `on_load` does not touch
    it.
  - **State-derived heap data** (a second `Vec<u32>` built from the state
    buffer field values in `on_load`): must reflect the restored field
    values after `on_load` fires.
  - Per-frame digest must be byte-identical to the equivalent run that
    never took a save/restore detour.

- **Cross-host validation.** The same two-Docker-image setup as Spike O
  (linux/arm64, linux/amd64). Save on arm64, restore on amd64; digest
  streams byte-equal post-load.

**Success criterion:**

- `AtomicU32::fetch_add` and `Once::call_once` produce correct, byte-
  deterministic results across arm64 and amd64; no illegal-instruction
  trap in rv32emu.
- `Vec<u32>` and `Arc<u32>` construct, mutate, and drop without
  segfaults or allocator panics under rv32emu.
- The two-category `on_load` demonstration confirms the design: resource-
  derived `Vec` is byte-identical before and after restore; state-derived
  `Vec` reflects restored state buffer values; digest streams match the
  no-restore baseline.
- Cross-host save/restore: digest streams byte-equal after loading on the
  opposite host.
- All cases reproduce from scratch in a clean Docker environment based on
  the Spike O image.

**What this spike does and does not decide:**

- Decides whether rv32emu's A-extension implementation is correct for the
  instructions Rust's standard library actually emits (`LR.W`/`SC.W` for
  `AtomicU32`, `AMOSWAP` or similar for `Arc` reference counts).
- Decides whether the SDK `#[global_allocator]` design (linker-symbol
  heap region, `heap_size` manifest field) is viable or needs revision.
- Pins down the `on_load` contract with a working demonstration, feeding
  back into ADR-0087 and ADR-0108 if any gap is found.
- Does not implement the production `blyt32` SDK crate — the allocator
  built here is spike-quality; production hardening (allocator algorithm
  choice, OOM handling, allocator stats API) is a post-spike follow-up.
- Does not address `Arc` in the context of `no_std` + `alloc` crate
  availability on older nightly toolchains — uses Rust nightly-2025-08-01
  as established by Spike O.
- Does not measure allocator performance overhead on the MIPS budget —
  that is a production concern once the allocator is known to be correct.

**Dependency:** Spike O (Rust cart pipeline, Docker image, digest harness).
Can run immediately; no other spikes are blocking.

---

## Spike Q — Lua+Rust hybrid binding: rv32 path and WASM call-on-demand

**The question:** Does the ADR-0111 Lua+Rust hybrid binding mechanism work
end-to-end on both execution paths?

- **rv32 path:** can `cart_lua_modules` be implemented in Rust, with Lua C
  API symbols resolved against `libconsolelua.so`, and can Lua successfully
  call Rust functions through the resulting bindings?
- **WASM path:** can rv32emu be operated as a per-call "function server" —
  initialised once, then invoked for individual guest functions with
  register arguments supplied by the host and results read back — so that
  the Lua-direct WASM host can call into it to execute Rust code without
  running rv32emu to exit?

**Why this is a risk:**

The rv32 path is a straightforward extension of Spike I's case d (Lua +
C userlib), but replacing C with Rust introduces two untested steps:
`extern "C"` Lua C API calls from Rust resolving against
`libconsolelua.so`, and Rust's `#[link_section]` mechanism emitting the
`.lua_exports` ELF section with the right layout. Neither has been
validated in the RV32IMAFC cart context.

The WASM path is the novel risk. Spike I Stage 4 demonstrated rv32emu
compiled to WASM running a full cart from `_start` to exit — but that
model runs the entire cart (including the Lua VM) inside rv32emu, which
Spike E confirmed is 7.4× over budget for Lua. ADR-0111's design for
hybrid carts requires Lua to run outside rv32emu (Lua-direct, per
Spike F) while Rust runs inside it, with per-call invocations crossing
the boundary. rv32emu's current API has no "call guest function by
address, pass register arguments, return when the function returns"
interface — it only has "run from entry point until the cart calls
exit". Whether such an interface can be added, and whether it works
correctly for f32/i32 arguments and return values across the host/guest
boundary, is unknown. This is the load-bearing question ADR-0111 rests
on.

There is also a structural question: a Lua+Rust hybrid cart on WASM
requires both the Lua-direct module (from Spike F/I Stage 5) and an
rv32emu instance (for the Rust code) in the same WASM page. Whether
these can be initialised and interleaved — Lua-direct running the cart
frame loop, calling into rv32emu for specific Rust functions — is
unproven.

**What to build:**

**Stage 1 — rv32 path: Rust `cart_lua_modules`.**

Extend Spike I case d, replacing the C userlib with a Rust static
library. The Rust library exports:

```rust
#[no_mangle]
pub extern "C" fn cart_lua_modules(L: *mut lua_State) { … }
```

and two Lua-callable functions with different primitive signatures:

```rust
// f32 args → f32
extern "C" fn lua_fast_add(L: *mut lua_State) -> c_int { … }
// i32 args → i32
extern "C" fn lua_fast_mul(L: *mut lua_State) -> c_int { … }
```

`cart_lua_modules` registers both via `package.preload` using the Lua C
API symbols resolved against `libconsolelua.so`. The Lua cart calls
`require("mylib")`, invokes both functions, and includes their return
values in the per-frame FNV-1a-64 digest.

Success gate: digest streams byte-equal on arm64 and amd64. `mylib.fast_add(3.0, 4.0)` returns `7.0`; `mylib.fast_mul(3, 4)` returns `12`.

Also validate the `.lua_exports` ELF section: the Rust library emits it
via `#[link_section = ".lua_exports"]`. Use `objdump -s -j .lua_exports`
to confirm the section is present in the cart ELF and the symbol
address and type encoding fields are non-zero and sane. The section
content need not be parsed by the runtime in this stage — presence and
non-corruption is the gate.

**Stage 2 — rv32emu call-on-demand API.**

Add a `rv32emu_call_fn` interface to the rv32emu build used by the
spike (minimal change, spike-quality):

```c
// Call guest function at sym_addr.
// args[0..nargs-1] are placed in a0..a5.
// ret receives the value of a0 when the function returns.
// Returns 0 on success, non-zero if the guest faults or times out.
int rv32emu_call_fn(rv32emu_t *emu, uint32_t sym_addr,
                   const uint32_t args[], int nargs,
                   uint32_t *ret);
```

Implementation: set PC to `sym_addr`, place args in a0–a5, place a
sentinel address in `ra` (a guest address mapped to a dedicated "return
stub" that executes ECALL 0 — the runtime detects ECALL 0 as the
"function returned" signal), then run the interpreter loop until
ECALL 0 fires. Read a0 as the return value.

Validate with two guest functions compiled from C (not Rust yet —
isolate the rv32emu API question from the Rust question):
- `uint32_t add32(uint32_t a, uint32_t b)` — integer round-trip
- `float addf(float a, float b)` — float round-trip (validates
  the ilp32f ABI through the call-on-demand path)

Run against the Stage 1 Docker images (arm64 and amd64). Emit the
return value of each call as a `RESULT <name> <hex>` line; the gate is
`add32(3, 4) = 0x00000007` and `addf(1.0f, 2.0f) = 0x40400000`
(IEEE 754 for 3.0f) across both hosts.

**Stage 3 — WASM: Lua-direct + rv32emu call-on-demand.**

The key integration. Build a single Emscripten WASM module containing:
- The Lua-direct runtime (from Spike I Stage 5 / Spike F) — Lua VM,
  `libconsolelua_wasm.c`
- rv32emu compiled to WASM with the Stage 2 call-on-demand patch,
  with a minimal Rust cart ELF (providing `fast_add` and `fast_mul`)
  embedded in its WASM filesystem

At WASM module init:
1. Boot rv32emu in "idle" mode: load the Rust cart ELF, resolve
   symbols, but do not call `_start`. rv32emu sits ready to accept
   `rv32emu_call_fn` invocations.
2. Read the `.lua_exports` section from the cart ELF. For each entry,
   register a host-side Lua C function (trampoline) with the
   `lua_State`.
3. The Lua cart calls `require("mylib")`, invokes `fast_add` and
   `fast_mul`. Each trampoline call exercises:
   - Lua stack → host register values (via `lua_tonumber`)
   - `rv32emu_call_fn` with those register values
   - rv32emu runs the Rust function inside the guest
   - Return value in a0 → `lua_pushnumber` back to the Lua stack

Emit results in the same `FRAME / SUMMARY` format as Spike F. Run
under Node (headless); emit the function return values in the frame
digest. Success gate: same results as Stage 1's rv32 path, byte-equal.

Measure the per-call overhead of `rv32emu_call_fn` from within the
WASM host. This is not a pass/fail gate — it is a data point for
evaluating whether the ADR-0111 trampoline cost is acceptable in
practice. Report the mean and p99 round-trip time for a single
`fast_add` call (a no-work function that exercises only the call
boundary).

**Stage 4 — determinism cross-check.**

Run Stage 1 (rv32, arm64 and amd64) and Stage 3 (WASM, Node) on the
same Lua script. The per-frame digests must be byte-equal across all
three runs. This confirms that f32 values crossing the rv32emu
call-on-demand boundary on WASM produce the same bits as f32 values
staying inside rv32emu on the native path.

**Success criterion:**

- Stage 1: `mylib.fast_add` and `mylib.fast_mul` produce correct
  results; digest streams byte-equal arm64/amd64; `.lua_exports`
  section present and non-corrupt in the cart ELF.
- Stage 2: `add32` and `addf` return correct values via
  `rv32emu_call_fn` on both hosts; float ABI confirmed correct
  (addf result = 0x40400000).
- Stage 3: Lua-direct WASM calls Rust functions inside rv32emu via
  the trampoline; results match Stage 1 byte-for-byte.
- Stage 4: digests byte-equal across rv32/arm64, rv32/amd64, WASM/Node.
- Stage 3 per-call overhead measured and reported (informational, not
  a pass/fail gate for this spike).

**What this spike does and does not decide:**

- Decides whether rv32emu can be operated as a call-on-demand function
  server, and whether the host→guest register-argument ABI works for
  f32 and i32 across the WASM boundary. If it does not, ADR-0111 needs
  revision (the fallback being either dual-compilation of the Rust
  binding layer to WASM, or restricting hybrid carts to rv32-only
  targets with a different WASM story).
- Decides whether `cart_lua_modules` in Rust resolves Lua C API symbols
  correctly against `libconsolelua.so` in the RV32IMAFC guest.
- Decides whether `.lua_exports` section emission from Rust via
  `#[link_section]` survives the cart link and is correctly laid out.
- Does not implement the `#[lua_export]` proc macro — Stage 1 uses
  hand-written `#[link_section]` to emit the section data and
  hand-written Lua C API glue to register functions. The proc macro
  is an SDK engineering task once the mechanism is confirmed correct.
- Does not measure the full per-frame budget impact of the call-on-demand
  overhead for realistic workloads — Stage 3 measures a no-work
  round-trip, which sets the floor. Real-workload amortisation is a
  follow-up if the floor looks acceptable.
- Does not address the `#[global_allocator]` placement question from
  Spike P F3 for Lua+Rust hybrid carts — that is a follow-up once the
  binding mechanism is proven.

**Dependency:** Spike I (Lua-direct WASM path, `cart_lua_modules` for
C, rv32emu multi-dynload patch). Spike O (Rust cart pipeline, Docker
image). Spike P (Rust in rv32emu baseline). Can run in parallel with
any remaining spikes that do not touch rv32emu's WASM build.

---

## Spike R — Two-phase seccomp with raw BPF for RV32 ILP32

**The question:** Can a hand-written raw BPF seccomp filter correctly
implement two-phase enforcement for a fork/exec launcher where the
launcher process is LP64 (`AUDIT_ARCH_RISCV64`) and the cart process
after exec is ILP32 (`AUDIT_ARCH_RISCV32`), given that `libseccomp` does
not define `SCMP_ARCH_RISCV32`?

**Why this is a risk:**

Spike H Stage 3 proved that seccomp-bpf kills forbidden syscalls
correctly on Fedora 42. However, all seccomp filters in Spike H were
built with libseccomp, which has no `SCMP_ARCH_RISCV32` constant. The
Spike H uname probe failed because the BPF arch-check path silently
killed all ILP32 syscalls (including the wanted `uname`) rather than
matching the ILP32 arch explicitly. The production deployment needs a
filter that:

1. Handles two architecture constants in a single BPF program:
   `AUDIT_ARCH_RISCV64` for the LP64 launcher and
   `AUDIT_ARCH_RISCV32` for the ILP32 cart after exec.
2. Implements two phases: phase 1 (pre-exec) allows `execve`; phase 2
   (post-exec, tighter) blocks `execve` and permits only the syscalls
   the cart runner actually needs.
3. Produces the correct production syscall allowlist derived from real
   cart workloads, not just the minimal bootstrap set used in Spike H.

Using libseccomp for either phase is not viable without the arch
constant. The production filter must be written as raw BPF
(`struct sock_filter[]` loaded via `seccomp(SECCOMP_SET_MODE_FILTER,
0, &prog)`).

**What to build:**

**Stage 1 — Raw BPF arch-dispatch filter.**

Write a `struct sock_filter[]` BPF program that:
- Loads `seccomp_data.arch` and branches on both
  `AUDIT_ARCH_RISCV64` (0xC00000F3) and `AUDIT_ARCH_RISCV32`
  (0x40000F3). Any other arch → `SECCOMP_RET_KILL_PROCESS`.
- For the matching arch, loads `seccomp_data.nr` and allows the
  Spike H allowlist (plus `uname`).
- All other syscalls → `SECCOMP_RET_KILL_PROCESS`.

Validate on Fedora 42 QEMU (same environment as Spike H). Re-run the
Spike H adversary probes: `open`, `socket`, `execve`, `mprotect-exec`
must produce SIGSYS; `uname` must return 0. This gate confirms the
arch-dispatch path works for both architecture constants.

**Stage 2 — Two-phase transition.**

Extend Stage 1 to implement two phases:

- Phase 1 filter: loaded in the child after fork, before `execve`.
  Allows `execve`. Installed via `PR_SET_SECCOMP` /
  `seccomp(SECCOMP_SET_MODE_FILTER, ...)`.
- Phase 2 filter: loaded by the cart runner on startup (before any
  cart code), using `SECCOMP_FILTER_FLAG_NEW_LISTENER` if available
  or a plain second `seccomp()` call. Removes `execve`; adds
  `SECCOMP_FILTER_FLAG_TSYNC` to cover all threads.

Verify: after exec, a `execve` probe produces SIGSYS. The legitimate
cart syscalls (mmap, read, write, exit) are not blocked.

**Stage 3 — Production allowlist derivation.**

Run rv32emu on a representative set of cart workloads (the Spike I case
binaries — native C cart, Lua cart, Rust cart — and the Spike Q
Lua+Rust hybrid) under `strace -f` on Fedora 42. Collect the full
syscall set for each workload. Cull to the union, then remove any
syscall that is required only for the launcher phase or that can be
handled by a namespace/rlimit instead.

Produce a C header `seccomp_allowlist.h` containing the raw BPF
`sock_filter[]` for both phases. Commit this as the canonical phase 2
filter; it replaces the Spike H `seccomp_filter.c`.

**Stage 4 — Adversary re-verification.**

Run all four Spike H adversary probes (`open`, `socket`, `execve`,
`mprotect-exec`) against the Stage 3 phase 2 filter with a real
rv32emu+cart workload running in the background. All four must
produce SIGSYS. The cart workload must complete a full run without
any unexpected SIGSYS under the phase 2 filter.

**Success criterion:**

- Stage 1: `uname` allowed; all four Spike H adversary probes blocked
  by raw BPF; both `AUDIT_ARCH_RISCV64` and `AUDIT_ARCH_RISCV32` paths
  tested.
- Stage 2: two-phase transition works; phase 2 blocks `execve`; no
  legitimate syscall is blocked by phase 2.
- Stage 3: production allowlist header committed; covers all four cart
  workload types; no syscall present that could be used for host escape.
- Stage 4: adversary probes blocked; rv32emu cart workload runs cleanly
  under the production filter.

**What this spike does and does not decide:**

- Decides the implementation mechanism for the two-phase seccomp filter
  in ADR-0116. If raw BPF is insufficient or unacceptably complex, the
  fallback is a Rust-based BPF assembler (e.g. `seccompiler` crate) that
  handles the RISCV32 arch constant gap via numeric literal.
- Decides the production syscall allowlist for the standalone deployment.
- Does not address the libretro deployment (which uses inline instruction-
  budget enforcement rather than seccomp; see ADR-0116).
- Does not address the Milk-V Duo hardware calibration (a Spike H
  hardware follow-up).

**Dependency:** Spike H (mechanism validated; gap identified). Can run
immediately; does not depend on any pending spike.

---

## Spike S — Hardware trusted native-exec seccomp path

**The question:** Does the hardware trusted native-exec path (ADR-0119)
work in practice? Specifically: can a LP64 launcher exec an ILP32 cart
natively, produce a cart process with `seccomp_data.arch =
AUDIT_ARCH_RISCV32`, enforce an arch-dispatch filter across the exec
boundary, and have `libblyt32.so`'s constructor install a restrictive
phase-2 filter before any cart code runs?

**Why this is a risk:**

Spike R validated the seccomp mechanism for emulated targets (rv32emu
on desktop, Pi, and WASM). On those targets `seccomp_data.arch` is
always `AUDIT_ARCH_RISCV64` because carts run inside rv32emu (an LP64
process). The hardware trusted-exec path is architecturally different:
the cart process is exec'd directly as ILP32 by the OS, and
`seccomp_data.arch = AUDIT_ARCH_RISCV32` is expected for it.

Three things are unvalidated:

1. **The ILP32 ELF loader.** The Fedora 42 QEMU kernel used in Spike R
   lacks the RISC-V ILP32 ELF binary loader. A patched kernel (or a
   board with vendor ILP32 support) is required before native exec can
   be tested at all.

2. **Arch-dispatch across exec.** The phase-1 filter is installed by
   the LP64 launcher; after exec the process is ILP32. The filter is
   inherited across exec per `seccomp(2)`, but the interaction of a
   pre-exec RISCV64 filter installation with a post-exec RISCV32 process
   needs empirical confirmation.

3. **Phase-2 constructor timing.** `libblyt32.so`'s constructor must
   install the phase-2 filter before any cart-code constructors run.
   Constructor ordering within a process depends on the ELF DT_NEEDED
   graph; the expected ordering (library before binary) needs
   verification.

**What to build:**

**Stage 0 — Environment: ILP32 kernel.**
Obtain or build a RISC-V kernel with the ILP32 ELF binary loader.
Cross-compile on the Apple Silicon host (faster than in-guest).
Confirm `execve` of the static ILP32 spike-h adversary succeeds.
Confirm `seccomp_data.arch = AUDIT_ARCH_RISCV32` via a minimal test.

**Stage 1 — Arch-dispatch filter across exec.**
Build `seccomp_raw_test_s.c` (LP64). Install a single arch-dispatch
filter; fork + exec the static ILP32 adversary. Verify: RISCV32 socket
→ SIGSYS; RISCV32 write → rc=0; RISCV32 execve → SIGSYS. Also verify
the RISCV64 rules apply to the launcher before exec.

**Stage 2 — ld.so startup syscall set (phase-1 scope).**
Build a minimal ILP32 stub `libblyt32.so` and a dynamically-linked ILP32
test binary. Run under strace from exec through `main()`. Capture the
syscalls ld.so makes during ILP32 dynamic-library resolution. These
define the RISCV32 ld.so-phase rules in the phase-1 filter.

**Stage 3 — ILP32 native cart syscall allowlist (phase-2 scope).**
Run spike-i and spike-q cart workloads natively (using the stub
`libblyt32.so`) under strace. Subtract the Stage 2 ld.so-phase syscalls.
The remainder is the phase-2 allowlist. Verify empirically that
RISC-V ILP32 uses the same syscall NRs as LP64 (unified table; no
`mmap2`, `clock_gettime64` etc.). Produce `seccomp_allowlist_s.h`.

**Stage 4 — Option B end-to-end.**
Update the stub `libblyt32.so` to install the phase-2 RISCV32 filter
in its constructor. Build `launcher_s.c` (LP64) that installs phase-1
and exec's the cart. Verify: adversary socket → SIGSYS; execve →
SIGSYS; write → rc=0; full spike-i cart runs without SIGSYS. Commit
`seccomp_allowlist_s.h`.

**Success criterion:**

- Stage 0: `execve` of static ILP32 ELF succeeds; RISCV32 arch
  confirmed in seccomp_data.
- Stage 1: arch-dispatch filter correctly applies RISCV32 rules to the
  cart process and RISCV64 rules to the launcher in a single installed
  filter.
- Stage 2: ld.so startup syscall set characterised; phase-1 filter
  defined.
- Stage 3: `seccomp_allowlist_s.h` committed; RISCV32 and LP64 syscall
  NR equivalence confirmed (or ILP32-specific NRs documented).
- Stage 4: full cart runs under the phase-1 + phase-2 filter; adversary
  probes blocked; Option B constructor ordering confirmed.

**Dependency:** Spike R (raw BPF mechanics validated; LIFO semantics
confirmed; Fedora 42 QEMU infrastructure reused). Requires a RISC-V
kernel with the ILP32 ELF loader — this is the only external dependency.

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
    │       └── M (managed Lua coroutine save/restore) ← exercises a
    │           │                               real algorithm using
    │           │                               blyt32.coroutine over
    │           │                               K's save-state buffer
    │           └── N (hot-reload via save/restore) ← uses K's buffer
    │                                              as the migration
    │                                              vehicle; depends on M
    │                                              for the Lua suite's
    │                                              coroutine-edit cases
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
└── R (two-phase seccomp raw BPF) ← depends on H (gap identified); can
    │                                run immediately on QEMU without
    │                                hardware
    └── S (hardware trusted-exec seccomp) ← depends on R (raw BPF
                                             mechanics + LIFO confirmed);
                                             requires ILP32-capable kernel

I (cart format end-to-end) ← depends on ADR-0024/ADR-0025 and Spikes
                              C, F, G; can run in parallel with H; QEMU
                              makes the native-RISC-V dimension accessible
                              without physical hardware
                              ↑
                              Spikes J, K, L, M, N consume Spike I's
                              case binaries as their cart workloads

O (Rust cart end-to-end) ← depends on I (cart format + packing
                            pipeline), C (shared-lib ABI), H (native
                            RV32 execution); independent of M and N;
                            can run in parallel with M/N follow-ups
                            ↑
P (Rust heap + atomics + on_load) ← depends on O only; can run
                                     immediately
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

Spikes J, K, L, M, N de-risk the architectural questions left after I
lands. J answers whether `lua_sethook` can serve three concurrent
consumers (budget, throttle, debugger) and whether GDB DWARF unwinding
works through Spike I's PLT/GOT layout. K extends D's per-frame digest
result to a serialise-on-A / deserialise-on-B / continue round-trip —
the structural case D did not cover. L composes K's buffer with
libretro's callback-pull inversion of the runtime's frontend-pulls
model (ADR-0036). M exercises a real `blyt32.coroutine.create{}`
algorithm through K's save-state buffer — the question Spike K
explicitly deferred when it simplified the coroutine save blob to fixed
POD per slot. N reuses K's buffer (and M's coroutine machinery) as the
migration vehicle for ADR-0045 hot reload. J, K, L can run in parallel
with each other once their listed dependencies are met; M follows K
and precedes N's Lua suite. All can run in parallel with H (which is
gated on hardware, not on these).

Spike O de-risks Rust as a first-class cart language (ADR-0108). It
depends on I (cart format and packing pipeline), C (shared-library ABI
and link model), and H (native RV32 execution substrate), and is
independent of M and N. Its secondary output — numbered API findings
from real Rust cart code — feeds directly into any in-flight API ADRs,
making it most valuable while those decisions are still open.

Spike P de-risks the Rust heap allocator and atomics (ADR-0108) and
validates the `on_load` rewind contract (ADR-0087). It depends only on
Spike O and has now been executed.

Spike S extends Spike R to the hardware trusted native-exec path. It
depends on Spike R (raw BPF mechanics and LIFO semantics confirmed) and
requires a RISC-V kernel with the ILP32 ELF binary loader — either a
cross-compiled patched kernel booted with the Spike R QEMU rootfs, or
real RISC-V hardware with vendor ILP32 support. It can run independently
of all other post-R spikes.

Spike R de-risks the two-phase seccomp design in ADR-0116. It depends on
Spike H having identified the `libseccomp`/`SCMP_ARCH_RISCV32` gap and
does not require hardware — the same Fedora 42 QEMU environment used in
Spike H is sufficient. Its output (a committed `seccomp_allowlist.h`
and a proven two-phase filter mechanism) unblocks the production
implementation of the standalone OS sandbox.

---

## Followup status

Spikes A through Q have all been executed. Several spikes have outstanding items that have not yet
been closed — either because real hardware is not yet on hand, because
a manual / visual gate has not yet been performed, or because a clearly-
scoped piece of post-spike engineering has been logged for later. This
section is the single-pane summary so deferrals do not get lost across
result documents. **Authoritative detail lives in each spike's
`docs/design/spike-X-results.md` and `spikes/spike-X/TASKS.md`** —
entries here are pointers, not the full record.

### Status at a glance

| Spike | Status   | Hardware             | Manual gate                      | Eng. followup | External blocker                              |
|-------|----------|----------------------|----------------------------------|---------------|-----------------------------------------------|
| A     | partial  | Pi Zero 2 W          | —                                | —             | —                                             |
| B     | partial  | Pi Zero 2 W          | —                                | yes           | —                                             |
| C     | done     | —                    | —                                | yes           | Debian rv32 multilib gap                      |
| D     | done     | —                    | —                                | yes           | —                                             |
| E     | partial  | mid-range Android    | phone perf run                   | yes           | —                                             |
| F     | partial  | mid-range Android    | phone perf run                   | yes           | —                                             |
| G     | partial  | mid-range Android, iPhone | —                           | yes           | iOS App Store forces JSC (no V8 path)         |
| G.2   | failed   | —                    | —                                | superseded    | Chrome timer clamp (~100 µs, Spectre era)     |
| G.3   | done     | —                    | confirm in real VS Code web view | yes           | —                                             |
| H     | partial  | Milk-V Duo           | —                                | yes           | libseccomp `SCMP_ARCH_RISCV32` gap; Ubuntu kernel ILP32 compat |
| I     | done     | —                    | —                                | yes           | —                                             |
| J     | partial  | —                    | VS Code F5 recording             | yes           | —                                             |
| K     | done     | —                    | —                                | yes           | rv32emu `syscall_read` overflow (upstream fix) |
| L     | partial  | Linux+RetroArch host | RetroArch demo (5-behaviour gate) | yes          | —                                             |
| M     | done     | —                    | —                                | yes           | —                                             |
| N     | done     | —                    | —                                | yes           | —                                             |
| O     | done     | —                    | —                                | yes           | —                                             |
| P     | done     | —                    | —                                | yes           | —                                             |
| Q     | done     | —                    | —                                | yes           | —                                             |
| R     | pending  | —                    | —                                | —             | —                                             |

### Hardware-blocked items

These spikes pass on their substitute platforms (Docker emulation,
QEMU, headless drivers) but their headline numbers can only be
finalised on real silicon.

- **A, B — Pi Zero 2 W.** CoreMark / Embench MIPS measurement and the
  realistic-cart frame-time check on Cortex-A53 @ 1 GHz. Replaces the
  500 MIPS placeholder used as the cap throughout the rest of the
  spike chain.
- **E, F — mid-range Android (Snapdragon 7-class / Tensor G2-class).**
  Chrome and Firefox WASM perf for the cart workloads; iPhone JSC for
  the iOS path (V8 unavailable on iOS).
- **H — Milk-V Duo (C906).** Replaces QEMU placeholder for the cgroups
  v2 quota constants and confirms Stage 4's CPU-budget mechanism on
  real silicon, not just a kernel that runs CONFIG_COMPAT.

### Manual / visual gates pending

Headless drivers exercise the same code paths but a few interactive
gates need eyes on a real display.

- **E, F — phone-on-LAN harness run** in Chrome and Firefox; numbers
  posted back into the result docs.
- **G.3 — confirm in actual VS Code web view.** Spike ran in
  HeadlessChrome 147; cross-origin-isolation state in the live web
  view may differ.
- **J — VS Code F5 recording** using the launch configs in
  `case_c_dbg/.vscode/launch.json` and `case_b/.vscode/launch.json`.
- **L — RetroArch five-behaviour demo recording** on a Linux/amd64
  host with X11 + audio: F2/F4 round-trip, ≥ 5 s rewind playback,
  `retro_input_descriptor` rendering, no underruns over a 60 s run.

### Post-spike engineering — short list

Each entry below is a follow-up task that has been *logged* in the
spike's results doc but *not yet started*. Cross-reference each
spike's TASKS.md or "Next-step backlog" / "Open items" section for
the full story.

- **B:** GC tuning (`collectgarbage("setpause"/"setstepmul")`) and
  mob-pool reuse if Pi worst-case tic exceeds 8 ms; replace stub
  transcendentals with a real libm port; replace simplified `snprintf
  %g` and free-list malloc.
- **C:** Production rv32 libgcc multilib strategy; shared-heap
  allocator across cart and library; symbol versioning / relro /
  lazy-vs-eager binding; build tooling enforces `-no-pie` on cart
  links.
- **D:** Pin cross-compiler version explicitly in Dockerfile;
  broaden NaN canonicalization to every state-buffer write site (not
  just `frame_state_emit_digest`); coroutine / GC / audio /
  input-event determinism extension.
- **E:** Investigate whether rv32emu's JIT/T2C can be compiled to
  WASM (currently `INTERPRETER_ONLY=y`); cross-stack determinism diff
  vs Spike D digests on the WASM build.
- **F:** Sandbox-model asymmetry writeup (RV32IMFC vs Lua-direct
  WASM CPU/memory bounds); production `lua_init_libs.c` allowlist
  trim from Spike B; replace naive `strtof` with correctly-rounded
  parsing on both sides.
- **G:** Runtime startup prewarming loop (~30 invisible tic calls
  before first rendered frame); Tier 2 watchdog (`setTimeout` ~1s in
  Web Worker calling `terminate()` for tight-loops with no function
  calls).
- **G.3:** Per-cart D calibration from cart's measured Lua-line
  density (single fleet-wide D not viable); dev-UI Pi-parity feature
  surrounding UX; throttle-off-while-stepping behaviour for
  `LUA_MASKLINE × DAP` composition.
- **H:** Production seccomp filter as raw BPF handling both
  `AUDIT_ARCH_RISCV64` (LP64 launcher) and `AUDIT_ARCH_RISCV32`
  (ILP32 cart); allowlist `riscv_flush_icache` for musl RV32
  startup; confirm cart-spec `ilp32f` vs `ilp32d` builds used in
  spike.
- **I:** FlatBuffers parsing of `.cart.info` / `.cart.config`
  (currently 4-byte magic stubs); production resource directory;
  cart `EI_OSABI` identity check; real-time scheduler (currently
  fixed 10-frame loop); `memory_fill` to upstream rv32emu API.
- **J:** Patch rv32emu to register GDB stub's `cpu_ops` and call
  `fc_gdb_stub_check_break(pc)` per dispatch; ECALL surface for
  libconsolelua → rv32emu DAP IPC; run Spike G `doom_tick` bench
  under each variant for real overhead numbers; concurrent DAP+GDB
  attached to same runtime; transport choice (TCP localhost vs
  ADR-0044 stdio).
- **K:** Wrap Spike D Lua workloads in Stage 2 cart_state pattern;
  on-disk container format (atomic write, checksumming,
  cart-binary-hash tag from ADR-0013); cart-side `static`
  mutable-state audit + static analyser pass; build-time
  layout-mismatch test compiling two binaries with reordered fields;
  enforcement of ADR-0079 standard-library allowlist. (Two further
  K follow-ups — recursive Lua-table flattening, and the
  transient-`coroutine.create()` must-throw rule from ADR-0012 —
  were originally folded into Spike M's hot-reload scope per
  47cbeb4; with the new Spike M (managed Lua coroutine save/restore)
  taking the dedicated coroutine-mechanism slot, both follow-ups
  now belong to the new M and are listed there.)
- **L:** Wire `FACADE_MODE_RV32EMU` (production path) — extract
  rv32emu `main()` into library entry point; add
  `blyt_runtime_get_framebuffer` / `_get_palette` accessors to
  libconsole; replace `fc_console_main`'s 10-iteration loop with
  tick-driven `fc_console_tick(input_mask)`; add
  `console_button(player, "A")` Lua accessor. Codify "shallow
  restore" path in libblyt for carts with non-trivial `init`;
  palette storage decision (XRGB8888 canonical vs per-frontend
  conversion); codify slot-immutability constraint at libblyt level.
- **M:** Per-resume dirty-bit flatten cache (required before netplay
  — unconditional flatten per resume is too expensive at netplay
  cadence); wrapper-managed transient ID list for boundary-cross
  invalidation (spike's `mark_boundary_crossed` is a stand-in;
  production needs the runtime to serialise and auto-mark); slot-keyed
  body-id in slot bytes to simplify destroy/recreate cart topology
  mirroring; manifest-declared dynamic slot-table cap (ADR-0009
  coordination); wire Stages 3–5 sweeps into the top-level Makefile
  (currently run inline; digests checked in as the PASS manifest).
- **N:** Asset-only hot reload (validate "strict subset of code reload"
  claim once the asset-transform pipeline exists); WASM module
  re-instantiation reload path; QEMU native-cart reload harness; DAP-
  side signal protocol composition with Spike J; reload-while-debugging
  (N + J full composition); asset-manifest-moves test (struct-field
  reorder n6 is the stand-in; tilemap-moved-between-manifest-entries
  needs the asset-manifest layer); Lua-callback shape for `on_retype`
  (N's callbacks are C functions; production Lua carts may need Lua-
  side migration hooks); stub-packer latency measurements against a
  representative asset set (current numbers are code-only; production
  Phase 1 is asset-transform dominated).
- **O:** Production `blyt32` SDK crate (full handle set, full error
  surface, `bitflags!` for flag parameters, `blyt32-sys` / `blyt32`
  crate split); full packer Rust codegen (parse `cart.build.yaml`,
  emit typed constants for all fields and assets); `cart.build.yaml`
  language-dispatch integration (ADR-0073 `language: rust` path);
  cross-language carts (Rust + C library); Milk-V Duo native target;
  WASM target; `blyt_last_error()` heap-clone path; deep state-buffer
  proxy (`players[slot].x` pattern, deferred by ADR-0108 to post-v1);
  sequence API (`SeqStep`, `sequence!` macro).
- **P:** Enable `CONFIG_EXT_A=y` in the base spike-a rv32emu config
  (currently off; Spike P rebuilds inline — production images should
  enable it as baseline). `#[global_allocator]` declaration: blytbuild
  Rust codegen must emit the allocator declaration in the generated
  cart preamble for `heap_size > 0` carts (can't live in the SDK rlib).
  Production allocator hardening: OOM strategy (graceful `Result`-
  returning surface rather than panic), TLSF or buddy allocator
  evaluation for fragmentation. `spin::Once` or `once_cell` for mutable
  statics (replace `SyncCell<UnsafeCell<Option<T>>>` pattern). ADR-0087
  amendment: clarify `init → state restored → on_load` as the correct
  sequence for all load scenarios including fresh-process restore; note
  `on_start` split as a candidate follow-up once real-cart complexity
  warrants it. ADR-0108 amendment: document `#[global_allocator]`
  placement constraint and blytbuild codegen responsibility.
- **Q:** `__extendsfdf2` stub: libconsolelua.so's double-precision stubs return 0.0 (intentional for RV32IMAFC — no D extension). Lua's number-to-string path casts float→double via this stub, so float results display as "0.0". Workaround: fast_add pushes integer result; production fix is a correct bit-manipulation implementation. Also: `LUA_REGISTRYINDEX = -1001000` (not -16000), controlled by `LUAI_IS32INT` (whether int ≥ 32 bits = always true on RV32), not by `LUA_32BITS`.
- **Q:** `#[lua_module]` / `#[lua_export]` proc macro (generates the
  `#[link_section = ".lua_exports"]` static and Lua C API glue from function
  signatures, including correct `sym_addr` references). Production
  `rv32emu_call_fn` API: error classification, timeout handling, re-entrancy
  analysis, debugger-hook integration (Spike J composition). Public
  `rv_set_fpreg`/`rv_get_fpreg` accessors to replace direct `riscv_private.h`
  struct access. Full `.lua_exports` parser (section integrity validation,
  unknown type encodings). `FC32_SENTINEL_ADDR` reservation in the cart
  memory map (ADR-0082 or equivalent). Per-frame budget analysis for realistic
  Rust payloads (the Stage 3 measurement is a no-work floor). `#[global_allocator]`
  placement in hybrid Rust+Lua carts (interaction with Spike P's allocator
  when both the Lua VM heap and the Rust allocator share the guest address
  space). Multi-module carts (multiple `#[lua_module]` instances). ADR-0111
  amendment: note `sym_addr` generation responsibility (proc macro) and
  `FC32_SENTINEL_ADDR` reservation requirement.

### External blockers

These are not fixable inside the project; each requires either an
upstream patch, a vendor change, or a workaround we have shipped.

- **C** — Debian's `gcc-13-riscv64-linux-gnu` ships only rv64 libgcc
  multilib (no rv32). Workaround: divmod helpers shipped from cart
  side. Production needs a real fix.
- **G.2** — Chrome web-worker `performance.now()` clamped to ~100 µs
  (post-Spectre security mitigation tied to cross-origin-isolation).
  Fundamental Chrome behaviour, not a fixable bug. G.2 abandoned;
  G.3 took its place.
- **H / R** — `libseccomp` does not define `SCMP_ARCH_RISCV32`. Vendor
  gap requiring custom raw-BPF workaround (the subject of Spike R).
  Ubuntu kernel lacks ILP32 CONFIG_COMPAT (vendor choice, not bug);
  Fedora 42 kernel works.
- **K** — Upstream rv32emu `syscall_read` writes to a fixed 4 KiB
  host buffer without bounds-checking the cart-supplied count.
  Worked around in spike by chunking reads to 4 KiB on cart side;
  one-line upstream fix needed (`min(count, PREALLOC_SIZE)`).

### Spikes M and N

Both are complete.

- **Spike M (managed Lua coroutine save/restore) — PASS.** 928
  cross-host runs byte-identical. ADR-0012 amended (2026-05-06) with
  the single-function `create(function(ctx), seed?)` shape, constrained
  `ctx` shape, and load-resume idiom contract. Post-spike engineering
  follow-ups logged below.
- **Spike N (hot-reload via save/restore) — PASS.** 148 cross-host
  runs byte-identical; l6/l9/l10 clean-failure diagnostics byte-equal
  cross-host; all latency gates pass (< 3 s native, < 500 ms Lua).
  ADR-0045 and ADR-0083 both amended (2026-05-07). Post-spike
  engineering follow-ups logged below.
- **Spike O (Rust cart end-to-end) — PASS.** Four-way digest equality
  A=B=C=D (Rust arm64 = Rust amd64 = C arm64 = C amd64); float ABI
  witness `vol=3f000000` on both hosts; both compile-fail guards reject
  with the expected type errors (2026-05-08). Eight findings recorded
  (four confirmations, four production follow-ups). ADR-0108 amended
  (2026-05-09): target corrected to `riscv32imafc-unknown-none-elf`
  (Finding O-1), Rust elevated to primary native language alongside Lua,
  single-threaded A-extension rationale added. ADR-0001 amended
  (2026-05-09): A extension added to the ISA (RV32IMFC → RV32IMAFC).
  Post-spike follow-ups: full `blyt32` crate, full packer Rust codegen,
  `cart.build.yaml` language-dispatch integration, Milk-V Duo native
  target.
- **Spike P (Rust heap allocator, atomics, on_load contract) — PASS.**
  All five stages and all digest gates pass on arm64 and amd64
  (2026-05-09). Six findings recorded. Key results: (1) `linked_list_allocator`-backed
  `#[global_allocator]` works correctly under rv32emu — `Vec`, `Arc`,
  and the realloc growth path all execute without panics; 30-frame
  cross-host digests are byte-equal. (2) `AtomicU32` on
  `riscv32imafc-unknown-none-elf` uses LLVM single-threaded atomic
  lowering (plain loads/stores, no AMO opcodes); explicit inline
  assembly (`amoadd.w.aqrl`, `lr.w.aq`/`sc.w.rl`) is required to emit
  A-extension instructions — rv32emu executes them correctly once
  `CONFIG_EXT_A=y` is set (disabled by default in the spike-a image).
  (3) The `on_load` two-category contract is confirmed: state-derived
  heap data rebuilt from restored state buffer values; cross-host
  arm64-save → amd64-load digest gate passes. Two design findings
  require ADR follow-up: `#[global_allocator]` must be in the final
  cart crate (not the SDK rlib), and the correct load sequence is
  `init → state restored → on_load` (not bare `on_load` without prior
  `init`). ADR-0087 and ADR-0108 amendments proposed in
  `docs/design/spike-p-results.md`. Post-spike follow-ups logged below.
- **Spike Q (Lua+Rust hybrid binding: rv32 path and WASM call-on-demand) — PASS.**
  All four stages and all digest gates pass on arm64 and amd64, with the
  WASM/Node path byte-equal to both rv32 paths (2026-05-09). Six findings
  recorded. Key results: (1) `extern "C"` Lua C API calls from `no_std` Rust
  resolve correctly against `libconsolelua.so` in the RV32IMAFC guest — the
  Rust compiler emits the same `R_RISCV_CALL`/`R_RISCV_JUMP_SLOT` relocations
  as C, and fc32_dynload resolves them identically. (2) `#[link_section =
  ".lua_exports"]` with `#[used]` and `KEEP(*(.lua_exports))` in the linker
  script correctly emits the 80-byte-per-entry section and survives gc-sections.
  (3) rv32emu can be operated as a call-on-demand function server via a
  sentinel-ECALL mechanism: `rv32emu_call_fn` sets PC to the target function,
  places args in a0–a5 (integer) or fa0–fa5 (float), runs until ECALL 0xDEAD
  fires, and reads the return from a0 or fa0. The ilp32f float register ABI
  is confirmed (addf(1.0f, 2.0f) → 0x40400000 on both hosts). (4) A single
  Emscripten WASM module can contain both the Lua-direct runtime and rv32emu
  in call-on-demand mode; the trampoline layer correctly bridges the Lua stack
  and the rv32emu register ABI. Determinism across all three paths is
  byte-exact. Post-spike follow-ups: proc macro for `.lua_exports` and Lua C
  API glue generation, production `rv32emu_call_fn` API (error classification,
  timeout, re-entrancy), full `.lua_exports` parser, `rv_set_fpreg`/`rv_get_fpreg`
  public API, sentinel-address reservation in the memory map, per-frame budget
  analysis for realistic Rust payloads, heap allocation in hybrid carts.

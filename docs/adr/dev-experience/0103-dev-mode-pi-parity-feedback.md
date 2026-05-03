# ADR-0103: Dev-mode Pi-parity feedback — throttle vs projection

## Status
Proposed

## Context

The Pi-Zero-2 (or equivalent reference floor) is the slowest target the
runtime supports. In dev, authors work in WASM under VS Code, where the
host is typically 10–100× faster than the floor. Without some form of
feedback, a cart that runs at 60 fps in the dev shell may quietly miss
the floor's per-frame budget, and the author won't notice until they
deploy — or never, if they don't have the floor hardware on hand.

Spike G investigated four mechanisms for surfacing Pi-equivalent
performance in the dev shell. Two are now settled:

- **`LUA_MASKCOUNT` budget hook (Spike G).** Production Tier 1 mechanism
  for enforcing the per-frame instruction budget. Unchanged by this
  ADR; ships in production WASM builds for budget enforcement, not for
  Pi parity.
- **Per-line busy-wait `LUA_MASKLINE` (Spike G.2).** Ruled out — Chrome
  web-worker `performance.now()` clamps to ~100 µs resolution, two
  orders of magnitude above every calibrated `ns_per_line` value, so
  the busy-wait collapses to "wait for the next 100 µs tick" regardless
  of the configured delay.
- **Accumulated-debt `LUA_MASKLINE` (Spike G.3).** Mechanism works.
  Effective ns/line tracks configured D within ±2.5 % across a 5-bench
  matrix; jitter (p99/p50) ≤ 1.010. The 100 µs floor becomes a quantum
  rather than a noise floor: the spin trips ~once per 33 lines on
  doom_tick (cumulative debt crosses the next tick); the other 32
  lines pay only the cheap-path `clock_gettime` cost (~157 ns).

What G.3 settled is the *mechanism*. What it did not settle — and the
question this ADR captures — is what feature the mechanism is in
service of, and what calibration policy supports it. Two distinct
features can be built on top of G.3 (or alongside it), and they lock in
different downstream work:

- **Throttle.** Actually slow execution in the dev shell so a frame
  that costs 30 ms on host costs ~329 ms wall-clock there too —
  matching projected Pi runtime. The dev *feels* the floor.
- **Projection.** Run the cart at full host speed; display a
  *projected* Pi frame time alongside the actual host frame time
  ("12 ms / ~140 ms projected"). The dev *sees* the floor.

The two features overlap in mechanism (both need a host→Pi scale or
debt) but diverge in implementation, calibration burden, and UX.

## Decision

**To be made.** This ADR captures the option space, the work each
path locks in, and the validations needed before a decision lands. It
is filed Proposed; subsequent work either accepts one option (and
moves the ADR to Accepted with the chosen path) or supersedes it.

### Option A — Throttle only

Use Spike G.3's accumulated-debt hook in the dev shell. The cart
executes slowly; wall-clock frame time approximates Pi-projected frame
time.

- **Mechanism:** `LUA_MASKLINE` + accumulated-debt body. Build-time
  `THROTTLE_DELAY_NS` parameter; toggle on/off at cart launch.
- **Calibration:** must pick a `D` (ns/line) per cart. Two sub-options:
  - *Per-cart D from a startup warmup.* Run one tic with the count-only
    hook to measure `lines_per_frame`; one tic with the hook compiled
    out to measure `nohook_mean_ms`; solve `D = (pi_target_ms −
    nohook_mean_ms) × 1e6 / lines_per_frame`. Requires a
    `pi_target_ms` from somewhere (see Open Questions below).
  - *Fleet-wide D.* Ship a single calibrated value (e.g., D=3,000 from
    Spike G.3). Cheap; only fits carts whose ns_per_line is close to
    `doom_tick`. Other workloads miss by up to ~10× (the `ns_per_line`
    spread across the bench suite).
- **Bytecode constraint:** `LUA_MASKLINE` only fires when bytecode
  carries source-line info. Carts compiled with `luac -s` strip it; the
  hook becomes a silent no-op. Either commit to non-stripped bytecode
  for dev-mode builds, or fall back to `LUA_MASKCOUNT` over
  bytecode-instruction count with a separately-calibrated
  `ns_per_instruction`.
- **UX:** the cart is visibly slow. Devs feel the budget. Side effect:
  iteration is also slower — a 16 ms working frame stretches to ~300 ms
  in dev. May or may not be tolerable depending on the workload.

**Validations gated on this path:**

1. C-library drift recovery. G.3 used pure-Lua benches.
   `LUA_MASKLINE` doesn't fire inside C functions, so long
   `string.gsub` / `table.sort` calls advance wall time without
   advancing `target_ns`. The accumulated-debt design *handles* this
   correctly (subsequent lines return instantly until debt catches up),
   but whether the resulting "brief unthrottled span after a C call"
   feels acceptable in real game code is untested.
2. (*Sanity check, not gating.*) A short run in the actual VS Code
   renderer to confirm jitter and scheduling behavior match
   HeadlessChrome 147. The timer floor itself is no longer in question
   — VS Code's default state matches the spike host's (see Open
   Questions §5) — but JIT tier-up and worker scheduling could differ.
   Low priority.

### Option B — Projection only

Run the cart at full host speed. Multiply observed frame time by a
host-to-Pi ratio derived from a startup synthetic. Display projection
alongside actual frame time.

- **Mechanism:** none in the cart's hot path. A small startup harness
  runs a representative Lua workload, measures `host_synthetic_ms`,
  divides into a shipped `pi_ref_synthetic_ms` constant.
- **Calibration:** one ratio per host session. `pi_projected_ms =
  host_observed_ms × (pi_ref_synthetic_ms / host_synthetic_ms)`.
  Re-sample periodically (every N minutes, or on frame-time anomaly)
  to absorb thermal-throttling and power-profile drift.
- **Bytecode constraint:** none — the cart runs unmodified.
- **UX:** dev sees a continuously-updating projection without
  iteration penalty. Best for "show me my Pi number"; doesn't help
  devs who only notice problems by *feeling* them.

**Validations gated on this path:**

1. Ratio stability across cart workloads. The host-to-Pi ratio is
   theoretically more stable than `ns_per_line` (it's mostly a
   property of the two interpreters, not the workload) but Spike B's
   data shows op-mix-dependent variation. Run the existing 9 benches
   on host and on Pi; compute per-bench ratios; check whether the
   spread is < ±25 % (single ratio works) or matches the ~10×
   `ns_per_line` spread (single ratio is unsound; needs
   ratio-per-workload-type or accept large error).
2. Synthetic content. A blend of the existing 9 benches running for
   ~2 s post-JIT-warmup is a reasonable starting point. Final
   composition depends on (1).
3. Pi reference constant. Initially derived from Spike B's
   Docker arm64 rv32emu × 4–8× projection (carries the same 2×
   uncertainty band). Replaced by a measured-on-Pi value once
   hardware is available.

### Option C — Both, layered

Projection always on (low-cost, informational). Throttle as an opt-in
mode (high-cost, visceral). Calibration: synthetic-startup ratio gives
projection; per-cart D for the throttle is derived from the same ratio
plus a per-cart `lines_per_frame` measurement.

- **Mechanism:** projection's startup synthetic + G.3's accumulated-debt
  hook, behind a UI toggle.
- **Calibration:** combines both options' work. The host-to-Pi ratio
  feeds `pi_target_ms = host_actual × ratio` (continuously updated).
  When throttle is enabled, derive `D` from the live target.
- **Validations gated:** the union of Options A and B.

## Consequences

- **Spike G's production `LUA_MASKCOUNT` hook is unchanged** by any
  outcome of this ADR. Production Pi parity comes from the bytecode
  budget, which is enforced regardless of the dev feature.
- **Native CLI builds are unaffected.** Those use rv32emu plus
  ADR-0082's MIPS cap, which gives Pi parity by construction; no
  dev-feedback layer needed.
- **Whichever option ships, calibration policy is real follow-up work.**
  Spike G.3's mechanism precision (±2.5 % effective vs configured D)
  means calibration error dominates the dev-feedback signal, not
  mechanism error. Getting the ratio or `D` close to right matters more
  than getting the hook timing perfect.
- **Pi hardware arrival changes nothing about the option choice** but
  collapses the 4–8× rv32emu uncertainty band into a single measured
  number — improving every downstream calibration.

## Open questions

1. **Throttle, projection, or both?** The strategic decision this ADR
   exists to capture. Should land before any of the validations below
   are run, since they branch on the answer.
2. **What is `pi_target_ms`?** Three candidates:
   - A per-cart deadline the dev specifies ("I want this in 16.67 ms").
     Simple; tells the dev whether their *self-imposed* budget holds.
     Doesn't tell them whether their cart will actually run on Pi.
   - The cart's measured Docker arm64 rv32emu × 4–8× projection.
     Accurate per cart; expensive to obtain.
   - `host_observed_ms × host_to_pi_ratio` (the synthetic-startup
     scheme). Cheap; accuracy bounded by ratio stability.
3. **Bytecode lineinfo policy for dev builds.** Either preserve it
   (and accept slightly larger `.luac`) or strip it and lose
   `LUA_MASKLINE`-based mechanisms entirely. The asset pipeline
   (ADR-0088) doesn't currently address this.
4. **Re-calibration cadence for projection.** Periodic timer? On
   frame-time anomaly? On suspend/resume? Cheap to implement; design
   call.
5. **Renderer (web view) or extension host (Node) for the WASM Lua
   engine?** Upstream of the throttle/projection question; affects
   which mechanism is even needed.

   - *Renderer.* Subject to VS Code's default 100 µs `performance.now()`
     floor (no cross-origin isolation; `--enable-coi` reduces to 5 µs
     but is not a distribution path — extensions can't force it).
     G.3's accumulated-debt design is the portable answer, and the
     spike's HeadlessChrome 147 measurement transfers directly.
   - *Extension host.* `process.hrtime.bigint()` gives nanosecond
     resolution with no browser security clamp. The G.2/G.3 saga
     becomes irrelevant for the throttle: per-line busy-wait works,
     accumulated-debt works, both at full configured precision. Cart
     logic in a Node worker, IPC to renderer for graphics.

   G.3's mechanism is still the right answer for portable
   web-shell builds (itch.io, hosted dev demo), regardless of where
   the VS Code extension chooses to run. The Node-side path is a
   VS-Code-specific accuracy upgrade, not a replacement.

## References

- Spike G results — `docs/design/spike-g-results.md` (production
  `LUA_MASKCOUNT` hook).
- Spike G.2 results — `docs/design/spike-g.2-results.md` (per-line
  busy-wait failure analysis; first articulation of the 100 µs floor).
- Spike G.3 results — `docs/design/spike-g.3-results.md`
  (accumulated-debt mechanism PASS; ±2.5 % accuracy, 1.01× jitter).
- Spike B results — `docs/design/spike-b-results.md` (Docker arm64
  rv32emu means and the 4–8× Pi projection band).
- Early-validation spikes — `docs/design/early-validation-spikes.md` §G,
  §G.2, §G.3.

### External — VS Code / Chromium timer resolution

- VS Code does not enable cross-origin isolation by default;
  `performance.now()` resolution is **100 µs** in the renderer
  (matching HeadlessChrome 147 — the spike host). Launching with
  `--enable-coi` reduces it to 5 µs, but extensions cannot force this.
- `window.crossOriginIsolated` exposes the runtime state.
- Node-side (`process.hrtime.bigint()`) gives nanosecond resolution with
  no browser security clamp.
- Sources: Chrome Developers — *Aligning timers with cross-origin
  isolation*; W3C High Resolution Time spec; VS Code Discussion #156
  (*Towards cross-origin isolation*).

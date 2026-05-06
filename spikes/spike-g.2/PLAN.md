# Spike G.2 — implementation plan

**Question (per `docs/design/early-validation-spikes.md` §G.2):** Can
`LUA_MASKLINE` be used in the VS Code extension's dev WASM build to slow
Lua-direct execution to approximately Pi-class throughput, giving developers
accurate per-frame budget signals in their primary edit-run loop?

**Why this spike exists:** The VS Code extension must use a web view, which
runs the Lua-direct WASM build recommended by Spike F. That build executes
`doom_tick` in ~17 µs per inner tick (1.71 ms for 100 inner ticks, Spike F
mean). A Pi Zero 2 W running the same cart under `rv32emu` is projected at
~2,190–4,380 µs per inner tick (the Spike B Docker figure of 548 µs × the
4–8× Docker→Pi scaling ratio). The WASM Lua-direct build is therefore roughly
**128–256× faster than the Pi floor it is supposed to represent**. Developers
iterating in VS Code see frame times in the low-single-digit milliseconds; the
same cart on the declared minimum platform may miss the 16.67 ms budget
entirely. Without a dev-mode throttle, the extension gives systematically wrong
performance feedback in the tool developers use most.

Native CLI builds are not affected — they already run Lua through `rv32emu`
with ADR-0082's MIPS cap, so they naturally exhibit Pi-class throughput.
The throttle is needed only on the WASM Lua-direct path.

**The proposed mechanism:** Install `lua_sethook(L, hook_fn, LUA_MASKLINE, 0)`
at cart startup. `LUA_MASKLINE` fires when the Lua VM is about to execute a new
source line, and also when it takes a backward jump (so tight loops fire on
every iteration even without a line-number change). The hook busy-waits for a
calibrated `ns_per_line` duration, injecting the synthetic delay that makes
wall-clock frame times match the Pi projection.

**Why `LUA_MASKLINE` over `LUA_MASKCOUNT`:** `LUA_MASKCOUNT` fires after every
N Lua VM instructions, where N is set at hook-install time — it cannot fire more
than once per hook call, so per-instruction granularity requires N=1, which
doubles every Lua C function call into a hook dispatch. `LUA_MASKLINE` fires
once per source line, which is coarser than N=1 and finer than any
practically useful N for Spike G's `LUA_MASKCOUNT` usage. Per-line granularity
maps better to the concept "slow down Lua execution proportionally" than
per-instruction counting does — a line that contains a tight loop body still
fires every iteration via the backward-jump rule. The spike measures the actual
cost; no assumption about which wins is baked into the design.

---

## Inputs we already have

- `spikes/spike-g/hook_host.c` — the host shim to adapt. Spike G.2 needs the
  same structure (Lua state setup, per-tic pcall loop, FRAME/SUMMARY output,
  `now_ns()` using `clock_gettime(CLOCK_MONOTONIC)`) with the hook type
  changed from `LUA_MASKCOUNT` to `LUA_MASKLINE` and the hook body changed
  from a budget check to a busy-wait.

- `spikes/spike-g/Makefile` — build orchestration to mirror. Spike G.2 reuses
  spike-g's `emsdk` symlink (via `../spike-g/emsdk`), the Lua 5.4.7 source,
  and the bench `.lua` files. It does not re-vendor them.

- `spikes/spike-g/baselines/` — the no-hook WASM timing baselines.  The
  relevant no-hook figures are:

  | bench          | p99 (ms) | mean (ms) |
  |----------------|----------|-----------|
  | doom_tick      | 3.4      | 1.71      |
  | entity_update  | —        | —         |
  | binarytrees    | 5.5      | —         |
  | mandelbrot     | 8.5      | 4.08      |
  | spectral-norm  | 8.4      | —         |

  These are the WASM frame times the throttle must inflate to match the Pi.

- `docs/design/spike-b-results.md` — the Docker arm64 rv32emu timing baselines
  used to derive Pi projections. The load-bearing figures (mean µs per outer
  frame, where one outer frame = 100 inner ticks):

  | bench          | Docker mean (µs) | Pi @ 4× (µs) | Pi @ 8× (µs) |
  |----------------|------------------|--------------|--------------|
  | doom_tick      | 54,822           | 219,288      | 438,576      |
  | entity_update  | 14,490           | 57,960       | 115,920      |
  | binarytrees    | 63,398           | 253,592      | 506,784      |
  | mandelbrot     | 92,655           | 370,620      | 741,240      |
  | spectral-norm  | 111,528          | 446,112      | 892,224      |

  The calibration for each bench uses the midpoint of the 4–8× band (6×) as
  the Pi-proxy target. The success criterion is checked against both ends of
  the band (±25% of the midpoint covers the 4–8× uncertainty).

  The Spike B Docker→Pi ratio (4–8×) is itself a projection — the Pi has not
  been measured. The spike is calibrating to a target that carries this
  uncertainty; the results doc must document this. If real Pi numbers become
  available before this spike runs, use them instead.

---

## What we are NOT building

- **No VS Code extension code.** The spike produces a measurement and a
  recommendation; the extension integration follows from that.

- **No production WASM changes.** The throttle is a dev-mode-only feature. The
  production hook from Spike G (`LUA_MASKCOUNT`, N=100, 16.67 ms budget) is
  unchanged by whatever this spike finds.

- **No mobile measurement.** The throttle targets the VS Code extension's
  desktop web view — macOS and Windows hosts only. Mobile is out of scope.

- **No watchdog (`setTimeout`) plumbing.** Spike G's Tier 2 watchdog is already
  specified. This spike does not revisit it.

- **No fallback design.** If `LUA_MASKLINE` is ruled out, the fallback is
  documented as a follow-up recommendation (`LUA_MASKCOUNT` N=1 with per-call
  delay), not implemented in this spike.

- **No new bench workloads.** The same nine benches from Spike F/G are used.
  `doom_tick` and `entity_update` are the load-bearing ones because they
  represent realistic cart game logic; `mandelbrot`, `binarytrees`, and
  `spectral-norm` round out the picture.

---

## Approach

Four stages. Stages 1 and 2 establish the cost floor and line-rate data the
calibration depends on. Stage 3 derives the per-line delay constant. Stage 4
measures whether throttled WASM frame times match the Pi targets.

### Stage 1 — Raw `LUA_MASKLINE` overhead (no artificial delay)

The first question is whether `LUA_MASKLINE` is survivable at all. Even a no-op
hook — one that returns immediately without reading a clock or spinning — imposes
overhead because every source-line boundary in the Lua VM becomes an indirect C
function call. If that call overhead is already large relative to WASM frame
times, `LUA_MASKLINE` cannot serve as the throttle mechanism regardless of the
delay strategy.

1. Create `spikes/spike-g.2/` mirroring spike-g's layout:
   `Makefile`, `throttle_host.c`, `web/`, `baselines/`. Symlink
   `emsdk → ../spike-g/emsdk`. Set `LUA_SRC := $(CURDIR)/../spike-g/../spike-f/lua-src/src`
   and `BENCH_DIR := $(CURDIR)/../spike-f/benchmarks` in the Makefile —
   reusing spike-f's vendored sources as spike-g does.

2. Author `throttle_host.c` as a copy of `spikes/spike-g/hook_host.c` with the
   hook mechanism replaced:

   - Remove `HOOK_COUNT` / `HOOK_BUDGET_NS` / `HOOK_ENABLED` parameters.
   - Add `THROTTLE_ENABLED` (default `1`) and `THROTTLE_DELAY_NS` (default `0`
     for Stage 1) build-time parameters.
   - Replace `hook_fn` with `throttle_hook`:

     ```c
     static void throttle_hook(lua_State *L, lua_Debug *ar)
     {
     #if THROTTLE_DELAY_NS > 0
         uint64_t until = now_ns() + (uint64_t)THROTTLE_DELAY_NS;
         while (now_ns() < until) {}
     #endif
         (void)L; (void)ar;
     }
     ```

   - Replace the `lua_sethook` call at tic start with:

     ```c
     lua_sethook(L, throttle_hook, LUA_MASKLINE, 0);
     ```

     The third argument is the mask (`LUA_MASKLINE`); the fourth is unused for
     `LUA_MASKLINE` (it is meaningful only for `LUA_MASKCOUNT`).

   - The hook is installed once at `luaL_newstate` time, not per tic. Unlike
     Spike G's budget-check hook (which needs a fresh `tic_start_ns` each tic),
     the throttle hook carries no tic-level state — it reads wall clock on each
     firing and that is all.

3. Build `make throttle-wasm THROTTLE_DELAY_NS=0` (hook installed but
   `THROTTLE_DELAY_NS=0` so `now_ns()` is never called and the hook body is
   empty). Run `make node-test-all` and compare structure against spike-g's
   Node baselines (same bench names, same FRAME/SUMMARY format). This confirms
   the build and wiring are correct.

4. Build `make throttle-wasm THROTTLE_ENABLED=0` (hook compiled out entirely —
   the no-hook baseline built through this path). Run `make node-test-all`.
   This is the WASM timing floor for Stage 3.

5. Run the full nine-bench suite under Chrome with `THROTTLE_DELAY_NS=0` (hook
   active, no delay):

   ```
   make chrome-noop-overhead
   ```

   Captures `baselines/chrome-noop/chrome-overhead.json`. Compare p99 per bench
   to spike-g's no-hook Chrome baselines. Compute the no-delay overhead factor:

   ```
   noop_overhead_pct = 100 × (p99_noop − p99_nohook) / p99_nohook
   ```

   **Exit criterion for Stage 1:** If `doom_tick` no-delay overhead is
   < 50% on Chrome, proceed to Stage 2. If it exceeds 50%, `LUA_MASKLINE` is
   prohibitively expensive in its raw form and the spike concludes with outcome 3
   (fallback to `LUA_MASKCOUNT` N=1) — document and stop. The 50% threshold is
   deliberately loose: a 50% overhead on the no-delay hook still leaves room to
   measure whether the throttle signal is useful; the real threshold is whether
   the *throttled* frame times land within ±25% of the Pi target (Stage 4).

### Stage 2 — Line counting per workload

To derive the calibration constant we need lines-per-frame for each benchmark.
`LUA_MASKLINE` fires on every backward jump too, not just on new lines, so the
line count cannot be read from the `.lua` source — it must be measured at runtime.

6. Build a line-counting variant: `THROTTLE_DELAY_NS=0` with a file-scope
   `uint64_t line_count` incremented in the hook body, printed to stderr after
   the run. One build per bench is not needed — a single binary suffices because
   all benches are embedded and run sequentially.

   ```
   make node-linecount BENCH=doom_tick
   make node-linecount-all
   ```

   Captures line count per bench per frame to `baselines/linecount/<bench>.txt`.
   Record lines-per-outer-frame (`L_bench`) for each bench. The outer frame
   count follows spike-g's convention: 30 for `doom_tick`/`doom_tick_gc`, 50
   for `entity_update`, 20 for others.

7. Compute the calibration constant for each bench:

   ```
   wasm_mean_ns_bench  = mean frame time from Stage 1 no-hook run (ns)
   pi_target_ns_bench  = Spike B Docker mean × 6 × 1000  (ns)
                         (6 is the midpoint of the 4–8× band)
   delay_needed_ns     = pi_target_ns_bench − wasm_mean_ns_bench
   ns_per_line_bench   = delay_needed_ns / L_bench
   ```

   If `ns_per_line_bench < 0` for any bench, the WASM bench is already slower
   than the Pi midpoint target — no throttle is needed for that bench. Document
   and skip it in Stage 4 (it auto-passes).

   The `doom_tick` value is the primary calibration target. A single constant
   `ns_per_line` derived from `doom_tick` is used for all benches in Stage 4
   to test whether a workload-independent constant gives ±25% accuracy across
   the suite. If it does not, a per-bench constant variant is also measured.

### Stage 3 — Build the throttled WASM

8. Build a set of WASM modules with `THROTTLE_DELAY_NS` set to the computed
   calibration values:

   ```
   make throttle-wasm THROTTLE_DELAY_NS=<ns_per_line_doom_tick>
   ```

   Name the output `build/throttle_lua_D<delay_ns>.{js,wasm}`.

   Build the following variants:
   - `D<ns_per_line_doom_tick>` — primary: calibrated to `doom_tick` Pi midpoint
   - `D<ns_per_line_doom_tick × 0.75>` — low end (calibrated to 4× Pi)
   - `D<ns_per_line_doom_tick × 1.25>` — high end (calibrated to 8× Pi)
   - `D0` — no-delay baseline (reuse Stage 1 build)

   The three-variant sweep tests whether a plausible range of `ns_per_line`
   values brackets the Pi projection band and gives a ±25% signal.

### Stage 4 — Throttled frame times vs Pi targets

9. Run the full nine-bench suite on Chrome for each delay variant:

   ```
   make chrome-throttle-all
   ```

   Captures per-variant JSON to `baselines/chrome-D<ns>/chrome-throttle.json`.

10. For each (bench, delay_variant) pair, compute:

    - `p50_throttled_ms`, `p99_throttled_ms`
    - `pi_target_ms_low`  = Spike B Docker mean × 4 / 1000
    - `pi_target_ms_mid`  = Spike B Docker mean × 6 / 1000
    - `pi_target_ms_high` = Spike B Docker mean × 8 / 1000
    - `accuracy_pct` = 100 × |p50_throttled − pi_target_mid| / pi_target_mid

    The success criterion checks p50 (not p99) against the Pi midpoint: busy-
    wait loops inject extra wall time on top of real computation, so p50 is the
    best central-tendency estimate of synthetic throughput; p99 adds host jitter
    and is not the right measure of calibration accuracy.

    **Success criterion:** `accuracy_pct ≤ 25%` for `doom_tick` and
    `entity_update` at the midpoint calibration variant, on Chrome desktop.
    Wider miss is a partial pass if the Pi-projection uncertainty (4–8×) is the
    dominant term — document which factor would need to be true for the throttle
    to be correct. `binarytrees`, `mandelbrot`, and `spectral-norm` are reported
    but not gating: they have different line-density profiles and a single
    `ns_per_line` constant calibrated to `doom_tick` may not fit them precisely.

11. Assess jitter: compute `p99 / p50` for each throttled bench on Chrome. If
    this ratio exceeds 3× for `doom_tick` or `entity_update`, the throttle
    signal is too noisy to be useful as a developer tool — document as a fail
    even if the mean is in range.

---

## Risk notes

- **Busy-wait interference with the JS event loop.** The throttle host runs in
  a Web Worker (same as Spikes F/G). Busy-waiting in a Web Worker is legal —
  it does not block the main thread's event loop. However, V8 may observe that
  the WASM execution time has inflated dramatically and increase JIT tier-up
  aggressiveness (or de-optimise the Lua dispatch loop if it now looks "cold").
  If throttled Chrome numbers are not monotonically inflated relative to no-hook
  (i.e., if adding delay actually speeds up p50), this is the likely cause —
  document the JIT interaction and treat the result as unreliable.

- **`now_ns()` cost inside the busy-wait loop.** The tight spin loop calls
  `clock_gettime` on every iteration. On Chrome, Spike G measured
  `clock_gettime` overhead at ~83 µs total contribution out of ~900 µs Node
  overhead — small relative to the throttle delays we are injecting (which will
  be in the hundreds-to-thousands of µs range). This should not be a problem in
  practice, but if throttled frame times are systematically higher than targets,
  check whether the spin loop itself contributes excess overhead beyond the
  intended delay.

- **Line density varies across workloads.** A single `ns_per_line` calibrated
  to `doom_tick` will under-throttle workloads with fewer lines per wall-second
  and over-throttle those with more. `mandelbrot` (tight float loops with few
  line changes but many backward jumps from the inner loop) may behave very
  differently from `binarytrees` (recursive calls with many line changes). The
  per-bench constant variant (step 8) tests whether per-bench calibration is
  needed; if the single constant misses by more than ±25% on most benches, the
  throttle may need a per-workload tuning step at cart load time.

- **`LUA_MASKLINE` and C extensions.** As with `LUA_MASKCOUNT` in Spike G,
  `LUA_MASKLINE` does not fire while the Lua VM is inside a C function (e.g.
  `table.sort`, `string.find`). If a cart spends significant time in C library
  calls, those sections are unthrottled. This is an inherent limitation of any
  Lua hook — the dev-mode throttle is approximate, not exact. Document this
  clearly so developers know the signal is for pure-Lua-heavy carts; C-heavy
  code paths need separate validation.

- **Pi projection uncertainty.** The 4–8× Docker→Pi scaling factor is
  extrapolated from Spike A's CoreMark analysis; real Pi hardware has not been
  measured. The calibration target therefore carries a 2× uncertainty band.
  A throttle that lands within ±25% of the *midpoint* estimate is still useful
  even if the true Pi factor is at one extreme — developers get a signal that
  is within 2–3× of real Pi, which is far better than the current 128–256× gap.
  Flag the Pi-measurement dependency in the results doc so the calibration
  constant can be updated once real Pi numbers exist.

- **`LUA_MASKLINE` and JIT tier-up on Node vs Chrome.** Spike G found that Node
  and Chrome diverge ~27× on hook overhead due to V8 JIT tier differences.
  Stage 1's Node `node-test-all` run is a correctness check only; all timing
  measurements in this spike use Chrome exclusively. Do not report Node timing
  numbers as the primary result.

- **Busy-wait timer resolution floor.** `clock_gettime(CLOCK_MONOTONIC)` in
  Emscripten typically resolves to ~1 µs via `performance.now()`. If the
  computed `ns_per_line` value is smaller than ~1,000 ns (1 µs), the busy-wait
  loop exits immediately on almost every call (since the clock cannot report
  sub-µs elapsed), making the delay effectively zero. In that case the approach
  fails at the mechanism level — record the `ns_per_line` value and note if it
  hits this floor. (Given the estimated 128–256× slowdown target and a ballpark
  of tens of thousands of line events per frame, `ns_per_line` will likely be
  in the range of a few thousand to tens of thousands of nanoseconds, well above
  the resolution floor.)

---

## Deliverables

- `spikes/spike-g.2/Makefile` — orchestration. Key targets:
  - `make throttle-wasm [THROTTLE_DELAY_NS=<ns>]` — build
    `build/throttle_lua_D<ns>.{js,wasm}`.
  - `make throttle-wasm THROTTLE_ENABLED=0` — no-hook baseline.
  - `make node-test-all` — correctness check under Node.
  - `make node-linecount-all` — line counting run, writes
    `baselines/linecount/<bench>.txt`.
  - `make chrome-noop-overhead` — Stage 1 Chrome measurement with no-delay
    hook; writes `baselines/chrome-noop/chrome-overhead.json`.
  - `make chrome-throttle D=<ns>` — Stage 4 Chrome measurement for one delay
    variant; writes `baselines/chrome-D<ns>/chrome-throttle.json`.
  - `make chrome-throttle-all` — runs all three delay variants + baseline.

- `spikes/spike-g.2/throttle_host.c` — host shim with `LUA_MASKLINE` throttle.
  Build-time parameters: `THROTTLE_ENABLED`, `THROTTLE_DELAY_NS`.

- `spikes/spike-g.2/baselines/linecount/` — per-bench line-count files.

- `spikes/spike-g.2/baselines/chrome-noop/` — Stage 1 no-delay Chrome results.

- `spikes/spike-g.2/baselines/chrome-D<ns>/` — one directory per delay variant,
  Stage 4 Chrome results.

- `spikes/spike-g.2/baselines/calibration.md` — the calibration table:
  bench → Docker mean, Pi midpoint target, WASM no-hook mean, L_bench,
  ns_per_line_bench.

- `docs/design/spike-g.2-results.md` — write-up: Stage 1 noop overhead,
  line counts, calibration constants, throttled frame times vs Pi targets,
  outcome (1 / 2 / 3 per the design doc), recommendation.

- `spikes/spike-g.2/TASKS.md` — checklist, kept current as work proceeds.

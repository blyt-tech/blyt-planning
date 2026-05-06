# Spike G.3 — implementation plan

**Question (per `docs/design/early-validation-spikes.md` §G.3):** Can
`LUA_MASKLINE` be used as the dev-mode Pi-parity throttle if the hook body
tracks an *accumulated debt* against an absolute deadline, instead of the
per-line busy-wait that Spike G.2 measured?

**Why this spike exists:** Spike G.2 ruled out the per-line-busy-wait variant
on Chrome — `performance.now()` resolution in web workers is ~100 µs, two
orders of magnitude above every calibrated `ns_per_line` value the spike
needed (756–7,682 ns). The hook body asked the timer for sub-µs delays the
timer cannot deliver, and overshot Pi targets by 13–131×.

The accumulated-debt design changes what is asked of the timer. Each line
event adds its `ns_per_line` to a cumulative `target_ns`; the spin loop only
runs when `now_ns()` already lags `start_ns + target_ns`. The 100 µs floor
becomes a *quantum* (one or more ticks per spin) rather than a *noise floor*
(every line costs a full tick). Predicted Stage 4 accuracy: within ~1 % of
Pi target across the five measured benches; Spike G.2's actual error was
13–131×. Two orders of magnitude of difference, attributable entirely to the
hook body.

If Spike G.3 passes, Spike G.2's Outcome 3 is superseded — `LUA_MASKLINE`
becomes the dev-mode throttle. If it fails, fall back to the non-throttling
alternatives in `docs/design/spike-g.2-results.md` ("Other recommendations").

---

## Inputs we already have

- `spikes/spike-g.2/throttle_host.c` — the host shim to adapt. Spike G.3
  changes only the hook body (and adds `start_ns`/`target_ns` file-scope
  state); everything else (Lua state setup, per-tic pcall loop, FRAME/SUMMARY
  output, build-time parameters) is already in place.

- `spikes/spike-g.2/Makefile` — orchestration to copy. Spike G.3 reuses
  spike-g's `emsdk` symlink (via `../spike-g/emsdk`), spike-f's vendored Lua
  5.4.7 source, and the bench `.lua` files. It does not re-vendor them.

- `spikes/spike-g.2/baselines/calibration.md` — the calibration table. The
  `ns_per_line` constants are properties of the workload and the Pi target,
  not the hook body. Reuse them directly:

  | bench         | ns_per_line @6× |
  |---------------|-----------------|
  | doom_tick     | **3,000** (D_MID) |
  | entity_update | 2,162           |
  | binarytrees   | 7,682           |
  | mandelbrot    | 756             |
  | spectral-norm | 3,952           |

  The Stage 4 sweep uses D=2,250 / 3,000 / 3,750 — the same 4× / 6× / 8×
  Pi-band points G.2 used.

- `spikes/spike-g.2/baselines/linecount/<bench>.txt` — per-bench line counts.
  Reuse directly; line density is a property of the workload, not the hook.

- `spikes/spike-g.2/web/run_chrome.cjs` — the chrome runner. Spike G.3 reuses
  it via `cp` or symlink. The hardening from G.2's final session (try/catch
  around `page.close` / `browser.close` so partial-result JSON always lands
  on disk) is load-bearing for variants that genuinely overshoot — keep it.

---

## What we are NOT building

- **No re-measurement of Stage 1 (raw `LUA_MASKLINE` dispatch overhead).**
  G.2 already measured it: ~24 ns/line dispatch on Chrome, mean inflation
  +163 % on doom_tick at D=0. The same hook installation, same dispatch,
  same number — Spike G.3 inherits it. We do measure the *cheap path*
  (no-spin lines) under the new design as part of Stage 1' below, since that
  is now the dominant cost on lines where the spin doesn't trip.

- **No re-derivation of `ns_per_line` calibration.** G.2 already did this
  from Spike B Docker means; the constants are reused.

- **No re-running of the per-line-busy-wait variant.** G.2's data is the
  reference for that mechanism.

- **No new bench workloads.** Same nine benches from Spike F/G/G.2.
  `doom_tick` and `entity_update` are gating; `binarytrees`, `mandelbrot`,
  `spectral-norm` are reported but not gating (single `ns_per_line` calibrated
  to `doom_tick` may not fit them precisely).

- **No VS Code extension code.** The spike produces a measurement and a
  recommendation; integration follows.

- **No production WASM changes.** Dev-mode-only feature; Spike G's
  `LUA_MASKCOUNT` budget hook is unchanged.

---

## Approach

Three stages. Stage 1' establishes whether the cheap path (no-spin lines) is
cheap enough that the calibration prediction holds. Stage 2 builds the three
delay variants. Stage 3 measures throttled frame times against Pi targets.

### Stage 1' — Cheap-path overhead

The accumulated-debt hook does an extra `clock_gettime` per line on top of
G.2's noop hook. Most lines hit only the cheap path: read clock, compare to
deadline, return. The spike must measure whether this added clock read is
small enough that the predicted ~329 ms throttled doom_tick frame is not
dominated by cheap-path dispatch.

Predicted budget breakdown for doom_tick at D=3,000:

| component                           | per-line | per-frame      |
|-------------------------------------|----------|----------------|
| hook dispatch (G.2 measurement)     | ~24 ns   | ~2.6 ms        |
| `clock_gettime` cheap-path read     | TBD      | TBD            |
| spin trips (~3,275/frame × ~100 µs) | —        | ~327.5 ms      |
| no-hook baseline                    | —        | 1.59 ms        |
| **predicted total**                 | —        | **~331 ms + cheap-path overhead** |

If cheap-path `clock_gettime` costs > ~10 µs per line on Chrome, doom_tick
overshoots Pi target by > 25 %. Spike G's profiling found Node `clock_gettime`
at ~83 µs total contribution out of ~900 µs overhead — a ballpark of ~0.5 µs
per call. That is well within budget but Chrome is untested.

1. Copy `spikes/spike-g.2/` to `spikes/spike-g.3/`, preserving the `Makefile`
   targets and rewiring symlinks (`emsdk → ../spike-g/emsdk`, bench dir →
   `../spike-f/benchmarks`, Lua src → `../spike-f/lua-src/src`).

2. Edit `throttle_host.c` to replace the hook body:

   ```c
   #if THROTTLE_ENABLED
   static uint64_t target_ns = 0;
   static uint64_t start_ns  = 0;

   static void throttle_hook(lua_State *L, lua_Debug *ar)
   {
   #if LINECOUNT_ENABLED
       line_count++;
   #endif
       if (start_ns == 0) start_ns = now_ns();
       target_ns += (uint64_t)THROTTLE_DELAY_NS;
       uint64_t deadline = start_ns + target_ns;
       while (now_ns() < deadline) {}
       (void)L; (void)ar;
   }
   #endif
   ```

   Reset `start_ns` and `target_ns` to 0 at the top of each tic (in the for
   loop in `main`, just before `lua_pcall`). This is the per-tic-fresh
   approach Spike G's hook used; it prevents pause-between-tics wall time
   from being charged against the cart's frame budget.

3. Build `make throttle-wasm THROTTLE_DELAY_NS=0` (hook installed but the
   `target_ns += 0` no-op leaves the deadline at `start_ns`, so the spin
   loop never trips — same behavior as G.2's noop variant). Run
   `make node-test-all` for correctness.

4. Build `make throttle-wasm THROTTLE_DELAY_NS=1` (smallest non-zero — every
   line accumulates 1 ns of debt; spin trips ~once per 100,000 lines).
   Run `make chrome-noop-overhead` analogue (write to
   `baselines/chrome-cheap/chrome-overhead.json`). Compute the cheap-path
   overhead per line:

   ```
   cheap_per_line_ns = (chrome_p99_D1 − chrome_p99_nohook) / lines_per_frame
                       on doom_tick
   ```

   **Exit criterion for Stage 1':** if `cheap_per_line_ns < 1,000` (1 µs),
   proceed. The doom_tick budget has 327.5 ms of spin time and ~329 ms total
   target; cheap-path of < 1 µs/line × 109,173 lines = < 109 ms is acceptable
   (puts predicted total at < 440 ms vs target 329 ms — still inside ±25 %
   on doom_tick). If `cheap_per_line_ns ≥ 1,000`, the cheap path consumes
   the budget and the design is ruled out without proceeding to Stage 3.

### Stage 2 — Build the throttled WASM variants

5. Build the three delay variants using the same `ns_per_line` constants
   from `spikes/spike-g.2/baselines/calibration.md`:

   ```
   make throttle-wasm THROTTLE_DELAY_NS=2250
   make throttle-wasm THROTTLE_DELAY_NS=3000
   make throttle-wasm THROTTLE_DELAY_NS=3750
   ```

   Output: `build/throttle_lua_D{2250,3000,3750}.{js,wasm}`.

### Stage 3 — Throttled frame times vs Pi targets

6. Run the Chrome sweep:

   ```
   make chrome-throttle-all D_LOW=2250 D_MID=3000 D_HIGH=3750
   ```

   Captures per-variant JSON to `baselines/chrome-D{ns}/chrome-throttle.json`.

7. For each (bench, delay_variant) pair, compute the same metrics G.2 used:

   - `p50_throttled_ms`, `p99_throttled_ms`
   - `pi_target_ms_low / mid / high` from Spike B Docker × {4, 6, 8}
   - `accuracy_pct = 100 × |p50 − pi_target_mid| / pi_target_mid`
   - `jitter_ratio = p99 / p50`

8. **Success criterion:** at the D_MID variant, on Chrome desktop:

   - `accuracy_pct ≤ 25 %` for `doom_tick` and `entity_update`.
   - `jitter_ratio ≤ 3×` for `doom_tick` and `entity_update`.
   - `binarytrees`, `mandelbrot`, `spectral-norm` reported but not gating.

   Wider miss on a gating bench is a partial pass *only* if the Pi-projection
   uncertainty (the 4–8× band) is the dominant term — i.e. p50 lands inside
   `[pi_target_low, pi_target_high]` even if outside ±25 % of the midpoint.

---

## Risk notes

- **Cheap-path `clock_gettime` cost.** The new dominant per-line cost. If
  Chrome's worker `performance.now()` call (the JS shim Emscripten uses) is
  more expensive than Node's, cheap-path overhead could eat the calibration
  band. Stage 1' measures this directly. If it rules out the design, we have
  evidence Stage 3 was not going to pass either.

- **Drift recovery after C-library calls.** `LUA_MASKLINE` does not fire
  inside C functions. A long `string.gsub` advances wall time without
  advancing `target_ns`. Subsequent hook calls then see `now_ns() > deadline`
  and don't wait until target_ns naturally catches up. This is *correct*
  (don't re-throttle work the host already paid for) but the dev sees brief
  Pi-faster-than-throttled frames after C-heavy spans. Document and accept;
  same limitation as G.2 had.

- **Quantum overshoot jitter.** Each spin can overshoot the deadline by up
  to one full timer tick (~100 µs). For doom_tick (~3,275 trips/frame),
  σ ≈ √N · 25 µs ≈ 1.4 ms — well under the ±25 % calibration band. But
  jitter must be measured directly against the p99/p50 ≤ 3× criterion; the
  prediction's noise is independent of host scheduler jitter, which Stage 3
  also captures.

- **Per-tic state reset.** If `start_ns` and `target_ns` are not reset
  between tics, pause time between tics counts as elapsed wall time and
  the next tic finds `now_ns() > deadline` for many lines, suppressing
  throttle until target catches up. The reset placement is load-bearing —
  it must be inside the for-loop body, not at hook-install time.

- **`ns_per_line` calibrated to `doom_tick`.** Same as G.2: the single
  constant under-throttles workloads with fewer lines per wall-second
  (`fasta`, `nbody`) and over-throttles those with more (`mandelbrot`).
  Per-bench calibration is a follow-up if the single constant misses by
  > ±25 % on most reported benches.

- **Pi projection uncertainty (4–8×).** Same as G.2. The 4–8× Docker→Pi
  factor is extrapolated from Spike A; real Pi hardware is still unmeasured.
  The calibration target carries 2× uncertainty. Use measured Pi numbers
  if they exist by the time this spike runs.

- **Browser/host divergence.** Spike G.2 used HeadlessChrome/147 on Apple
  Silicon. The dev-host VS Code web view is Electron-Chromium; its
  cross-origin-isolation state can differ. If G.3 passes here, schedule a
  confirmatory measurement in the actual dev-host shell before integrating.

---

## Deliverables

- `spikes/spike-g.3/Makefile` — orchestration (copy of G.2's, with output
  paths rewritten to `spike-g.3`).
- `spikes/spike-g.3/throttle_host.c` — host shim with accumulated-debt hook.
- `spikes/spike-g.3/web/` — bench harness (copy of G.2's; possibly via
  symlinks, since the JS does not change).
- `spikes/spike-g.3/baselines/chrome-cheap/chrome-overhead.json` — Stage 1'
  cheap-path measurement at D=1.
- `spikes/spike-g.3/baselines/chrome-D{2250,3000,3750}/chrome-throttle.json`
  — Stage 3 sweep.
- `spikes/spike-g.3/baselines/calibration.md` — short note pointing back to
  Spike G.2's calibration table; the constants are identical.
- `docs/design/spike-g.3-results.md` — write-up: cheap-path overhead, Stage
  3 throttled frame times vs Pi targets, jitter, outcome (1 / 2 / 3 per the
  design doc), recommendation.
- `spikes/spike-g.3/TASKS.md` — checklist, kept current as work proceeds.

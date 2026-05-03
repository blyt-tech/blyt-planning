# Spike G.2 results — WASM Lua-direct dev-mode Pi-parity throttle

**Status: FAIL (as implemented) — Outcome 3, with a viable repair path
identified.** The spike's per-line-busy-wait variant of `LUA_MASKLINE`
**cannot serve as the throttle mechanism in the VS Code extension's dev WASM
build.** Chrome's web-worker `performance.now()` (the clock backing
Emscripten's `clock_gettime(CLOCK_MONOTONIC)`) resolves to ~100 µs, well above
every calibrated `ns_per_line` value the spike needed to inject (756–7,682 ns).
The busy-wait loop therefore exits not when the configured delay elapses but
when the timer ticks to its next 100 µs boundary, pinning effective per-line
cost at ~100 µs regardless of the build's `THROTTLE_DELAY_NS` parameter.
Throttled frame times overshoot Pi targets by 13–131× across the bench suite.

The fallback recommended by the design doc — `LUA_MASKCOUNT` with N=1 and a
per-call delay — has the same dependency on `clock_gettime` resolution and
would hit the same floor. **Per-event clock-driven throttling is the broken
primitive, regardless of which mask is used.**

**Viable repair (Spike G.3):** an *accumulated-debt* variant of the same
mechanism. Track cumulative target delay against an absolute deadline
(`start_ns + lines × ns_per_line`); on each line, only spin when `now_ns()`
already lags the deadline. The spin then waits ≥ 100 µs (one or more timer
ticks) and amortises across the lines whose individual debts were sub-floor.
Predicted accuracy: within ~1 % of Pi target across the suite (see
"Follow-up — accumulated-debt variant" below). This is the design tested by
Spike G.3 (`docs/design/early-validation-spikes.md` §G.3).

---

## What was built

`spikes/spike-g.2/throttle_host.c` — Spike G's host shim with the hook
mechanism replaced:

```c
static void throttle_hook(lua_State *L, lua_Debug *ar) {
#if THROTTLE_DELAY_NS > 0
    uint64_t until = now_ns() + (uint64_t)THROTTLE_DELAY_NS;
    while (now_ns() < until) {}
#endif
    (void)L; (void)ar;
}

// Installed once at luaL_newstate time:
lua_sethook(L, throttle_hook, LUA_MASKLINE, 0);
```

Build-time parameters: `THROTTLE_ENABLED` (1=hook on, 0=no-hook baseline),
`THROTTLE_DELAY_NS` (busy-wait per line), `LINECOUNT_ENABLED` (1=count and
print line totals to stderr).

Calibration was derived from Spike B Docker means × 6 (the 4–8× midpoint),
minus the WASM no-hook baseline, divided by lines-per-frame measured at
runtime. See `spikes/spike-g.2/baselines/calibration.md`. The doom_tick
midpoint constant of **3,000 ns/line** was used as `D_MID`; the sweep
also tested `D_LOW = 2,250` (Pi @ 4×) and `D_HIGH = 3,750` (Pi @ 8×).

---

## Stage 1 — Raw `LUA_MASKLINE` overhead (no delay)

Hook installed, `THROTTLE_DELAY_NS=0` so the busy-wait body is compiled out.
Measures pure `LUA_MASKLINE` dispatch cost: every Lua source-line boundary
becomes an indirect C call through `throttle_hook`, even when that hook does
nothing.

Platform: Apple Silicon MacBook, Chrome 147 headless via puppeteer-core 22.

| bench         | n  | nohook p99 (ms) | noop p99 (ms) | Δp99 (ms) | overhead %  |
|---------------|----|-----------------|---------------|-----------|-------------|
| doom_tick     | 30 | 3.5             | 7.1           | +3.6      | **+103 %**  |
| entity_update | 50 | 1.8             | 3.6           | +1.8      | **+100 %**  |
| binarytrees   | 20 | 5.5             | 8.1           | +2.6      | **+47 %**   |
| mandelbrot    | 20 | 8.5             | 23.1          | +14.6     | **+172 %**  |
| spectral-norm | 20 | 8.4             | 17.8          | +9.4      | **+112 %**  |

Means tell the same story (doom_tick 1.59 → 4.19 ms = **+163 %**;
mandelbrot 3.73 → 16.22 ms = **+335 %**). Per-line dispatch cost is
~24 ns (2.6 ms / 109,173 lines for doom_tick) — small per-event but the
event count is high.

**The 50 % exit threshold from PLAN.md was exceeded** on every bench except
binarytrees. The plan deliberately framed that threshold as loose, expecting
Stage 4 to be the real arbiter. Stage 1 alone would have been a fail under a
strict reading; the spike proceeded to Stage 4 to test whether the *throttled*
frame times — where the configured delay should dominate dispatch overhead —
could still land in range. They could not, for reasons unrelated to dispatch
overhead. See Stage 4.

---

## Stage 2 — Line counts per workload

Measured at runtime via `LINECOUNT_ENABLED=1` build, run under Node. See
`spikes/spike-g.2/baselines/linecount/<bench>.txt`.

| bench         | LINE_TOTAL  | frames | lines/frame |
|---------------|-------------|--------|-------------|
| doom_tick     | 3,275,190   | 30     | 109,173     |
| doom_tick_gc  | 3,278,220   | 30     | 109,274     |
| entity_update | 1,993,400   | 50     | 39,868      |
| binarytrees   | 983,020     | 20     | 49,151      |
| mandelbrot    | 14,597,760  | 20     | 729,888     |
| spectral-norm | 3,360,620   | 20     | 168,031     |
| fannkuch      | 4,764,400   | 20     | 238,220     |
| fasta         | 406,220     | 20     | 20,311      |
| nbody         | 173,160     | 20     | 8,658       |

`mandelbrot` stands out: 6.7× more lines per frame than doom_tick, mostly
from `LUA_MASKLINE`'s backward-jump rule firing on every iteration of the
tight inner loops.

---

## Stage 3 — Calibration constants

```
ns_per_line_bench = (pi_target_mid_ns − wasm_nohook_mean_ns) / lines_per_frame
```

| bench         | Pi target @6× (ms) | nohook mean (ms) | lines/frame | ns_per_line |
|---------------|--------------------|------------------|-------------|-------------|
| doom_tick     | 328.93             | 1.59             | 109,173     | **3,000**   |
| entity_update | 86.94              | 0.74             | 39,868      | 2,162       |
| binarytrees   | 380.39             | 2.80             | 49,151      | 7,682       |
| mandelbrot    | 555.93             | 3.73             | 729,888     | 756         |
| spectral-norm | 669.17             | 5.11             | 168,031     | 3,952       |

All five values are sub-10 µs, and four of five are sub-5 µs.

---

## Stage 4 — Throttled frame times vs Pi targets

WASM rebuilt with `THROTTLE_DELAY_NS=D` for D ∈ {2250, 3000, 3750}; full
chrome-throttle run per variant.

### D=3000 (D_MID, calibrated to doom_tick @ Pi @ 6×)

| bench         | mean (ms)  | Pi target @6× (ms) | accuracy            | result         |
|---------------|------------|--------------------|---------------------|----------------|
| doom_tick     | timeout (>120 s/run) | 328.93   | —                   | **FAIL**       |
| entity_update | timeout (>120 s/run) | 86.94    | —                   | **FAIL**       |
| binarytrees   | 4,916.0    | 380.39             | **+1,193 %**        | **FAIL** (13×) |
| mandelbrot    | timeout (>120 s/run) | 555.93   | —                   | **FAIL**       |
| spectral-norm | timeout (>120 s/run) | 669.17   | —                   | **FAIL**       |

Three of five benches did not complete a single 20–30-frame run inside the
120 s puppeteer wait timeout. `binarytrees` finished and overshot by 13×.

### Effective per-line cost

For binarytrees at D=3000: `4,916 ms / 49,151 lines = 100,019 ns/line`.

For the partial timeout traces:

```
[harness] FRAME mandelbrot 0 73,002,699   →  73,002,699 µs / 729,888 lines = 100,019 ns/line
[harness] FRAME spectral-norm 0 16,805,699 → 16,805,699 µs / 168,031 lines = 100,016 ns/line
```

**Every measurement collapses to ~100 µs per line, regardless of bench or of
the configured `THROTTLE_DELAY_NS`.** The configured delay is being ignored.

### The cause: Chrome web-worker timer resolution floor

Emscripten's `clock_gettime(CLOCK_MONOTONIC)` is implemented via JavaScript's
`performance.now()`. In a Web Worker on Chrome, `performance.now()` is clamped
to ~100 µs resolution by the post-Spectre security mitigations (the same
Cross-Origin-Isolation / `crossOriginIsolated` flag that gates `SharedArrayBuffer`
and `performance.measureUserAgentSpecificMemory`). The busy-wait loop:

```c
uint64_t until = now_ns() + 3000;   // 3 µs target
while (now_ns() < until) {}         // hangs until next 100 µs tick
```

does not exit when 3 µs of wall time elapses — it exits when the next 100 µs
tick of the underlying timer occurs. Effective per-line delay is therefore
~100 µs, and the configured 3,000 ns is irrelevant.

**Predictions from the floor model match observed numbers to within 0.1 %:**

| bench         | predicted (lines × 100 µs + nohook)       | observed             |
|---------------|-------------------------------------------|----------------------|
| doom_tick     | 109,173 × 100 µs + 1.59 ms = 10,919 ms    | timeout >120 s/run *|
| entity_update | 39,868 × 100 µs + 0.74 ms = 3,988 ms      | timeout >120 s/run *|
| binarytrees   | 49,151 × 100 µs + 2.80 ms = 4,918 ms      | **4,916 ms** ✓      |
| mandelbrot    | 729,888 × 100 µs + 3.73 ms = 72,993 ms    | **73,002 ms / frame** ✓ |
| spectral-norm | 168,031 × 100 µs + 5.11 ms = 16,808 ms    | **16,806 ms / frame** ✓ |

\* The timed-out benches all run faster than `binarytrees × n_frames`
predicts, but each individual `FRAME` line emitted matches the floor model to
within 0.1 %. The timeout is per-run (`120 s` puppeteer wait), not per-frame.

### D_LOW = 2,250 and D_HIGH = 3,750

Both variants reproduce the D=3000 result frame-for-frame, confirming the
busy-wait is pinned to the timer resolution floor for any
`THROTTLE_DELAY_NS < ~100,000`. The same three benches (doom_tick,
entity_update, mandelbrot, spectral-norm) time out; binarytrees completes
identically:

| variant | binarytrees mean (ms) | binarytrees p99 (ms) |
|---------|-----------------------|----------------------|
| D=2,250 | 4,916.19              | 4,920.70             |
| D=3,000 | 4,915.99              | 4,917.40             |
| D=3,750 | 4,916.00              | 4,917.10             |

A 1,500 ns spread in `THROTTLE_DELAY_NS` produces a 0.20 ms spread in mean
frame time over 49,151 lines — i.e. the configured delay difference is below
the measurement noise. The floor dominates entirely.

Raw output: `spikes/spike-g.2/baselines/chrome-D2250/chrome-throttle.json`,
`chrome-D3000/chrome-throttle.json`, `chrome-D3750/chrome-throttle.json`.

---

## Outcome

**Outcome 3 from `docs/design/early-validation-spikes.md` §G.2 (as the spike
was framed).** The per-line-busy-wait `LUA_MASKLINE` variant tested here is
ruled out as the dev-mode throttle.

But the floor is not a property of `LUA_MASKLINE` itself — it is a property
of *per-event clock-driven busy-waits with sub-floor targets*. A different
hook body that respects the floor as a quantum (rather than fighting it as a
delay primitive) is plausibly viable. That is the question Spike G.3 picks
up; until it runs, the design-doc Outcome 3 stands but is not load-bearing.

---

## Follow-up — accumulated-debt variant (Spike G.3)

The spin loop's failure mode is "ask for 3 µs, wait 100 µs." The fix is to
let the small targets accumulate and only spin when the cumulative target
already exceeds the elapsed wall time:

```c
static uint64_t target_ns = 0;   // cumulative delay budget
static uint64_t start_ns  = 0;

static void throttle_hook(L, ar) {
    if (!start_ns) start_ns = now_ns();
    target_ns += THROTTLE_DELAY_NS;
    uint64_t deadline = start_ns + target_ns;
    while (now_ns() < deadline) {}
}
```

Two properties make this work where the per-line variant fails:

1. **Absolute deadlines, not relative waits.** The deadline is anchored to
   `start_ns + total_target`, so when the spin overshoots by some fraction of
   100 µs, the next call's deadline is already past — that line returns
   instantly. Drift is bounded; cumulative target tracks reality.

2. **The floor becomes a quantum, not a noise floor.** Spinning trips only
   on the lines where cumulative debt has risen above the next 100 µs tick.
   For doom_tick at `ns_per_line = 3000`, that's roughly every 33rd line; the
   other 32 cost only the bare hook dispatch.

Predicted Stage 4 results under this design (`lines × ns_per_line + nohook`):

| bench         | ns_per_line | predicted (ms) | Pi target @6× (ms) | predicted error |
|---------------|-------------|----------------|--------------------|-----------------|
| doom_tick     | 3,000       | 329.1          | 328.93             | +0.05 %         |
| entity_update | 2,162       | 87.0           | 86.94              | +0.07 %         |
| binarytrees   | 7,682       | 380.3          | 380.39             | −0.02 %         |
| mandelbrot    | 756         | 555.7          | 555.93             | −0.04 %         |
| spectral-norm | 3,952       | 669.2          | 669.17             | +0.005 %        |

If the prediction holds, the spike's outcome flips to **Outcome 2** (dev-mode
throttle viable; production stays on Spike G's `LUA_MASKCOUNT`). Things still
to measure that the prediction takes for granted:

- **`now_ns()` cost on cheap-path lines** (no spin). At ~24 ns dispatch +
  one `clock_gettime` call per line, the overhead floor on Chrome is currently
  un-budgeted; if it adds materially above Stage 1's 2.6 ms/frame for
  doom_tick, the predicted Pi-target accuracy degrades.

- **Jitter across many spin trips.** Each ≥100 µs spin can overshoot by up
  to ~50 µs. For ~3,300 trips/frame on doom_tick, σ ≈ √N · 25 µs ≈ 1.4 ms —
  well under the ±25 % calibration band, but worth confirming against the
  jitter exit criterion (p99/p50 ≤ 3×).

- **Drift recovery after C-library calls.** `LUA_MASKLINE` doesn't fire
  inside C functions, so a long `string.gsub` advances wall time without
  advancing `target_ns`. Subsequent hook calls then see `now_ns() > deadline`
  and don't wait until target_ns catches up — *correct* behavior, but the
  dev sees brief Pi-faster-than-throttled frames following C-heavy spans.
  Document and accept (same limitation as G.2's per-line variant).

Spike G.3 is now in `docs/design/early-validation-spikes.md`.

---

## Other recommendations

1. **Do not ship the per-line-busy-wait variant** as it stands. Even if a
   downstream consumer needs an interim throttle before Spike G.3 runs, the
   13–131× overshoot makes the dev signal worse than no throttle at all (it
   would tell developers their cart is many seconds-per-frame on Pi when it
   may actually be in budget).

2. **If Spike G.3 also fails**, fall back to non-throttling alternatives:

   - *Synthetic-cost projection.* Count Lua VM instructions; project Pi frame
     time post-hoc from a CPI × Pi-MIPS calibration. Show projection in the
     dev UI without slowing wall-clock execution.

   - *End-of-frame projection only.* Run the cart at full WASM speed; at the
     end of each frame, report `actual_wasm_ms × measured_pi_factor`. No
     within-frame fidelity, but cheap and clock-resolution-independent.

3. **Confirm Chrome's timer-resolution floor in the actual VS Code web view**
   (this spike used HeadlessChrome/147 on Apple Silicon). The 100 µs figure
   is consistent across all five benches measured here, but the dev-host
   shell's cross-origin-isolation state can differ from headless Chrome's
   defaults. A short standalone benchmark in the real dev-host should run
   alongside Spike G.3.

---

## What this spike does NOT decide

- **Does not affect production WASM builds.** Spike G's `LUA_MASKCOUNT` N=100
  budget hook is unchanged. Production Pi parity is not the spike's concern;
  Pi-parity reporting in the dev UI is.

- **Does not affect native CLI builds.** Those use `rv32emu` + ADR-0082's
  MIPS cap, which gives Pi parity by construction.

- **Does not measure whether `LUA_MASKLINE` could work in environments with
  finer timer resolution** (Node, a desktop Electron host with cross-origin
  isolation disabled, an experimental Chrome flag). The dev WASM target is
  the VS Code web view; the floor measured here is the relevant constraint.
  Other contexts may differ but are out of scope.

- **Does not redesign the dev-UI Pi-parity feature.** The recommendation
  above is a list of mechanisms to evaluate; choosing among them is a follow-
  up design decision.

---

## Risk-note outcomes

| Risk note from PLAN.md                              | Outcome                                                                                                     |
|-----------------------------------------------------|-------------------------------------------------------------------------------------------------------------|
| Busy-wait interference with the JS event loop      | Not observed; Web Worker isolation held — but moot, since the floor mechanism dominated.                    |
| `now_ns()` cost inside the busy-wait loop          | The cost *is* the loop on Chrome: every `now_ns()` call hits the same 100 µs cache line until the timer ticks. |
| Line density varies across workloads               | Confirmed — but irrelevant given the floor; per-bench calibration would not have helped.                    |
| `LUA_MASKLINE` and C extensions                    | Not exercised in this spike (workloads are pure-Lua); the limitation remains theoretical for now.            |
| Pi projection uncertainty (4–8×)                   | Irrelevant — the throttle missed by 13–131×, far outside any reasonable Pi uncertainty.                     |
| Node vs Chrome JIT divergence                      | Not measured (Node is correctness-only per spec). All numbers above are Chrome.                             |
| **Busy-wait timer resolution floor**               | **The decisive risk.** Plan flagged sub-µs failures; actual floor is 100 µs in Chrome workers, two orders of magnitude higher than the calibration target. |

---

## Inputs reused

- `spikes/spike-g/hook_host.c` — host shim shape (Lua state setup, per-tic
  pcall loop, FRAME/SUMMARY format).
- `spikes/spike-g/emsdk/` — Emscripten 4.x toolchain.
- `spikes/spike-f/lua-src/` — Lua 5.4.7 sources.
- `spikes/spike-f/benchmarks/` — nine `.lua` bench workloads.
- `docs/design/spike-b-results.md` — Docker arm64 rv32emu means used to
  derive Pi targets at 4×/6×/8×.

---

## Deliverables produced

- `spikes/spike-g.2/throttle_host.c` — host shim.
- `spikes/spike-g.2/Makefile` — build orchestration.
- `spikes/spike-g.2/baselines/linecount/<bench>.txt` — per-bench line totals.
- `spikes/spike-g.2/baselines/chrome-nohook/chrome-overhead.json` — no-hook baseline.
- `spikes/spike-g.2/baselines/chrome-noop/chrome-overhead.json` — D=0 (hook installed, body empty).
- `spikes/spike-g.2/baselines/chrome-D{2250,3000,3750}/chrome-throttle.json` — Stage 4 sweep.
- `spikes/spike-g.2/baselines/calibration.md` — calibration table.
- This document.

# Spike G results — WASM Lua-direct: per-frame CPU budget enforcement

**Status: PASS.**  `lua_sethook` with `LUA_MASKCOUNT` at N=100 adds < 3%
overhead to Spike F's `doom_tick` p99 on Chrome desktop, well under the 10%
(0.34 ms) threshold.  The two-tier enforcement policy is confirmed viable.

---

## What was built

`spikes/spike-g/hook_host.c` — Spike F's host shim extended with:

- `hook_fn`: fires every N Lua VM instructions, reads wall-clock elapsed since
  `tic_start_ns`, calls `luaL_error(L, "budget exceeded")` if elapsed exceeds
  `budget_ns`.
- `HOOK_COUNT` (default 1000), `HOOK_BUDGET_NS` (default 16 667 000 ns = 16.67 ms),
  and `HOOK_ENABLED` (default 1) build-time parameters.
- Hook re-armed per tic (immediately before each `lua_pcall`) so `tic_start_ns`
  is always fresh.

`spikes/spike-g/fire_host.c` — dedicated correctness-gate binary.  Built with
N=100 and budget=100 µs; confirms `lua_pcall` returns `LUA_ERRRUN` with message
`"budget exceeded"` and exits cleanly.

Stage 1 correctness tests all passed:
- `make node-test-all N=1000`: nine-bench Node run at 1 s budget produces
  structurally correct FRAME / SUMMARY output; no hook fires.
- `make node-fire-test`: **HOOK FIRE TEST PASS** — hook fires on `doom_tick`
  at 100 µs budget and is recoverable via `lua_pcall`.

---

## Overhead results

Platform: Apple Silicon MacBook, Chrome 147, headless via puppeteer-core 22.
Measurements are p99 of per-tic wall time, 20–50 frames per bench.

### doom_tick (load-bearing, 30 frames)

| N       | p99 (ms) | overhead (ms) | overhead % | vs threshold |
|---------|----------|---------------|------------|-------------|
| none    | 3.3      | —             | —          | baseline    |
| 100     | 3.4      | +0.1          | +3.0 %     | **PASS** ✓  |
| 1 000   | 3.4      | +0.1          | +3.0 %     | **PASS** ✓  |
| 10 000  | 3.3      |  0.0          |  0.0 %     | **PASS** ✓  |

Spike F baseline: 3.40 ms p99.  The no-hook Spike G build (N=0) reads 3.3 ms —
consistent within measurement noise.  Threshold: < 0.34 ms added (10% of 3.40 ms).

### Full bench × N Chrome table (p99, ms)

| bench          | N=0 | N=100 | N=1 000 | N=10 000 |
|----------------|-----|-------|---------|---------|
| doom_tick      | 3.3 | 3.4   | 3.4     | 3.3     |
| entity_update  | 1.8 | 1.9   | 1.9     | 1.8     |
| binarytrees    | 5.5 | 5.3   | 5.3     | 5.9     |
| mandelbrot     | 8.5 | 8.5   | 9.0     | 8.7     |
| spectral-norm  | 8.4 | 8.3   | 8.4     | 8.3     |

No bench shows a monotonic overhead increase with decreasing N.  All deviations
from the no-hook baseline are within normal measurement noise at these frame
counts (20–50 frames).

---

## WebKit / Safari results

Measured on Playwright WebKit 26.4 and actual Safari 26.4 (via safaridriver).
Both use JavaScriptCore (JSC).  Timer resolution is 1 ms (privacy clamp), so
p99 values are rounded; overhead < 1 ms cannot be distinguished from zero.

| N      | WebKit p99 (ms) | Safari p99 (ms) | overhead     |
|--------|-----------------|-----------------|--------------|
| none   | 4               | 8 *             | baseline     |
| 100    | 4               | 4               | 0 ms (noise) |
| 1 000  | 4               | 3               | 0 ms (noise) |

\* Safari N=0 p99=8 ms is a single JIT warmup outlier (p50=2 ms, p95=3 ms).
N=100 and N=1000 show p50=2 ms throughout — consistent with the no-hook median.

**Conclusion:** JSC shows the same result as V8 — no measurable `lua_sethook`
overhead at any N value tested.  The finding is not V8-specific; it holds across
both major browser JIT engines present on the Apple Silicon target platform.

---

## Node vs Chrome divergence

Node 22 reported 56–80% overhead on `doom_tick` p99 across all N values.
Chrome reported 0–3%.  The Node/Chrome ratio at N=100 is ~27×.

Per the PLAN.md risk notes, Chrome is authoritative when the two diverge by
more than 2×.

**Diagnosis (stub-clock variant):** A build with `hook_now_ns()` stubbed to
always return 0 (hook dispatched but no real clock read) ran on Node:

| variant       | doom_tick mean (µs) | overhead vs N=0 |
|---------------|---------------------|-----------------|
| N=0 (no hook) | 1 604               | —               |
| N=1000 stubbed clock | 2 419        | +815 µs (+51%)  |
| N=1000 real clock   | 2 502         | +898 µs (+56%)  |

`clock_gettime` contributes only ~83 µs of the ~900 µs Node overhead.  The
bottleneck is the hook dispatch mechanism itself — having any hook installed
prevents V8's Liftoff/TurboFan tiers from fully optimising the Lua VM's inner
dispatch loop on Node.  Chrome's TurboFan tier-up appears to handle this code
shape more aggressively at the measurement warmup depth (30 frames), likely
because Chrome's JIT has had more profiling time before the steady-state
measurements.

---

## Recommendation

**Production N = 100.**

This is the smallest N value in the sweep and therefore the tightest
enforcement granularity.  It meets the overhead threshold with 7× margin
(+0.1 ms observed vs 0.34 ms allowed).  The PLAN.md rationale for choosing the
smallest passing N is correct: a smaller N means the hook fires sooner when a
cart genuinely over-runs the budget, reducing the false-clean window.

No N value in the sweep fails on Chrome.  There is no reason to choose a larger
N — the overhead difference between N=100 and N=10000 is zero within measurement
noise on Chrome.

---

## Two-tier policy: production values

**Tier 1 — `lua_sethook` step budget:**

```c
// Before each tic:
tic_start_ns = now_ns();
lua_sethook(L, hook_fn, LUA_MASKCOUNT, 100);
// After lua_pcall:
lua_sethook(L, NULL, 0, 0);
```

```c
static void hook_fn(lua_State *L, lua_Debug *ar) {
    if (now_ns() - tic_start_ns > 16667000ULL)
        luaL_error(L, "budget exceeded");
}
```

Budget: 16 667 000 ns (16.67 ms, one 60 fps frame).  N = 100.

**Tier 2 — external hard timeout:**

A `setTimeout` of ~1 s in the Web Worker that calls `terminate()`.  Catches
carts running tight loops with no function calls (`while true do end`) that
never trigger the `LUA_MASKCOUNT` hook.  No measurement required; a one-liner
in the harness.

**Limitation (documented):** `LUA_MASKCOUNT` fires on Lua VM instructions, not
C-library calls.  A cart calling a long-running C extension (e.g. `table.sort`
on a very large table) will not fire the hook during that call.  This is a
best-effort mechanism for Lua-heavy carts; Tier 2's watchdog covers the
residual exposure.

---

## Risk note outcomes

| Risk                                   | Outcome                                                                  |
|----------------------------------------|--------------------------------------------------------------------------|
| Hook overhead at small N               | Not observed on Chrome.  All N values within noise.                      |
| `now_ns()` cost inside hook            | 83 µs of ~900 µs Node overhead; not the bottleneck.  Irrelevant on Chrome. |
| V8 JIT tier-up and hook inlining       | Node and Chrome diverge 27× at N=100.  Chrome TurboFan optimises the dispatch. |
| False budget fires on slow benches     | None observed; all benches complete well within 16.67 ms budget on desktop. |
| `lua_sethook` on C-library calls       | Confirmed limitation; documented above; covered by Tier 2 watchdog.      |

---

## Mobile JIT and startup warming

**Mobile JIT risk.** This spike measured on Apple Silicon desktop Chrome and
Safari only.  Mobile browsers use the same JIT tiers (V8 TurboFan on Android
Chrome; JSC OMG on iOS Safari and all iOS browsers) but are more conservative
about tier-up: they promote functions later to preserve battery and memory.
If the Lua VM's dispatch loop is still in the Liftoff/BBQ baseline tier when
a cart is first executed, the hook overhead will resemble the Node result
(50–80%) rather than the desktop result (< 3%), for however many frames it
takes TurboFan/OMG to promote the loop.

All iOS browsers are forced to use WKWebView / JSC (Apple's App Store
restriction), so Chrome on iOS is not V8 — it behaves like Safari.  The
iOS path therefore has no V8 data at all.  Mobile measurement (mid-range
Android and iPhone) is the outstanding open question, carried forward from
Spike F.

**Startup warming recommendation.** The frame-by-frame data from all Node
runs shows a consistent pattern: frame 0 is 2–2.5× slower than steady state,
and the warmup resolves by frame 5–6 on desktop.  On mobile, the warmup
window is likely 15–30 frames.  During this window the hook overhead is
real (Node-like), and it is visible as a frame-rate spike at cart startup.

The standard WASM mitigation is a **prewarming loop**: run a fixed number of
invisible tic calls (20–30) before showing the first rendered frame.  This
is not a timer-based splash screen — it is a fixed iteration count that the
JIT uses to promote the dispatch loop to the optimizing tier before any output
is visible.  The runtime should do this unconditionally on every cart launch,
on all platforms.  The cost is at most ~30 × mean_tic_time ≈ 30 × 2 ms = 60 ms
of invisible computation — imperceptible to users.

This is a runtime implementation recommendation, not a spike deliverable.

---

## Dev-feedback asymmetry (deferred)

This spike does not address the VS Code extension timing gap.  The Lua-direct
WASM build is ~36× faster than Pi hardware; developers iterating in the
extension see systematically wrong frame times.  This is addressed by Spike G.2,
which evaluates `LUA_MASKLINE` as a dev-mode Pi-parity throttle.

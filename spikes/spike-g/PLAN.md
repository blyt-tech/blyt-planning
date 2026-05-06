# Spike G — implementation plan

**Question (per `docs/design/early-validation-spikes.md` §G):** Can
per-frame CPU exhaustion be enforced in the Lua-direct WASM execution model —
recommended by Spike F — with overhead low enough to preserve Spike F's
timing numbers?

**Why this spike exists:** Spike F's recommendation to adopt Lua-direct on
the WASM target removes the `rv32emu` instruction-cycle counter that
ADR-0082's MIPS cap relies on. On the `rv32emu` path the interpreter counts
every guest instruction and throttles via `nanosleep` to match Pi-class
throughput; every budget overrun is detectable at instruction granularity.
On Lua-direct there is no instruction counter. The remaining enforcement
hook in the Lua VM is `lua_sethook` with `LUA_MASKCOUNT`, which fires after
every N Lua instructions rather than every N RISC-V instructions. Whether
that hook can catch over-budget tics cheaply enough to be the production
mechanism is the question this spike resolves.

**The two-tier policy (from the early-validation-spikes design):**

- *Tier 1 — per-tic step budget (`lua_sethook`).* At the start of each
  tic the host calls `lua_sethook(L, hook_fn, LUA_MASKCOUNT, N)`. The
  hook reads wall-clock time and aborts the tic via `lua_error()` if the
  frame budget (16.67 ms) is exceeded. This catches well-behaved carts
  that simply do too much work per tic.

- *Tier 2 — external hard timeout (~1 s watchdog).* `LUA_MASKCOUNT` does
  not fire when the VM is inside a tight C-extension call or when the
  cart runs a hot loop with no function calls (`while true do end`).
  Those carts are terminated by an external watchdog — a `setTimeout` of
  ~1 s that `terminate()`s the Web Worker. A 1 s timeout for a cart that
  is actively bypassing the budget mechanism is an acceptable outcome.
  `LUA_MASKLINE` is explicitly out of scope: it is more expensive and
  does not prevent hot-loop bypass any better than a 1 s watchdog.

The spike validates Tier 1 only. Tier 2 requires no measurement — a
`setTimeout` in the harness is a one-liner with negligible overhead.

**Inputs we already have:**

- `spikes/spike-f/host.c` — the shim to extend. The hook shim is a
  two-line addition at the top of the tic loop (`lua_sethook` before
  `lua_pcall`, wall-clock check inside the hook function). No structural
  changes to the run loop.
- `spikes/spike-f/Makefile` — the build orchestration to mirror. Spike G
  reuses spike-f's `emsdk` symlink, Lua 5.4.7 source, and bench
  `.lua` files via relative paths; it does not re-vendor them.
- `spikes/spike-f/baselines/chrome-desktop.json` — the no-hook baseline.
  The load-bearing numbers: `doom_tick` mean 1.71 ms / p99 3.40 ms;
  `mandelbrot` mean 4.08 ms / p99 9.90 ms; `binarytrees` p99 5.40 ms.
  The 10% overhead target for `doom_tick` is +0.34 ms, i.e. p99 ≤ 3.74 ms.
- `spikes/spike-f/baselines/*.node-wasm.txt` — Node baseline output for
  correctness comparison.

**What we are *not* building:**

- No Tier 2 implementation. The watchdog is a JS `setTimeout` in the
  existing harness; it does not require a C change or a spike measurement.
- No `LUA_MASKLINE`. This spike does not evaluate it. Whether `LUA_MASKLINE`
  is viable as a dev-mode Pi-parity throttle is deferred to Spike G.2, which
  measures its overhead and decides if it belongs in dev or production builds.
- No dev-feedback throttle (Pi-parity in the VS Code extension). The VS Code
  extension uses the Lua-direct WASM build, which is ~36× faster than Pi
  hardware; without a throttle, developers see systematically wrong frame times
  in their primary edit-run loop. Addressing this is explicitly deferred to
  Spike G.2.
- No mobile measurement. Spike G is a desktop-Chrome overhead measurement,
  same as Spike F stage 2. If the hook is cheap on Apple Silicon V8, it
  will be cheap on any V8 — the relative overhead of a branch into a
  C hook function does not change with host speed.
- No new bench workloads. The same nine benches from Spike F are used
  unchanged. The workload-sizing caveat from Spike F (several benches
  complete in < 1 ms / tic) applies equally here; `doom_tick` and
  `mandelbrot` are the load-bearing benches because they spend the most
  wall-clock time per tic.
- No web harness changes beyond swapping the WASM module URL. The spike-f
  harness (`spike_f.html`, `spike_f_worker.js`, `run_chrome.cjs`) is
  reused end-to-end, pointed at `hook_lua.js` instead of `lua.js`.

---

## Approach

Two stages. Stage 1 establishes that the hook shim builds, runs, and
produces correct output — and that the hook *fires* when the budget is
intentionally exceeded. Stage 2 sweeps N and measures overhead.

### Stage 1 — hook shim builds and fires correctly

1. Create `spikes/spike-g/` mirroring the spike-f layout:
   `Makefile`, `hook_host.c`, `web/`, `baselines/`. Symlink
   `emsdk → ../spike-e/emsdk` and reuse spike-f's vendored Lua source
   via `LUA_SRC := $(CURDIR)/../spike-f/lua-src/src` in the Makefile.
   Copy the nine `.lua` bench files from spike-b via the same pattern
   spike-f uses, or symlink `benchmarks/ → ../spike-f/benchmarks`.

2. Author `hook_host.c` as a copy of `spikes/spike-f/host.c` extended
   with the hook mechanism:

   - A file-scope `static uint64_t tic_start_ns` set immediately before
     each `lua_pcall`, storing the result of `now_ns()`.
   - A file-scope `static uint64_t budget_ns` set from a
     `HOOK_BUDGET_NS` preprocessor define (default `16667000` — 16.67 ms
     in nanoseconds). Overridable at build time so the correctness test
     can use a tiny budget to force early termination.
   - A `hook_fn(lua_State *L, lua_Debug *ar)` that computes
     `now_ns() - tic_start_ns`, and calls `luaL_error(L, "budget
     exceeded")` if the elapsed time exceeds `budget_ns`.
   - A `HOOK_COUNT` preprocessor define (default `1000`) controlling the
     N argument passed to `lua_sethook`. Parameterised at build time for
     the N sweep.
   - A build-time `HOOK_ENABLED` flag (default `1`). When `0`, the
     `lua_sethook` call is compiled out entirely, producing a binary
     byte-equivalent to spike-f's `host.c` but through the same build
     path. This is the no-hook baseline for the sweep.
   - `lua_sethook` is called once per tic (immediately before
     `lua_pcall`), not once per run. This is important: calling it once
     at `luaL_newstate` time would apply across tics but would leave
     `tic_start_ns` stale after the first `lua_pcall` returns.
     Re-arming per tic keeps the budget window correct.

3. Build `make hook-wasm HOOK_COUNT=100000 HOOK_BUDGET_NS=1000000000`
   (N=100000, 1 s budget — hook enabled but never fires on our benches).
   Run `make node-test-all` and diff the output against
   `spikes/spike-f/baselines/<bench>.node-wasm.txt` for all nine benches.
   Expected: byte-identical FRAME / SUMMARY lines (modulo timing values,
   which the diff ignores by comparing only structure). Any structural
   difference is a bug in `hook_host.c`.

4. Build `make node-fire-test HOOK_COUNT=100 HOOK_BUDGET_NS=100000`
   (N=100, 100 µs budget — guaranteed to fire on any real bench). Run
   `doom_tick` under Node and confirm:
   - `lua_pcall` returns `LUA_ERRRUN` (not `LUA_OK`).
   - The error message contains "budget exceeded".
   - The process exits cleanly (no panic, no abort).
   This confirms the hook fires and the error is recovered via `pcall`.

   The `node-fire-test` target should print `HOOK FIRE TEST PASS` or
   `HOOK FIRE TEST FAIL` and exit accordingly. It is a correctness gate,
   not a timing gate.

Exit criterion: `node-test-all` produces structurally correct output at a
non-firing budget, and `node-fire-test` confirms the hook fires and is
recoverable at a tight budget.

### Stage 2 — N sweep and overhead measurement

5. Define the N sweep set: `100, 500, 1000, 5000, 10000`, plus
   `HOOK_ENABLED=0` (no hook) as the baseline. Build one WASM module
   per N value; keep the production budget (16.67 ms) for all of them
   since the benches all complete well within it.

   Build naming convention: `build/hook_lua_N<count>.js` and
   `build/hook_lua_N<count>.wasm`. The no-hook build is
   `build/hook_lua_N0.js` (or `hook_lua_baseline.js`).

   The Makefile target `make n-sweep` drives all builds sequentially.

6. For each N value, run the full nine-bench suite under Node:
   ```
   make node-overhead-all N=<value>
   ```
   capturing output to `baselines/node-N<value>/`. The Node run is
   faster than Chrome (no JIT warm-up variance) and gives a clean
   signal for the overhead trend. All nine benches are included, not just
   `doom_tick`, because `mandelbrot` and `spectral-norm` have the longest
   per-tic wall times and therefore accumulate the most hook invocations
   per tic.

7. Run the Chrome overhead measurement for the three candidate N values
   closest to the 10% threshold identified in step 6, plus the no-hook
   baseline, using the existing `run_chrome.cjs` driver:
   ```
   make chrome-overhead N=<value>
   ```
   capturing output to `baselines/chrome-N<value>/chrome-overhead.json`.
   The Chrome run is authoritative for the final recommendation because
   the production harness runs in V8.

8. Construct the overhead table. For each (bench, N) pair, compute:
   - `p99_overhead_ms` = p99(N) − p99(no-hook)
   - `overhead_pct` = 100 × p99_overhead_ms / p99(no-hook)

   The primary row is `doom_tick`. The acceptance threshold is
   `p99_overhead_ms < 0.34 ms` (10% of Spike F's 3.40 ms p99).

   The minimum N at which this threshold is met is the production
   candidate. If multiple N values meet it, recommend the smallest
   (tightest enforcement granularity) that still clears the threshold
   with margin, since false fire-rate (how often the hook fires on a
   budget-clean tic) decreases with larger N.

9. Verify the chosen N against `mandelbrot` (p99 9.90 ms baseline, a
   more demanding hook-invocation workload). The overhead threshold is
   proportionally looser here — `mandelbrot` is not the load-bearing
   workload — but a result that shows monotonically consistent overhead
   across benches supports the conclusion. Report it either way.

Exit criterion: at the chosen N, `doom_tick` p99 overhead on Chrome ≤ 0.34 ms.
A failure here (no N in the sweep meets the threshold) does not necessarily
invalidate the Tier 1 concept — it means the step count needs to go coarser,
or an alternative Tier 1 mechanism is needed (see risk notes). The spike
documents whichever outcome it produces.

---

## Risk notes

- **`lua_sethook` firing on C-library calls.** `LUA_MASKCOUNT` counts
  Lua VM instructions, not C function calls. A cart that calls a
  long-running standard library function (e.g. a large `table.sort`) will
  not fire the hook during that call, only between Lua VM instructions
  around it. The benchmarks do not exercise this case — `doom_tick` and
  `entity_update` are pure Lua loops. Document this limitation in the
  write-up: the hook is not a strict real-time deadline, only a best-effort
  one for Lua-heavy carts. Tier 2's watchdog covers the remaining exposure.

- **Hook overhead increasing with smaller N.** The hook is a C function
  call from the Lua VM's hot dispatch loop. At N=100 it fires roughly once
  per microsecond on `doom_tick`; at N=10000 it fires roughly once per
  100 µs. The sweep should show whether V8's JIT tier-up (which can inline
  or speculatively skip hooks if they return quickly) flattens the overhead
  curve at small N, or whether overhead scales roughly linearly with N. If
  the overhead is already negligible at N=100, the production value can be
  the smallest N the VM supports (N=1), giving the tightest enforcement.
  The sweep will reveal this.

- **`now_ns()` cost inside the hook.** `clock_gettime(CLOCK_MONOTONIC)` in
  a WASM module goes through Emscripten's monotonic clock implementation,
  which typically uses `performance.now()` internally with a resolution
  of ~1 µs. On Apple Silicon Chrome 147 this is fast (sub-µs round trip),
  but it is still a system call boundary from V8's perspective. If the hook
  overhead is dominated by the `clock_gettime` call rather than the branch
  overhead, a cheaper alternative — reading a `performance.now()`
  counter written by the JS side into shared memory before each tic, and
  comparing against it in the hook without another system call — may be
  needed. The spike should diagnose whether this is the case (by building
  a variant with a stub `now_ns()` that always returns 0 and comparing
  its overhead to the real `now_ns()` variant). If the clock call is not
  the bottleneck, the original design stands.

- **V8 JIT tier-up and hook inlining.** V8 can de-optimise and re-optimise
  WASM functions based on profiling. A hook function that always returns
  quickly may be speculatively removed from the JIT hot path after warm-up;
  one that occasionally fires `lua_error` will be treated differently.
  The Node baseline (step 6) uses V8's `--liftoff` and `--turbofan` defaults
  and should reflect realistic Chrome JIT behaviour. If Node and Chrome
  numbers diverge by more than 2×, take the Chrome number as authoritative
  and note the discrepancy.

- **False budget fires on slow benches under load.** The benches run
  sequentially; each tic is timed independently. On a loaded host (e.g.
  CI machine), `mandelbrot` or `spectral-norm` (both ~5–10 ms p99) could
  occasionally exceed 16.67 ms and trigger the hook. The correctness test
  (step 3) uses a 1 s budget to avoid this. The N sweep uses the production
  16.67 ms budget; if the host is loaded and a bench fires the hook on a
  legitimate tic, the FRAME line will contain the partial execution time,
  which will read low (not high). The results doc should note if any
  unexpected budget fires occur during the sweep.

---

## Deliverables

- `spikes/spike-g/Makefile` — orchestration. `make hook-wasm N=<n>`
  builds `build/hook_lua_N<n>.{js,wasm}`. `make node-fire-test` runs
  the forced-fire correctness test. `make node-overhead-all N=<n>` runs
  all nine benches under Node and writes `baselines/node-N<n>/`.
  `make chrome-overhead N=<n>` drives headless Chrome and writes
  `baselines/chrome-N<n>/chrome-overhead.json`. `make n-sweep` builds
  all N values and runs the Node overhead pass for each.
- `spikes/spike-g/hook_host.c` — host shim with `lua_sethook` Tier 1
  mechanism. `HOOK_COUNT`, `HOOK_BUDGET_NS`, `HOOK_ENABLED` build-time
  parameters.
- `spikes/spike-g/baselines/` — per-N Node output directories and Chrome
  JSON captures.
- `spikes/spike-g/baselines/overhead-summary.txt` — formatted table:
  bench × N → p99 (ms), p99 overhead vs. no-hook (ms), overhead %.
- `docs/design/spike-g-results.md` — write-up: what was built, the
  overhead table, chosen N, two-tier policy documentation, risk note
  outcomes, recommendation for the production WASM Lua-direct path.
- `spikes/spike-g/TASKS.md` — checklist, kept current as work proceeds.

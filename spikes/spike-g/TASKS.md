# Spike G — task checklist

## Stage 1 — hook shim builds and fires correctly

- [x] Create `spikes/spike-g/` layout: `Makefile`, `hook_host.c`, `fire_host.c`,
      `web/`, `baselines/`.  Symlink `emsdk → ../spike-e/emsdk`.  Reuse spike-f's
      vendored Lua source and bench .lua files.
- [x] Author `hook_host.c` with `HOOK_ENABLED`, `HOOK_COUNT`, `HOOK_BUDGET_NS`
      build-time parameters; `STUB_CLOCK` diagnostic variant.
- [x] `make node-test-all N=1000` — correctness at non-firing budget. **PASS**
- [x] `make node-fire-test` — hook fires and is recoverable. **HOOK FIRE TEST PASS**

## Stage 2 — N sweep and overhead measurement

- [x] `make n-sweep` — build all N values (0, 100, 500, 1000, 5000, 10000) and
      run Node overhead pass.  Results in `baselines/node-N*/`.
- [x] Stub-clock diagnostic: `make node-stubclock-test`.  Confirms clock_gettime
      is not the bottleneck (≈9% of Node overhead); dispatch mechanism dominates.
- [x] Chrome baseline: `make chrome-overhead N=0`.
- [x] Chrome N=100: `make chrome-overhead N=100`.
- [x] Chrome N=1000: `make chrome-overhead N=1000`.
- [x] Chrome N=10000: `make chrome-overhead N=10000`.
- [x] Overhead table computed.  **All Chrome N values PASS** (≤ +0.1 ms, < 3%).

- [x] WebKit (Playwright JSC) N=0, N=100, N=1000: all p99 identical — overhead < 1 ms (timer resolution limit).
- [x] Safari (safaridriver) N=0, N=100, N=1000: same result as WebKit.  Finding not V8-specific.

## Deliverables

- [x] `spikes/spike-g/Makefile`
- [x] `spikes/spike-g/hook_host.c`
- [x] `spikes/spike-g/fire_host.c`
- [x] `spikes/spike-g/baselines/` — per-N Node output and Chrome JSON captures
- [x] `spikes/spike-g/baselines/overhead-summary.txt`
- [x] `docs/design/spike-g-results.md`
- [x] `spikes/spike-g/TASKS.md` (this file)

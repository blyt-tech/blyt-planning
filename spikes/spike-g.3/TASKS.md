# Spike G.3 — TASKS

Status: **complete (PASS — Outcome 1)**.

See `docs/design/spike-g.3-results.md` for the full write-up.

---

## Stage 1 — Scaffolding

- [x] Copy `throttle_host.c`, `Makefile`, `web/` from `spike-g.2/`.
- [x] Symlink `emsdk → ../spike-g/emsdk`.
- [x] Verify `LUA_SRC` (`../spike-f/lua-src/src`) and `BENCH_DIR`
      (`../spike-f/benchmarks`) resolve correctly via the existing
      relative paths in the Makefile.
- [x] Update Makefile header comment + add `chrome-cheap-overhead` target
      (D=1) for Stage 1'.

## Stage 1' — Implement accumulated-debt hook

- [x] Replace per-line busy-wait body in `throttle_host.c` with the
      absolute-deadline form (cumulative `target_ns`; spin only while
      `now_ns() < start_ns + target_ns`).
- [x] Reset `start_ns` and `target_ns` to 0 inside the per-tic for-loop,
      just before each `lua_pcall`.
- [x] Build `throttle-wasm THROTTLE_DELAY_NS=0`; run `node-test-all` for
      correctness across all nine benches.

## Stage 1' — Cheap-path measurement

- [x] Build `throttle-wasm THROTTLE_DELAY_NS=1` and run
      `chrome-cheap-overhead`. Output:
      `baselines/chrome-cheap/chrome-overhead.json`.
- [x] Re-run `chrome-nohook` for a fresh same-host comparison baseline.
- [x] Compute `cheap_per_line_ns` per bench:
      doom_tick=157, entity_update=162, binarytrees=173, mandelbrot=151,
      spectral-norm=178. **Exit criterion (< 1,000 ns/line) met.**

## Stage 2 — Build calibrated variants

- [x] Build `throttle-wasm THROTTLE_DELAY_NS=2250 / 3000 / 3750`.

## Stage 3 — Throttled measurement vs Pi targets

- [x] Run `chrome-throttle-all D_LOW=2250 D_MID=3000 D_HIGH=3750`. Output:
      `baselines/chrome-D{0,2250,3000,3750}/chrome-throttle.json`.
- [x] Compute per-(bench, D) `accuracy_pct` and `jitter_ratio`.
- [x] Apply success criteria:
      - doom_tick @ D_MID: 0.4 % accuracy, jitter 1.002. **PASS.**
      - entity_update @ D_MID: 37.7 % vs midpoint at single-D calibration;
        3.3 % at per-bench D=2,250.
      - jitter ≤ 1.010 across all benches and all D > 0 variants.

## Documentation

- [x] `baselines/calibration.md` — constants reused from G.2.
- [x] `docs/design/spike-g.3-results.md` — full write-up.
- [x] This file.

---

## Outcome

**Outcome 1.** `LUA_MASKLINE` with accumulated-debt is the recommended
dev-mode Pi-parity throttle. The mechanism's per-line accuracy versus
configured `THROTTLE_DELAY_NS` is ≤ 2.5 % across the whole test matrix;
jitter is ≤ 1.010. The single caveat — `D_MID = 3,000` only fits
`doom_tick` — is a calibration-policy issue (per-cart D is straightforward
to derive), not a property of the hook.

Spike G.2's Outcome 3 is superseded.

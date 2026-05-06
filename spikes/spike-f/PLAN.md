# Spike F — implementation plan

**Question (per `docs/design/early-validation-spikes.md` §F):** does Lua 5.4
compiled *directly* to WASM (no `rv32emu`, no RV32IMFC step — just the Lua
VM as a WASM module) hold 60 fps in a browser on a development desktop and
on a mid-range 2–3-year-old Android phone, on the same cart workloads
Spike E measured at 7.4× over budget?

**Why this spike exists:** Spike E showed the
`rv32emu` + Lua-in-RV32IMFC stack on WASM misses the desktop frame budget
by 7.4× on `lua_cart_doom_tick` (123 ms mean / 126 ms p99 vs 16.67 ms).
The native-C cart on the same WASM stack hits 2 ms / 4.2 ms p99,
demonstrating the WASM tax alone is small — the Lua-cart penalty comes
from the *two-layer* interpreter dispatch (Lua VM → RV32IMFC → rv32emu).
Removing the `rv32emu` layer collapses the inner two layers into one. F
measures whether that collapse buys back the 7.4× headroom on desktop and
projects to viable on mid-range Android. ADR-0025 names this fallback
("the host-embedded Lua architecture") explicitly. Spike E open question
#3 is the same question.

**Inputs we already have:**
- `spikes/spike-b/benchmarks/*.lua` — the four Lua workloads Spike E used
  (`doom_tick.lua`, `entity_update.lua`, `binarytrees.lua`) plus the
  longer benchmark suite (`fannkuch`, `fasta`, `mandelbrot`, `nbody`,
  `spectral-norm`, `doom_tick_gc`). These are the *source* — Spike F
  feeds the same `.lua` files to a Lua-in-WASM build instead of feeding
  `lua_cart_*.elf` to rv32emu-in-WASM.
- `spikes/spike-e/web/spike_e.html`, `web/spike_e_worker.js`,
  `web/run_chrome.cjs` — the measurement harness, headless-Chrome
  driver, per-tick distribution / p50/p95/p99 / histogram, rAF jank
  meter. Reusable end-to-end; only the WASM module URL changes.
- `spikes/spike-e/baselines/*.docker.txt` and `chrome-desktop.json` —
  the Spike B Docker arm64 baselines and the Spike E Chrome 147
  numbers. The cross-stack comparison table in
  `docs/design/spike-e-results.md` gets a fourth column from this spike.
- `spikes/spike-e/emsdk/` — Emscripten 3.1.51 already installed and
  activated. Spike F can reuse it (or upgrade if a newer SDK matters
  for Lua-direct; not expected).

**What we are *not* building:**
- No `rv32emu`. Not even partially. The whole point of the spike is to
  remove that layer.
- No RV32IMFC cross-toolchain, no `liblua54.rv32.so`, no Spike B cart
  ELFs. Spike F consumes the Lua *source* directly.
- No real graphics, no audio, no SDL. Same as Spike E — the deliverable
  is a frame-time histogram, not a playable demo.
- No console-API ECALL implementation. Where the cart calls a console
  primitive that Spike E stubbed at the rv32emu boundary, Spike F stubs
  it as a no-op JS host function (or a no-op C function in the host
  shim). Spike F is a CPU-bound Lua-VM measurement.
- No determinism comparison against Spike D. Deferred — same as Spike E
  deferred it.
- No security-model exploration. ADR-0038 / ADR-0079 already describe
  what an asymmetric (Lua-direct) sandbox looks like; F does not test
  the sandbox, only the timing.

---

## Approach

Two stages, mirroring Spike E. Stage 1 answers "does Lua-direct-to-WASM
build and run our cart workloads correctly"; Stage 2 answers "does it
hit 60 fps on the two device classes."

### Stage 1 — Lua 5.4 in WASM, correctness

1. Set up `spikes/spike-f/` mirroring the spike-e layout:
   `Makefile`, `PLAN.md`, `TASKS.md`, `web/`, `baselines/`. Reuse
   `spikes/spike-e/emsdk/` (symlink or `EMSDK=../spike-e/emsdk` in
   the Makefile).
2. Vendor Lua 5.4 source (`make lua-src` — fetch the upstream tarball
   pinned to the same version Spike B used; verify checksum). Patch
   only what `LUA_32BITS` requires; do not touch the VM.
3. Build `lua.wasm` via Emscripten:
   - `emcc -O3 -DLUA_32BITS -DLUA_USE_C89` over the Lua source set,
     producing `lua.js` + `lua.wasm`.
   - `--embed-file <bench>.lua@/<bench>.lua` for each cart workload.
   - `-sMODULARIZE=1 -sEXPORT_ES6=0 -sEXPORTED_FUNCTIONS=_main,_run_user`
     to match Spike E's worker bootstrap shape (so the harness needs
     minimal diff).
   - No `-sPTHREAD_POOL_SIZE` (same lesson as Spike E — no SAB, no
     COOP/COEP, no service-worker reload dance).
   - `-sINITIAL_MEMORY=64MB` is plenty (Lua VM + cart bytecode +
     headroom; Spike E ran at 256 MB but most of that was rv32emu's
     guest memory map).
4. Author a thin host shim (`host.c`) that:
   - Calls `luaL_newstate()` + `luaL_openlibs()` (with the standard
     library trimmed to ADR-0079's allowlist if doing so is one
     line; otherwise leave the full stdlib — F does not test the
     sandbox).
   - Loads the embedded cart `.lua` file via `luaL_loadfile` and
     calls `lua_pcall` once to define the cart's globals
     (`update`, etc.).
   - Implements `run_user(name)` exported to JS: looks up `update`,
     calls it N times in a loop using `lua_clock`-style
     `clock_gettime(CLOCK_MONOTONIC)` for per-tick deltas, and
     emits the *exact same* `FRAME <name> <i> <us>` and
     `SUMMARY <name> frames=N min=us max=us mean=us` printf format
     Spike E's harness already parses.
   - Stubs the console-API entry points the carts touch (draw, input,
     audio) as Lua functions registered into the cart's environment
     before the main script runs. They become no-ops returning sane
     defaults.
5. Run `node lua.js doom_tick` and confirm:
   - Same FRAME / SUMMARY line count as Spike E's
     `lua_cart_doom_tick.elf` baseline (30 / 1).
   - Same frame indices, same benchmark name field. Timing values
     differ — that is the point of the spike.
6. Confirm the same under Chrome 147 via Spike E's
   `make chrome-bench`-equivalent target, with the WASM module URL
   pointed at `lua.js` instead of `rv32emu.js`. The harness should
   need at most a one-line change (worker bootstrap script name).

Exit criterion: `doom_tick` produces the same FRAME / SUMMARY structure
as Spike E (same line count, same benchmark name, same frame indices)
under (a) Node 22 + Lua-direct-WASM, (b) Chrome 147 + Lua-direct-WASM.
Numerical agreement of timing values is *not* required; structural
agreement is.

### Stage 2 — frame-time measurement on desktop and mobile

7. Reuse `spikes/spike-e/web/spike_e.html` /
   `web/spike_e_worker.js` / `web/run_chrome.cjs` end-to-end.
   Either copy them into `spikes/spike-f/web/` and edit the worker
   import to load `lua.js` instead of `rv32emu.js`, or parameterise
   the worker so a single harness can drive both modules side by
   side.
8. Desktop run: same four carts as Spike E (`doom_tick`,
   `entity_update`, `binarytrees`, native-C control). Capture
   p50/p95/p99 and histogram per cart on Apple Silicon Chrome 147
   via puppeteer-core. Record into
   `spikes/spike-f/baselines/chrome-desktop.json`.
9. Construct the cross-stack comparison row. Pull Spike E's
   numbers from `chrome-desktop.json` and the Spike B Docker baselines
   from `spike-e/baselines/*.docker.txt`. Output:
   - Docker arm64 (Lua-in-RV32IMFC under native rv32emu)
   - Node 22 + WASM (Lua-in-RV32IMFC under rv32emu-in-WASM)
   - Chrome 147 + WASM (Lua-in-RV32IMFC under rv32emu-in-WASM)
   - **Chrome 147 + WASM (Lua-direct)** ← new column from F.
   The Lua-direct/Docker-arm64 ratio is the headline number.
10. Mobile run on the test Android phone, current Chrome and current
    Firefox. Same hand-off shape as Spike E
    (`make start-web-lan`, navigate phone browser to
    `http://<dev-ip>:8000/?cart=<name>&auto=1`, screenshot the
    results panel). Same four carts.
11. Write up `docs/design/spike-f-results.md` mirroring the spike-e
    results doc shape: what was built, raw numbers, projection vs
    16.67 ms budget, side-by-side comparison to Spike E
    (rv32emu+Lua-in-RV32IMFC) on the same workloads, headroom
    analysis, the determinism question (deferred), and the
    architectural recommendation that follows from the numbers.
12. If F passes performance, open follow-up tickets:
    - ADR addendum / new ADR — per-target execution model (RV32IMFC
      on hardware + native; Lua-direct on WASM). Touches ADR-0007
      (determinism), ADR-0025 (Lua cart execution), ADR-0038
      (sandboxing).
    - Determinism cross-check vs Spike D once D produces digests.
    - Security-model write-up: how `_ENV` + ADR-0079 allowlist
      reproduce the RV32IMFC sandbox's guarantees (and where they
      do not).

Exit criterion: `doom_tick` p99 ≤ 8 ms on desktop Chrome (so the 2–4×
mid-range-Android projection band lands at 16–32 ms p99 — borderline
to marginal, with optimistic case fitting the 16.67 ms budget). A
desktop p99 above ~16 ms is a structural fail and the spike's
recommendation flips to "WASM target reduces to native-C carts only."
Mobile measurement refines but does not invalidate the desktop number.

---

## Risk notes

- **Lua's standard library and the no-`io`/`os` sandbox.** The carts
  do not need `io` / `os` for the timing run (per-tick timer comes from
  the host shim, not from inside Lua). If `luaL_openlibs()` brings them
  in and any cart accidentally relies on them, replace with explicit
  per-library `luaopen_base` / `luaopen_table` / `luaopen_string` /
  `luaopen_math` calls. Documented in ADR-0079.
- **`LUA_32BITS` consistency with Spike B.** Spike B compiled Lua to
  RV32IMFC with `LUA_32BITS`; Spike F compiles Lua to WASM with
  `LUA_32BITS`. Both should agree on `lua_Number = float`,
  `lua_Integer = int32_t`. Verify the `printf("%g", lua_Number)` output
  format matches Spike B's so SUMMARY lines look the same.
- **Per-tick timer resolution under `performance.now()`.** Browsers
  clamp `performance.now()` to ~5 µs–100 µs depending on cross-origin
  isolation status. Spike E ran without COI (per its lesson #1) and got
  microsecond-scale FRAME numbers because the timing happens *inside*
  the WASM module via `clock_gettime(CLOCK_MONOTONIC)`, which
  Emscripten implements against the same monotonic source but emits
  microsecond resolution. Verify Spike F's host shim does the same —
  do not switch to `performance.now()`-from-JS or the timing
  measurements get coarser than Spike E's.
- **Cart workloads that grew to fit the rv32emu time budget.** Some of
  Spike B's carts iterate enough work to be measurable on rv32emu;
  they may complete in microseconds on Lua-direct-to-WASM. If
  `doom_tick` finishes in <0.5 ms per tick, the 30-frame run is too
  noisy to characterise — consider scaling the per-tick workload up
  (more entities, more iterations) so the histogram is meaningful.
  Document any scaling factor; do not silently change the workload.
- **Mobile-Android JS engine differences.** V8 on Snapdragon-7-class
  Android is 2–4× slower than M-series desktop V8 on benchmark
  workloads. SpiderMonkey's WASM tier-up profile differs again.
  Capture both browsers — the result might differ by more than the
  desktop/mobile factor alone.
- **No determinism cross-check.** Spike D has not run. Lua-direct-WASM
  uses different `printf("%g", float)` rounding paths than Lua-in-RV32IMFC
  through musl libm; expect digest divergence. Documenting this as a
  known cost is part of the spike's writeup; resolving it is not.

---

## Deliverables

- `spikes/spike-f/Makefile` — orchestration. `make lua-wasm` builds
  Lua 5.4 + host shim to WASM with the cart sources embedded.
  `make node-test BENCH=doom_tick` runs a single cart under Node.
  `make node-test-all` runs all carts. `make chrome-bench` drives
  headless Chrome 147 via puppeteer-core. `make start-web` /
  `make start-web-lan` for desktop-interactive and phone-LAN runs.
- `spikes/spike-f/lua-src/` — vendored Lua 5.4 source (build artefact,
  not committed; produced by `make lua-src`).
- `spikes/spike-f/host.c` — host shim (run loop, FRAME/SUMMARY emitter,
  ECALL stub registrations).
- `spikes/spike-f/web/spike_f.html` + `web/spike_f_worker.js` +
  `web/run_chrome.cjs` — harness, copied from spike-e and edited to
  load `lua.js` instead of `rv32emu.js`. Or unified into a single
  harness driving both — either works.
- `spikes/spike-f/baselines/` — captured per-cart JSON + per-tick
  distribution data.
- `docs/design/spike-f-results.md` — final write-up; mirrors
  `spike-e-results.md` shape; updates the cross-stack comparison
  table in `spike-e-results.md` (or supersedes it) with the
  Lua-direct column.
- `spikes/spike-f/TASKS.md` — checklist, kept current as work
  proceeds.

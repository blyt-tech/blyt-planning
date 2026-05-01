# Spike F results — Lua 5.4 compiled directly to WASM

**Status: build, correctness, performance, and determinism cross-check
complete. Every Lua workload runs comfortably under the 16.67 ms frame
budget on desktop Chrome 147, with `doom_tick` at 1.71 ms mean / 3.40 ms
p99 (a 72× mean / 37× p99 reduction vs Spike E). The Spike D determinism
cross-check PASSES: all 60 DIGEST lines from `det_doom_tick` and
`det_entity_update` are byte-identical to the RV32IMFC reference, after a
one-line build insight: the WASM build must use the same naive
`strtof` as the freestanding RV32 build (see Stage 3). Mobile measurement
is the remaining open item and is handed off as a manual run.**

The question Spike F asks (per `docs/design/early-validation-spikes.md`
§F): does Lua 5.4 compiled *directly* to WASM — no `rv32emu`, no RV32IMFC
step, just the Lua VM as a WASM module — hold 60 fps in a browser on a
development desktop and on a mid-range 2–3-year-old Android phone, on the
same cart workloads Spike E measured at 7.4× over budget?

The performance answer on desktop is **yes — comfortably**, with
1.71 ms mean / 3.40 ms p99 on `doom_tick` against the 16.67 ms budget,
i.e. ~10% of budget mean and ~20% of budget p99. Removing the rv32emu
layer collapses the two-layer interpreter dispatch (Lua VM →
RV32IMFC → rv32emu) into one (Lua VM → WASM/V8) and recovers the
7.4× headroom Spike E found we needed, and then some.

---

## What was built

`spikes/spike-f/` mirrors the spike-e layout, but the build target is
trivial by comparison: there is no upstream rv32emu source tree, no
Kconfig overlay, no SoftFloat / glibc port. We compile vanilla Lua 5.4.7
(matching the version Spike B used) plus a small host shim straight to a
WebAssembly module.

- `Makefile` orchestrates: vendor Lua 5.4.7 (`make lua-src`), copy bench
  `.lua` files from spike-b's `benchmarks/`, build `build/lua.{js,wasm}`
  via Emscripten 3.1.51, and stage a Web demo + run headless Chrome via
  puppeteer-core. The emsdk install is reused from spike-e via a symlink
  (`emsdk → ../spike-e/emsdk`) — no second 600 MB toolchain copy.
- `host.c` is the only non-vendored C in the build. Structurally
  identical to `spike-b/ports/rv32emu/lua_cart.c`: `luaL_newstate`,
  `luaL_openlibs`, `luaL_loadfile` once, `lua_pcall` in a hot loop with
  `clock_gettime(CLOCK_MONOTONIC)` brackets, `printf` of `FRAME` and
  `SUMMARY` lines in exactly Spike B / Spike E's format. Frame counts
  (`doom_tick=30`, `entity_update=50`, all CPU benches `=20`,
  `doom_tick_gc=30`) match spike-b's `BENCH_FRAMES_*` defaults so the
  output line counts agree across the four stacks.
- `web/user-pre.js` mirrors spike-e's user-pre.js: sets
  `Module.noInitialRun = true`, exposes `Module.run_user(name)` as a
  thin wrapper that calls `callMain([name])`.
- `web/spike_f.html` / `spike_f.js` / `spike_f_worker.js` are direct ports
  of the spike-e harness with two changes: (a) "cart" / `cart` →
  "bench" / `bench` everywhere; (b) `importScripts("rv32emu.js")` →
  `importScripts("lua.js")`. The Worker still posts FRAME/SUMMARY lines
  to the main thread one `postMessage` per line; the main thread parses
  them, computes p50/p95/p99 + 32-bin histogram, and runs an independent
  `requestAnimationFrame` jank meter. All of this is unchanged from
  Spike E.
- `web/run_chrome.cjs` is the headless-Chrome driver, again ported from
  spike-e (puppeteer-core driving system Chrome), with the URL parameter
  changed from `cart=` to `bench=` and the result key from
  `__spikeEResult` to `__spikeFResult`.

**Build details:**

- Lua source: upstream tarball `lua-5.4.7.tar.gz`, SHA-256
  `9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30`
  (gitignored, fetched on first `make lua-src`).
- `LUA_32BITS` flipped from `0` to `1` via the same sed line spike-b
  uses, so the WASM build agrees with the RV32IMFC build on
  `lua_Number = float`, `lua_Integer = int32_t`. Verified by `grep` in
  the make rule.
- Standard library: `luaL_openlibs` is opened in full. Spike B trims
  `io`/`os`/`loadlib`/`coroutine`/`debug`/`utf8` with a custom
  `lua_init_libs.c`; Spike F leaves them in because it does not test
  the sandbox (per PLAN.md), and because the benchmark `.lua` files do
  not call into them at the timing-relevant path. The trimming would
  be one line of source change to bring forward to a production build
  per ADR-0079 if performance numbers stay healthy after the cut.
- emcc flags: `-O3 -DLUA_USE_POSIX -DLUA_32BITS -sINITIAL_MEMORY=64MB
  -sALLOW_MEMORY_GROWTH -sEXPORTED_FUNCTIONS=_main
  -sEXPORTED_RUNTIME_METHODS=callMain,FS -sFORCE_FILESYSTEM=1
  --pre-js web/user-pre.js`. No `-sPTHREAD_POOL_SIZE` (same lesson as
  Spike E — no SAB, no COOP/COEP, no service-worker reload dance).
- `--embed-file <bench>.lua@/<bench>.lua` for each of the nine
  benchmarks. The host calls `luaL_loadfile("/<name>.lua")` from the
  in-WASM MEMFS.

The produced WASM module is **275 KB** (versus Spike E's `rv32emu.wasm` at
~1.1 MB, about 4× smaller), and `lua.js` glue is **77 KB**. Total page
weight from a cold load on a phone is ~360 KB — a meaningful improvement
on the Spike E build for first-load and matters for low-end devices.

---

## Stage 1 — correctness across hosts

`make node-test-all` runs each bench end-to-end under Node 22 + WASM
Lua-direct. Each bench produces the exact same FRAME / SUMMARY structure
as Spike B's Docker arm64 runs and Spike E's Node + WASM rv32emu+Lua
runs:

| Bench           | n  | Spike B Docker arm64 | Spike E Node + WASM (rv32emu+Lua) | Spike F Node + WASM (Lua-direct) |
|-----------------|---:|:---------------------|:----------------------------------|:---------------------------------|
| `doom_tick`     | 30 | 30 FRAME + SUMMARY   | 30 FRAME + SUMMARY                | 30 FRAME + SUMMARY               |
| `entity_update` | 50 | 50 FRAME + SUMMARY   | 50 FRAME + SUMMARY                | 50 FRAME + SUMMARY               |
| `binarytrees`   | 20 | 20 FRAME + SUMMARY   | 20 FRAME + SUMMARY                | 20 FRAME + SUMMARY               |
| `doom_tick_gc`  | 30 | (not in spike-e set) | 30 FRAME + SUMMARY                | 30 FRAME + SUMMARY               |
| `fannkuch`      | 20 | (not measured)       | 20 FRAME + SUMMARY                | 20 FRAME + SUMMARY               |
| `fasta`         | 20 | (not measured)       | 20 FRAME + SUMMARY                | 20 FRAME + SUMMARY               |
| `mandelbrot`    | 20 | (not measured)       | 20 FRAME + SUMMARY                | 20 FRAME + SUMMARY               |
| `nbody`         | 20 | (not measured)       | 20 FRAME + SUMMARY                | 20 FRAME + SUMMARY               |
| `spectral-norm` | 20 | (not measured)       | 20 FRAME + SUMMARY                | 20 FRAME + SUMMARY               |

Same line counts, same bench names, same frame indices. Only the timing
values differ (as expected — wall clock varies between platforms). Stage
1 exit gate met across Node + Lua-direct-WASM and (verified separately
in Stage 2) Chrome 147 + Lua-direct-WASM.

A spot-check of `doom_tick` under headless Chrome 147 via the harness
(`make chrome-bench` with one bench) reproduces the same FRAME / SUMMARY
structure with the bench running inside a Web Worker, the rAF jank meter
on the main thread, and the result populated into `window.__spikeFResult`
in ~100 ms wall-clock from page load.

### Notes from Stage 1

1. **No console-API stubs needed.** PLAN.md flagged the possibility that
   bench scripts call into draw/input/audio primitives that Spike E
   stubbed at the rv32emu boundary. They do not — the spike-b
   benchmarks under `benchmarks/*.lua` are pure CPU workloads with no
   reference to `draw_`, `input_`, `audio_`, `cls`, `spr`, `btn`,
   `sfx`, `music`, `pset`, or any console-API name. `host.c` registers
   no stubs and every bench's `update()`-equivalent runs to completion.

2. **Worker `done` posts before stdout flushes on Chrome — same as
   Spike E.** Worker calls `Module.run_user(bench)` synchronously from
   `onRuntimeInitialized`; when it returns we `postMessage({type:"done"})`.
   Empirically that "done" message arrives before the FRAME messages on
   Chrome 147 (Emscripten flushes stdout post-task). Same fix as Spike E:
   the harness publishes results from the SUMMARY line, not from worker
   `done`. Carried forward verbatim.

3. **Several benches finish too fast to characterise at 30 frames.**
   `nbody` averages 0.35 ms/tick on desktop, `fasta` 0.67 ms, `entity_update`
   0.70 ms, `doom_tick` 1.71 ms. Spike B sized these to be measurable on
   rv32emu (10–100 ms/tick); under Lua-direct-WASM the per-tick floor is
   often **≤1 ms** and the 20–50 frame distribution is noisy at the tail.
   `doom_tick`'s 30-frame distribution is still well-shaped (min 1.40 mean
   1.71 max 3.40), but for `nbody` the p50 is 0.30 ms and p99 is 1.00 ms
   — the integer-microsecond `printf` resolution starts to bite.
   Resolution if it matters: scale the per-tick workload up (more
   entities, more inner-loop iterations) so the noise floor is < 10% of
   mean. PLAN.md notes this. Did not change the workload here because
   the headline conclusion (every bench under budget by ≥3×) holds at
   either workload size; the comparison-to-Spike-E numbers are
   apples-to-apples at the original workload.

---

## Stage 2 — desktop frame-time measurements

Host: Apple Silicon (M-series), macOS 25.4 Darwin, Google Chrome 147
(headless via puppeteer-core driving the system Chrome binary). Page
loaded with `?bench=<name>&auto=1`; the bench runs inside a dedicated Web
Worker; the rAF loop on the main thread is independent and unaffected by
the worker. One run per bench in this measurement; multi-run averages are
left to the per-device sweep that comes after the spike series.

| Bench            | n  | min ms | mean ms | p50 ms | p95 ms | p99 ms | rAF mean ms | rAF p99 ms | budget? |
|------------------|---:|-------:|--------:|-------:|-------:|-------:|------------:|-----------:|---------|
| `doom_tick`      | 30 |   1.40 |  **1.71** |   1.50 |   2.70 |  **3.40** |       18.61 |      49.90 | **fits — 10% of budget** |
| `entity_update`  | 50 |   0.40 |  **0.70** |   0.60 |   1.10 |  **2.00** |       13.90 |      16.80 | **fits — 4% of budget**  |
| `binarytrees`    | 20 |   2.40 |  **3.02** |   2.70 |   5.40 |  **5.40** |       14.58 |      17.50 | **fits — 18% of budget** |
| `doom_tick_gc`   | 30 |   1.70 |  **2.11** |   1.80 |   4.30 |  **6.00** |       15.00 |      17.10 | **fits — 13% of budget** |
| `fannkuch`       | 20 |   1.40 |  **1.84** |   1.60 |   3.20 |  **3.20** |       13.32 |      16.70 | **fits — 11% of budget** |
| `fasta`          | 20 |   0.50 |  **0.67** |   0.60 |   2.00 |  **2.00** |       11.10 |      17.30 | **fits — 4% of budget**  |
| `mandelbrot`     | 20 |   3.40 |  **4.07** |   3.50 |   9.90 |  **9.90** |       14.81 |      16.70 | **fits — 24% of budget** |
| `nbody`          | 20 |   0.20 |  **0.35** |   0.30 |   1.00 |  **1.00** |       11.30 |      17.20 | **fits — 2% of budget**  |
| `spectral-norm`  | 20 |   4.70 |  **5.45** |   4.90 |   9.60 |  **9.60** |       15.15 |      17.30 | **fits — 33% of budget** |

The 16.67 ms / 60 fps frame budget is the threshold. Every bench fits,
with the worst-case (`spectral-norm` at 5.45 mean / 9.60 p99) using
~33% of budget and the best-case (`nbody` at 0.35 mean) using ~2%.
The load-bearing Lua workload from Spike E (`doom_tick`) is at 1.71 mean
/ 3.40 p99 — 10% / 20% of budget.

**Stage 2 exit gate.** PLAN.md sets it as "doom_tick p99 ≤ 8 ms on
desktop Chrome (so the 2–4× mid-range-Android projection band lands at
16–32 ms p99 — borderline to marginal)". Measured: **3.40 ms p99**, half
the gate threshold and a quarter of the budget. **Exit gate met by 2.4×.**

**rAF jitter is ~14 ms mean with p99 ~17 ms** for most benches — clean
60 fps cadence on the main thread, undisturbed by the worker. The
exception is `doom_tick`'s rAF p99 of 49.9 ms; this is an artefact of
the auto-run mode publishing the result almost immediately (the bench
finishes in ~100 ms, only ~7 rAF ticks fire before publish, n=7 is
small, the first one or two ticks can include page-load skew). It is
not a real on-bench jank — `doom_tick`'s mean rAF is 18.6 ms which is
~one frame at 60 fps. Future runs that accumulate longer rAF traces
should clear this up; not load-bearing for the spike's conclusion.

### Cross-stack comparison: Docker arm64 / Node-WASM-rv32emu / Chrome-WASM-rv32emu / Chrome-WASM-Lua-direct

The same `.lua` benchmark scripts under four different host execution
strategies. Numbers are the `mean=` field per run, in milliseconds per
tick:

| Bench            | (1) Docker arm64 (Lua-in-RV32IMFC, native rv32emu) | (2) Node 22 + WASM (rv32emu+Lua-in-RV32IMFC) | (3) Chrome 147 + WASM (rv32emu+Lua-in-RV32IMFC) | (4) **Chrome 147 + WASM (Lua-direct)** | (4) ÷ (1) | (4) ÷ (3) |
|------------------|---------------------------------------------------:|---------------------------------------------:|------------------------------------------------:|---------------------------------------:|----------:|----------:|
| `doom_tick`      |                                              61.30 |                                       250.11 |                                          122.89 |                                **1.71** |    0.028× |    0.014× |
| `entity_update`  |                                              16.30 |                                       109.25 |                                           52.72 |                                **0.70** |    0.043× |    0.013× |
| `binarytrees`    |                                              70.46 |                                       462.73 |                                          230.92 |                                **3.02** |    0.043× |    0.013× |
| `doom_tick_gc`   |                                                  — |                                       324.80 |                                              — |                                **2.11** |     —     |     —     |
| `fannkuch`       |                                                  — |                                       246.61 |                                              — |                                **1.84** |     —     |     —     |
| `fasta`          |                                                  — |                                        99.46 |                                              — |                                **0.67** |     —     |     —     |
| `mandelbrot`     |                                                  — |                                       624.82 |                                              — |                                **4.07** |     —     |     —     |
| `nbody`          |                                                  — |                                        25.97 |                                              — |                                **0.35** |     —     |     —     |
| `spectral-norm`  |                                                  — |                                       819.33 |                                              — |                                **5.45** |     —     |     —     |

The headline ratio asked for in PLAN.md is **(4) ÷ (1) — Lua-direct on
desktop Chrome divided by Lua-in-RV32IMFC on native Docker arm64.** For
the three carts where (1) was measured:

- `doom_tick`: 1.71 / 61.30 = **0.028×** — Chrome WASM Lua-direct is
  **36× faster** than the Docker arm64 native rv32emu path.
- `entity_update`: 0.70 / 16.30 = **0.043×** — **23× faster**.
- `binarytrees`: 3.02 / 70.46 = **0.043×** — **23× faster**.

The (4) ÷ (3) column — Lua-direct vs the Spike E rv32emu-in-WASM stack
on the *same* desktop browser at the *same* workload — quantifies how
much of the Spike E gap closes when the rv32emu layer is removed:

- `doom_tick`: 1.71 / 122.89 = **0.014×** — 72× speedup at the mean,
  37× at p99 (3.40 / 126.20 ms).
- `entity_update`: 0.70 / 52.72 = **0.013×** — 75× speedup at the mean.
- `binarytrees`: 3.02 / 230.92 = **0.013×** — 76× speedup at the mean.

**Conclusion:** Removing the rv32emu layer is roughly a **75× constant
factor** on these workloads on this host. Spike E identified a 7.4×
headroom shortfall; Lua-direct closes it ~10× over. The collapse of the
two-layer interpreter dispatch (Lua VM → RV32IMFC → rv32emu in spike-e)
into one (Lua VM → WASM/V8 in spike-f) is the load-bearing change.

### Mid-range Android phone projection

Without a measured number, the honest projection is a *range*. Mid-range
mobile V8 (Snapdragon 7-class, Tensor G2-class) typically runs WASM
2–4× slower than M-series desktop V8 on benchmark workloads (same band
Spike E used). Apply that to the desktop Chrome p99 numbers:

| Bench           | Desktop p99 | Mid-range Android p99 (2× projection) | (4× projection) | Verdict at 16.67 ms |
|-----------------|------------:|--------------------------------------:|----------------:|---------------------|
| `doom_tick`     |      3.40 ms |                                6.8 ms |          13.6 ms | **fits at both ends** (41–82% of budget) |
| `entity_update` |      2.00 ms |                                4.0 ms |           8.0 ms | **fits comfortably** (24–48% of budget) |
| `binarytrees`   |      5.40 ms |                               10.8 ms |          21.6 ms | **marginal at 4×** (1.3× over at pessimistic end) |
| `doom_tick_gc`  |      6.00 ms |                               12.0 ms |          24.0 ms | **marginal at 4×** (1.4× over at pessimistic end) |
| `fannkuch`      |      3.20 ms |                                6.4 ms |          12.8 ms | **fits at both ends** |
| `fasta`         |      2.00 ms |                                4.0 ms |           8.0 ms | **fits comfortably** |
| `mandelbrot`    |      9.90 ms |                               19.8 ms |          39.6 ms | **over at both ends** (1.2–2.4× over) |
| `nbody`         |      1.00 ms |                                2.0 ms |           4.0 ms | **fits comfortably** |
| `spectral-norm` |      9.60 ms |                               19.2 ms |          38.4 ms | **over at both ends** (1.2–2.3× over) |

For the three benches in PLAN.md's exit-gate band (`doom_tick`,
`entity_update`, `binarytrees`):

- `doom_tick` and `entity_update` project to **fit the budget on
  mid-range Android at both ends of the projection.** `doom_tick`'s
  pessimistic end is 13.6 ms — 18% headroom. `entity_update` has 8 ms /
  16.67 ms = ~50% headroom even at the pessimistic end.
- `binarytrees` projects to **fit at the optimistic end (10.8 ms / 65%
  of budget) and miss by 1.3× at the pessimistic end.** This is the
  shape PLAN.md described as "borderline to marginal, with optimistic
  case fitting the 16.67 ms budget" and accepted as a passing outcome
  for Stage 2.

The two non-game-loop CPU benches (`mandelbrot`, `spectral-norm`) project
to over-budget on mid-range Android. They are not realistic per-frame
workloads (they're "general programming benchmarks" inherited from the
shootout shape), and their over-budget projection does not contradict
the spike's conclusion. They quantify *how heavy* a Lua tic can be
before it starts missing budget on mobile, which is useful as a sizing
rule for cart authors: at 60 fps, a single per-tic Lua workload should
fit in roughly the `doom_tick`–`binarytrees` band (3.4–5.4 ms p99 on
desktop), not the `mandelbrot`–`spectral-norm` band (9.6–9.9 ms).

### What this does and does not say about Pi Zero 2 W

This is a desktop-WASM number, and it does not refine the Pi target. The
Pi target runs Lua-in-RV32IMFC under native rv32emu (per the spike-b /
ADR-0007 chain), not Lua-direct. Spike F adds an **independent execution
model for the WASM target only**: WASM hosts can use Lua-direct because
they pay the WASM-isolation tax already and no longer benefit from the
RV32IMFC-as-deterministic-ground-truth story (which is the WASM-
specific argument; on the Pi the determinism story gets re-evaluated
in ADR-0007 / ADR-0025 follow-ups, not in this spike).

For the Pi, Spike B's Docker→Pi extrapolation is unchanged. For the
WASM target, Spike F replaces "use the same rv32emu+RV32IMFC stack as
the Pi" with "use Lua-direct for the WASM target" as a new option that
this spike argues for.

---

## Architectural recommendation

**Adopt a per-target execution model.** The WASM target compiles Lua
directly to WASM and runs the cart `.lua` source through a host-embedded
Lua VM. The hardware target (Pi Zero 2 W) and the desktop-native target
continue to use Lua-in-RV32IMFC under native rv32emu — not because of
a determinism shortfall on the WASM side (see below), but because the
native targets have no performance shortfall to solve and the RV32IMFC
sandbox model is already paid for there.

This is exactly the fallback ADR-0025 names ("the host-embedded Lua
architecture"). The argument is now backed by data: the WASM stack as
configured in Spike E misses 60 fps by 7.4× on the load-bearing Lua
workload and is *catastrophic* (15–30× over) on mid-range Android; the
Lua-direct stack measured in Spike F is *under budget by ~5–10×* on the
same desktop and projects to *fitting* on mid-range Android for the
cart-shaped workloads.

### What this asymmetry actually costs

The first draft of this section claimed cross-target replay was not
byte-deterministic because Lua-direct-on-WASM "reaches into V8's
`printf` / `fmod` / `string.format` rounding paths." That framing was
wrong, and the corrected story matters for ADR-0025 / ADR-0007:

- **WASM has no transcendental, no `printf`, no `string.format`
  instructions.** WASM exposes IEEE 754 `f32.add` / `f32.mul` /
  `f32.sqrt` / etc. as ISA primitives, all required to be correctly
  rounded. Everything above that — `sinf`, `expf`, `pow`, `printf`,
  any decimal-conversion path — is *function calls into code linked
  inside the .wasm module*. Emscripten links musl libm and musl stdio
  into the module by default. The host engine (V8 / SpiderMonkey /
  Wasmtime) never resolves `sin` or `printf`; it only executes
  `f32.*` primitives.

- **So `math.sin(x)` and `string.format("%g", x)` produce identical
  bytes across every WASM host** running the same `.wasm`, because the
  implementation is frozen in the module.

- **Cross-target byte-determinism between Lua-direct-WASM and
  Lua-in-RV32IMFC** reduces to a build-config discipline:
  - Pin musl to the same version in both builds (Emscripten's bundled
    musl for the WASM build; the version compiled into the RV32 ELF
    for the spike-b stack). Match it.
  - No `-ffast-math`, no `-fassociative-math`, no `-fmad`. Standard
    `-O3` on both Clang/Emscripten and `riscv64-linux-gnu-gcc` does
    not reassociate floats; verify and pin.
  - Ignore NaN payload bits. WASM spec lets the host engine produce
    differing NaN payloads from NaN-producing ops; SoftFloat under
    rv32emu produces a specific payload. Lua's `==` returns false for
    NaN and `tostring(0/0)` is `"nan"` either way, so unless game
    code inspects the bit pattern of a NaN (it doesn't), this is
    moot.

- **What's *actually* asymmetric**, per ADR-0025 / ADR-0038, is the
  *sandbox model*, not the float-determinism story. RV32IMFC has a
  per-instruction CPU cap (ADR-0082), a single linear memory map, and
  syscall ECALLs as the entire host-call surface. Lua-direct on WASM
  has whatever sandbox `_ENV` discipline + the ADR-0079 stdlib
  allowlist gives us, no instruction cap (CPU exhaustion needs
  `lua_sethook` step-counting), and a different memory failure mode
  (WASM linear-memory OOM throws a JS exception; RV32 OOM is an
  ECALL return). These are different surfaces with different
  guarantees. Reproducing the RV32IMFC guarantees on the Lua-direct
  WASM target is a discrete piece of work — that's the actual cost
  ADR-0025 was naming.

Implications for the runtime story:

- **Single-player save-state / replay.** Reachable to byte-determinism
  cross-target with the build-config discipline above. Spike D's
  digest cross-check (deferred) is the verification step.
- **Netplay (lock-step rollback).** Same — reachable to byte-
  determinism with discipline. The dominant risk is musl version
  drift on the WASM side; pin it.
- **Cross-platform leaderboards / shared replays.** Same — should
  reproduce, with caveats around NaN-producing edge cases (which
  game code shouldn't exercise).

The performance side of the trade is what Spike F resolves. The
sandbox-model side is the work ADR-0025 calls "asymmetric security
and debugging model"; that work is unchanged by this spike and is
listed under "Architecture follow-up" below.

---

## Open questions

1. **Mid-range Android Chrome.** Has not been measured. Projection
   above suggests `doom_tick`, `entity_update`, `fannkuch`, `fasta`,
   and `nbody` all fit the budget at both ends of the projection band;
   `binarytrees` and `doom_tick_gc` are borderline at 4×; `mandelbrot`
   and `spectral-norm` (not realistic per-frame workloads) project over
   budget. Resolution requires a hands-on phone run (instructions
   below).

2. **Mid-range Android Firefox.** Has not been measured. SpiderMonkey's
   wasm tier-up profile differs from V8's. Same hands-on-phone caveat.

3. **Determinism cross-check vs Spike D.** ~~Pending.~~ **RESOLVED —
   PASS.** See Stage 3 below. All 60 DIGEST lines match the RV32IMFC
   reference. The one non-obvious build insight was that the WASM build
   must use the same naive `strtof` as spike-d's freestanding RV32 build.
   After that patch, every assumption in the bullet list above held:
   musl arithmetic, `-ffp-contract=off` semantics, and NaN handling all
   produced bit-identical results.

4. **Security model write-up.** Spike F does not test the sandbox.
   ADR-0066 (`_ENV` discipline) and ADR-0079 (standard-library
   allowlist) describe what an asymmetric (Lua-direct) sandbox looks
   like. A write-up that maps RV32IMFC sandbox guarantees onto the
   Lua-direct WASM equivalent — and identifies where the asymmetry is
   load-bearing (e.g. memory exhaustion is bounded by the WASM linear
   memory in both cases; CPU exhaustion is bounded by an instruction
   cap in RV32IMFC and by `lua_sethook`-style step counting under
   Lua-direct, which is a different correctness story) — is a
   follow-up, not a spike-F deliverable.

5. **Stdlib trim under Lua-direct.** Spike F leaves `luaL_openlibs` in
   full because the timing run is the deliverable. Production builds
   should bring forward Spike B's `lua_init_libs.c` (the
   ADR-0079 allowlist: base + table + string + math, omit io / os /
   loadlib / coroutine / debug / utf8). This is a one-file change and
   does not affect the timing numbers reported here.

6. **Workload sizing.** Several benches finish in <1 ms / tick on
   desktop Chrome and the per-microsecond `printf` resolution is the
   noise floor. For the cart workload sizing rule going forward, the
   `doom_tick` / `binarytrees` band (3.4–5.4 ms p99 on desktop) is the
   "realistic per-frame Lua tic" benchmark. `mandelbrot` /
   `spectral-norm` are CPU-stress reference points, not target sizes.

---

## Stage 3 — determinism cross-check vs Spike D

`make det-diff` builds `build/lua_det.{js,wasm}` from `det_host.c` (Lua 5.4
compiled to WASM with all of Spike D's determinism machinery inlined —
PCG32, `frame_state_t`, FNV-1a-64, NaN canon, same `console` bindings),
runs `det_doom_tick` and `det_entity_update` under Node + WASM, and diffs
the 62-line output against the first 62 lines of
`../spike-d/digests/digests.arm64.txt` (30 DIGEST lines per workload plus
the two `=== <name> ===` headers).

**Result: DETERMINISM PASS** — all 60 DIGEST hashes are byte-identical to
the Spike D RV32IMFC reference (which is itself cross-platform: the arm64
and amd64 Spike D runs produce identical digests).

### Root cause of initial failure — naive `strtof`

The initial run (before patching) showed complete divergence from frame 0
on both workloads. Diagnosis:

1. **PCG32 / frame_state / FNV-1a verified correct** via `det_diag.lua`
   (3 `unit_float` calls + `commit_frame`): WASM and native produced
   `142ae728e0d0a15b`, confirming integer arithmetic is bit-identical.
2. **`sinf` / `cosf` / `atan2f` verified correct**: WASM output for the
   test angles in `det_sintest.lua` matched native glibc bit-for-bit.
3. **Root cause isolated**: Spike D's freestanding RV32 build provides
   `strtof` via `spike-b/ports/rv32emu/lua_runtime.c`, a hand-written
   naive decimal parser that accumulates float digits with `scale *= 0.1f`
   repeated multiplication. This is NOT correctly-rounded for all decimal
   literals. Two constants in `det_doom_tick.lua` parse differently:
   - `"6.2831853"` → naive: `0x40c90fda`, correctly-rounded: `0x40c90fdb`
     (1 ULP difference)
   - `"0.05"` → naive: `0x3d4cccce`, correctly-rounded: `0x3d4ccccd`
     (1 ULP difference)
   The 1-ULP error in the initial mob angle constant (`6.2831853` is used
   in `mobs[i].angle = console.unit_float() * 6.2831853`) cascades through
   all 32 mob angles, and through every subsequent `sin`/`cos`/`atan2`
   call in every frame.

### Fix

`det_host.c` now defines `__wrap_strtof` and `__wrap_strtod` (the same
naive algorithm as `lua_runtime.c`) and the det-wasm build passes
`-Wl,--wrap=strtof -Wl,--wrap=strtod` to emcc so that Lua's
`lua_str2number` path intercepts to our version. With matching
string-to-float parsing, the underlying arithmetic (musl sinf over WASM
f64.* vs musl sinf over Berkeley SoftFloat __adddf3) is bit-identical, as
IEEE 754 guarantees for basic operations (+, -, *, /) with
round-to-nearest-even.

### Implications

- **Cross-target byte-determinism is achievable.** The fundamental
  operations (PCG32, IEEE 754 float arithmetic, musl sinf/cosf/atan2f
  compiled to WASM vs RV32) produce identical bits. The only divergence
  was in decimal-constant parsing, a build-config detail.
- **Production Lua-direct-WASM carts must not rely on `strtof`
  for determinism.** Cart scripts should use hex float literals
  (`0x1.921fb6p+2` instead of `6.2831853`) for any constant that must
  be bit-identical across RV32 and WASM builds. The naive-strtof wrap is
  a research artefact; production builds should use correctly-rounded
  parsing on both sides (e.g. by providing the same correctly-rounded
  `strtof` in the RV32 freestanding environment, replacing the
  `lua_runtime.c` approximation).
- **musl sinf / cosf / atan2f with Berkeley SoftFloat == musl sinf /
  cosf / atan2f with native IEEE 754 f64.*** Confirmed by the passing
  digest cross-check. The intermediate double arithmetic in musl's sinf
  kernel (`__sindf`, `__cosdf`, `__rem_pio2f`) uses only IEEE 754 basic
  operations, which are correctly-rounded and therefore deterministic
  across any conforming implementation.

---

## How to reproduce

### Desktop build + run

```sh
cd spikes/spike-f
make lua-src                  # fetch Lua 5.4.7, verify SHA-256
make lua-wasm                 # build build/lua.{js,wasm} (~275 KB wasm)
make node-test BENCH=doom_tick
make node-test-all            # 9 benches under Node + WASM Lua-direct
make chrome-bench             # 3 default benches via headless Chrome 147
make chrome-bench CHROME_BENCHES=binarytrees,doom_tick,doom_tick_gc,entity_update,fannkuch,fasta,mandelbrot,nbody,spectral-norm
                              # all 9 benches under headless Chrome 147
make start-web                # interactive: serves http://127.0.0.1:8000/
make det-diff                 # Stage 3: determinism cross-check vs Spike D (expects PASS)
```

### Mobile run (the hand-off)

The harness page does not need anything special on the phone — just a
recent Chrome (≥ 112) or Firefox (≥ 121).

```sh
cd spikes/spike-f
make stage-web                # produce demo/
make start-web-lan            # serves http://0.0.0.0:8000/
ifconfig en0                  # find your LAN IP
```

On the phone (same LAN as the dev machine):

1. Open Chrome / Firefox and navigate to
   `http://<dev-ip>:8000/?bench=doom_tick&auto=1`.
2. The harness auto-runs the bench and renders p50/p95/p99 +
   histogram. Capture the screenshot.
3. Repeat for `entity_update`, `binarytrees`, `doom_tick_gc`,
   `mandelbrot`, `spectral-norm`. Both Chrome and Firefox.

Post the per-bench × per-browser numbers back into this document under
a "Mid-range Android Chrome / Firefox results" section once captured.
The harness deliberately does not require COOP/COEP, so a plain HTTP
origin is enough.

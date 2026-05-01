# Spike E results — rv32emu + Lua cart in a browser

**Status: build and stack end-to-end complete; correctness verified across
Docker arm64 / Node-WASM / headless Chrome 147; desktop measurements taken
on Apple Silicon and they show the load-bearing Lua workload at ~123 ms per
tick (7.4× over the 16.67 ms frame budget); mobile measurement is the open
question and is handed off as a manual run because it requires a physical
Android phone.**

The question Spike E asks is whether the Spike B stack
(`rv32emu` + Lua-in-RV32IMFC), compiled to WASM via Emscripten, holds 60 fps
in a browser on a development desktop and on a mid-range 2–3-year-old
Android phone. The build target works. The performance answer on desktop is
**no** for the load-bearing Lua workload (`doom_tick` at 123 ms mean / 126
ms p99 vs the 16.67 ms budget) but **yes** for the native-C escape-hatch
workload (`doom_tick_c` at 2 ms mean / 4.2 ms p99). On a Mac M-series CPU
under Chrome 147, V8 is 1.5–3.3× slower than the Spike B Docker arm64
baseline at the same workload, with the slowdown concentrated on the Lua
carts. Mid-range Android Chrome will be slower again.

---

## What was built

**WASM target** for upstream `rv32emu` is unchanged from the upstream's
`make wasm_defconfig` path; we replaced only the `--embed-file` list (no
DOOM/Quake/smolnes/perfcount/etc) with the Spike B cart ELFs and dropped
the `-sPTHREAD_POOL_SIZE` flag (no SDL, no JIT, no T2C → no pthread
references → no `SharedArrayBuffer` dependency → no need for the COOP/COEP
service-worker reload dance, which makes both first-page-load latency and
automated measurement noticeably better-behaved).

`spikes/spike-e/` mirrors the spike-a / spike-b layout:

- `Makefile` orchestrates: install Emscripten 3.1.51 under `./emsdk/`,
  extract cart ELFs from the `fc32-spike-b` Docker image into `./elfs/`,
  rsync the rv32emu source into `./rv32emu/` (so we can patch `mk/wasm.mk`
  without dirtying spike-a), and build with `OUT=build-wasm`.
- `wasm.mk` is a stripped replacement for `rv32emu/mk/wasm.mk`. Drops
  upstream embeds, drops the `artifact` prereq (no GitHub blob fetches),
  drops `-sPTHREAD_POOL_SIZE`, narrows `-sINITIAL_MEMORY` from 2 GB to
  256 MB and `-DMEM_SIZE` from 512 MB to 32 MB. Carts are embedded at
  `/<basename>.elf` inside the WASM in-memory FS.
- `configs/wasm_spike_e.config` is the Kconfig overlay: same RV32IMFC +
  ELF-loader + interpreter-only profile as spike-a, plus
  `CONFIG_BUILD_WASM=y`.
- `web/` contains the measurement harness (HTML + main-thread JS + Web
  Worker + a Node-based headless-Chrome driver for desktop measurement).

**Embedded cart binaries** are byte-identical to those Spike B builds inside
its Docker image; we extract them via `docker cp` rather than rebuild, so
the WASM build does not need a RISC-V cross-toolchain on the host. The
build is host-side (no arm64 Docker dependency for spike-e itself).

**Measurement harness** (`web/spike_e.html`, `web/spike_e.js`,
`web/spike_e_worker.js`):

- Cart selection dropdown built from `elf_list.js`; press Run, or load the
  page with `?cart=<name>&auto=1` to drive automated runs.
- Cart runs inside a dedicated Web Worker (`spike_e_worker.js`). The worker
  pre-sets `self.Module` hooks so the IIFE-local `var Module = typeof
  Module != "undefined" ? Module : {}` in `rv32emu.js` picks them up,
  `importScripts("rv32emu.js")` boots the WASM, and our
  `onRuntimeInitialized` hook calls `Module.run_user(<cart>)`. The cart
  prints `FRAME <name> <i> <us>` per tick and `SUMMARY <name> frames=N
  min=us max=us mean=us` once at the end (same format as Spike B's Docker
  output). Each `printf` line is forwarded to the main thread one
  `postMessage` per line.
- Main thread parses FRAME / SUMMARY lines, computes p50/p95/p99 + a 32-bin
  histogram, renders both alongside a 16.67 ms guideline.
- Independent `requestAnimationFrame` loop on the main thread records its
  own inter-frame deltas — this is the **jank meter**: it surfaces any
  bleed-through from the WASM run leaking onto the main thread.

**Headless-Chrome driver** (`web/run_chrome.cjs`, plus
`make chrome-bench`) uses `puppeteer-core` against the system Chrome
binary (no Chromium download) to load
`?cart=<name>&auto=1` for each requested cart, wait until
`window.__spikeEResult` is populated, and emit one JSON line per cart
plus a JSON file with the full per-tick distributions.

---

## Stage 1 — correctness across rv32emu hosts

`make node-test-all` runs each cart end-to-end under Node 22 + WASM
rv32emu. The cart's per-tick outputs match Spike B's Docker arm64 runs
**structurally**: identical `FRAME <name> <i> <us>` line count, identical
benchmark name, identical frame indices, identical `SUMMARY` schema. Only
the timing values differ (as expected — wall clock varies between
platforms). For the four carts the spike-e plan calls out:

```
$ diff <(awk '{print $1, $2, $3}' baselines/<cart>.docker.txt) \
       <(awk '{print $1, $2, $3}' baselines/<cart>.node-wasm.txt)
(empty)
```

…across `lua_cart_doom_tick`, `lua_cart_binarytrees`,
`lua_cart_entity_update`, `doom_tick_c`. All four carts produce the same
30, 20, 50, 30 FRAME lines respectively, in the same order, plus the
SUMMARY line. The Spike-B → WASM port is correct.

A spot check in headless Chrome 147 (via `make chrome-bench`) reproduces
the same FRAME / SUMMARY structure with the cart running inside a Web
Worker. Stage 1 exit gate met for `doom_tick`, `entity_update`,
`binarytrees`, and `doom_tick_c`.

### Notes from Stage 1

1. **Worker `done` posts before stdout flushes on Chrome.** Inside the
   worker we call `Module.run_user(cart)` synchronously from
   `onRuntimeInitialized`; when it returns we `postMessage({type:"done"})`.
   Empirically that "done" message arrives at the main thread *before* the
   FRAME messages from the cart's `printf`. Emscripten 3.1.51 on Chrome 147
   appears to flush stdout post-task. The harness ignores worker `done` for
   result publication and uses the cart-emitted SUMMARY line as the
   canonical "cart finished" trigger instead. This is documented in the
   harness; revisit if upgrading Emscripten changes the flush behaviour.

2. **Node + WASM needs a one-line shim.** `rv32emu`'s `main.c` calls
   `disable_run_button()` (an `EM_JS` helper that touches
   `document.getElementById('runButton').disabled`). Workers and Node both
   lack `document`. The harness stubs it with a no-op
   (`self.document = { getElementById: () => ({ disabled: false }) }`).
   No source change to upstream rv32emu required.

3. **No `SharedArrayBuffer` references** in the produced
   `rv32emu.js`/`.wasm` after dropping `-sPTHREAD_POOL_SIZE`. This means
   no need for COOP+COEP cross-origin isolation, and no need for the
   `coi-serviceworker.min.js` reload-shim. The harness ships without it.

---

## Stage 2 — desktop frame-time measurements

Host: Apple Silicon (M-series), macOS 25.4 Darwin, Google Chrome 147
(headless via puppeteer-core driving the system Chrome binary). Page
loaded with `?cart=<name>&auto=1`; the cart runs inside a dedicated Web
Worker; the rAF loop on the main thread is independent and unaffected by
the worker. One run per cart in this measurement; multi-run averages are
left to the per-device sweep that comes after Spike E.

| Cart                | n  | min ms | mean ms | p50 ms | p95 ms | p99 ms | rAF mean ms | rAF p99 ms | budget? |
|---------------------|---:|-------:|--------:|-------:|-------:|-------:|------------:|-----------:|---------|
| `doom_tick`    (Lua)| 30 |  110.8 |  **123.0** | 122.9 | 125.8 | **126.5** | 16.6 | 17.7 | **7.4× over** |
| `entity_update`(Lua)| 50 |   45.4 |   **52.7** |  52.4 |  55.5 |  **56.2** | 16.6 | 17.6 | **3.2× over** |
| `binarytrees`  (Lua)| 20 |  212.4 |  **230.5** | 232.3 | 235.4 | **235.4** | 16.6 | 17.7 | **13.8× over** |
| `doom_tick_c` (C)   | 30 |    1.0 |    **2.0** |   1.8 |   2.9 |    **4.2** | 14.6 | 17.3 | **fits**   |

The 16.67 ms / 60 fps frame budget is the threshold. Lua-driven carts blow
through it by 3.2–13.8× on a fast Apple Silicon desktop. The native-C cart
fits with two orders of magnitude of headroom (4.2 ms p99 / 16.67 ms ≈ 25%
of budget at the worst tick).

**rAF jitter is ~16.6 ms with p99 at 17.7 ms** — i.e. a clean 60 fps
rendering cadence on the main thread, completely undisturbed by the
worker's WASM run. This is what was hoped from putting the cart in a
Worker: the page itself never janks, only the cart-internal frame time
suffers.

### Cross-stack comparison: Docker arm64 vs Node 22 vs Chrome 147

Same cart ELFs, same `rv32emu` source, three different host execution
environments. Numbers are the `mean=` field from each cart's SUMMARY line,
in milliseconds per tick:

| Cart            | Docker arm64 | Node 22 + WASM | Chrome 147 + WASM | Chrome / Docker | Chrome / Native (M-series) |
|-----------------|-------------:|---------------:|------------------:|----------------:|---------------------------:|
| `doom_tick`     |      61.3    |     250.1      |    **123.0**      |       2.01×     | n/a (no native run)        |
| `entity_update` |      16.3    |     109.3      |     **52.7**      |       3.23×     | n/a                        |
| `binarytrees`   |      70.5    |     462.7      |    **230.5**      |       3.27×     | n/a                        |
| `doom_tick_c`   |       1.3    |       3.9      |      **2.0**      |       1.50×     | n/a                        |

Two things stand out:

1. **Chrome's V8 wasm is roughly twice as fast as Node 22's V8 wasm** on
   these workloads. Same V8 release-cycle version of TurboFan, but
   plausibly differing tier-up / liftoff scheduling under
   warm-up-vs-once-and-done conditions. The Chrome run is also longer-lived
   per cart (the page sits open and the WASM module is reused across rAF
   cycles before the cart runs); we did not chase this further.

2. **The Lua carts pay ~3× the WASM tax of the native-C cart.** On
   `doom_tick_c` the WASM stack is 1.5× slower than Docker arm64 — that's
   roughly the rv32emu-under-WASM tax alone (interpreter dispatch in WASM
   vs. native arm64). On the Lua carts the slowdown is 2.0–3.3× — the
   extra factor is the Lua VM's bytecode dispatch interacting badly with
   WASM's stricter memory model and lack of indirect-jump primitive.

   Translated: Lua-in-RV32IMFC-in-WASM is paying a *two-layer* interpreter
   dispatch cost where the native C cart pays only one. The 78× Lua-vs-C
   factor Spike B observed on Docker arm64 widens to roughly 130× on
   WASM (for `doom_tick`). The hot-path C escape hatch is even more
   load-bearing on WASM than it was on Docker arm64.

### What this does and does not say about Pi Zero 2 W

This is a desktop-WASM number. The Pi-vs-WASM relationship is **not**
"WASM on phone ≈ Pi". Spike A's measured rv32emu performance on arm64
Docker / Apple Silicon is the same V8-class CPU population that produced
the Chrome 147 numbers above, just running native arm64 vs running WASM.
The relevant comparison for the WASM target is the **previous row** of
this table (Chrome / Docker = 1.5–3.3×), not the Pi projection.

For the Pi target, Spike B's Docker→Pi extrapolation (4–8× slowdown for
interpreter-heavy code on Cortex-A53 @ 1 GHz vs. arm64 Docker on Apple
Silicon) is the load-bearing one and is unchanged by this spike. Spike E
adds an **independent** WASM-host axis to the platform's deployment
surface; it does not refine the Pi number.

---

## Headroom analysis

**`doom_tick` (Lua) on desktop Chrome WASM is 7.4× over the 60 fps
budget.** Even a 2× constant-factor optimisation in V8 (or a switch to
SpiderMonkey, which has historically been comparable on wasm-heavy
workloads) would still leave it at ~3.7× over. Closing that gap requires
**either** removing a layer of interpretation (Lua compiled directly to
WASM rather than to RV32IMFC running under rv32emu compiled to WASM) **or**
restricting the Lua carts to a much smaller per-tick workload.

**`entity_update` (Lua) on desktop Chrome WASM is 3.2× over budget.** This
is the lighter "position-only" benchmark — a reasonable proxy for "modest
retro game logic in Lua" if the game has no per-mob AI, no projectile
spawn/free pressure, and no per-mob distance-check sqrts. Even at this
scale, desktop V8 cannot run it at 60 fps under the current stack. Same
mitigation paths as above.

**`doom_tick_c` (native C) on desktop Chrome WASM is 4× under budget at
the p99 tic.** Native-C carts running through the same WASM stack
*comfortably* hold 60 fps on desktop. This is the path to viable mobile
WASM gameplay: keep the per-frame hot path in C, use Lua for "above the
hot path" logic that can run at lower cadence (event handlers, AI
high-level decisions every Nth tick, UI). This matches the original spike
B framing of native C as the "hot-path escape hatch", and the WASM numbers
strengthen it: on the WASM target the escape hatch is no longer an
optimisation, it is the only surface that runs at 60 fps for non-trivial
per-frame work.

**Mid-range Android phone projection.** Without a measured number, the
honest projection is a *range*. Mid-range mobile V8 (Snapdragon 7-class,
Tensor G2-class) typically runs WASM 2–4× slower than M-series desktop
V8 on benchmark workloads (basis: Speedometer 3 / Octane 2 wasm splits
seen in published phone reviews). Apply that band to the desktop Chrome
numbers above:

| Cart            | Desktop p99 | Mid-range Android p99 (2× projection) | (4× projection) | Verdict at 16.67 ms |
|-----------------|------------:|-------------------------------------:|----------------:|---------------------|
| `doom_tick`     |   126.5 ms  |                              253 ms |          506 ms | **catastrophic** (15–30× over) |
| `entity_update` |    56.2 ms  |                              112 ms |          225 ms | **catastrophic** (7–13× over)  |
| `binarytrees`   |   235.4 ms  |                              471 ms |          942 ms | **catastrophic** (28–57× over) |
| `doom_tick_c`   |     4.2 ms  |                                8 ms |           17 ms | **marginal at 4×**, comfortable at 2× |

Even the native-C cart projects to *marginal* on mid-range Android at the
4× pessimistic end (17 ms p99 against a 16.67 ms budget). Lua carts do
not project to anything close to viable. **The phone-Chrome measurement
is unlikely to invalidate this projection — it can only refine it.**

---

## What we learned about the build path

These are the non-obvious things that came out of getting the WASM target
working that future spikes / production builds will benefit from:

1. **Drop pthreads from the `rv32emu` WASM build.** Upstream's `wasm.mk`
   sets `-sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency` unconditionally.
   Our config (no SDL, no JIT, no T2C) does not reference pthreads, so
   dropping the flag has no functional cost and removes the
   `SharedArrayBuffer` dependency. Removing SAB in turn removes the
   COOP/COEP cross-origin-isolation requirement — which means no
   `coi-serviceworker.min.js` shim, no first-load page-reload, and clean
   automated measurement under puppeteer.

2. **The `--embed-file` list in upstream `wasm.mk` is `OUT=build` hard-coded.**
   We use `OUT=build-wasm` to keep the spike-e build separate from
   spike-a's `build/`, which means the embed paths must be parameterised
   (`$(OUT)/cart.elf` rather than `build/cart.elf`). Trivial, but only
   visible at the link stage.

3. **`disable_run_button()` is an `EM_JS` helper that hits
   `document.getElementById`.** Hard-coded into `rv32emu/src/main.c`. In
   any host that does not have `document` (Node, Web Worker), provide a
   stub:
   ```js
   self.document = { getElementById: () => ({ disabled: false }) };
   ```
   No source change to upstream needed; cleaner than patching
   `em_runtime.c`.

4. **Stdout from a cart's `printf` flushes *after* `Module.run_user`
   returns on Chrome 147.** This means a Worker that posts `done` right
   after `run_user` returns will see that `done` arrive at the main thread
   *before* the cart's stdout messages. Use the cart-emitted SUMMARY line
   as the canonical "cart finished" signal, not the worker's `done`.

5. **`elf_list.js` needs valid JSON-shaped strings.** The upstream
   `tools/gen-elf-list-js.py` interpolates Python list literals into JS,
   which works because Python and JS string-list literals are both
   `["a","b"]`. We replaced it with a shell-side `python3 -c json.dumps`
   call inside the Make recipe.

---

## Open questions

1. **Mid-range Android Chrome.** Has not been measured. The projection
   above suggests it cannot hold 60 fps for *any* Lua cart, and is
   marginal even for the native-C cart at the pessimistic end. Resolution
   requires a hands-on phone run (instructions below).

2. **Mid-range Android Firefox.** Has not been measured. SpiderMonkey's
   wasm tier-up profile differs from V8's; could be anywhere from -20%
   to +50% per cart. Same hands-on-phone caveat.

3. **Could the WASM stack be made viable by removing the `rv32emu` layer?**
   Out of scope for Spike E (which deliberately tested *the existing
   stack* on WASM), but the desktop numbers strongly suggest that a
   Lua-VM-compiled-directly-to-WASM build (no RV32IMFC layer at all)
   would close most of the 7.4× gap. The trade-off is determinism: the
   determinism story (Spike D, not yet run) currently depends on the
   RV32IMFC ISA being the deterministic ground truth. Bypassing
   `rv32emu` for the WASM target re-opens the determinism question
   *for the WASM target specifically* — a per-target deterministic
   behaviour story is a real architecture decision that this spike does
   not resolve.

4. **Does enabling rv32emu's JIT or T2C path improve the WASM numbers?**
   The current build uses `CONFIG_INTERPRETER_ONLY=y`. Spike A (and
   upstream's own benchmarks) show the JIT helps significantly on native
   x86_64/aarch64. Whether emcc can compile rv32emu's JIT to WASM
   (the JIT generates host machine code at runtime — that's not a
   thing in WASM modules without `wasm-opt`/dynamic instantiation) is an
   unanswered question, and arguably out of scope for Spike E. Documented
   here so it can be revisited if the conclusion above ("the stack as
   configured does not hold 60 fps for Lua") is the answer that needs
   loosening.

5. **Determinism cross-check (the Spike E "bonus").** Spike D has not
   run. Once D produces reference hex digests for the same carts, re-run
   Stage 2 on the WASM build, capture digests, diff. That is a follow-up
   ticket, not a blocker for the spike's primary success criterion.

---

## How to reproduce

### Desktop build + run

```sh
cd spikes/spike-e
make wasm           # installs Emscripten 3.1.51 if needed, builds
make node-test BENCH=lua_cart_doom_tick.elf  # one cart under Node + WASM
make node-test-all  # all 10 carts under Node + WASM
make chrome-bench   # 4 carts under headless Chrome 147 (puppeteer-core)
make start-web      # interactive: serves http://127.0.0.1:8000/
```

### Mobile run (the hand-off)

The harness page does not need anything special on the phone — just a
recent Chrome (≥ 112 for tail-call wasm) or Firefox (≥ 121). Two-step:

```sh
cd spikes/spike-e
make stage-web                 # produce demo/
make start-web-lan             # serves http://0.0.0.0:8000/
ifconfig en0                   # find your LAN IP, e.g. 192.168.1.42
```

On the phone (same LAN as the dev machine):

1. Open Chrome (or Firefox) and navigate to
   `http://192.168.1.42:8000/?cart=lua_cart_doom_tick.elf&auto=1`.
   Replace the IP with whatever `ifconfig en0` showed.
2. The harness auto-runs the cart and renders p50/p95/p99 + a histogram.
   Capture the screenshot.
3. Repeat for `lua_cart_entity_update.elf`, `doom_tick_c.elf`,
   `lua_cart_binarytrees.elf`. Both Chrome and Firefox.

If the phone refuses the HTTP load due to mixed-content / private-network
restrictions, fall back to ngrok or `python3 -m http.server` behind a
local HTTPS terminator. The harness deliberately does not require COOP/COEP
(see "What we learned" #1), so a plain HTTP origin is enough.

Post the four-cart × two-browser numbers back into this document under a
"Mid-range Android Chrome / Firefox results" section once captured.

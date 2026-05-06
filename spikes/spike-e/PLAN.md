# Spike E — implementation plan

**Question (per `docs/design/early-validation-spikes.md` §E):** does the
Spike B stack (rv32emu + Lua-in-RV32IMFC), compiled to WASM via Emscripten,
hold 60 fps in a browser on a development desktop and on a mid-range
2–3-year-old Android phone?

**Inputs we already have:**
- `spikes/spike-a/rv32emu/` — upstream rv32emu with `mk/wasm.mk` + a working
  Emscripten path (`make wasm_defconfig`, embeds demo ELFs, ships
  `assets/wasm/html/user.html`, serves via `python3 -m http.server`).
- `spikes/spike-b/ports/rv32emu/build/lua_cart_*.elf` — RV32IMFC guest
  binaries: the six Lua microbenchmarks plus `entity_update`, `doom_tick`,
  `doom_tick_gc`, and the native-C `doom_tick_c`.
- Spike B baselines (`docs/design/spike-b-results.md`): per-tick mean / p95
  numbers under arm64 Docker rv32emu. WASM frame time on desktop should
  land in the same order of magnitude on a fast x86/ARM host; mobile is the
  open question.

**What we are *not* building:**
- No audio. No real graphics output. No SDL. The success criterion is a
  frame-time histogram, not a playable demo.
- No new emulator. We use upstream rv32emu's Emscripten target unchanged
  except for swapping in our cart ELFs and our HTML harness.
- No determinism cross-check (Spike D bonus). Spike D has not run yet; revisit
  once D produces reference digests.

---

## Approach

Two stages. Stage 1 answers "does it build and run correctly under WASM at
all"; stage 2 answers "does it hit 60 fps on the two device classes."

### Stage 1 — WASM port working in Node + browser, correctness only

1. Install Emscripten 3.1.51 (the version `mk/wasm.mk` warns about) on the
   host. Document the command in the spike-e `Dockerfile`/`Makefile`.
2. Build rv32emu with `make wasm_defconfig && make` to confirm upstream's
   WASM target compiles in our environment. Drop the upstream `--embed-file`
   list (DOOM, Quake, smolnes, …) — we don't need it.
3. Replace embed-file list with our Spike B carts (`lua_cart_doom_tick.elf`,
   `lua_cart_entity_update.elf`, the six Lua benches, plus `doom_tick_c.elf`
   for the native baseline). Wire them through `tools/gen-elf-list-js.py` so
   the existing demo selector finds them.
4. Run one cart end-to-end via `node` (Emscripten produces a `.js` runnable
   under Node) and confirm output matches the arm64 Docker run from Spike B.
5. Bring up `make start-web`, load `user.html` in a desktop browser, run the
   same cart, confirm output matches.

Exit criterion: `lua_cart_doom_tick.elf` produces byte-identical stdout under
(a) arm64 Docker rv32emu, (b) Node + WASM rv32emu, (c) Chrome + WASM rv32emu.

### Stage 2 — frame-time measurement on desktop and mobile

6. Replace `user.html` with a minimal harness (`spike_e.html`) that:
   - Boots the WASM module.
   - Lets the user pick a cart from a dropdown.
   - Runs the cart to completion in a Web Worker (so the main thread is free
     for `requestAnimationFrame`).
   - Drives an idle rAF loop on the main thread that advances a canvas
     counter and records `performance.now()` deltas — confirms the page is
     not janking during the cart run.
   - Receives per-tick timing from the worker (carts already print
     `frame N: t = …` in Spike B; pipe stdout back via `postMessage`).
   - Renders a histogram + p50 / p95 / p99 summary for both the cart's
     internal per-tick numbers *and* the main-thread rAF cadence.
7. Decide on per-frame stepping vs whole-cart:
   - Default: keep the upstream "run cart to completion" model. Cart prints
     per-tick timings; we compute the distribution. Frame budget pass = cart
     mean + p95 well under 16.67 ms. This requires no emulator changes.
   - Stretch: expose a `step_one_tick()` JS-callable function so each rAF
     drives exactly one guest tick. Only worth doing if Stage 1 looks healthy
     and we want a true real-time feel.
8. Run on the development desktop (this Mac). Capture
   `lua_cart_doom_tick.elf` (load-bearing), `entity_update`, `doom_tick_c`
   (native baseline), and `binarytrees` (allocation-heavy upper bound).
9. Mobile run. Two-step:
   - Serve `spike_e.html` from the dev machine (`make start-web`, exposed via
     ngrok or local Wi-Fi HTTPS — Emscripten threads need
     cross-origin-isolation, hence `coi-serviceworker.min.js`).
   - Run on the test Android phone in current Chrome and current Firefox.
     Capture the same four carts and the same histograms.
10. Write up `docs/design/spike-e-results.md` mirroring `spike-a-results.md`
    / `spike-b-results.md`: what was built, raw numbers, projection vs the
    16.67 ms budget, comparison to the Spike B Docker baseline, headroom
    analysis, open questions.

Exit criterion: `lua_cart_doom_tick.elf` mean per-tick wall-clock under
~10 ms on desktop and under ~16.67 ms on the test Android phone, with p95
not blowing the budget. If mobile is marginal, the result doc must
quantify the headroom precisely (see spike doc's secondary ask).

---

## Risk notes

- **Emscripten 3.1.51 vs current SDK.** The Makefile is pinned to 3.1.51 era
  features (mimalloc detection runs from 3.1.50). If we end up on a much
  newer emcc the warnings are fine but verify the output runs. If we end up
  older, install 3.1.51 explicitly via emsdk.
- **Tail-call optimisation.** `mk/wasm.mk` enables `-mtail-call` and warns
  if the local browser is below Chrome 112 / Firefox 121 / Safari 18.2.
  Both target devices need to be at or above those.
- **Cross-origin isolation.** SharedArrayBuffer / pthreads require COOP+COEP
  headers. Upstream ships `coi-serviceworker.min.js` to fake them on the
  static `python3 -m http.server`. For ngrok / phone testing we may need
  real headers — note in advance.
- **2 GB initial memory.** `wasm.mk` sets `INITIAL_MEMORY=2GB`. Mid-range
  Android browsers may refuse. If allocation fails, drop `MEM_SIZE` /
  `INITIAL_MEMORY` to the minimum that fits a Lua cart (Spike B carts are
  tiny — 64 MB is plenty).
- **Cart embed size.** Each `lua_cart_*.elf` is hundreds of KB. Embedding
  all of them is fine for desktop, possibly excessive for the first mobile
  load. If first-load size becomes an issue, switch to fetch-on-demand.
- **Determinism bonus is deferred.** Spike D has not run. Once D produces
  reference hex digests for the same carts, re-run Stage 2 on the WASM
  build, capture digests, diff. That is a follow-up, not a blocker for
  spike E's primary success criterion.

---

## Deliverables

- `spikes/spike-e/Dockerfile` + `spikes/spike-e/Makefile` mirroring the
  spike-a / spike-b layout. Build is host-side (no arm64 Docker needed) but
  we keep the Make-driven layout for consistency.
- `spikes/spike-e/web/spike_e.html` — harness page.
- `spikes/spike-e/web/spike_e.js` — worker + histogram code.
- `docs/design/spike-e-results.md` — final write-up.
- `spikes/spike-e/TASKS.md` — checklist, kept current as work proceeds.

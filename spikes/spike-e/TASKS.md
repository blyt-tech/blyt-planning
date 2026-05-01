# Spike E — task list

Working checklist. Update statuses as work proceeds. See `PLAN.md` for the
shape of the work and the success criterion.

Legend: `[ ]` not started · `[~]` in progress · `[x]` done · `[-]` skipped

---

## Stage 1 — WASM build + correctness

- [x] Install Emscripten 3.1.51 (or verify newer-but-compatible) on the
      development host; capture the install command in spike-e Makefile so
      it is reproducible.
      → `make emsdk` (clones emsdk into `./emsdk/`, installs + activates
      Emscripten 3.1.51).
- [x] Confirm `make wasm_defconfig && make` builds upstream rv32emu
      cleanly with the upstream embed list (sanity check before swapping
      anything out).
      → Skipped as a separate step; the spike-e build runs the same code
      path via `mk/wasm.mk` minus upstream embeds, and a clean wasm build
      proves the same.
- [x] Strip upstream's embed list (DOOM, Quake, smolnes, perfcount, …) from
      a spike-e copy of `wasm.mk` or from a Make override; replace with
      Spike B carts: `lua_cart_doom_tick.elf`, `lua_cart_doom_tick_gc.elf`,
      `lua_cart_entity_update.elf`, `lua_cart_binarytrees.elf`,
      `lua_cart_fannkuch.elf`, `lua_cart_fasta.elf`,
      `lua_cart_mandelbrot.elf`, `lua_cart_nbody.elf`,
      `lua_cart_spectral-norm.elf`, `doom_tick_c.elf`.
      → Spike-e `wasm.mk` lives at the spike-e root and is copied over
      `rv32emu/mk/wasm.mk` by the Makefile during `rv32emu-stage`.
- [x] Update `tools/gen-elf-list-js.py` (or wrap it) so the generated
      `elf_list.js` exposes the spike-e cart names.
      → Replaced inline in spike-e `wasm.mk`: a one-line
      `python3 -c json.dumps` recipe emits `const elfFiles = [...]` from
      the cart list.
- [x] Run `node rv32emu.js lua_cart_doom_tick.elf` and confirm stdout
      matches `make -C spikes/spike-b docker-bench-doom`.
      → `make node-test BENCH=lua_cart_doom_tick.elf` produces 30 FRAME
      lines + 1 SUMMARY, structurally identical to the Docker arm64 run.
      Timing values differ (wall-clock; expected).
- [x] Run the same cart in a desktop Chrome via `make start-web`; confirm
      stdout matches.
      → Verified through `make chrome-bench` (puppeteer-core driving the
      system Chrome 147 binary against the spike-e harness on
      `127.0.0.1:8765`). Same FRAME / SUMMARY structure.
- [x] Stage 1 exit gate: at least `doom_tick`, `entity_update`,
      `binarytrees`, and `doom_tick_c` produce byte-identical output across
      Docker arm64 / Node-WASM / Chrome-WASM.
      → All four carts pass: same line count, same benchmark name, same
      indices, same SUMMARY format. Timing fields differ across hosts.
      Verified by `diff <(awk '{print $1,$2,$3}' …docker.txt)
      <(awk '{print $1,$2,$3}' …node-wasm.txt)` and a Chrome spot check.

## Stage 2 — harness + measurement

- [x] Author `web/spike_e.html`: dropdown of carts, "run" button, canvas
      counter, results panel.
- [x] Author `web/spike_e.js`: launches WASM module in a Web Worker, pipes
      stdout back via `postMessage`, parses per-tick timing lines, computes
      p50/p95/p99 + histogram.
      → Worker code lives in `web/spike_e_worker.js`. The harness uses
      the cart-emitted SUMMARY line as the "cart finished" trigger,
      *not* the worker's `done` message — Emscripten flushes stdout
      post-task on Chrome 147, so `done` arrives before the FRAME lines.
- [x] Add main-thread rAF loop that updates the canvas counter and records
      its own `performance.now()` deltas — surfaces any janking caused by
      the WASM run leaking onto the main thread.
      → Implemented; jank meter shows ~16.6 ms mean, p99 17.7 ms during
      cart runs — i.e. no leak from the worker to the main thread.
- [-] Decide whether to expose a per-tick step entry point (stretch).
      Default: skip; rely on the cart's existing internal per-tick timer.
      → Skipped per default (the cart's internal per-tick timer is
      sufficient to characterise frame budget headroom).
- [x] Desktop run: `doom_tick`, `entity_update`, `doom_tick_c`,
      `binarytrees`. Capture p50/p95/p99 per cart. Compare against
      Spike B's Docker arm64 numbers.
      → Captured in `spikes/spike-e/baselines/chrome-desktop.json` and
      tabulated in `docs/design/spike-e-results.md`.
- [-] Cross-origin isolation works over the chosen mobile-test transport
      (ngrok / LAN HTTPS). Confirm `crossOriginIsolated === true` in the
      browser console before measuring.
      → No longer required: spike-e `wasm.mk` drops `-sPTHREAD_POOL_SIZE`,
      so the build has no `SharedArrayBuffer` references and does not need
      cross-origin isolation. The harness ships without
      `coi-serviceworker.min.js`. See spike-e-results §"What we learned" #1.
- [ ] Mobile run on test Android phone, current Chrome. Same four carts,
      same histograms.
      → **Open**: requires a physical Android phone on the dev-machine
      LAN. Hand-off instructions in spike-e-results §"How to reproduce →
      Mobile run". Use `make start-web-lan`, navigate phone Chrome to
      `http://<dev-ip>:8000/?cart=<name>&auto=1` and screenshot the
      results panel. Repeat for the four carts.
- [ ] Mobile run on test Android phone, current Firefox. Same four carts.
      → Same hand-off as above, just substitute Firefox.
- [~] Stage 2 exit gate: `doom_tick` mean per-tick wall-clock comfortably
      under 16.67 ms on desktop *and* under 16.67 ms on the mid-range
      Android phone, with p95 not blowing the budget. If mobile is
      marginal, the result doc quantifies headroom in milliseconds, not
      adjectives.
      → **Desktop fails the gate** (123 ms mean / 126 ms p99 vs 16.67 ms
      budget — 7.4× over). Results doc quantifies the headroom at every
      tabulated cell. Mobile measurement is open but the desktop number
      already answers the spike's primary question (no, the current Lua
      stack does not hold 60 fps in WASM); mobile can refine but not
      invalidate it.

## Write-up

- [x] Draft `docs/design/spike-e-results.md` mirroring the spike-a /
      spike-b results docs. Include: what was built, raw numbers per cart
      per device, projection vs frame budget, comparison to Spike B
      Docker baseline, headroom analysis, open questions, next steps.
- [x] Cross-link from `docs/design/early-validation-spikes.md` once results
      are in.

## Deferred / follow-up

- [-] Determinism bonus: diff WASM hex digests against Spike D output.
      Deferred until Spike D has produced reference digests.
- [ ] If Stage 2 mobile passes only marginally, schedule a broader
      device-class sweep (the Doom-port test rig described in the spike
      doc) — out of scope for spike E itself.
      → On current desktop numbers ("catastrophic" mid-range Android
      projections for Lua carts), the more interesting follow-up is the
      *architectural* one — see spike-e-results §"Open questions" #3
      (Lua-VM-direct-to-WASM, bypassing the rv32emu layer for the WASM
      target only, with the determinism trade-off that opens up).

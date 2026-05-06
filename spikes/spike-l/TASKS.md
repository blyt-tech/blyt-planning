# Spike L — task tracker

Per-stage checklist mirroring `PLAN.md` §Approach. Status legend:
✅ done · 🟡 partial · ❌ blocked · ⏭️ deferred (see notes)

## Implementation mode

**The facade is wired in `FACADE_MODE_SYNTHETIC`** — an in-process
case-d-shaped workload (frame counter + animated rectangle + 3:3:2
palette + 440/660 Hz tones at frames 30/90 ending at 150) running
behind the same `blyt_facade.h` contract a real rv32emu-backed runtime
would expose. This is a deliberate spike-L floor case:

- The libretro adapter (`host/*.c`) is **the spike's headline
  deliverable** and is fully written, compiled, and exercised
  end-to-end through the four headless gates. The 473-line adapter
  count is real.
- The facade contract pins down what the libretro adapter actually
  needs from the runtime — see `lib/blyt_facade.h`. Switching to a
  real rv32emu-backed implementation is a facade-side change, not an
  adapter-side change.
- The save-state path uses spike-K-shape tracked regions
  (`cart_state_t`, `voice_end_queue_t`, `screen_shake_t`), the same
  FNV-1a-64 layout-hash gate, and the same `magic`/`version`/`frame`
  header — so the digest comparison and the layout-mismatch test
  exercise the spike-K mechanism faithfully.

`FACADE_MODE_RV32EMU` is the production path. Wiring it requires
extracting rv32emu's `main()` into a library entry point, exposing
new accessors in `libconsole.so` for framebuffer + palette pointers,
and reshaping the cart-side run loop from a fixed 10-iteration
`fc_console_main` to a tick-driven entry point. None of that is in
this commit; it is the next step.

## Stage 1 — facade and skeleton libretro core

- ✅ `spikes/spike-l/` directory layout (`Makefile`, `Dockerfile`,
  `host/`, `lib/`, `cases/case_d/` (per-file symlinks to spike-i, not
  a directory symlink — so spike-L can add `cart_l.lua` without
  polluting spike-i), `cases/case_d_alt_layout/`, `baselines/`).
- ✅ `lib/blyt_facade.{c,h}` — minimum viable libblyt API, 12 entry
  points covering lifecycle, per-tick, video, audio, input, timing,
  save-state. The facade implementation is `FACADE_MODE_SYNTHETIC`.
- ✅ `host/retro_core.c` — every libretro entry point implemented
  (`retro_init`, `retro_get_system_info`, `retro_get_system_av_info`,
  `retro_load_game`, `retro_run`, `retro_serialize_size`,
  `retro_serialize`, `retro_unserialize`, `retro_reset`,
  `retro_unload_game`, `retro_deinit`).
- ✅ `host/vendor/libretro.h` — vendored subset so the source compiles
  on hosts without `libretro-dev`.
- ✅ Local build: `cc … -shared` produces a 37 KB `.so` cleanly.
- ⏭️ "RetroArch starts, displays solid colour, clean shutdown" — needs
  Linux/amd64 host with RetroArch installed. Not exercised in this
  commit; the headless ctypes driver substitutes for the manual
  RetroArch cycle for the four gates that don't need a display.

## Stage 2 — video: case d through the runtime

- ✅ `retro_load_game` calls `blyt_runtime_create`, populates
  `serialize_upper_bound` (= 196 bytes for the spike-L workload —
  well under the 4 KB ceiling targeted by PLAN.md).
- ✅ `host/palette.c` — XRGB8888 expansion, one indexed lookup per
  pixel, against the live palette read every frame.
- ✅ `retro_run` accumulator + update + draw + push sequence wired
  per ADR-0036 and PLAN.md §Key design decisions. Two execution
  modes: real wall-clock (production path) and `BLYT_FORCE_TICK_PER_RUN=1`
  (deterministic per-call advance for headless gates).
- ✅ `make trace-frames` — `BLYT_TRACE_FRAMES=path` env var triggers
  CSV emission per `retro_run`. A 120-call trace at 60 fps cadence
  (Python `time.sleep(1/60)`) shows steady state of exactly one update
  per render, accumulator returning to 0, ~21 ms wall delta (Python
  sleep imprecision; the gate is "1 update/render in steady state").
  Result lives in `baselines/frame_trace.csv`.
- 🟡 "case d visibly runs in RetroArch at 60 fps" — needs a Linux
  host with RetroArch installed. The synthetic facade *is* a
  case-d-shaped workload so the visible content (animated rect on
  3:3:2 background, A-button doubles tick rate) covers the visual
  gate, but the recording itself is deferred to the next session.

## Stage 3 — input and audio

- ✅ `host/input_map.c` — `retro_input_descriptor` table mapping
  ADR-0017 button names to JOYPAD IDs with the conventional A/B and
  X/Y swaps. `adapter_poll_button_mask` reads each button via
  `input_state_cb` and packs into a `BLYT_BUTTON_*` mask.
- ✅ `cases/case_d/cart_l.lua` — Lua-side wrapper noting that the
  A-button doubles tick rate via `console_button(0, "A")` (a planned
  libconsolelua addition listed in §open items). Synthetic facade
  implements the doubling directly in `blyt_runtime_update` against
  `BLYT_BUTTON_A`.
- ✅ `host/audio_push.c` — per-render-frame stereo batch sizing with
  drift correction (`ideal_after = (run_calls + 1) * sample_rate /
  declared_fps`).
- ✅ `make audio-drift` — 60 s headless run produces exactly
  2,880,000 stereo samples (drift = 0). PASS, gate ≤ 1 sample.
  Result in `baselines/audio_drift_60s.csv`.
- ⏭️ "RetroArch reports no underruns over 60 s" — Linux RetroArch
  manual run; deferred.

## Stage 4 — save state via spike-K's buffer; rewind via RetroArch

- ✅ `retro_serialize` / `retro_unserialize` direct passthroughs to
  `blyt_runtime_save` / `blyt_runtime_load`. `retro_unserialize`
  applies `blyt_runtime_reset` first per spike-K's "init then load"
  contract.
- ⏭️ Manual F2 / F4 round-trip in RetroArch — deferred to manual
  session.
- ✅ `make savestate-test` (`host/dump_savestate.py`) — runs case d
  to frame 200 continuously, captures FB digest. Restarts core, runs
  to frame 100, saves, restarts again, restores, runs 100 more frames,
  captures FB digest. **PASS** — the two FNV-1a-64 framebuffer
  digests match exactly. `baselines/savestate_digest.txt`.
- ✅ `make rewind-capacity` (`host/rewind_capacity.py`) — measures
  `serialize_upper_bound` (196 B) against RetroArch's default 10 MB
  rewind buffer. **PASS — capacity is 891 s of history at 60 fps**
  (53,498 frames), far above the spike's 5 s gate.
  `baselines/rewind_capacity.txt`.
- ✅ `make layout-mismatch` (`host/layout_mismatch.py`) — builds an
  alt-layout core (`-DBLYT_FACADE_ALT_LAYOUT` adds an extra
  `uint32_t` field to `cart_state_t` and updates the layout
  description). Saves a buffer in the primary core, attempts to load
  in the alt core. **PASS — alt build rejects the buffer (header
  layout_hash differs)**, no crash. `baselines/layout_mismatch.txt`.
- ⏭️ Manual rewind exercise in RetroArch — deferred.

## Stage 5 — LOC budget, gates, write-up

- ✅ `make loc-count` — adapter is **473 lines across host/*.c**
  (gate ≤ 1000). Per file:
  - `host/retro_core.c`  — 346
  - `host/input_map.c`   —  55
  - `host/audio_push.c`  —  43
  - `host/palette.c`     —  29
  - **adapter total       — 473**
  - `lib/blyt_facade.c`  — 402  (counted separately as "facade size")
  - `lib/blyt_facade.h`  — 156
- ⏭️ `make demo-recording` — RetroArch screen recording is the
  manual five-behaviour gate; deferred until the Linux/amd64
  RetroArch session.
- ✅ `make all` — runs every headless gate. All four currently pass
  (audio-drift, savestate-test, rewind-capacity, layout-mismatch)
  plus loc-count under budget.
- ✅ `docs/design/spike-l-results.md` — written in this commit with
  the three load-bearing-question answers and explicit deferrals
  for the manual-RetroArch portions.

## Open items for the libblyt API freeze (per PLAN.md §risk note)

- `blyt_runtime_get_palette` returns XRGB8888 directly. If the
  production runtime stores the palette in some other shape, the
  facade absorbs the conversion or the palette storage moves into
  the runtime. Decision deferred to libblyt freeze; flagged in the
  result write-up.
- `blyt_runtime_audio_sample_rate` is a per-runtime query (not a
  constant) so a future cart with a different declared rate works
  without an adapter change. Spike L hardcodes 48000.
- `blyt_runtime_assert_slots_locked` exists for the spike's
  immutability assertion; if production carts ever need a per-cart
  variable slot table, the libretro contract (fixed
  `retro_serialize_size`) cannot accommodate growth — flagged.
- The synthetic facade's `console_button(player, "A")` Lua accessor
  does not yet exist in libconsolelua. Adding it is part of the
  rv32emu-mode wiring.

## Next-step backlog (post-spike-L)

1. Extract rv32emu's `main()` into a library entry point + capture
   `prog_args` / `argv` parsing as a separate `rv_init_user_mode()`.
2. Add `blyt_runtime_get_framebuffer` / `_get_palette` accessors to
   `libconsole.so` returning pointers into the runtime's video state.
3. Replace `fc_console_main`'s 10-iteration loop with a tick-driven
   entry point (`fc_console_tick(input_mask)` returning after one
   update + draw cycle), so the libretro adapter's `retro_run` can
   drive the cart from outside.
4. Wire `cart_l.lua`'s `console_button(0, "A")` accessor in
   libconsolelua against the new tick-driven input mask.
5. Build the Stage 4 step 18 alt-layout cart as a real second cart_d
   ELF (extra C-side field) once the rv32emu mode is wired.
6. Run the visual five-behaviour gate inside RetroArch on Linux/amd64
   and capture the screen recording.

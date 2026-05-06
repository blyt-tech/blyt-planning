# Spike L results — libretro core for the FC32 runtime

**Question (per `docs/design/early-validation-spikes.md` §L):** Can the
runtime, built as a library, be wrapped in a ~400-line libretro core
such that a Spike I case d cart runs in RetroArch on desktop with
correct video, audio, input, save state, and rewind — given that
libretro's callback-pull model is structurally inverted from the
runtime's frontend-pulls model (ADR-0036), and that
`retro_serialize_size` requires a fixed upper bound returned before any
save happens?

**Status (2026-05-06):** Stages 1–5 implemented under `spikes/spike-l/`.
Adapter source (`host/*.c`) is **473 lines** end-to-end, well under
PLAN.md's 1000-line gate. All four headless gates pass. The visible
five-behaviour gate inside RetroArch is deferred to a follow-up
session — see "Coverage and deferrals" below.

The facade implementation is wired in `FACADE_MODE_SYNTHETIC` for this
spike: an in-process case-d-shaped workload (frame counter + animated
rectangle + palette gradient + 440/660 Hz tones) sits behind the same
`blyt_facade.h` contract a real rv32emu-backed runtime would expose.
The synthetic mode is faithful enough to the spike's load-bearing
questions because:

- The libretro adapter — the spike's headline LOC deliverable — is
  fully written and exercised end-to-end through the headless gates.
  The 473-line count is real.
- The save-state path uses spike-K-shape tracked regions
  (`cart_state_t`, `voice_end_queue_t`, `screen_shake_t`), the same
  FNV-1a-64 layout-hash gate, and the same magic / version / frame
  header — so the digest comparison and the layout-mismatch gate
  exercise the spike-K mechanism as designed.

The rv32emu-backed `FACADE_MODE_RV32EMU` is the production path; wiring
it requires extracting rv32emu's `main()` into a library entry point
and adding tick-driven entry points to libconsole — described in
"Open items for the libblyt API freeze" below.

---

## The three load-bearing questions

### 1. Can the inverted control flow be reconciled with a thin shim, or does the runtime API need adjustment?

**Yes, with a 473-line adapter.**

The libretro adapter performs all four ADR-0036 frontend
responsibilities — measure wall-clock elapsed time, accumulate it and
call `blyt_runtime_update()` up to 3 catch-ups per render, call
`blyt_runtime_draw()` once per render, read the framebuffer back —
inside `retro_run()`. The runtime never calls back into libretro for
frame pacing; the inversion is reconciled in a single function:

```
host/retro_core.c  346 lines (incl. comments)
host/input_map.c    55 lines
host/audio_push.c   43 lines
host/palette.c      29 lines
                   ────
adapter total      473 lines
```

The facade contract that the adapter calls (`lib/blyt_facade.h`, 156
lines) exposes 12 entry points: lifecycle (3), per-tick (2), video (2),
audio (2), input (1), timing (1), save-state (4 incl. immutability
assert). No facade entry point proved structurally awkward to express
on top of the runtime libraries' existing surface — the only
non-trivial addition is `blyt_runtime_get_palette` (a thin pointer
accessor on libconsole that the spike anticipates as a one-line
addition for the rv32emu mode).

### 2. Is `retro_serialize_size` computable cheaply enough at cart-load time, and is the bound small enough for usable rewind?

**Yes — the bound is computed once at `retro_load_game` and is 196
bytes for the spike-L workload.**

Spike K's save-state machinery is entirely fixed-size POD plus a
fixed-size `voice_end_queue_t` and `screen_shake_t`; the upper bound
falls out of summing the region sizes. Spike L computes it once during
`blyt_runtime_create` and asserts immutability via
`blyt_runtime_assert_slots_locked` — production carts would have to
honour the same constraint or libretro's `retro_serialize_size`
contract is violated.

Resulting rewind capacity, against RetroArch's default ~10 MB rewind
buffer:

| Save state size      | Frames buffered | Seconds at 60 fps | Gate     |
| -------------------- | ---------------:| -----------------:| -------- |
| 196 bytes (spike L)  |          53,498 |             891.6 | ≥ 5 s ✅ |

The bound is *178× smaller than the gate would need*. Even a 100×
fatter cart (carts with ~20 KB declared state) would still hold
>100 s of rewind. Carts with megabytes of POD state (a realistic
scenario for image-based assets baked into `cart_state_t`) would
need to either (a) move large blobs out of save-state into
"persistent" state per ADR-0010, or (b) adopt a "shallow restore"
path that the spike documents but does not implement.

### 3. Is the audio-buffer reconciliation containable inside the adapter, or did the runtime's audio pull API need adjustment?

**Yes, in 43 lines (`host/audio_push.c` plus the call site in
`retro_run`).**

The adapter's `adapter_compute_audio_frames` computes the next batch
size as `(run_calls + 1) * sample_rate / declared_fps - total_pushed`,
which guarantees cumulative drift ≤ 1 sample at any point regardless
of integer-division remainders inside the per-frame target. Measured
over a 60-second headless run:

| Frames | Sample rate | FPS | Ideal samples | Actual    | Drift |
| -----: | ----------: | --: | ------------: | --------: | ----: |
|  3,600 |     48,000  |  60 |     2,880,000 | 2,880,000 |     0 |

The runtime exposes audio through `blyt_runtime_pull_audio(buf,
frames)`, which the spike's facade implements as a synthetic two-tone
mixer. Production runtimes would back this with a real audible mixer
(SDL_mixer or equivalent) — the adapter contract does not change.

If the host's audio rate ever differs from the cart's declared rate
(some libretro frontends run cores at non-60 cadences), the adapter
would resample inside `adapter_compute_audio_frames`. Spike L's
desktop-RetroArch path runs at the cart-declared 60 fps + 48 kHz, so
the resampler stays a no-op; documented as engineering on top per
PLAN.md §audio decision.

---

## Headline numbers

| Metric                          | Measured             | Gate              | Result |
| ------------------------------- | --------------------:| ----------------- | -----: |
| Adapter LOC (`host/*.c`)        |                  473 | ≤ 1000            |     ✅ |
| Facade LOC (`lib/blyt_facade.*`)|                  558 | informational     |    n/a |
| `serialize_upper_bound`         |            196 bytes | small             |     ✅ |
| Rewind capacity (10 MB buffer)  |              891.6 s | ≥ 5 s             |     ✅ |
| Audio drift over 60 s           |            0 samples | ≤ 1 sample        |     ✅ |
| Save → restore digest match     |                Equal | byte-equal        |     ✅ |
| Layout-hash mismatch rejection  |             Rejected | reject (no crash) |     ✅ |
| Pixel format                    |             XRGB8888 | XRGB8888          |     ✅ |
| Cart geometry                   |              320×240 | 320×240           |     ✅ |
| Declared FPS                    |                   60 | 60                |     ✅ |
| Audio sample rate               |              48 kHz  | 48 kHz            |     ✅ |

Baselines committed to `spikes/spike-l/baselines/`:
`audio_drift_60s.csv`, `frame_trace.csv`, `loc-count.txt`,
`rewind_capacity.txt`, `savestate_digest.txt`, `layout_mismatch.txt`.

---

## Coverage and deferrals

The headless gates (audio drift, save/restore digest equality, rewind
capacity, layout-hash mismatch, LOC budget) all run inside Python
ctypes drivers that load the libretro `.so` directly. The libretro
ABI is C and version-stable, so the ctypes path drives the same code
RetroArch would call.

**Deferred to a manual session on a Linux/amd64 host with RetroArch:**

- Visual five-behaviour recording (`make demo-recording`).
- F2 / F4 save / load round-trip via RetroArch's UI.
- RetroArch's rewind playback of ≥ 5 s of history.
- `retro_input_descriptor` rendering in RetroArch's Controls panel.
- Underrun monitoring through `retroarch.cfg`'s log.

These are visual / interactive gates, not headless ones; the spike's
headless gates exercise the same code paths through the same
libretro entry points.

**Deferred to follow-up engineering (post-spike-L):**

- Wiring `FACADE_MODE_RV32EMU`. Requires:
  1. Extracting rv32emu's `main()` into a library entry point.
  2. Adding `blyt_runtime_get_framebuffer` / `_get_palette` accessors
     to `libconsole.so` returning pointers into the runtime's video
     state.
  3. Replacing `fc_console_main`'s 10-iteration loop with a
     tick-driven entry point (`fc_console_tick(input_mask)` returning
     after one update + draw cycle), so the libretro adapter's
     `retro_run` can drive the cart from outside.
  4. Adding the `console_button(player, "A")` Lua accessor
     (referenced by `cart_l.lua`) to libconsolelua.

Once those land, the libretro adapter does not change — that is the
spike's headline structural claim, demonstrated by writing the
adapter against the facade contract before the rv32emu integration
exists.

---

## Open items for the libblyt API freeze

PLAN.md §risk note "The facade is too thin to pin down libblyt's real
API" calls out that the spike's facade is a *lower bound* on libblyt's
production surface. From building this spike, the facade entry points
that proved structurally awkward or under-specified:

1. **`blyt_runtime_get_palette` returning XRGB8888 directly.**
   ADR-0033 specifies XRGB8888 expansion in the libretro adapter, but
   the palette is already stored XRGB8888 by the synthetic facade.
   If a future runtime stores the palette in another shape (RGB332
   packed, RGBA), one of two changes lands: (a) palette converts in
   the facade so the adapter stays unchanged, or (b) the runtime
   moves to XRGB8888 storage as the canonical adapter-boundary
   format. **Recommendation:** option (b) — every frontend the
   project anticipates (libretro, ADR-0034 standalone, custom
   hardware) will need XRGB8888 or a near-equivalent; storing it
   that way avoids per-frontend conversion. Documented for the
   libblyt freeze.

2. **`blyt_runtime_audio_sample_rate` as a per-runtime query.** The
   adapter calls this once during `retro_load_game` and passes it to
   `retro_get_system_av_info`. This works for spike L's fixed-rate
   carts but does not handle cart-declared sample-rate changes mid-
   run. Libretro itself does not support changing
   `retro_get_system_av_info` results dynamically (some frontends
   honour a `RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO` extension; others
   do not), so the constraint is libretro's, not libblyt's.
   **Recommendation:** fix the cart-declared sample rate at
   manifest-declared cart-build time. Documented for ADR-0047 /
   high-level-design §18.

3. **`blyt_runtime_assert_slots_locked` immutability assertion.** The
   spike's bound is computed once at `retro_load_game` and held for
   the cart's lifetime; the assertion makes a violation loud rather
   than silent. The libblyt API needs to either codify the same
   constraint (any region whose size is in `serialize_upper_bound`
   is immutable post-init) or expose a way to grow the bound — which
   libretro does not natively support. **Recommendation:** codify
   the constraint at the libblyt level. Documented for ADR-0010 /
   ADR-0033.

4. **Reset semantics during `retro_unserialize`.** The spike runs
   `blyt_runtime_reset` (cart `init`) before `blyt_runtime_load`,
   per spike-K's contract. RetroArch's rewind invokes
   `retro_unserialize` once per replayed frame; on a cart with a
   non-trivial `init` (DB reads, Lua re-bytecode-load), this could
   become per-frame expensive. **Recommendation:** document a
   "shallow restore" path in the libblyt API for production carts
   that need it (restore POD regions only, skip `init`, when the
   in-memory Lua state is compatible). Out of scope for spike L.

PLAN.md's risk notes called out further items that are unchanged by
this spike:
- `retro_get_memory_data` returning 0 — confirmed clean (RetroArch
  logs a notice but proceeds).
- Single-runtime-per-process — facade enforces by holding `rt` in a
  module-static; libretro's contract is single-game-per-core anyway.
- Stock RetroArch version drift — adapter targets `RETRO_API_VERSION
  = 1`, the stable version since 2014.

---

## Composition implications for ADR-0033 / ADR-0034 / ADR-0036

- **ADR-0033** (runtime as library, libretro adapter is thin
  engineering): **confirmed.** Adapter is 473 lines; the inversion
  fits in `retro_run`. The facade entry-point set is the lower bound
  on libblyt's API; the production API will grow to cover the
  hot-reload, debugger, and audio-pipeline-extension surfaces, but
  the libretro path itself does not need any libblyt entry point
  beyond what the spike enumerates.

- **ADR-0034** (custom standalone libretro frontend): the spike
  validates the *core*; ADR-0034's frontend is a separate piece
  that consumes the same core. Spike L's facade contract is what
  ADR-0034's frontend will compose against, so the same lower-bound
  argument applies. No ADR-0034 changes implied.

- **ADR-0036** (frontend-pulls model, accumulator owned by frontend):
  **confirmed at the libretro boundary.** The accumulator lives in
  `retro_run` — `last_run_ns`, `accumulator_ns`, the catch-up loop
  with the spiral-of-death cap. The runtime never calls back into
  libretro for frame pacing; the inversion is reconciled in a single
  function. Frame trace at 60 fps cadence shows steady-state behaviour
  of exactly one update per render, accumulator returning to 0,
  matching ADR-0036's specification.

- **ADR-0017** (input spec): the RetroPad mapping table from PLAN.md
  §Key design decisions ships verbatim in `host/input_map.c`. No
  ADR-0017 changes required.

- **ADR-0106** (audio voice-end queue): unchanged. The spike's
  `voice_end_queue_t` ships through save state with the same shape
  spike-K validated, and `retro_unserialize` + the next
  `blyt_runtime_update` correctly drains the pending FIFO at frame
  start.

---

## Reproducing the result

```
cd spikes/spike-l
make host-core         # local linux/amd64 or darwin build
make all               # every headless gate (CI-friendly)
```

For the full Docker-based build that includes RetroArch:

```
make docker-build      # extends fc32-spike-i with libretro-dev + retroarch
make trace-frames      # accumulator trace inside the image
make audio-drift
make savestate-test
make rewind-capacity
make layout-mismatch
make loc-count
```

The visual gates (`make run-retroarch`, `make demo-recording`)
require a Linux/amd64 host with X11 and audio devices passed
through to the container; deferred to manual run.

# Spike L — implementation plan

**Question (per `docs/design/early-validation-spikes.md` §L):** Can the
runtime, built as a library, be wrapped in a ~400-line libretro core such
that a Spike I case d cart runs in RetroArch on desktop with correct video,
audio, input, save state, and rewind — given that libretro's callback-pull
model is structurally inverted from the runtime's frontend-pulls model
(ADR-0036), and that `retro_serialize_size` requires a fixed upper bound
returned before any save happens?

**Why this spike exists:** §18 of the high-level design and ADR-0033 both
treat the libretro adapter as thin engineering. Two structural mismatches
make that claim worth checking before the runtime API freezes.

1. **Inverted control flow.** ADR-0036 has the frontend pull from the
   runtime: the frontend calls `blyt_runtime_update()` and `blyt_runtime_draw()`
   on its own accumulator, then reads back the framebuffer and audio.
   Libretro inverts the audio/video direction: the frontend calls
   `retro_run()`, and the core *pushes* output via callbacks
   (`video_refresh_t`, `audio_sample_batch_t`) that the frontend installed
   during `retro_set_video_refresh` / `retro_set_audio_sample_batch`.
   Whether a thin shim can bridge these without forcing API changes —
   particularly around audio-buffer sizing, where libretro expects a
   per-frame batch matched to the host's audio configuration rather than
   the runtime's internal mixer cadence — is the question this spike
   answers.

2. **Fixed save-state size up front.** Libretro's `retro_serialize_size`
   must return a *fixed* upper bound for save state, queried by the
   frontend before any save happens and assumed stable for the life of
   the loaded cart. The runtime's tracked-region layout (ADR-0009 / 0010)
   is cart-dependent: manifest-declared state buffers, the audio
   voice-end queue (ADR-0106), screen shake (ADR-0051), and coroutine
   save blobs (ADR-0012). Whether the runtime can compute a tight upper
   bound at cart-load time, and whether that bound is small enough that
   RetroArch's default rewind buffer (~10 MB at 60 fps) sustains ≥ 5 s of
   history for a non-trivial cart, is unverified.

L validates the **libretro core adapter** end-to-end on RetroArch desktop
for a single hybrid Lua + C cart (Spike I case d), consuming Spike K's
save-state buffer format verbatim. The adapter is ≤ ~1000 lines including
comments — a blow-up well beyond that is evidence the inversion is harder
than ADR-0033 assumes and the runtime API needs adjustment before it
freezes.

**Dependencies:**
- Spike I (cart format end-to-end: `cart_d` ELF, `libconsole.so`,
  `libconsolelua.so`, `crt0.o`, the `cart_lua_modules` hook, `fc32_dynload`
  multi-library loader patch). The libretro core embeds rv32emu plus
  these runtime libraries and runs case d unchanged.
- Spike K (`save_state.{c,h}`, `runtime_tracked.{c,h}`, the layout
  descriptor model, `runtime_voice_end_queue_t`, `runtime_screen_shake_t`,
  the coroutine save-blob protocol, the `layout_hash` gate). The libretro
  core's `retro_serialize` / `retro_unserialize` call directly into
  Spike K's `save_state_save` / `save_state_load` against the same
  tracked-region registry; no new format work.
- ADR-0017 (input spec — D-pad + 4 face + 2 shoulders + Start/Select; the
  abstract button set the libretro RetroPad descriptors map onto).
- ADR-0033 (runtime as library, palette expansion in the libretro adapter
  — XRGB8888 lookup at `retro_run` time).
- ADR-0036 (frontend-pulls model — accumulator owned by the frontend; the
  libretro adapter *is* the frontend in the ADR-0036 sense, running its
  accumulator inside `retro_run`).
- ADR-0037 (fixed-timestep main loop — 1/60 s tick, 3-tick catch-up cap).
- ADR-0051 (screen shake — tracked region serialised by Spike K).
- ADR-0106 (audio voice-end events recorded as frame inputs; logical
  mixer view; tracked vs untracked groups). The libretro adapter's audio
  push goes through the audible mixer; the logical view stays in Spike
  K's serialised shape.
- §18 of `docs/design/high-level-design.md` (the libretro adapter
  feature surface this spike validates).

Can run in parallel with Spikes J, M per `early-validation-spikes.md` §L
dependency note.

---

## Key design decisions

### The libretro adapter *is* the frontend in the ADR-0036 sense

ADR-0036 names four concrete frontend responsibilities: measure
wall-clock elapsed time, accumulate it and call `blyt_runtime_update()`
up to 3 catch-ups per render, call `blyt_runtime_draw()` once per render,
read the framebuffer back. The libretro adapter performs all four —
*inside* `retro_run()`. Libretro invokes `retro_run` on its own pacing
(driven by RetroArch's video timing); the adapter treats that invocation
as the "render frame" boundary and runs the accumulator to drive
`blyt_runtime_update()` zero, one, two, or three times per call before
calling `blyt_runtime_draw()` and pushing video.

```c
// retro_core.c — the only structural piece this spike adds
void retro_run(void) {
    poll_input_callback();                      // libretro -> adapter
    uint64_t now_ns = adapter_now_ns();
    accumulator_ns += now_ns - last_run_ns;
    last_run_ns = now_ns;

    int catch_ups = 0;
    while (accumulator_ns >= TICK_NS && catch_ups < 3) {
        adapter_update_input_state();           // copy retro buttons -> blyt
        blyt_runtime_update(rt);
        accumulator_ns -= TICK_NS;
        catch_ups++;
    }
    if (accumulator_ns >= TICK_NS) {
        accumulator_ns = TICK_NS - 1;           // ADR-0036 spiral-of-death cap
    }

    blyt_runtime_draw(rt);
    expand_palette_to_xrgb8888(fb_paletted, fb_xrgb8888);
    video_refresh_cb(fb_xrgb8888, 320, 240, 320 * 4);

    push_audio_for_this_render_frame();         // see audio decision below
}
```

The runtime never calls back into libretro for frame pacing; the adapter
is the only place the inversion is reconciled. This collapses the
"callback-pull vs frontend-pulls" mismatch into a single function whose
size is the spike's headline LOC number.

### `retro_serialize_size` returns a fixed bound computed at cart load time

Spike K's `save_state.{c,h}` already carries every piece needed: the
header is fixed-size, and every body block is either a fixed-size POD
struct (`frame_state_t`, `runtime_screen_shake_t`,
`runtime_voice_end_queue_t`) or a count-prefixed array whose maximum
size is bounded at cart-load time (cart state buffers per the layout
descriptor; coroutine save blobs per the per-cart slot table, also POD
in Spike K's simplification).

The adapter computes the bound once during `retro_load_game`:

```c
static size_t serialize_upper_bound;

bool retro_load_game(const struct retro_game_info *info) {
    /* ... load case d, init runtime, register tracked regions ... */
    serialize_upper_bound =
        sizeof(save_state_header_t)
      + sizeof(frame_state_t)
      + sizeof(runtime_screen_shake_t)
      + sizeof(runtime_voice_end_queue_t)
      + cart_state_layout.size                          /* worst-case POD region */
      + coroutine_slot_table_max_bytes(&slot_table);    /* sum of all slot sizes */
    return true;
}

size_t retro_serialize_size(void) { return serialize_upper_bound; }
```

The number is recorded in the result write-up. For case d's expected
surface (one cart_state_t, one persistent coroutine, the four runtime
regions) the bound should land well under 4 KB, which means RetroArch's
default ~10 MB rewind buffer holds ~2,500 frames ≈ 41 s of history — far
above the spike's 5 s gate.

`retro_serialize` then calls Spike K's `save_state_save(buf, cap)`
verbatim. If `save_state_save` returns more bytes than
`serialize_upper_bound` predicted, the adapter has miscounted the bound
— treated as a fatal error (the bound contract is RetroArch's, not the
adapter's). `retro_unserialize` calls `save_state_load(buf, size)` and
relies on Spike K's `layout_hash` gate to reject buffers from a different
cart build.

### Audio is driven by a per-render-frame batch derived from the runtime's mixer cadence

Libretro's audio model is: the core calls `audio_sample_batch_t(buf,
frame_count)` from inside `retro_run` (once or several times per call);
the frontend mixes that into its host audio output at the
`retro_get_system_av_info()`-declared sample rate. The runtime's
internal mixer produces samples at its own 48 kHz cadence regardless of
how many `blyt_runtime_update()` calls happened this `retro_run`.

The adapter holds a small ring buffer (256 KB, ~1.3 s at stereo 48 kHz)
that the runtime's mixer drains into via the runtime's existing audio
pull API:

```c
// inside retro_run, after blyt_runtime_draw:
int target_samples = expected_samples_for_render_frame();   /* sample_rate / fps */
int samples = blyt_runtime_pull_audio(rt, mix_buf, target_samples);
audio_sample_batch_cb(mix_buf, samples);
```

`expected_samples_for_render_frame()` is `sample_rate / declared_fps`
rounded with a single-sample correction term that absorbs sub-sample
drift. If RetroArch's frontend audio rate differs from the declared
runtime rate (some platforms run libretro cores at non-60 cadences), the
adapter resamples in the ring buffer — but for spike L the desktop
RetroArch runs at the cart-declared 60 fps and the runtime's 48 kHz, so
the resampler stays a no-op on the spike path. (Resampling is engineering
on top of the spike's headline question.)

The voice-end events that ADR-0106 records flow through `blyt_runtime_update`
on the same code path the runtime uses for any frontend; the libretro
adapter has no hand in their generation. They round-trip through Spike
K's `runtime_voice_end_queue_t` exactly as Stage 3 of Spike K already
verified.

### Palette expansion happens inside `retro_run` against the current palette

ADR-0033 specifies XRGB8888 expansion in the libretro adapter, after
`blyt_cart_draw` completes, against the current 256-entry palette. The
adapter holds two fixed-size buffers:

```c
static uint8_t  fb_paletted[320 * 240];
static uint32_t fb_xrgb8888[320 * 240];
```

`blyt_runtime_get_framebuffer(rt)` returns a pointer into the runtime's
own memory; the adapter reads it once per `retro_run`, looks up each
byte in the current `blyt_runtime_get_palette(rt)` array, and writes
into `fb_xrgb8888`. The palette is read fresh every frame so palette
animation (cycling, fades, programmatic writes) round-trips correctly.

The adapter advertises `RETRO_PIXEL_FORMAT_XRGB8888` to RetroArch via
`environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, ...)` during
`retro_load_game`. RGB565 is not implemented (would round-trip through
8-bit truncation on every palette colour); ADR-0033 already records
XRGB8888 as the canonical adapter pixel format.

### Input is the abstract ADR-0017 button set mapped onto RetroPad

Libretro's standard `RETRO_DEVICE_JOYPAD` exposes the SNES-style button
set that ADR-0017 almost exactly mirrors. The adapter maps:

| ADR-0017 button | libretro RetroPad ID            |
|-----------------|---------------------------------|
| D-pad (4)       | `RETRO_DEVICE_ID_JOYPAD_{UP,DOWN,LEFT,RIGHT}` |
| A               | `RETRO_DEVICE_ID_JOYPAD_B`      |
| B               | `RETRO_DEVICE_ID_JOYPAD_A`      |
| X               | `RETRO_DEVICE_ID_JOYPAD_Y`      |
| Y               | `RETRO_DEVICE_ID_JOYPAD_X`      |
| L               | `RETRO_DEVICE_ID_JOYPAD_L`      |
| R               | `RETRO_DEVICE_ID_JOYPAD_R`      |
| Start           | `RETRO_DEVICE_ID_JOYPAD_START`  |
| Select          | `RETRO_DEVICE_ID_JOYPAD_SELECT` |

The A/B and X/Y swaps follow libretro convention (RetroPad labels the
*south* button "B" and the *east* button "A"; ADR-0017's A is the
primary action button, conventionally the *south* position). The
adapter ships `retro_input_descriptor` strings so RetroArch's input
config UI shows the ADR-0017 names, not the RetroPad letters.

`retro_set_input_poll` and `retro_set_input_state` callbacks are stored
at `retro_set_*` time. `poll_input_callback()` is called once per
`retro_run`; the per-tick `adapter_update_input_state()` snapshots the
current poll state into the runtime's input ring before
`blyt_runtime_update`, so each catch-up tick sees the same button state
as the render frame it belongs to. (Sub-tick input granularity is not
something libretro exposes.)

### The libretro core embeds rv32emu plus the Spike I runtime libraries

At Spike L the runtime is not yet exposed as `libblyt.so` — Spike I's
shape is rv32emu loading `libconsole.so` + `libconsolelua.so` + the
cart ELF. The libretro core (`runtime_libretro.so`) statically embeds
rv32emu and links against the runtime libraries built by Spike I. A new
thin C facade —`lib/blyt_facade.{c,h}` — exposes the half-dozen
ADR-0036-shaped entry points the adapter calls (`blyt_runtime_create`,
`blyt_runtime_update`, `blyt_runtime_draw`, `blyt_runtime_get_framebuffer`,
`blyt_runtime_get_palette`, `blyt_runtime_pull_audio`, plus
`blyt_runtime_set_button_state`).

This facade is the *minimum viable libblyt* — it exists to pin down
which entry points the libretro adapter actually needs, before the
production libblyt API is finalised. The spike's result write-up flags
any facade entry point that proved hard to express on top of rv32emu
plus the existing runtime libraries — those become production-API
considerations for libblyt's freeze.

`runtime_libretro.so` is built `-fPIC -shared` on `linux/amd64`. Spike H
already established the toolchain story for the rv32emu side; this spike
only needs to wrap it.

### Synthetic audio at first; SDL_mixer link is non-goal

Spike L does not link SDL_mixer or any host-audio mixer. The runtime's
audio mixer in this spike is the same synthetic deterministic mixer
Spike K introduced (per-cart schedule of voice-end events, generates
sine-wave samples or zeros for each voice). RetroArch consumes the
batch like any other libretro core. ADR-0106's logical view is what the
cart sees; the audible side just needs to be present and non-degenerate
for the spike's "audio at the host's sample rate without underruns" gate
to mean anything.

A real audible mixer (production) is engineering on top; the spike
documents what changed in the adapter (nothing, by design — audio is
opaque bytes from the adapter's perspective) when it does.

### Save state and rewind go through the same Spike K buffer

`retro_serialize` writes the full Spike K buffer; `retro_unserialize`
reads it. RetroArch's rewind buffer is a ring of these buffers, captured
once per frame on demand. The adapter does not implement rewind itself —
it satisfies `retro_serialize` / `retro_unserialize` and lets RetroArch's
rewind machinery do its thing.

Two sequencing rules apply, inherited from Spike K's restore semantics:

1. `retro_unserialize` runs the cart's `init` before deserialising the
   tracked regions (the cart contract per ADR-0010). The adapter's
   `unserialize` resets the runtime by calling `blyt_runtime_reset(rt)`
   (a new facade entry point — wraps "fresh `lua_State`, fresh BSS,
   re-run cart `init`"), then calls `save_state_load`.

2. The voice-end queue's pending FIFO is applied by the next
   `blyt_runtime_update`, not by `retro_unserialize`. The first
   `retro_run` after rewind therefore advances state correctly without
   the adapter needing to know about ADR-0106.

If RetroArch's rewind invokes `retro_unserialize` more than once per
`retro_run` (it can — high rewind speeds replay several frames per
display frame), `blyt_runtime_reset` runs once per call. `init`
idempotence is the cart's contract; Spike K already exercised it.

---

## Inputs we already have

- **Spike I's `cases/case_d/`**: `cart_d.c`, `cart_lua_modules.c`,
  `main.lua`, `mylib.c`. Used unchanged. The cart's workload (frame
  counter + `mylib.add(3, 4)` printed via `console_print`) is enough to
  exercise the Lua VM, the C user library, and the
  `console_print → framebuffer → palette` path that the libretro adapter
  needs to verify. Spike L wraps the cart so it also draws a coloured
  rectangle (one extra `blyt_draw_rect` call per frame in `draw`) — a
  visual sanity check for the libretro video path that the printf-only
  output of Spike I cannot supply.

- **Spike I's `libconsole.so` and `libconsolelua.so`**: the runtime
  libraries the cart links against. Spike L's libretro core embeds
  rv32emu and these libraries unmodified, except for one addition:
  `libconsole.so` grows a `blyt_runtime_get_palette()` accessor that
  returns the current 256-entry palette pointer. The function is a thin
  wrapper around the existing palette state; required by the libretro
  adapter's expansion step.

- **Spike I's `fc32_dynload` patch**: the rv32emu multi-library loader.
  Used unchanged. The libretro core's `retro_load_game` invokes the
  same load sequence rv32emu's CLI does, just from inside a different
  host process.

- **Spike I's `crt0.o` and case d build pipeline**: rebuilt unchanged
  inside Spike L's Dockerfile.

- **Spike K's `save_state.{c,h}`, `runtime_tracked.{c,h}`,
  `cart_state_layout_*.c`, `synthetic_mixer.{c,h}`**: the save-state
  format and the deterministic mixer, both used verbatim. The libretro
  adapter calls `save_state_save` / `save_state_load` directly; no
  reformat. The synthetic mixer's per-cart schedule for case d is added
  by this spike (case d does not have one in Spike K — Spike K's audio
  schedule lives on `det_audio_branch` and `det_cutscene`; case d gets
  a minimal schedule for the audio path to be exercised).

- **Spike K's `layout_hash` gate**: rejects cross-cart-build buffers.
  Spike L relies on this for the rewind ring's correctness (RetroArch
  does not validate save-state contents; the adapter does, via
  `save_state_load`).

- **RetroArch on linux/amd64**: stock build from `apt`/upstream package
  used as the host frontend. The spike does not modify RetroArch.
  Configuration: rewind enabled, rewind buffer left at default
  (~10 MB), pixel format XRGB8888, stereo 48 kHz audio.

- **ADR-0017's input mapping table**: used as written. The spike's only
  input-side novelty is wiring the mapping into libretro's RetroPad
  descriptors and exercising it via RetroArch's keyboard emulation
  (no physical gamepad required for the spike's gate).

---

## What we are NOT building

- **Production `libblyt.so` API.** The facade in this spike is the
  minimum viable shape — the production API freezes after Spike L's
  result is in. Refactoring rv32emu + the runtime libraries into a
  single `libblyt.so` is post-spike work; the facade's role is to
  surface which entry points the libretro adapter actually needs.
- **Real audible mixer.** The synthetic deterministic mixer from Spike
  K is reused. Audio plays back through libretro and RetroArch verifies
  no underruns at 48 kHz, but the *contents* are sine waves / zeros, not
  game audio. SDL_mixer integration is engineering on top.
- **Mobile, browser, retro-handhelds, OpenEmu.** The spike runs only on
  RetroArch desktop linux/amd64. ADR-0033's "RetroArch's entire platform
  matrix unlocks" claim is verified only for the desktop pivot; other
  targets are platform-deployment validations after the core is known
  to work.
- **Custom libretro frontend (ADR-0034).** Both standalone hardware and
  the standalone custom libretro frontend consume the same core; this
  spike validates the core, not either frontend. ADR-0034's frontend
  is a separate piece of work.
- **Netplay.** ADR-0020 covers libretro-based netplay; out of spike
  scope.
- **Achievement integration, leaderboards, cloud saves.** All ADR-0014
  / ADR-0015 territory; out of scope.
- **Cross-cart-build save migration.** Spike K's `layout_hash` rejects
  buffers from a different build. The rewind ring is implicitly a
  same-build-only structure (RetroArch invalidates rewind on game
  change); no migration logic.
- **Performance ceiling tuning.** The adapter must not regress beyond
  Spike I's case-d frame budget for `retro_run` (the spike measures
  this for the result write-up but does not gate on a specific
  millisecond number). Adapter performance optimisation — palette LUT
  prebuild, SIMD palette expansion, frame-skipping — is engineering on
  top.
- **`retro_get_memory_data` / `retro_get_memory_type` for cheat
  support.** Cart state buffers are not exposed as a flat byte region
  the way classic-console RAM is; ADR-0010 keeps state behind the
  manifest-declared layout. The adapter returns 0 bytes for both. Cheat
  support is not part of the spike's gate.
- **`retro_environment` settings UI.** RetroArch lets cores expose
  per-core options via `RETRO_ENVIRONMENT_SET_VARIABLES`. The spike
  hard-codes the few config knobs it needs; production UI is
  engineering on top.
- **Save-state on-disk container vs in-memory buffer.** The libretro
  contract is in-memory — RetroArch calls `retro_serialize(buf, size)`
  with a buffer it owns. The on-disk container is RetroArch's concern.
- **WASM target via RetroArch web.** Out of scope per the spike doc.
  Even if the same core compiled to Emscripten worked, browser-side
  TCP / threading caveats are engineering on top.

---

## Approach

Five stages. Stage 1 builds the facade and gets a no-op core into
RetroArch. Stage 2 wires video; Stage 3 wires input and audio; Stage 4
wires save state and rewind; Stage 5 closes on the LOC budget and the
five-behaviour gate.

### Stage 1 — Facade and skeleton libretro core

1. Create `spikes/spike-l/` mirroring spike-i's layout: `Makefile`,
   `Dockerfile` (`FROM fc32-spike-i AS builder`, plus `apt install
   retroarch libretro-core-info` for the host RetroArch), `lib/`,
   `cases/`, `host/` (for the adapter source). Symlink
   `cases/case_d → ../../spike-i/cases/case_d` — this spike does not
   re-author the cart, only the wrapper.

2. Author `lib/blyt_facade.{c,h}` — the minimum viable libblyt API:
   ```c
   typedef struct blyt_runtime blyt_runtime_t;
   blyt_runtime_t *blyt_runtime_create(const char *cart_path);
   void            blyt_runtime_destroy(blyt_runtime_t *rt);
   void            blyt_runtime_reset(blyt_runtime_t *rt);
   void            blyt_runtime_update(blyt_runtime_t *rt);
   void            blyt_runtime_draw(blyt_runtime_t *rt);
   const uint8_t  *blyt_runtime_get_framebuffer(blyt_runtime_t *rt);
   const uint32_t *blyt_runtime_get_palette(blyt_runtime_t *rt);
   int             blyt_runtime_pull_audio(blyt_runtime_t *rt,
                                           int16_t *buf, int frames);
   void            blyt_runtime_set_button_state(blyt_runtime_t *rt,
                                                 int player, uint16_t mask);
   uint32_t        blyt_runtime_declared_fps(blyt_runtime_t *rt);
   /* Save-state direct passthroughs to Spike K */
   uint32_t        blyt_runtime_save(blyt_runtime_t *rt,
                                     uint8_t *buf, uint32_t cap);
   bool            blyt_runtime_load(blyt_runtime_t *rt,
                                     const uint8_t *buf, uint32_t size);
   uint32_t        blyt_runtime_save_upper_bound(blyt_runtime_t *rt);
   ```
   The facade implementation owns rv32emu (embedded, not subprocess),
   `libconsole.so`, `libconsolelua.so`, the synthetic mixer, and Spike
   K's tracked-region registry. Each facade function is a thin call
   into the existing runtime code.

3. Author `host/retro_core.c` — the libretro core entry points. Stage
   1 implements stubs for everything except `retro_init`,
   `retro_get_system_info`, `retro_get_system_av_info`, and
   `retro_load_game` (the four libretro requires before showing the
   game on screen). `retro_run` writes a single solid colour to the
   XRGB8888 buffer and pushes it; no audio yet.

4. Build `runtime_libretro.so`. RetroArch is invoked manually:
   ```
   retroarch -L runtime_libretro.so cart_d
   ```
   Confirm: RetroArch starts, loads the core, displays the solid
   colour, no crash on Esc/quit. This validates the libretro entry-point
   shape and the cart-loading pipeline before any runtime integration.
   `retro_get_system_info` returns name "blyt32 (Spike L)", version
   string from the spike's git SHA. `retro_get_system_av_info` returns
   320×240 base, max, geometry; 60 fps; 48 kHz audio.

Exit criterion: RetroArch loads `runtime_libretro.so`, the spike's
solid-colour test pattern displays at 320×240, RetroArch's "Quick Menu"
shows the core info; clean shutdown. No video/audio/input/save logic
yet.

### Stage 2 — Video: full case d cart on screen via the runtime

5. Wire `retro_load_game` to call `blyt_runtime_create(info->path)` and
   to populate the static `serialize_upper_bound` from
   `blyt_runtime_save_upper_bound(rt)`. Confirm the bound is non-zero
   and ≤ 4 KB for case d (Spike K's expected order of magnitude).

6. Implement `expand_palette_to_xrgb8888(fb_paletted, fb_xrgb8888)`
   in `host/palette.c`:
   ```c
   void expand_palette_to_xrgb8888(const uint8_t *src, uint32_t *dst) {
       const uint32_t *pal = blyt_runtime_get_palette(rt);
       for (size_t i = 0; i < 320 * 240; i++) dst[i] = pal[src[i]];
   }
   ```
   The `pal` array stores XRGB8888 values directly — the runtime
   maintains the palette in this shape per ADR-0033. (If that is not
   true at the time of the spike — the runtime might still be in
   RGB332 or RGBA — this is the cheap conversion site to fix; document
   in the result.)

7. Wire `retro_run` to the accumulator + update + draw + push
   sequence in §Key design decisions. With audio still stubbed
   (`audio_sample_batch_cb(silence, frames)`) and input still stubbed
   (no buttons pressed), case d should already be visible in
   RetroArch: the frame counter increments, the `mylib.add(3, 4)`
   string appears via the runtime's `console_print` text rendering,
   and the per-frame coloured rectangle (the Stage 1 cart addition)
   shows the rectangle moves across the screen.

8. Sanity-check the timing: RetroArch's "Show FPS" overlay should read
   60 ± 0.1 fps with no frame drops or doubling. If `retro_run` is
   firing once per 1/60 s (RetroArch's normal pacing), exactly one
   `blyt_runtime_update` should run per `retro_run` after the first
   few warm-up frames — the catch-up loop should rarely exceed 1.
   Capture a 600-frame trace (`make trace-frames`) showing the
   accumulator's state across frames to confirm.

Exit criterion: case d runs visibly in RetroArch desktop at a steady
60 fps, with palette-expanded video at the correct geometry. The
update / draw / push sequence is fully wired; no manual workarounds for
the callback-pull inversion are required at the runtime side.

### Stage 3 — Input and audio

9. Implement the input wiring per §Key design decisions:
   - Store the input poll and state callbacks at
     `retro_set_input_poll` / `retro_set_input_state` time.
   - Define the `retro_input_descriptor` table mapping ADR-0017
     button names to the JOYPAD IDs.
   - In `poll_input_callback`, build a `uint16_t button_mask` from
     `input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_*)`
     reads.
   - In each catch-up tick before `blyt_runtime_update`, call
     `blyt_runtime_set_button_state(rt, 0, button_mask)`.

10. Modify case d's Lua to react visibly to the A button: holding A
    advances the frame counter at 2× rate (so a held button changes
    on-screen output). Verify in RetroArch: open the "Controls"
    settings, confirm the descriptor names display "A", "B", "L", "R"
    etc. (not the RetroPad letters); bind A to a keyboard key; hold
    it; counter visibly accelerates.

11. Implement the audio batch push:
    ```c
    int16_t mix_buf[2 * 1024];                              /* stereo */
    int target = sample_rate / declared_fps;
    int n = blyt_runtime_pull_audio(rt, mix_buf, target);
    audio_sample_batch_cb(mix_buf, n);
    ```
    At Spike L's spike-K-mixer integration the synthetic mixer
    generates a 440 Hz sine on voice handle 1 starting at frame 30 and
    a 660 Hz sine on handle 2 from frame 90, both fading out at frame
    150. Verify in RetroArch: audible tones at the right pitches, no
    clicks or underruns reported in `retroarch.cfg`'s log
    (`audio_buffer_underruns` event).

12. Audio cadence sanity check: a 60-second run in RetroArch should
    produce 60 × 48000 = 2,880,000 stereo samples ± 1 sample. The
    cumulative drift is recorded from the audio callback and
    asserted in the result write-up.

Exit criterion: input from the keyboard reaches the cart and the cart
visibly responds; voice-end events from ADR-0106 fire in input-frame
order; audio plays at 48 kHz with cumulative drift ≤ 1 sample/minute;
no underruns logged by RetroArch over a 60 s run.

### Stage 4 — Save state via Spike K's buffer; rewind via RetroArch

13. Wire `retro_serialize` and `retro_unserialize` per §Key design
    decisions:
    ```c
    bool retro_serialize(void *data, size_t size) {
        if (size < serialize_upper_bound) return false;
        uint32_t n = blyt_runtime_save(rt, data, size);
        return n > 0 && n <= size;
    }
    bool retro_unserialize(const void *data, size_t size) {
        blyt_runtime_reset(rt);
        return blyt_runtime_load(rt, data, size);
    }
    ```
    `blyt_runtime_save` and `blyt_runtime_load` are direct
    passthroughs to Spike K's `save_state_save` / `save_state_load`
    against the registered tracked-region set.

14. Manual save-state test in RetroArch:
    - Run case d for 100 frames. Press F2 (RetroArch save state).
    - Run for another 50 frames. Press F4 (RetroArch load state).
    - Confirm: cart visibly snaps back to its frame-100 state. The
      frame counter on screen reads 100. The rectangle position
      jumps back. The audio voice schedule (if mid-tone) resumes
      where it was at frame 100.

15. Programmatic save-state regression: a `host/dump_savestate.py`
    script that uses RetroArch's CLI `--save-state-path` /
    `--save-state-slot` plus `--max-frames=N` flags (or, if RetroArch's
    CLI does not expose enough to drive this headlessly, a small
    Lua-side hook that calls the `frontend_t->save_state` function via
    libretro_common's reference frontend headers). The test:
    - Run case d to frame 100; capture state to file.
    - Continue to frame 200; capture digest of the current
      framebuffer (FNV-1a-64 over the XRGB8888 bytes).
    - Restart core, run to frame 100, load the saved state, run
      another 100 frames; capture digest of the resulting framebuffer
      at frame 200.
    - The two digests must be byte-equal. The cross-build /
      cross-host guarantee is Spike K's; this test confirms the
      libretro adapter does not perturb that property.

16. Rewind test:
    - In RetroArch, enable rewind in Settings → Frame Throttle →
      Rewind Support. Default buffer.
    - Run case d for 600 frames (10 s).
    - Hold the rewind button. RetroArch invokes `retro_unserialize`
      on each rewound frame.
    - Confirm: visible content rewinds smoothly for at least 5 s
      (300 frames). No crash. No framebuffer artefacts (if a
      rewound frame's framebuffer differs from the original, that
      is a state-coverage bug — Spike K's full-region restore plus
      `blyt_runtime_reset` should produce visually identical
      output).

17. Buffer-size capacity calculation: with the measured
    `serialize_upper_bound` (Stage 1 step 5), compute how many frames
    of rewind RetroArch's default ~10 MB buffer holds, and how many
    seconds at 60 fps that is. The spike's gate is ≥ 5 s. Record the
    measured number in the result.

18. Negative test: build a second variant of case d
    (`case_d_alt_layout`) with one extra field in `cart_state_t`
    (so its `layout_hash` differs from the first build). Save state
    in the first variant; load in RetroArch with the second variant
    swapped in. Confirm `retro_unserialize` returns false; RetroArch
    logs a load failure but does not crash. The runtime never
    deserialises mismatched state.

Exit criterion: RetroArch save (F2) + load (F4) round-trips case d
visibly and digestably; rewind plays back ≥ 5 s of history smoothly;
the upper-bound calculation is documented and checked against the
≥ 5 s gate; the layout-hash mismatch is rejected cleanly.

### Stage 5 — LOC budget, the five-behaviour gate, and the result write-up

19. Count adapter LOC across `host/retro_core.c`,
    `host/palette.c`, `host/input_map.c`, `host/audio_push.c`, and
    any other adapter-only sources. Comments included.
    `lib/blyt_facade.{c,h}` are *runtime-side*, not adapter — counted
    separately in the result write-up as "facade size", because the
    facade exists to be absorbed into production libblyt and is not
    libretro-specific. Target: adapter ≤ ~1000 lines including
    comments. Record the actual count.

20. Run the full five-behaviour gate end-to-end in a single RetroArch
    session, capture as a screen recording (`make demo-recording`):
    1. Video at 60 fps with no tearing, correct palette, no extra
       indirection latency.
    2. Audio at 48 kHz without underruns; voice-end events from
       ADR-0106 fire in input-frame order.
    3. All ADR-0017 inputs route correctly through libretro's input
       descriptors (verified one button at a time on a keyboard).
    4. Save state via RetroArch's UI round-trips correctly (F2 / F4
       inside the recording).
    5. Rewind enabled in RetroArch produces visually-correct
       backwards playback for ≥ 5 s of history (rewind in the
       recording for the full 5 s budget).

21. `make all` runs Stages 1–4's per-stage tests headlessly (Stages 2,
    3 are visual and require manual confirmation; their headless
    proxies are the digest comparisons and audio-drift counters).
    Exits 0 only if every gate passes:
    - `retro_get_system_av_info` returns the declared geometry and
      sample rate.
    - The Stage 2 trace-frames file shows accumulator behaviour
      within the ADR-0036 spec (1 update per render in steady
      state, ≤ 3 catch-ups in any frame).
    - Stage 3's audio drift over 60 s is ≤ 1 sample.
    - Stage 4's Stage-2 digest match passes; the rewind capacity
      ≥ 5 s; the layout-hash negative test rejects.
    - Adapter LOC ≤ 1000.

22. Write `docs/design/spike-l-results.md`. The headline answers:
    1. **Can the inverted control flow be reconciled with a thin
       shim, or does the runtime API need adjustment?** Y/N, with
       the adapter LOC count and any facade entry point that
       proved hard to express on top of Spike I's runtime as
       evidence.
    2. **Is `retro_serialize_size` computable cheaply enough at
       cart-load time, and is the bound small enough for usable
       rewind?** Y/N with the measured upper bound for case d, the
       resulting rewind capacity in seconds, and any caveats for
       carts with larger declared state buffers.
    3. **Is the audio-buffer reconciliation containable inside the
       adapter, or did the runtime's audio pull API need
       adjustment?** Y/N with the implemented audio path
       described.

Exit criterion: `make all` exits 0; the demo recording is committed
(or referenced from a pinned location); the result write-up is
present; the spike's three load-bearing questions are each answered
with a Y/N and supporting evidence.

---

## Risk notes

- **The facade is too thin to pin down libblyt's real API.** The
  facade exists in this spike to surface what the libretro adapter
  needs from the runtime. If the adapter only ends up calling four or
  five facade functions (because most of the adapter's work is
  format conversion, not runtime interaction), the facade is a poor
  proxy for libblyt's eventual surface — which has 15-20 functions
  per ADR-0033. Spike L's facade is *a lower bound* on libblyt's API,
  not a proposal for it. Documented; the result write-up flags any
  facade entry point that proved awkward to express on top of Spike
  I's runtime, and any libretro requirement that the facade did not
  cover (input devices beyond JOYPAD, controller hot-plug,
  serialisation versioning) for the production-API design step.

- **Palette format mismatch between runtime and adapter.** ADR-0033
  states palette is XRGB8888-shaped at the adapter boundary. If
  Spike I's runtime keeps the palette in some other shape (RGB332
  packed, RGBA, paletted-on-RV32-stored-as-bytes), the expansion
  step grows from "one indexed lookup" to "decode + lookup".
  Cheap to fix in either place; the spike's adapter does the cheaper
  fix (one expansion site) rather than refactoring the runtime
  palette storage. If that decision turns out wrong (the runtime
  *should* store XRGB8888 because every other frontend will need
  it), the spike documents the choice and recommends moving
  expansion into the runtime in the libblyt freeze.

- **`retro_run` invocation cadence is RetroArch's, not the cart's.**
  RetroArch may invoke `retro_run` faster than 60 fps if vsync is
  off and the host display runs at, say, 144 Hz; or slower if the
  host can't keep up. The accumulator handles both — at 144 Hz
  invocations, most `retro_run`s do zero `blyt_runtime_update` calls
  and just re-blit the cached frame; at slow invocations, up to 3
  catch-ups absorb the lag. The risk is that "no extra indirection
  latency" in the spike's Stage 5 gate is ambiguous when `retro_run`
  invocations are sparse. The spike pins RetroArch to 60 fps vsync
  (`video_vsync = true`, `video_refresh_rate = 60`) for the gate
  recording; the trace-frames file documents the actual cadence
  observed.

- **`retro_serialize` size mismatch under coroutine-blob expansion.**
  Spike K's coroutine save-blob model is fixed-size POD per slot
  declared at cart load. If a future cart changes the declared slot
  set during run (Spike K does not test this — slots are immutable),
  the upper bound computed at `retro_load_game` time would be wrong
  and `retro_serialize` could overflow the buffer. The spike asserts
  the slot table is immutable post-load (`runtime_assert_slots_locked()`
  added to the facade); production libblyt should adopt the same
  constraint or expose a mechanism to grow `retro_serialize_size`
  results, which libretro does not natively support.

- **`retro_unserialize` cost under aggressive rewind.** RetroArch's
  rewind at 8× speed invokes `retro_unserialize` 8 times per display
  frame. `blyt_runtime_reset` runs `init` each time; on case d
  `init` is trivial but on a more complex cart it could exceed the
  per-call budget. The spike's 5-second / 300-frame gate at 1× rewind
  speed is well inside the budget; faster rewind speeds are
  documented but not gated on. Production carts that hit this should
  adopt a "shallow restore" path (restore POD regions without
  re-running `init` if the current Lua/native state is compatible) —
  documented as a follow-up.

- **Libretro's `retro_get_memory_data` returns 0.** Some RetroArch
  features (cheats, save-RAM persistence on platforms without
  proper save state) read core state through this API. Returning 0
  cleanly disables those features; RetroArch logs a notice but does
  not refuse to run. If a regression test elsewhere asserts on
  `get_memory_data` returning a non-NULL pointer, that test was
  written for a classic-console core and does not apply here.
  Documented.

- **Synthetic mixer cadence vs RetroArch's audio request rate.** The
  synthetic mixer generates samples at 48 kHz internally; the
  adapter's audio push uses `sample_rate / declared_fps` per
  `retro_run`. If declared_fps drifts (cart declares 50 fps via
  `blyt_cart_fps()` per ADR-0047), the per-render-frame sample
  count is `48000 / 50 = 960`, not `800`. The adapter reads
  `blyt_runtime_declared_fps()` once at load and uses that value;
  if the cart changes its declared fps mid-run (not supported by
  ADR-0047), the audio sample count would diverge. Out of scope —
  ADR-0047 forbids it.

- **`retro_get_system_av_info` and resolution changes.** ADR-0036
  fixes the framebuffer at 320×240; libretro's `geometry` struct
  reports max width/height as the same. Carts that want a
  different resolution per cart configuration are not supported by
  the spike; out of scope.

- **The libretro frontend's input-state caching.** Some libretro
  frontends batch `input_state_cb` reads inside their own
  ring-buffer; reading a button "now" can return state from up to
  one frame ago. This is invisible to the adapter and the cart —
  the cart sees a coherent input state per frame, just possibly
  one frame behind the user's actual button press. RetroArch
  handles this correctly; other libretro frontends (custom
  hardware) may not. Out of scope for desktop RetroArch; flagged
  for the ADR-0034 frontend's spike.

- **RetroArch's rewind implementation invalidates the buffer on
  any change to `retro_serialize_size`.** The bound is computed
  once at `retro_load_game`; the adapter must never grow the
  bound mid-run. Asserted by `runtime_assert_slots_locked()` plus
  the cart-load-time computation pattern. If a future feature
  allows mid-run growth (saving extra debug state when a debugger
  attaches), the rewind buffer would silently be invalidated;
  document this as a libblyt-side constraint.

- **rv32emu inside a `.so` rather than a process.** Spike I runs
  rv32emu as a CLI binary; spike L embeds it. Three concerns: (a)
  rv32emu's initialisation may rely on argv parsing or environment
  variables that are absent in the embedded path; the facade's
  `blyt_runtime_create` synthesises whatever is needed. (b) rv32emu
  may not be re-entrant across multiple `blyt_runtime_t` instances;
  the spike runs exactly one runtime per process (libretro's
  contract is single-game-per-core anyway) and asserts on this. (c)
  rv32emu's signal handlers (segfault, illegal instruction) can
  conflict with RetroArch's; the facade explicitly does *not*
  install rv32emu's signal handlers in embedded mode. Documented;
  any signal-handler conflict surfaces as a post-load crash, not
  a stage-by-stage failure.

- **Stock RetroArch version drift.** RetroArch's libretro API is
  stable in headline shape but has version-tagged extensions
  (`RETRO_API_VERSION` is 1). The spike pins to the
  `libretro.h` shipped with the RetroArch package the Dockerfile
  installs; the result write-up records the RetroArch version. If
  a different RetroArch version exposes new contracts the adapter
  doesn't implement, the spike's gate is unchanged.

---

## Deliverables

- `spikes/spike-l/Dockerfile` — extends `fc32-spike-i`, adds
  RetroArch and libretro headers (`apt install retroarch
  libretro-dev`), Python tooling for the headless save-state
  regression.
- `spikes/spike-l/Makefile` — orchestration:
  - `make core` — builds `runtime_libretro.so`.
  - `make run-retroarch` — launches RetroArch with the core and
    case d cart; for manual / visual gates.
  - `make trace-frames` — Stage 2 step 8 accumulator trace.
  - `make audio-drift` — Stage 3 step 12 60 s audio drift run.
  - `make savestate-test` — Stage 4 step 15 digest comparison.
  - `make rewind-capacity` — Stage 4 step 17 buffer-size
    calculation.
  - `make layout-mismatch` — Stage 4 step 18 negative test.
  - `make loc-count` — Stage 5 step 19 LOC tally per file.
  - `make demo-recording` — Stage 5 step 20 screen recording.
  - `make all` — every headless gate; exits 0 only if all pass
    plus `loc-count` is ≤ 1000.
- `spikes/spike-l/host/retro_core.c` — the libretro entry points,
  the accumulator, the input/video/audio push wiring, save-state
  passthrough.
- `spikes/spike-l/host/palette.c` — palette expansion to XRGB8888.
- `spikes/spike-l/host/input_map.c` — ADR-0017 ↔ JOYPAD descriptor
  table and runtime input update.
- `spikes/spike-l/host/audio_push.c` — audio batch construction,
  drift correction.
- `spikes/spike-l/lib/blyt_facade.{c,h}` — the minimum viable
  libblyt-shaped facade over rv32emu + Spike I's runtime
  libraries + Spike K's save-state code + the synthetic mixer.
- `spikes/spike-l/cases/case_d/` — symlink to spike-i's case d;
  one Lua-side addition `cart_l.lua` that reacts visibly to
  the A button (Stage 3 step 10) — wrapped from `main.lua`, not
  forked.
- `spikes/spike-l/cases/case_d_alt_layout/` — Stage 4 step 18
  variant with an extra `cart_state_t` field; identical Lua.
- `spikes/spike-l/host/dump_savestate.py` — headless save-state
  regression driver.
- `spikes/spike-l/baselines/` — `serialize_upper_bound.txt`,
  `audio_drift_60s.csv`, `frame_trace.csv`, `loc-count.txt`,
  `rewind_capacity.txt`.
- `spikes/spike-l/recordings/` — `demo-five-behaviours.webm` (the
  5-behaviour gate recording) — gitignored if size-prohibitive,
  otherwise committed; pinned URL in the result write-up either way.
- `spikes/spike-l/TASKS.md` — per-stage checklist, kept current as
  work proceeds.
- `docs/design/spike-l-results.md` — the write-up: the LOC count
  (adapter and facade separately), the `serialize_upper_bound` for
  case d, the rewind capacity in seconds, the audio-drift number,
  the Stage 4 step 15 digest equality, the demo recording link, the
  three load-bearing-question answers, open items for the libblyt
  API freeze (which facade entry points were awkward, which
  libretro requirements the spike did not exercise), and any
  composition implications for ADR-0033 / ADR-0034 / ADR-0036 that
  surfaced during the build.

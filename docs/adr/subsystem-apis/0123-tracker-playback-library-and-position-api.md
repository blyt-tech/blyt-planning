# ADR-0123: libopenmpt for tracker playback; tracker position API

## Status
Accepted — library choice (partially supersedes ADR-0004's libxmp selection)
Deferred post-v1 — tracker position cart API

## Context

ADR-0004 chose **libxmp** for XM/IT tracker module decode. libxmp is a
competent decoder but exposes no playback state to the host application: there
is no way to query the current order, pattern, or row through its API. SDL_mixer,
which wraps libxmp (or similar) for its tracker decode path, likewise exposes
nothing beyond play/stop/is-playing.

**Tracker position is the canonical way to synchronise visuals tightly to
music.** A demoscene-style effect knows that the bass hits every 4 rows, or
that a particular pattern transition signals a scene cut. Reading the current
row from the decoder lets the cart react on the exact frame a beat arrives,
rather than running a freewheel timer that drifts. This is not achievable
with libxmp or SDL_mixer's tracker path regardless of the API surface added
above them.

**libopenmpt** is the reference implementation for OpenMPT, the dominant
tracker authoring tool in the contemporary demoscene and hobbyist community.
It exposes a stable C API (`libopenmpt.h`) covering:
- Playback position: order index, pattern index, row index.
- Timing: current BPM and speed (ticks-per-row).
- Per-channel state: current note, instrument, volume, panning, effect column
  (deferred beyond the initial state API).
- Render quality controls (interpolation, filter settings).
- All formats XM/IT/MOD/S3M/MTM/OKT/… are supported to the same accuracy
  as the GUI application, which matters for modules authored in OpenMPT.

libopenmpt is BSD 3-Clause licensed (no copyleft obligation); libxmp is
LGPL-2.1. Both are usable in the runtime; the license distinction is not the
deciding factor.

## Decision

### Library: libopenmpt replaces libxmp

The runtime uses **libopenmpt** as the tracker module decoder. ADR-0004's
choice of libxmp is superseded for this decode path.

The runtime manages tracker decode directly via the libopenmpt C API rather
than delegating it through SDL_mixer's internal decoder. libopenmpt decodes
to interleaved PCM which is then fed into the runtime's mixing pipeline.
SDL_mixer (or equivalent) continues to handle SFX voices; the music decode
path is separated so the runtime retains direct access to libopenmpt's state
query functions at frame boundaries.

This is a runtime implementation detail. The cart API surface defined in
ADR-0053 (`blyt_music_play`, `blyt_music_stop`, `blyt_music_set_channel_mask`,
`blyt_music_is_playing`) is unchanged. Channel muting maps directly to
libopenmpt's per-channel mute API. The transition and crossfade machinery
described in ADR-0053 works by running two libopenmpt instances during the
crossfade window, exactly as libxmp would have.

### v1 behaviour

v1 ships libopenmpt as the tracker decoder. **The tracker position cart API
is not exposed in v1.** The library choice is made now so that the state API
can be added without re-architecting the audio stack later; no tracker state
is visible to carts in v1.

### Tracker position API (deferred post-v1)

#### State snapshot

At the boundary between each audio buffer and the next logical frame, the
runtime queries libopenmpt for the current playback position. This snapshot is
captured and recorded as a per-frame input alongside player button state and
voice-end events (ADR-0106):

```
(frame_n, music_position, order, pattern, row, bpm, speed)
```

The position is applied to the cart's logical view at the start of the next
`update()`, before the cart runs.

Tracker position is theoretically locally derivable — given the same module,
starting position, sample rate, and samples-per-frame, the row at frame N is
a pure function of the module content. Recording it as a per-frame input is
therefore redundant for determinism in a single-implementation world, but it
provides a cheap guarantee of replay faithfulness across platform
implementations that differ in audio buffer granularity or sample-rate
conversion, matching the robustness standard set by ADR-0106 for voice-end
events.

For netplay, tracker position is locally derivable on every peer (same module,
same playback start, deterministic decode) and requires no wire traffic, by
the same argument as cosmetic RNG post-draw state (ADR-0041).

#### Cart API

```c
typedef struct {
    int32_t order;    // index into the module's order list; -1 if not playing
    int32_t pattern;  // pattern number at current order position; -1 if not playing
    int32_t row;      // row within the current pattern; -1 if not playing
    int32_t bpm;      // current tempo in beats per minute; 0 if not playing
    int32_t speed;    // ticks per row (the module's SPD value); 0 if not playing
} blyt_music_position_t;

// Fills *out with the snapshotted position at the start of the current frame.
// Valid in both update() and draw(). Returns BLYT_ERR_NO_MUSIC if no
// tracker module is currently the active music handle (Opus streamed music
// returns BLYT_ERR_NO_MUSIC — this API covers tracker modules only).
blyt_result_t blyt_music_get_position(blyt_music_position_t *out);
```

Lua:
```lua
local pos = music.get_position()
if pos then
    if pos.row ~= last_row then
        if pos.row % 4 == 0 then  -- every 4 rows = one beat at typical SPD
            trigger_flash()
        end
        last_row = pos.row
    end
end
```

`blyt_music_get_position` returns the position as it was at the start of the
current logical frame, not the live decoder position. This is the correct
value for `update()` logic: a row-change that arrived this frame is visible;
one that arrives mid-frame (i.e., between the snapshot and the next frame
boundary) is not visible until the following frame. The maximum latency is
one frame, which is below the perceptual threshold for visual reaction to
audio events.

**Valid in both `update()` and `draw()`**, because the value is a snapshotted
per-frame input — identical wherever within the frame it is called. Calling
it in `draw()` does not violate ADR-0076 (the restriction on tracked-state
writes) because this is a pure read of a deterministic snapshot.

During crossfades (ADR-0053), the position reflects the **current** (fading-in
or steady-state) module. The fading-out previous module's position is not
exposed.

`BLYT_ERR_NO_MUSIC` is returned when the active music source is a streamed
Opus track (ADR-0004) rather than a tracker module. The Opus path has no
equivalent row/order concept.

#### Per-channel state

Per-channel note, instrument, volume, panning, and effect columns are
available in libopenmpt but **not exposed through the cart API in this ADR**.
A per-channel state API (suitable for channel-level VU meters, note-reactive
visualisers, and per-voice spectrum effects) is a further post-v1 item and
will be addressed in a dedicated ADR when the use case and API shape are
established.

## Consequences

- Demoscene-style beat-sync effects become straightforward: poll
  `music.get_position().row`, react on row change or row-modulo-N events.
  No free-wheel timer to calibrate or drift to manage.
- All ADR-0053 cart APIs are unaffected. Channel muting, crossfades, and
  the logical-mixer-view determinism model from ADR-0106 all carry forward
  unchanged.
- The runtime's audio architecture splits into two paths at the decode layer:
  tracker modules through libopenmpt (directly runtime-managed), Opus/WAV/QOA
  through the existing mixer decode chain. This is an implementation detail
  not visible to carts.
- libopenmpt is C++ with a C API. The runtime already links C++ (Rust carts
  compile to a RISC-V ELF; the runtime itself may be C or C++). On all target
  platforms (Linux RISC-V, Linux x86, macOS, WASM via Emscripten) C++ is
  available without additional constraint.
- libopenmpt supports a wider format set than libxmp (MOD/S3M/MTM/OKT/etc.
  in addition to XM/IT). ADR-0004's format declarations remain the
  blessed set; the broader support is a runtime capability floor, not a
  commitment to expose all formats through the asset pipeline.
- Tracker position is recorded as a per-frame input (a few bytes per frame).
  Replay buffer cost is negligible.
- The position API returns `BLYT_ERR_NO_MUSIC` for Opus streamed music.
  Carts that use Opus for music and need visual sync must manage their own
  frame-count-based timing; this is consistent with Opus being a Large/Flagship
  cart format (ADR-0004) where more complex cart code is expected.
- Per-channel state for note-reactive visualisers is deferred; libopenmpt
  already exposes it and no architectural work is required to add it in a
  future ADR.

# ADR-0124: Per-channel volume control for tracker music

## Status
Accepted

## Context

ADR-0053 provides `blyt_music_set_channel_mask`, a bitmask that hard-mutes
individual tracker channels. This is sufficient for the stems use case: toggle
a layer in or out cleanly on a beat. It is not sufficient for gradual
atmosphere work: a game that wants to fade in a melody layer as the player
enters a tense room, or fade strings out under an underwater effect, needs
analog per-channel volume control, not binary switching.

ADR-0055 exposes the transient layer of the three-layer volume model at the
**voice-group** level (`blyt_audio_set_group_volume`). This can fade the
entire music group, but not individual tracker channels within a module. A
whole-group fade loses the per-layer texture; there is no way to, say, keep
drums loud while the melody fades away.

ADR-0071 establishes the pattern for smooth value transitions: the runtime
provides a stateless easing library; the cart stores progress in its own state
buffers and advances it each frame. A runtime-owned per-channel tween manager
would be redundant given this infrastructure and would introduce runtime-owned
state that complicates save/restore.

## Decision

**Two new functions expose per-channel volume as an analog scalar in [0.0, 1.0]:**

```c
// Set the volume scalar for a single tracker channel. volume is clamped to
// [0.0, 1.0]. channel is 0-based, matching the mute mask bit index (ADR-0053).
// Out-of-range channel indices are silently ignored (dev mode warns).
// Valid only in update().
blyt_result_t blyt_music_set_channel_volume(uint32_t channel, float volume);

// Returns the current per-channel volume scalar. Valid in update() and draw().
// Returns 1.0 for channels that have never been set, or if no module is playing.
float blyt_music_get_channel_volume(uint32_t channel);
```

Lua:
```lua
music.set_channel_volume(channel, volume)
music.get_channel_volume(channel)   -- returns 1.0 if unset
```

### Signal chain

Per-channel volume multiplies into the signal after the mute mask and before
the group-level transient volume (ADR-0055):

```
final = frontend_master
      × prefs_music_volume
      × transient_music_volume
      × per_channel_volume      ← this ADR
      × channel_authored_volume
```

If a channel is muted by the mute mask (ADR-0053), the mute takes precedence
regardless of the per-channel volume: muted channels produce silence. The
per-channel volume value is unchanged by muting — it takes effect again when
the channel is removed from the mute mask.

### Reset on track change

Both the per-channel volumes and the mute mask reset to their defaults — all
volumes 1.0, mask 0 (all channels unmuted) — when `blyt_music_play()` is
called. Module channel layouts are not portable across modules; carrying the
previous module's channel configuration into a new one would produce
unpredictable results. Carts that want non-default state for the new track
(e.g., starting a piece with the melody layer at 0.0 and fading it in from
the first frame) set the volumes immediately after the `blyt_music_play()` call.

This formalises the reset behaviour that ADR-0053 left implicit for the mute
mask.

### Phase restriction

`blyt_music_set_channel_volume` is valid only in `update()`. Audio state
changes belong in the simulation tick (ADR-0076 — audio triggers in `draw()`
risk double-firing and violate the simulation/presentation boundary). A call
in `draw()` is a dev-mode warning, a hard error under `--strict`.

`blyt_music_get_channel_volume` is a pure read of a snapshotted value and is
valid in both `update()` and `draw()`.

### Implementing fades

The expected pattern, using the easing library (ADR-0071):

```lua
-- In state buffers (cart.config.yaml): melody_fade_t: f32 per channel entry
-- update() — fade melody channel in over 120 frames
S.melody_t = math.min(S.melody_t + 1.0 / 120, 1.0)
music.set_channel_volume(MELODY_CHANNEL, blyt32.ease_lerp(EASE.OUT_CUBIC, 0.0, 1.0, S.melody_t))
```

The fade progress `t` lives in the cart's tracked state buffer. It survives
save/restore automatically; the cart re-applies the corresponding volume call
in `on_load_state` if needed, exactly as it would re-apply any other
mixer state derived from saved values.

### Save and restore

Per-channel volumes are part of the runtime's audio-mixer state. They are
included in full snapshots (`retro_serialize` — save states, rewind). They are
**not** automatically included in POD buffer saves (the cart's own save files)
— only the cart's tracked state buffers are serialised by that path. Carts
that need channel volumes to survive a POD save/load store the desired values
in their own state buffers (as natural state — e.g., `S.current_intensity`)
and call `set_channel_volume` in `on_load_state` to restore the mixer to the
correct configuration. This is the same pattern as any other mixer state the
cart drives (group muting, stem masks, etc.).

### Interaction with crossfades

During a crossfade between two modules (ADR-0053), the fading-out previous
module uses the per-channel volumes that were active on it at the moment
`blyt_music_play()` was called — its volumes are frozen at that snapshot.
The incoming module's per-channel volumes reset to 1.0 at `blyt_music_play()`
and can be set immediately after that call. This gives the cart independent
control over each module's channel mix during the crossfade window.

## Consequences

- Smooth layer fades for atmosphere — a melody that eases in, a percussion
  layer that fades under a cutscene, a bass drone that drops away before a
  quiet passage — are straightforward: store `t` in a state buffer, advance
  it each frame, pass the eased value to `set_channel_volume`.
- No runtime tween state; no new save/restore complexity; consistent with
  ADR-0071.
- The mute mask (ADR-0053) and per-channel volume are orthogonal. Authors can
  use whichever is appropriate: the mask for instant beat-aligned cuts, the
  volume scalar for gradual fades. Both are in play simultaneously on any
  channel.
- Both reset on `blyt_music_play()`, making the reset behaviour of the mute
  mask explicit where ADR-0053 left it implicit.
- Per-channel volume sits in the signal chain below the group transient volume
  (ADR-0055). A whole-group fade and a per-channel fade are independent and
  compose multiplicatively; the cart can use both simultaneously for layered
  effects.
- `get_channel_volume` returning 1.0 for never-set channels means the cart
  can read back the current value for fade-from-current patterns without
  needing to track the last-set value separately.
- Valid channel range is 0–31, matching the mute mask's uint32 range. Modules
  with fewer channels ignore out-of-range sets silently; dev mode warns.
  libopenmpt (ADR-0123) supports up to 256 channels in IT format; the 32-
  channel cap aligns with the existing mute mask and is sufficient for the
  common stem-layering idiom. A wider range can be addressed in a future ADR
  if IT modules with deep channel counts become a real use case.

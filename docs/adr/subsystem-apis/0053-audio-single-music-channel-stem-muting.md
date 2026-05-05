# ADR-0053: Audio — single music channel with stem muting

## Status
Accepted

## Context

Many retro consoles support one background music track at a time. The
question is how to handle musical variation: loading different tracks for
different intensity levels creates seams at transitions; mixing multiple
independent streams adds complexity and memory cost.

Tracker music formats (XM, IT) already support the concept of channels that
can be individually muted. Using channel muting as a stem system provides
dynamic music without multiple streams.

## Decision

**One music handle at a time from the cart's perspective; dynamic layering
via channel muting; crossfades briefly overlap two decoders inside the
mixer.**

```c
blyt_result_t blyt_music_play(blyt_resource_h res, blyt_music_transition_t transition,
                          uint32_t duration_ms);
blyt_result_t blyt_music_stop(blyt_music_transition_t transition,
                          uint32_t duration_ms);
blyt_result_t blyt_music_set_channel_mask(uint32_t muted_channels);
bool        blyt_music_is_playing(void);

typedef enum {
    BLYT_MUSIC_CUT       = 0,  // instant; duration_ms ignored
    BLYT_MUSIC_FADE      = 1,  // fade-out then fade-in (gap of silence)
    BLYT_MUSIC_CROSSFADE = 2,  // overlap old and new for duration_ms
} blyt_music_transition_t;
```

Only one tracker module is "current" at any time. Calling `blyt_music_play()`
while music is already playing transitions to the new module: cut, fade
(sequential — old fades out completely, then new fades in), or crossfade
(old and new overlap for `duration_ms`).

Crossfades are the only case in which the mixer runs two music decoders
simultaneously. The overlap is bounded to the crossfade window — once the
old track has faded to silence, its decoder is released. Carts cannot
observe or address the previous track during the crossfade; the cart-facing
contract remains "one music handle at a time."

**Stacked transitions.** The mixer maintains exactly two slots — "current"
and "fading-out previous" — and never more. If a new transition arrives
while a previous one is still in flight, the slot in "fading-out previous"
is hard-cut (its decoder released immediately), the current track moves
into that slot taking the new transition's fade-out trajectory, and the
new track becomes current with the requested fade-in. This bounds
simultaneous decoders at two regardless of how rapidly the cart triggers
transitions; the audible cost is that a track which was already most of
the way to silence may end abruptly.

**Stem muting:** the cart provides a bitmask of which tracker channels to
silence. Channel 0 = bit 0. A cart that structures its tracker module as
stems (e.g., channels 0–3 = bass/drums, channels 4–7 = melody, channels
8–11 = intensity layer) can dynamically enable or disable layers by updating
the mute mask each frame.

```lua
-- Increase intensity: unmute the intensity channels
blyt32.music.set_channel_mask(0x0000_00FF)  -- mute channels 8–31
blyt32.music.set_channel_mask(0x0000_0000)  -- unmute all channels
```

## Consequences

- Dynamic music without multiple audio streams: one module, one decoder,
  per-channel muting applied at the mixer level.
- Tracker authors structure their modules as stems; this is a well-established
  idiom in the XM/IT community.
- Crossfade between modules covers scene transitions where a whole new
  musical context is needed; the mixer transparently runs two decoders for
  the crossfade window only.
- Steady-state memory footprint stays at one module, since the second
  decoder is allocated on transition and released as soon as the old track
  fades to silence.
- Streamed Opus (Large/Flagship carts) does not support channel muting;
  those carts use separate stream files for transitions (ADR-0004). The
  crossfade overlap rule applies to Opus too — two streams briefly during
  the crossfade window.

# ADR-0006: Audio is_playing queries actual mixer state

## Status
Accepted

## Context

The high-level design document initially stated that audio playback queries
should be derived from cart state (frame-count-based tracking), not from the
mixer state, in order to preserve full determinism. Under that model, a cart
would determine whether a sound is still playing by tracking how many frames
have elapsed since it started, using the known duration of the clip.

This was reconsidered during API design. Frame-count-based is_playing queries
are brittle: they require the cart to track start frames and clip durations
exactly, fail silently when a voice is stolen by the mixer, and give wrong
answers whenever a voice is stopped early or interrupted. The ergonomic cost
is high and the determinism benefit is narrow.

## Decision

**`fc_voice_is_playing()` queries actual mixer state**, not a frame-count
estimate. The mixer is authoritative for whether a voice is currently active.

This is a deliberate, bounded exception to the full-determinism rule
(ADR-0007). Audio is treated as a one-way output flow for determinism
purposes: the cart drives the mixer, but queries about mixer state are
read-backs from an output channel, not state that participates in the
simulation. Two instances running the same inputs may theoretically return
slightly different `is_playing` values under voice-stealing pressure, but
this is acceptable because:

- Cart logic that branches on `is_playing` is inherently presentation-layer
  code, not simulation-layer code.
- Voice stealing is itself deterministic given the same voice-budget
  configuration and the same input sequence.
- The alternative (frame-count estimates) produces wrong answers under early
  stops and voice stealing, which is worse than theoretical divergence.

## Consequences

- `fc_voice_is_playing()` and `fc_music_is_playing()` return live mixer state.
- Carts that branch on `is_playing` for game logic (not just presentation)
  accept a theoretical determinism limitation in exchange for correct answers.
- Netplay and replay remain correct because voice-stealing behaviour is
  deterministic given the same sequence of cart API calls.
- ADR-0007 (structural determinism) notes this as an explicit, documented
  exception rather than an oversight.

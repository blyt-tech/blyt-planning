# ADR-0051: Screen shake tracked in save state

## Status
Accepted

## Context

Screen shake (a brief offset of the rendered framebuffer to convey impact or
rumble) is often implemented as a purely presentational effect: started on an
event, it plays out over a few frames and is invisible to the simulation.
Under this model it need not be serialized.

However, a save state taken mid-shake should restore with the shake still in
progress. If shake state is not saved, restoring to a save-state taken during
a shake will silently drop the remaining shake frames, which is detectable by
the player and inconsistent with the rest of the save/restore model.

## Decision

**Screen shake state is tracked in the runtime's serialized state.**

The runtime maintains a shake region as part of its own tracked state (not
in a cart-declared buffer). The region stores 4 values:
- `remaining_frames` (i32): ticks of shake left.
- `intensity` (f32): current amplitude in pixels.
- `decay` (f32): per-frame decay factor.
- `seed` (i32): deterministic noise seed for offset generation.

Shake offsets are computed deterministically from `(frame_count, seed,
intensity)` so that restoring a save state produces identical shake behavior
on replay.

The cart-facing API:

```c
fc_result_t fc_screen_shake(int32_t frames, float intensity);
```

Calling `fc_screen_shake` replaces (not accumulates) any existing shake. The
framebuffer blit at end-of-frame applies the computed offset; no individual
draw call is affected (consistent with ADR-0048).

## Consequences

- Save states taken mid-shake restore correctly with the shake continuing.
- Deterministic replay of shake effects is guaranteed by the seeded
  noise approach.
- The shake region is a small fixed-size addition to the runtime's own
  tracked state — no cart API changes to declare it.
- Replacing rather than accumulating shake is a simplification; carts that
  want compounding shake call `fc_screen_shake` repeatedly with updated
  parameters.

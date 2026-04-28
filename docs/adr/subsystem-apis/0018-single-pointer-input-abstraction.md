# ADR-0018: Single-pointer input abstraction

## Status
Accepted

## Context

Mouse and touch input are qualitatively different from the button model but
are essential for point-and-click adventures, puzzle games, menus, and any
game designed for mobile. Rather than exposing platform-specific mouse or
multi-touch APIs, a unified abstraction reduces the platform-branching carts
would otherwise need.

## Decision

The runtime exposes a **single-pointer abstraction** alongside the button API:

```lua
console.input.pointer_is_held()
console.input.pointer_position()    -- x, y in game pixels (0–319, 0–239)
console.input.pointer_pressed()     -- edge-triggered on press
console.input.pointer_released()    -- edge-triggered on release
```

Pointer source by platform:
- Desktop: mouse.
- Mobile: first active touch.
- Libretro frontends: frontend-provided pointer.

**Multi-touch is deferred.** v1 exposes only the single-pointer abstraction;
carts that conceptually need multi-finger gestures (pinch-zoom, two-finger
rotate, multi-finger puzzles) either wait for a future multi-touch API or
implement their interaction with the single pointer in v1. Keeping v1
single-pointer keeps the determinism contract (ADR-0007) simple — one
position + press/release per frame — and avoids designing a multi-touch
event model before there is enough cart experience to know what shape it
should take.

Pointer and button input coexist. Carts can use both simultaneously.

## Consequences

- Cart code never branches on "am I on mobile?" — it queries what input is
  available and uses it.
- Touch-first carts (`tap_to_select`, `direct_manipulation` schemes) work
  identically on desktop mouse and mobile touch without any code changes.
- The pointer is part of the deterministic input snapshot (ADR-0007):
  position and press/release state are recorded per-frame.
- In netplay, pointer input is associated with the local player
  (`console.input.local_player()`), not broadcast to all machines.
- Multi-touch is a future addition; v1 ships single-pointer only.

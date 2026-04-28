# ADR-0048: No clip rectangle or camera offset in the runtime

## Status
Accepted

## Context

Many 2D runtimes provide a global camera offset (so drawing at world
coordinates automatically translates to screen coordinates) and a clip
rectangle (so drawing outside a viewport is automatically clipped). These
reduce per-draw boilerplate in cart code but add hidden global state that
affects every drawing call, complicating reasoning and save-state semantics.

## Decision

**The runtime has no global camera offset and no clip rectangle.**

All drawing coordinates are screen-space pixels, always measured from the
top-left of the 320×240 framebuffer. There is no implicit translation or
clipping applied to draw calls.

Carts that need camera transforms or viewport clipping implement them in
cart code by adjusting coordinates before calling draw functions. The
framebuffer acquire/present API (ADR-0008) is available for full-screen
pixel operations.

## Consequences

- No hidden global state that draw calls silently depend on. Every draw call
  is self-contained: what you pass is what gets drawn.
- Save/restore does not need to capture camera or clip state (it doesn't
  exist). Screen shake (ADR-0051) is the sole exception — it shifts the
  framebuffer blit, not individual draw calls.
- Cart code that implements cameras is explicit about the transform; no
  "why is my draw call offset?" debugging.
- Carts pay a small coordinate-offset cost (a few additions) that the
  runtime would otherwise hide. This is acceptable at this fidelity.
- Tilemap rendering (a native primitive, ADR-0039) handles its own scroll
  offset internally; that is not a global camera — it is per-tilemap.

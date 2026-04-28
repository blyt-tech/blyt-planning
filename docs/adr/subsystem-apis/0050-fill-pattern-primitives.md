# ADR-0050: Fill pattern for drawing primitives

## Status
Accepted

## Context

Solid-color fill for rectangles, circles, triangles, and lines covers most
use cases but limits expressiveness. Dithering patterns, hatching, and
ordered-dither crossfades between two palette entries are valuable for the
aesthetic of a 256-color console and for transition effects. These can be
implemented per-primitive or via a global fill state.

## Decision

**A fill pattern (`fc_fillp_t`) is a `uint16_t` 4×4 bitmask that modifies
how filled primitives are rasterized.**

```c
typedef uint16_t fc_fillp_t;
// FC_FILLP_SOLID = 0xFFFF (all bits set — fully opaque, default)
// FC_FILLP_NONE  = 0x0000 (all bits clear — fully transparent)
```

The 16 bits map to a 4×4 pixel grid tiled across the primitive's extent.
Bit 0 = top-left; bit 15 = bottom-right. Where a bit is 1, the fill color
is written; where a bit is 0, the pixel is skipped (the pixel behind the
primitive shows through).

Fill pattern is passed per-call, not stored as global state:

```c
fc_rect_fill(x, y, w, h, color, FC_FILLP_SOLID);  // solid fill
fc_rect_fill(x, y, w, h, color, 0xAAAA);           // 50% checker dither
```

Use cases:
- **Dithering:** approximate intermediate colors between two palette entries.
- **Hatching:** lines, crosses, diagonal patterns.
- **Crossfade:** animate from `FC_FILLP_SOLID` to `FC_FILLP_NONE` over N
  frames to fade a screen overlay in or out.

Fill pattern applies to all filled primitives (rect, circle, triangle, convex
poly). It does not apply to lines (stroke width), blits, or text.

## Consequences

- Expressive filled graphics without requiring authors to manipulate the
  framebuffer directly.
- No global fill state — every call is self-contained.
- The 4×4 tile is small enough to hardcode in the inner rasterizer loop with
  no additional memory indirection.
- Crossfade effects are achievable with 16 distinct fill patterns covering
  0–100% opacity, giving smooth dither-based transitions.
- Ordered dithering on a 256-color palette produces visually pleasing results
  at 320×240, matching the aesthetic of the console.

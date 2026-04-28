# ADR-0003: Display — 320×240 paletted, 256 colors, double-buffered

## Status
Accepted

## Context

Resolution and color depth are the most consequential constraints on what
games look and feel like, and on whether a solo creator or small team can
realistically produce a full game's art. The options considered ranged from
lower (PICO-8's 128×128, 16 colors) to higher (640×480 truecolor).

A 16:9 aspect ratio was also considered as more modern and better for desktop
fullscreen. Mobile ergonomics were a significant factor in the final choice.

## Decision

**Resolution:** 320×240 pixels.
**Color model:** Paletted, 256 colors. The palette is fully programmable
(cart replaces it at runtime); the runtime ships several default palettes
(see ADR-0036).
**Rendering:** Double-buffered. Cart writes to a back buffer via
`acquire_framebuffer()` / `present_framebuffer()`; runtime flips on vsync.
**Aspect ratio:** 4:3.

A 400×300 "widescreen" mode is explicitly deferred and leaning toward
not being added, to maintain identity strength and mobile ergonomic
consistency.

## Consequences

- **Production realism:** a solo artist can produce a full game's worth of
  art at 320×240 in weeks to months. 640×480 truecolor requires team-years.
- **Palette effects are free:** palette cycling, damage flashes, time-of-day
  shifts, fades — all achievable by changing palette entries, with zero
  per-pixel cost.
- **AI and off-the-shelf asset compatibility:** this resolution and palette
  model is a strong fit for AI-generated pixel art pipelines and commercial
  tileset libraries.
- **Mobile ergonomics:** in portrait orientation on a phone, 4:3 fills the
  full width with ~50% of the screen below for touch controls (Game Boy
  layout). In landscape, 4:3 fills the vertical space and leaves natural
  side strips for controls without occluding the game. 16:9 would force
  touch controls to overlay the game.
- **Memory footprint:** framebuffer subsystem requires <200 KB — trivially
  fits every target platform.
- **Framebuffer API:** direct pixel writes in the back buffer in hot loops
  without per-pixel API overhead — covers the rendering-intensive case
  efficiently.

# ADR-0042: Default assets bundled in the runtime binary

## Status
Accepted

## Context

New carts need to display text, draw with colors, and produce sound without
requiring authors to supply their own resources first. Without defaults, the
first `console.text.draw(...)` call fails unless the author has already
configured a font. Additionally, the console needs a consistent first-
impression identity across all carts.

## Decision

**The runtime ships with a bundle of default assets (<70 KB total), always
available to all carts.**

**Fonts:**
- `font_builtin` (8×8): primary text font. Current plan is the public-domain
  IBM CGA/VGA CP437 font; a custom-designed font may replace it before v1.0
  release as a console-identity investment.
- `font_small` (4×6 or 3×5): compact ASCII micro-font for dense UI, debug
  overlays, crowded HUDs. Source: permissively-licensed public-domain pixel
  font (Cozette, Tamzen, or similar).
- `font_icons`: companion icon font, ~120–150 glyphs. Includes abstract
  geometric button glyphs (`geom_cross`, `geom_triangle`, `geom_square`,
  `geom_circle` — deliberately distinct from Sony's trade dress), gamepad
  D-pad glyphs, common UI symbols.

**Palettes:**
- `palette_default`: modern curated 256-color palette optimized for pixel art
  production (recommended; AI-generated and off-the-shelf assets fit). Sourced
  from a permissively-licensed palette (AAP-256, Blk Neo 256, or similar) or
  designed custom for the console.
- `palette_vga`: IBM VGA default 256-color palette.
- `palette_ega`: EGA 16-color palette expanded to 256 entries.
- `palette_cga`: CGA 4-color palette expanded to 256 entries.

**System sounds:**
- Startup chime: brief "console powered on" sound played on boot. Suppressed
  via manifest flag. A console-identity investment — worth designing carefully.
- Crash sound: played when a cart panics, hits the watchdog, or fails to load.

**SDK-bundled optional assets** (not in the runtime binary; opt-in per cart):
- SFX library: ~200–500 KB of general-purpose sound effects (QOA/ADPCM).
- Example tracker modules: a handful of "console style" pieces for
  prototyping.
- Placeholder sprite/tile sets: labeled untextured sprites, numbered tile
  atlases.

## Consequences

- The 70 KB runtime overhead is negligible.
- New carts display text and render colored pixels without supplying any
  resources — lowers the initial friction significantly.
- The default palette shapes the "hello world" first impression. The choice
  to default to a modern curated palette (not VGA) is a production-support
  decision: makes the console useful for small teams using AI-generated or
  off-the-shelf art.
- Authors wanting the VGA aesthetic swap in one line:
  `console.gfx.set_palette(FC_PALETTE_VGA)`. Built-in asset constants
  (`FC_FONT_BUILTIN`, `FC_PALETTE_VGA`, etc.) are defined in `fc_cart.h`
  and follow the `FC_` prefix convention (ADR-0059).
- Icon font uses neutral geometric shapes to avoid PlayStation trade dress
  concerns. Abstract prompt API resolves `{prompt_action_a}` to the right
  icon for the current input device automatically.
- All default assets are public domain or permissively licensed; no legal
  encumbrance to redistribution. Licensing documented explicitly.

# ADR-0042: Default assets bundled in the runtime binary

## Status
Accepted — amended 2026-07-01 (palette specifics tracked by #200); the palette
half implemented 2026-07-02 (#201)

> **Amendment (2026-07-01, tracked by #200):**
> - The `palette_default` source palettes named below — **"AAP-256" and
>   "Blk Neo 256" — do not exist** (verified: the AAP family tops out at
>   AAP-Splendor128 / 128 colors; "BLK NEO" is ~40 colors). Those citations are
>   struck.
> - Provisional `palette_default` = **DawnBringer "Aurora"** (256), used as-is
>   for prototyping. **Licensing is unresolved** — Aurora has no explicit license,
>   so it does **not** cleanly meet the "public-domain/permissive, no encumbrance"
>   bar asserted in the Consequences below; before 1.0, either accept the risk +
>   attribute, choose a CC0 palette, or author blyt's own (recommended).
> - `palette_default` must reserve a **sacrificial index 255** for the ADR-0049
>   transparency key; the packer quantizes transparent sprites to indices 0–254
>   (the ADR-0049/0088 seam). Aurora's index 255 (`#911437`) serves this.
> - Named color constants are **palette-specific** (`BLYT_EGA_*` / `BLYT_VGA_*` /
>   `BLYT_AURORA_*`, using the EGA-16 as a shared naming vocabulary); custom
>   palettes get packer-generated `C_<NAME>` constants (ADR-0059). Full design +
>   the Aurora nearest-to-EGA index table are in #200.

> **Amendment (2026-07-02, #201 implemented):** the licensing question above is
> **resolved for now**: Aurora ships provisionally — accepting that a bare RGB
> list is not independently copyrightable, and attributing it (DawnBringer /
> Ettinger) in `docs/contributing/third-party.md`. Revisit before 1.0 (author
> blyt's own CC0 palette, or an explicitly CC0 source, if the risk calculus
> changes). All four palettes (`aurora`/`vga`/`ega`/`cga`) are hand-authored
> `const uint32_t[256]` tables in `runtime/shared/blyt_palettes.c`, addressed by
> `BLYT_PALETTE_AURORA`/`_VGA`/`_EGA`/`_CGA`/`_DEFAULT` (`BLYT_PALETTE_DEFAULT`
> aliases `_AURORA`) — encoded via ADR-0134's `BLYT_RESOURCE_ENCODE(id,
> PROV_RUNTIME)`, the first population of that provenance bit (see ADR-0134
> amendment). `blyt_gfx_palette_set(handle)` loads one wholesale; a cart
> declares its default via `palettes: default: <name>` in `blyt.config.yaml`
> (see ADR-0088 amendment — supersedes this ADR's implicit assumption that
> palette config lives in `blyt.build.yaml`'s single-palette mode, which
> remains reserved for #203's *custom*-palette declaration). Undeclared falls
> back to `BLYT_PALETTE_DEFAULT` (aurora), auto-loaded before `init()`. VGA/EGA/
> CGA are public-domain PC hardware standards, not copies of any third-party
> source file (VGA is a from-scratch reconstruction of the documented DAC
> construction — 16 EGA colors + a 16-step grayscale ramp + a 216-color HSV
> cube). Named color constants (`BLYT_EGA_*` etc.) remain undone — tracked by
> #203.

## Context

New carts need to display text, draw with colors, and produce sound without
requiring authors to supply their own resources first. Without defaults, the
first `blyt32.text.draw(...)` call fails unless the author has already
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
  production (recommended; AI-generated and off-the-shelf assets fit). Source
  provisionally DawnBringer "Aurora"; see the Amendment above (the palettes
  originally named here do not exist, and the licensing/authoring decision is
  open — tracked by #200). Must reserve a sacrificial index 255 (ADR-0049).
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
  `blyt32.gfx.set_palette(BLYT_PALETTE_VGA)`. Built-in asset constants
  (`BLYT_FONT_BUILTIN`, `BLYT_PALETTE_VGA`, etc.) are defined in `blyt32.h`
  and follow the `BLYT_` prefix convention (ADR-0059).
- Icon font uses neutral geometric shapes to avoid PlayStation trade dress
  concerns. Abstract prompt API resolves `{prompt_action_a}` to the right
  icon for the current input device automatically.
- All default assets are public domain or permissively licensed; no legal
  encumbrance to redistribution. Licensing documented explicitly.

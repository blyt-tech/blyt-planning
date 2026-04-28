# ADR-0072: BMFont as internal font format; TTF/OTF as input-only

## Status
Accepted

## Context

Text rendering requires a font format in the runtime. Options include FreeType
(renders TTF/OTF at runtime — flexible, large dependency), pre-rasterized
bitmap fonts (simple, fast, no runtime rasterizer), or a combination (accept
TTF as input but pre-rasterize at pack time). The target hardware is resource-
constrained; FreeType is ~300KB and requires significant CPU for rasterization.

## Decision

**BMFont (`.fnt` format with sprite sheet) is the internal font format. The
runtime contains no TTF/OTF rasterizer.**

**What the packer accepts as font input:**
- PNG sprite grid (fixed-width bitmap font, simplest format).
- BMFont `.fnt` + sprite sheet (variable-width, kerning, multiple sizes).
- TTF/OTF files (packer rasterizes to BMFont at build time at specified sizes).
- Aseprite files (for hand-drawn pixel fonts).

**What the packer produces:**
- BMFont-format binary embedded in the cart resource.

**What the runtime uses:**
- BMFont binary exclusively. All input formats are converted to BMFont by the
  packer.

**Charset:** defaults to ASCII (95 printable characters). Additional codepoints
for locale support are specified in `cart.config`:

```lua
font_charsets = {
  latin_extended = true,
  cyrillic = true,
  japanese_kana = true,  -- hiragana + katakana
  -- cjk_unified = true  -- large; only include if needed
}
```

**Variable-width fonts:** fully supported via BMFont's per-glyph advance and
kerning tables.

## Consequences

- Zero runtime rasterization cost: all glyph rendering is blitting pre-
  rasterized sprites.
- No FreeType dependency: smaller runtime binary (~300KB saved), no license
  complications.
- Authors use their preferred font tools (Glyph Designer, Hiero, BMFont
  app, or the packer's built-in TTF converter) without changing the runtime.
- The charset selection keeps Asian-language fonts manageable (Hiragana +
  Katakana is ~200 glyphs; full CJK is ~7000 glyphs and a large resource).
- TTF/OTF inputs are rasterized at the sizes declared in `cart.config`;
  each size is a separate font resource.

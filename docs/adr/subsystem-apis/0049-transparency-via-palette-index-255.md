# ADR-0049: Transparency via palette index 255 convention

## Status
Accepted

## Context

Paletted 256-color graphics need a way to express transparency in sprites and
other blitted graphics. Options include: a per-call transparent color argument
(flexible, verbose), a per-image transparent color stored in the image (clean
but requires metadata), or a fixed convention for the transparent index
(simple, predictable).

## Decision

**Palette index 255 is the transparency sentinel by packer convention.**

The packer sets index 255 as transparent in any source image that has
transparent pixels. Cart authors never need to pass a transparency argument
to blit calls; the transparent color is encoded in the image data itself.

`blyt_image_blit()` skips pixels with index 255 by default (they are not
written to the framebuffer). To suppress the check and write all pixels
including index 255, pass the `BLYT_BLIT_OPAQUE` flag:

```c
blyt_image_blit(img, x, y, 0);             // transparent blit (skip index 255)
blyt_image_blit(img, x, y, BLYT_BLIT_OPAQUE); // opaque blit (write all pixels)
```

Index 255 may be used for any visible color in the palette for full-screen or
opaque-only images; it is only treated as transparent during blit operations
that do not pass `BLYT_BLIT_OPAQUE`.

## Consequences

- No per-call transparent color argument — blit calls are shorter and
  the transparency convention is uniform across the entire codebase.
- The packer is the single point that encodes transparency; authors work with
  normal transparent PNGs and do not manage palette assignments manually.
- `BLYT_BLIT_OPAQUE` is a performance optimization for sprites that are known
  to have no transparent pixels (skips the index-255 check in the inner loop).
- Carts that need a different transparent color (e.g., they use index 255 as
  a visible color) must ensure they use `BLYT_BLIT_OPAQUE` or remap their
  palettes. This is an edge case; the convention is chosen to match common
  practice in paletted graphics tools.

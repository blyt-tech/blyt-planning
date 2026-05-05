# ADR-0069: Text rendering via params struct

## Status
Accepted

## Context

Text rendering has many optional parameters: font, color, wrap width, border
style, alignment, typewriter tracking, repositioning after wrap. Variadic
argument lists and long positional argument lists are error-prone. A struct
(or Lua table) collects all options and allows sane defaults.

Two further problems show up once carts try to build dialog boxes and speech
bubbles:

- **Background fill.** A speech bubble wants a filled rectangle behind the
  glyphs. Drawing the rectangle separately requires the cart to know the
  rendered text's box.
- **Measurement.** Wrap, line breaks, and `auto_reposition` mean the cart
  cannot compute the rendered box from the raw string. Without runtime
  measurement, carts re-implement the layout the runtime is about to do.

## Decision

**Text rendering is controlled by `blyt_text_params_t`, a struct with sensible
defaults; most fields are optional. `blyt_text_draw` reports the rendered box
through a `blyt_text_result_t` out-param, and `blyt_text_measure` computes
the same box without drawing.**

```c
typedef struct {
    blyt_font_h   font;             // default: BLYT_FONT_BUILTIN
    uint8_t       color;            // palette index; default: 7 (white)
    uint8_t       border_color;     // default: 0 (black)
    uint8_t       background_color; // default: 255 = no fill (ADR-0049)
    uint32_t      border;           // BLYT_TEXT_BORDER_NONE / _4 / _8 / _SHADOW
    int32_t       wrap_width;       // 0 = no wrap
    int32_t       padding;          // pixels between text and background fill
    int32_t       max_chars;        // 0 = all; >0 = typewriter truncation
    bool          auto_reposition;  // advance x/y to end of text for chaining
} blyt_text_params_t;

typedef struct {
    int32_t   x, y;          // top-left of the rendered box (post-reposition)
    int32_t   w, h;          // box dimensions, including padding and border
    uint32_t  lines_drawn;
    uint32_t  chars_drawn;   // chars actually drawn (typewriter / pagination)
} blyt_text_result_t;

// NULL params = all defaults; NULL result = caller doesn't need the box.
blyt_result_t blyt_text_draw(int32_t x, int32_t y, const char *str,
                             const blyt_text_params_t *params,
                             blyt_text_result_t *result);

// Same layout pipeline as blyt_text_draw, no rasterisation. Cheap.
blyt_result_t blyt_text_measure(int32_t x, int32_t y, const char *str,
                                const blyt_text_params_t *params,
                                blyt_text_result_t *result);
```

**Border styles:**
- `BLYT_TEXT_BORDER_NONE`: plain text.
- `BLYT_TEXT_BORDER_4`: 4-directional outline (up, down, left, right).
- `BLYT_TEXT_BORDER_8`: 8-directional outline (corners included).
- `BLYT_TEXT_BORDER_SHADOW`: drop shadow (down-right only).

**Background fill.** When `background_color != 255`, the runtime fills the
rendered box (text bounds plus `padding`, minus any border outset) before
drawing the glyphs. The fill is the inner rectangle of the box reported in
`result`. Setting `background_color = 255` (the transparency sentinel from
ADR-0049) means "no fill" — the common case for in-world text. A
non-transparent fill is the one-call form of the dialog/speech-bubble
pattern; carts that want shaped frames or 9-slice sprites still draw their
own backing and pass `background_color = 255`.

**Word wrap:** breaks on space characters only. Hyphenation is not supported
in v1.

**Typewriter effect:** set `max_chars` to the number of characters to display;
increment each frame. `result->chars_drawn` receives the actual count drawn,
useful when wrap causes fewer glyphs than `max_chars` to fit on screen.

**`auto_reposition`:** after drawing, the `x` and `y` passed in are updated
to the position immediately after the last character, enabling chaining of
styled text segments. The `result` reports the final box.

**Measurement:** `blyt_text_measure` runs the same layout pipeline as
`blyt_text_draw` and writes the resulting box into `result`. It is
typewriter-aware (`max_chars` clamps the measured box) so cursor placement
and incremental backings match the drawn output exactly. Measurement is
intended to be cheap enough to call every frame for typewriter-driven
backings.

In Lua, params are an optional table and the result is returned:

```lua
local r = blyt32.text.draw(x, y, "Hello", {
    font = BLYT_FONT_SMALL,
    border = BLYT_TEXT_BORDER_SHADOW,
    wrap_width = 200,
})
-- r.x, r.y, r.w, r.h, r.lines_drawn, r.chars_drawn

local m = blyt32.text.measure(x, y, "Hello", { wrap_width = 200 })
```

## Consequences

- A single `blyt_text_draw()` entry point handles all text rendering cases.
- No global text state (font, color, etc.) — every call is self-contained.
- The params struct is forward-compatible: new fields can be added with
  zero-default semantics without breaking existing call sites.
- Typewriter effect is zero-overhead when `max_chars = 0`.
- The result struct gives carts the rendered box, eliminating the need to
  re-implement layout for speech-bubble tails, cursor positioning, or
  pagination boundaries.
- `background_color` covers the common dialog case in one call. Carts that
  want shaped frames (rounded bubble, 9-slice border, sprite frame) draw
  their own backing using the box from `blyt_text_measure`, then call
  `blyt_text_draw` with `background_color = 255`.
- Reusing palette index 255 as the "no fill" sentinel keeps the convention
  consistent with ADR-0049's transparency rule — carts that already treat
  255 as transparent for blits get the same semantics here.
- `blyt_text_measure` lets carts size containers, scrollbars, and animated
  reveals without rasterising; the runtime owns the layout pipeline.

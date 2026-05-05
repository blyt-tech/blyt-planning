# ADR-0069: Text rendering via params struct

## Status
Accepted

## Context

Text rendering has many optional parameters: font, color, wrap width, border
style, alignment, typewriter tracking, repositioning after wrap. Variadic
argument lists and long positional argument lists are error-prone. A struct
(or Lua table) collects all options and allows sane defaults.

## Decision

**Text rendering is controlled by `blyt_text_params_t`, a struct with sensible
defaults; most fields are optional.**

```c
typedef struct {
    blyt_font_h   font;          // default: BLYT_FONT_BUILTIN
    uint8_t     color;         // palette index; default: 7 (white)
    uint8_t     border_color;  // default: 0 (black)
    int32_t     wrap_width;    // 0 = no wrap
    uint32_t    border;        // BLYT_TEXT_BORDER_NONE / _4 / _8 / _SHADOW
    int32_t     max_chars;     // 0 = all; >0 = typewriter truncation
    int32_t    *chars_drawn;   // out: chars actually drawn (typewriter support)
    bool        auto_reposition; // advance x/y to end of text for chaining
} blyt_text_params_t;

blyt_result_t blyt_text_draw(int32_t x, int32_t y, const char *str,
                          const blyt_text_params_t *params); // NULL = all defaults
```

**Border styles:**
- `BLYT_TEXT_BORDER_NONE`: plain text.
- `BLYT_TEXT_BORDER_4`: 4-directional outline (up, down, left, right).
- `BLYT_TEXT_BORDER_8`: 8-directional outline (corners included).
- `BLYT_TEXT_BORDER_SHADOW`: drop shadow (down-right only).

**Word wrap:** breaks on space characters only. Hyphenation is not supported
in v1.

**Typewriter effect:** set `max_chars` to the number of characters to display;
increment each frame. `chars_drawn` (if non-null) receives the actual count
drawn, useful when wrap causes fewer glyphs than `max_chars` to fit on screen.

**`auto_reposition`:** after drawing, the `x` and `y` passed in are updated
to the position immediately after the last character, enabling chaining of
styled text segments.

In Lua, params are an optional table:

```lua
blyt32.text.draw(x, y, "Hello", {
    font = BLYT_FONT_SMALL,
    border = BLYT_TEXT_BORDER_SHADOW,
    wrap_width = 200,
})
```

## Consequences

- A single `blyt_text_draw()` entry point handles all text rendering cases.
- No global text state (font, color, etc.) — every call is self-contained.
- The params struct is forward-compatible: new fields can be added with
  zero-default semantics without breaking existing call sites.
- Typewriter effect is zero-overhead when `max_chars = 0`.

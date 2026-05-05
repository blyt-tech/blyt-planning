# ADR-0071: Easing library — no runtime tween manager

## Status
Accepted

## Context

Smooth animations between values require easing functions (ease-in, ease-out,
bounce, elastic, etc.). These can be provided as: (a) a tween manager that
the cart registers animations with and that the runtime advances each frame,
or (b) a stateless function library that the cart calls to compute eased
values. A tween manager is convenient but adds state that must survive
save/restore; a function library requires the cart to manage timing but has
no runtime-owned state.

## Decision

**The runtime provides a stateless easing function library. There is no
runtime tween manager.**

```c
typedef uint32_t blyt_ease_t;
// BLYT_EASE_LINEAR, BLYT_EASE_IN_QUAD, BLYT_EASE_OUT_QUAD, BLYT_EASE_INOUT_QUAD,
// BLYT_EASE_IN_CUBIC, BLYT_EASE_OUT_CUBIC, BLYT_EASE_INOUT_CUBIC,
// BLYT_EASE_IN_SINE, BLYT_EASE_OUT_SINE, BLYT_EASE_INOUT_SINE,
// BLYT_EASE_IN_EXPO, BLYT_EASE_OUT_EXPO, BLYT_EASE_INOUT_EXPO,
// BLYT_EASE_IN_BOUNCE, BLYT_EASE_OUT_BOUNCE, BLYT_EASE_INOUT_BOUNCE,
// BLYT_EASE_IN_ELASTIC, BLYT_EASE_OUT_ELASTIC, BLYT_EASE_INOUT_ELASTIC,
// BLYT_EASE_IN_BACK, BLYT_EASE_OUT_BACK, BLYT_EASE_INOUT_BACK

// t in [0.0, 1.0] → eased value in [0.0, 1.0]
float blyt_ease(blyt_ease_t type, float t);

// Interpolate between a and b using easing
float blyt_ease_lerp(blyt_ease_t type, float a, float b, float t);
int32_t blyt_ease_lerp_i(blyt_ease_t type, int32_t a, int32_t b, float t);
```

The cart stores tween progress (`t`) in its own state buffers and advances it
each frame. This is explicit and integrates naturally with save/restore: tween
progress is cart state, not runtime state.

**Optional helper:** the SDK ships `blyt_tween.lua`, an optional Lua module
that wraps the easing functions with a tween object API for authors who prefer
it. This module is not part of the runtime; it is cart-side Lua code that the
cart includes if it wants tween-object ergonomics.

## Consequences

- No runtime-owned tween state to serialize; save/restore is unaffected.
- Cart authors manage tween progress in their state buffers, which is the
  correct place for it.
- `blyt_tween.lua` provides tween-object ergonomics for Lua authors who want
  them without imposing runtime complexity.
- The easing function library covers all common CSS easing types; authors
  are unlikely to need custom easing in practice.

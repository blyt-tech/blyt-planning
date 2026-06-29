# ADR-0046: Core API conventions — handles, errors, naming, headers, flags

## Status
Accepted

> **Forward note (2026-06-29):** the `img:blit(x, y)` example (image as the
> method receiver) is **superseded** by `gfx.blit(img, x, y)` — canvas-is-the-
> receiver — per spec **#195**. Read #195 before following the blit shape here;
> this ADR is not yet amended.

## Context

The C API surface (`blyt32.h` and `blyt_runtime.h`) covers a large number of
subsystems. Consistent conventions across the entire surface reduce cognitive
overhead for cart authors and frontend integrators, and make the API easier
to document, audit, and extend.

## Decision

**Four core conventions apply uniformly across the entire API.**

### 1. Opaque uint32_t handles

All runtime objects are opaque `uint32_t` handles (typedefd as
`blyt_image_h`, `blyt_voice_h`, `blyt_buffer_h`, etc.). Zero is always the
invalid/null sentinel. Handles are cheap to copy and compare. The runtime
owns the backing storage; carts never see or manipulate it directly.

```c
typedef uint32_t blyt_image_h;
typedef uint32_t blyt_voice_h;
typedef uint32_t blyt_buffer_h;
// BLYT_INVALID_HANDLE = 0
```

### 2. blyt_result_t error returns with blyt_last_error()

Every function that can fail returns `blyt_result_t` (a typedef'd int32_t).
Zero is `BLYT_OK`. Negative values are error codes.
Out-values are passed as pointer parameters:

```c
blyt_result_t blyt_image_load(blyt_resource_h res, blyt_image_h *out_image);

// Usage:
blyt_image_h img;
if (blyt_image_load(R_HERO, &img) != BLYT_OK) {
    // blyt_last_error() returns a descriptive string
}
```

`blyt_last_error()` always returns the last error string, even after a
successful call resets it. Debug builds include file/line in error strings.

### 3. Naming convention: blyt_<subsystem>_<verb>[_<qualifier>]

```c
blyt_image_load()       // image subsystem, load verb
blyt_image_blit()       // image subsystem, blit verb
blyt_voice_play()       // voice subsystem, play verb
blyt_voice_is_playing() // voice subsystem, query
```

Constants follow `BLYT_<SUBSYSTEM>_<NAME>`. Handles follow `blyt_<noun>_h`.

### 4. Two-header split: blyt32.h / blyt_runtime.h

- `blyt32.h`: the cart-facing API — everything a cart calls. Used by cart
  code and by the runtime when compiling the Lua shim.
- `blyt_runtime.h`: the frontend-facing API — `blyt_runtime_create/destroy`,
  `blyt_runtime_update/draw`, `blyt_runtime_get_framebuffer`, audio buffer
  callbacks, input poll callbacks. Cart code never includes this header.

### 5. Bitmask flags in C; option tables in Lua

C API functions accept `uint32_t flags` parameters for bitmask options:

```c
blyt_image_blit(img, x, y, BLYT_BLIT_FLIP_H | BLYT_BLIT_OPAQUE);
```

Lua API functions accept an optional options table instead:

```lua
img:blit(x, y, { flip_h = true, opaque = true })
```

## Consequences

- The entire API is auditable by searching for `blyt_` or `BLYT_` prefixes.
- Out-parameter conventions are consistent; callers never need to guess
  whether a return value or an out-pointer carries the result.
- The two-header split prevents cart code from accidentally calling
  frontend-only functions.
- Bitmask flags are fast and compact; the Lua option table variant requires
  no knowledge of bitmask mechanics from Lua authors.

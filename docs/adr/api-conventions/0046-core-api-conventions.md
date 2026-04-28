# ADR-0046: Core API conventions — handles, errors, naming, headers, flags

## Status
Accepted

## Context

The C API surface (`fc_cart.h` and `fc_runtime.h`) covers a large number of
subsystems. Consistent conventions across the entire surface reduce cognitive
overhead for cart authors and frontend integrators, and make the API easier
to document, audit, and extend.

## Decision

**Four core conventions apply uniformly across the entire API.**

### 1. Opaque uint32_t handles

All runtime objects are opaque `uint32_t` handles (typedefd as
`fc_image_h`, `fc_voice_h`, `fc_buffer_h`, etc.). Zero is always the
invalid/null sentinel. Handles are cheap to copy and compare. The runtime
owns the backing storage; carts never see or manipulate it directly.

```c
typedef uint32_t fc_image_h;
typedef uint32_t fc_voice_h;
typedef uint32_t fc_buffer_h;
// FC_INVALID_HANDLE = 0
```

### 2. fc_result_t error returns with fc_last_error()

Every function that can fail returns `fc_result_t` (a typedef'd int32_t).
Zero is `FC_OK`. Negative values are error codes.
Out-values are passed as pointer parameters:

```c
fc_result_t fc_image_load(fc_resource_h res, fc_image_h *out_image);

// Usage:
fc_image_h img;
if (fc_image_load(R_HERO, &img) != FC_OK) {
    // fc_last_error() returns a descriptive string
}
```

`fc_last_error()` always returns the last error string, even after a
successful call resets it. Debug builds include file/line in error strings.

### 3. Naming convention: fc_<subsystem>_<verb>[_<qualifier>]

```c
fc_image_load()       // image subsystem, load verb
fc_image_blit()       // image subsystem, blit verb
fc_voice_play()       // voice subsystem, play verb
fc_voice_is_playing() // voice subsystem, query
```

Constants follow `FC_<SUBSYSTEM>_<NAME>`. Handles follow `fc_<noun>_h`.

### 4. Two-header split: fc_cart.h / fc_runtime.h

- `fc_cart.h`: the cart-facing API — everything a cart calls. Used by cart
  code and by the runtime when compiling the Lua shim.
- `fc_runtime.h`: the frontend-facing API — `fc_runtime_create/destroy`,
  `fc_runtime_update/draw`, `fc_runtime_get_framebuffer`, audio buffer
  callbacks, input poll callbacks. Cart code never includes this header.

### 5. Bitmask flags in C; option tables in Lua

C API functions accept `uint32_t flags` parameters for bitmask options:

```c
fc_image_blit(img, x, y, FC_BLIT_FLIP_H | FC_BLIT_OPAQUE);
```

Lua API functions accept an optional options table instead:

```lua
img:blit(x, y, { flip_h = true, opaque = true })
```

## Consequences

- The entire API is auditable by searching for `fc_` or `FC_` prefixes.
- Out-parameter conventions are consistent; callers never need to guess
  whether a return value or an out-pointer carries the result.
- The two-header split prevents cart code from accidentally calling
  frontend-only functions.
- Bitmask flags are fast and compact; the Lua option table variant requires
  no knowledge of bitmask mechanics from Lua authors.

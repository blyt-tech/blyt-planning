# ADR-0040: Resource addressing — names in source, compile-time constants at build, IDs at runtime

## Status
Accepted

## Context

Resources (sprites, tilemaps, audio, fonts) need to be referenced from cart
code. Using raw integer IDs in source is fast but opaque and error-prone.
Using string names everywhere is ergonomic but adds hash lookup overhead on
every access in a hot loop. Debug builds need names for error messages.

The state buffer decision (ADR-0009) established the pattern of packer-
generated compile-time constants for buffer and field handles. The same
pattern applies to resource handles.

## Decision

**Names in source (in the manifest), packer-generated compile-time constants
for access, IDs at runtime.**

Resource names are declared in `cart.config`. The packer generates a header
(for native carts) or Lua module (for Lua carts) containing compile-time
constants of type `fc_resource_h`:

```c
// Generated: cart_resources.h
#define R_HERO_SPRITES   ((fc_resource_h)1)
#define R_TILEMAP_WORLD  ((fc_resource_h)2)
#define R_THEME_MUSIC    ((fc_resource_h)3)
```

```lua
-- Generated: cart_resources.lua
local R = require("cart_resources")
R.HERO_SPRITES   -- integer constant
R.TILEMAP_WORLD
R.THEME_MUSIC
```

Built-in runtime resources (`FC_FONT_BUILTIN`, `FC_PALETTE_DEFAULT`, etc.)
set bit 31 to guarantee no collision with cart-generated IDs; see ADR-0059
for the full convention.

Resource IDs are stable within a development session but not guaranteed
across packer rebuilds that add or remove resources. Carts must not
serialize resource IDs to disk (use names for persistence, or load by
constant at init).

Generated files are gitignored; the packer regenerates them on every build.
The packer enforces that all declared resource names resolve to actual files
in the project directory — a class of runtime errors becomes a build-time
error.

## Consequences

- Authoring ergonomics: resource names are readable and refactorable in the
  manifest; access in cart code uses named constants, not magic integers.
- Runtime speed: ID-based lookup after the initial load call is O(1) with no
  string comparison in hot paths.
- Packer validates all resource references at build time.
- Debug error messages use the debug name table (preserved in debug builds,
  stripped in release); release error messages reference IDs, which is
  acceptable since debug builds are used during development.
- The generated-header pattern is consistent with state buffer field constants
  (ADR-0009), RNG stream constants, localisation key constants, and
  palette cycle constants — one authoring mental model for all packer-
  generated access.
- Matches commercial engine convention (Unity asset GUIDs, Unreal FNames).

# ADR-0009: State buffers are manifest-declared with packer-generated constants

## Status
Accepted

## Context

The initial design (ADR-0010) specified that carts allocate state buffers
at runtime by calling `alloc_state(layout, count)`, passing layout
descriptions constructed in cart code. This is flexible but has drawbacks:
layout definitions are duplicated between cart code and packer/tooling;
field access requires string lookups or manual offset arithmetic; and there
is no single source of truth for the cart's memory footprint.

During API design, the authoring model was reconsidered. The packer already
reads the manifest to build the cart artifact; having the manifest also
declare state buffer schemas lets the packer generate typed access constants
at build time and gives the runtime full knowledge of the memory layout
before the cart runs.

## Decision

**State buffers are declared in `cart.config.yaml` (the manifest), not
allocated dynamically in cart code.** The packer generates a header (for
native carts) or a Lua module with compile-time constants for field access.

```yaml
# cart.config.yaml — excerpt
state_buffers:
  players: { type: Player, count: 4 }
  enemies: { type: Enemy,  count: 128 }
```

The packer generates constants of type `blyt_field_h` (an integer typedef):

```c
// Generated: cart_state.h
#define S_PLAYERS  ((blyt_buffer_h)1)
#define S_ENEMIES  ((blyt_buffer_h)2)

// Field handles (blyt_field_h = uint32_t)
#define S_PLAYER_X      ((blyt_field_h)0x00010001)
#define S_PLAYER_Y      ((blyt_field_h)0x00010002)
#define S_ENEMY_HP      ((blyt_field_h)0x00020001)
// ...
```

Field access uses these compile-time constants rather than string lookups:

```c
blyt_buffer_set_f32(S_PLAYERS, slot, S_PLAYER_X, x);
float hp = blyt_buffer_get_f32(S_ENEMIES, slot, S_ENEMY_HP);
```

In Lua, the same constants are integers; the SOA metatable sugar
(ADR-0011) wraps them for ergonomic access.

`BLYT_FIELD_NONE` (zero) is the sentinel for an invalid or missing field.

The runtime validates that the declared layouts match the compiled-in
schema at cart load time.

## Consequences

- The manifest is the single source of truth for state buffer schemas; no
  layout duplication between cart code and tooling.
- Field access is a compile-time constant integer lookup — zero overhead
  beyond the API call itself.
- The packer can statically compute the cart's total state memory footprint
  and enforce size-class caps before the cart runs.
- Hot reload layout migration (ADR-0045) operates on manifest-declared
  schemas, making schema diffing straightforward.
- ADR-0010 (POD typed state buffers) is updated to reflect the
  manifest-declaration model. The `alloc_state(layout, count)` dynamic
  API is not present in v1.

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

## Amendment — record shapes, inline embedding, and cross-buffer references

### Terminology: records

The unit of schema declaration is a **record** — a named, flat collection
of typed fields. Records replace the earlier informal use of "type" and
"layout" for this concept.

### `records:` manifest section

Record shapes are declared in a top-level `records:` section, separate from
the `state_buffers:` section that declares the named buffer pools. This
allows a record shape to be referenced from multiple buffers, used as an
inline embedded type, or used for transient heap-allocated structs that
never appear in a state buffer.

```yaml
records:
  Vec2:
    fields:
      - { name: x, type: f32 }
      - { name: y, type: f32 }

  Player:
    fields:
      - { name: pos,    type: Vec2 }      # inline embed — see below
      - { name: hp,     type: u8   }
      - { name: max_hp, type: u8   }
      - { name: weapon, ref: weapons }    # cross-buffer reference — see below

  Enemy:
    fields:
      - { name: pos,  type: Vec2 }
      - { name: hp,   type: u16  }
      - { name: kind, type: u8   }

state_buffers:
  players: { record: Player, count: 4   }
  enemies: { record: Enemy,  count: 128 }
  weapons: { record: Weapon, count: 64  }
```

The `state_buffers:` key changes from `type:` to `record:` to match the
new vocabulary.

The packer generates a C struct (`blyt_player_t`, `blyt_enemy_t`, etc.) for
every declared record, regardless of whether it appears in a `state_buffers:`
entry. Records used only for transient heap objects still get packer-generated
structs and field constants.

### Inline embedding (`type: RecordName`)

When a field's `type:` names a record rather than a primitive, the record's
fields are laid out inline inside the containing record. The serialised
representation is identical to listing all primitive fields directly —
inline embedding is purely a manifest-authoring convenience.

```yaml
records:
  Vec2:
    fields:
      - { name: x, type: f32 }
      - { name: y, type: f32 }
  Player:
    fields:
      - { name: pos, type: Vec2 }   # expands to pos.x (f32) + pos.y (f32) inline
      - { name: hp,  type: u8  }
```

Generated C:

```c
typedef struct { float x, y; } blyt_vec2_t;
typedef struct { blyt_vec2_t pos; uint8_t hp; } blyt_player_t;
```

**Cycle detection.** The packer rejects cyclic type references (e.g. a
record that directly or transitively embeds itself) with a build error.
Cycles are detected via topological sort at pack time.

### Cross-buffer references (`ref: buffer_name`)

When a field uses `ref:` instead of `type:`, it stores a
`blyt_entity_ref_t` — a packed `u32` containing a slot index and generation
counter (ADR-0096). The target is a buffer name, not a record type name,
because multiple buffers can share the same record type and the reference
must be unambiguous.

```yaml
records:
  Player:
    fields:
      - { name: weapon, ref: weapons }   # blyt_entity_ref_t into the weapons buffer
```

- **Null reference:** `BLYT_ENTITY_REF_NONE` (0). Zero-initialisation of
  state buffers before `init` (ADR-0087) means all `ref` fields start as
  null without any explicit initialisation.
- **Staleness detection:** the generation counter catches stale references
  after the target slot is freed and reallocated (ADR-0096).
- **Packer enforcement:** a `ref:` field targeting a buffer that declares
  `generations: false` is a build error. Cross-buffer references require
  generation tracking to provide a well-defined null and staleness
  detection.

The distinction between `type:` (inline embed) and `ref:` (index reference)
is explicit in the YAML key, making the two very different semantics
impossible to confuse.

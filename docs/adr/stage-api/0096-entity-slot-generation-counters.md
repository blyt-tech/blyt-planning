# ADR-0096: Entity slot generation counters

## Status
Accepted

## Context

State buffers (ADR-0058) use slot indices for entity identity. When an
entity is destroyed, its slot is freed and may be reused by a newly spawned
entity. Any code that stored the old slot index now holds a stale reference
— but there is no way to detect this, because the slot index itself is
unchanged. The new entity silently receives operations intended for the
destroyed one.

Examples of stale references in practice:

- A camera target field holding the slot of an enemy that has died and been
  replaced by a different enemy in the same slot.
- An event handler receiving a slot reference and operating on the wrong
  entity because the original entity died during the frame.
- An AI entity holding a `target` slot field pointing to a player who
  disconnected and was replaced in the same slot.

The standard solution is a generation counter: each slot carries a
monotonically increasing generation value. References store both the slot
index and the generation at the time they were created. A reference is valid
only if the stored generation matches the slot's current generation.

## Decision

**Each state buffer slot carries a `u16` generation counter. References to
entities are stored as a packed `u32` containing the slot index (low 16 bits)
and the generation (high 16 bits). Stage provides functions for creating
references and checking their validity.**

### Buffer storage

Generation counters are stored in a compact parallel array alongside the
slot's POD data. The packer allocates space for them automatically for any
buffer that opts in:

```yaml
# cart.config.yaml
state_buffers:
  enemies: { type: Enemy, count: 128, generations: true }
  players: { type: Player, count: 4,  generations: true }
```

`generations: true` adds 2 bytes per slot (128 slots = 256 bytes overhead).
Buffers that never store inter-slot references (e.g. a particle pool) can
omit this to save memory.

When `fc_buffer_free_slot` is called, the slot's generation counter is
incremented. Generation wraps at 65535 back to 1 (never 0, which is
reserved as the invalid sentinel).

### Reference type

```c
typedef uint32_t fc_entity_ref_t;
// FC_ENTITY_REF_NONE = 0  (invalid sentinel)
// encoding: high 16 bits = generation, low 16 bits = slot
```

### C API

```c
// Create a reference to the entity currently occupying this slot
fc_entity_ref_t stage_entity_ref(fc_buffer_h buf, int32_t slot);

// Check if a reference still points to the same entity
bool stage_entity_ref_valid(fc_buffer_h buf, fc_entity_ref_t ref);

// Extract slot index from a reference (check validity first)
int32_t stage_entity_ref_slot(fc_entity_ref_t ref);
```

Usage:

```c
// Store a reference when the target is acquired
ai.target_ref = stage_entity_ref(S_PLAYERS, player_slot);

// Use the reference each frame
if (stage_entity_ref_valid(S_PLAYERS, ai.target_ref)) {
    int32_t tgt = stage_entity_ref_slot(ai.target_ref);
    // use tgt safely
} else {
    ai.target_ref = FC_ENTITY_REF_NONE;  // target gone; clear ref
    ai.state = AI_PATROL;
}
```

### Lua API

```lua
-- Create reference
local ref = stage.entity.ref(players, slot)

-- Check and use
if stage.entity.ref_valid(players, ref) then
    local tgt = stage.entity.ref_slot(ref)
    -- use tgt
else
    enemies.target_ref[slot] = 0  -- FC_ENTITY_REF_NONE
    enemies.ai_state[slot] = AI_PATROL
end
```

### Storage in buffer fields

`fc_entity_ref_t` is a `u32` and can be stored directly in a buffer field:

```yaml
types:
  Enemy:
    x:          f32
    y:          f32
    ai_state:   u8
    target_ref: u32   # fc_entity_ref_t — slot + generation packed
```

Because it is a POD u32 field, it serializes automatically with the buffer.

### Generation counter persistence

Generation counters are part of the buffer's tracked region and are included
in save/restore. A reference saved to disk and restored later is still valid
if the target entity was not destroyed in the intervening time.

## Consequences

- Stale references are detectable at any call site that holds an
  `fc_entity_ref_t`. The pattern "check validity, then use slot" replaces
  the silent "use slot, get wrong entity" failure mode.
- The `u32` encoding fits in a single buffer field with no metadata overhead
  at the point of storage.
- Wrapping at 65535 means a slot freed and reallocated 65535 times may
  produce a generation collision. At game-scale spawn rates this is
  effectively impossible during a single session. Save files persisted over
  long play sessions could theoretically hit this; for v1 it is an
  acceptable risk.
- Buffers that never hold inter-slot references can omit `generations: true`
  to save 2 bytes per slot.
- The counter is maintained by `fc_buffer_free_slot` — no cart code is
  required to increment it.

# ADR-0096: Entity slot generation counters

## Status
Accepted — implemented in the core buffer API (see Amendment: implementation
notes)

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

When `blyt_buffer_free_slot` is called, the slot's generation counter is
incremented. Generation wraps at 65535 back to 1 (never 0, which is
reserved as the invalid sentinel).

### Reference type

```c
typedef uint32_t blyt_entity_ref_t;
// BLYT_ENTITY_REF_NONE = 0  (invalid sentinel)
// encoding: high 16 bits = generation, low 16 bits = slot
```

### C API

```c
// Create a reference to the entity currently occupying this slot
blyt_entity_ref_t stage_entity_ref(blyt_buffer_h buf, int32_t slot);

// Check if a reference still points to the same entity
bool stage_entity_ref_valid(blyt_buffer_h buf, blyt_entity_ref_t ref);

// Extract slot index from a reference (check validity first)
int32_t stage_entity_ref_slot(blyt_entity_ref_t ref);
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
    ai.target_ref = BLYT_ENTITY_REF_NONE;  // target gone; clear ref
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
    enemies.target_ref[slot] = 0  -- BLYT_ENTITY_REF_NONE
    enemies.ai_state[slot] = AI_PATROL
end
```

### Storage in buffer fields

`blyt_entity_ref_t` is a `u32` and can be stored directly in a buffer field:

```yaml
types:
  Enemy:
    x:          f32
    y:          f32
    ai_state:   u8
    target_ref: u32   # blyt_entity_ref_t — slot + generation packed
```

Because it is a POD u32 field, it serializes automatically with the buffer.

### Generation counter persistence

Generation counters are part of the buffer's tracked region and are included
in save/restore. A reference saved to disk and restored later is still valid
if the target entity was not destroyed in the intervening time.

## Amendment — generations on by default; ref fields require generations

### Default changed to opt-out

Generation counters are enabled by default for all state buffers. Buffers
that genuinely never hold or are referenced by cross-buffer `ref:` fields
may opt out to save 2 bytes per slot:

```yaml
state_buffers:
  enemies:   { record: Enemy,    count: 128  }               # generations on (default)
  players:   { record: Player,   count: 4    }               # generations on (default)
  particles: { record: Particle, count: 1000, generations: false }  # opt out
```

`generations: false` is appropriate for ephemeral pools (particles, audio
voices) where nothing holds a reference to a specific slot and staleness
detection is meaningless by design. For any buffer that game entities may
reference across frames, the default (on) is correct.

### `ref:` fields require generations

A manifest `ref:` field (ADR-0009) stores a `blyt_entity_ref_t` into a
named buffer. The packer enforces that the target buffer must have
generations enabled — declaring `ref: some_buffer` where `some_buffer` has
`generations: false` is a build error. This guarantees that:

- `BLYT_ENTITY_REF_NONE` (0) is always a valid null sentinel for `ref:`
  fields (generation 0 is reserved as invalid).
- Staleness of a stored reference is always detectable via
  `stage_entity_ref_valid`.

## Amendment — implementation notes (2026-06)

Generation counters and entity refs landed ahead of the stage API, in the
**core buffer API** rather than the `stage` namespace:

- C: `blyt_buffer_ref(buf, slot)`, `blyt_buffer_ref_valid(buf, ref)`, and
  `blyt_buffer_ref_slot(ref)` (a `static inline` in `blyt.h` — pure bit
  math, no ECALL/import) replace the specced `stage_entity_ref*` functions.
  Lua: `blyt.buf.ref / ref_valid / ref_slot`. Rust SDK: `entity_ref`,
  `ref_valid`, `ref_slot` (`ref` is a Rust keyword).
- Generations are **always on** for every buffer; the `generations:`
  opt-out (and the packer rule rejecting `ref:` into an opted-out buffer)
  is deferred — at current buffer scales the overhead (2 bytes/slot) is
  negligible, and the schema-less native bare-metal path stays simpler
  with no per-buffer flags.
- Generations initialize to 1 at cart start (never 0, preserving
  `BLYT_ENTITY_REF_NONE == 0` even for slot 0), bump only on *successful*
  `free_slot`, wrap 65535 → 1, and are tracked state: snapshot/restore,
  reset-every-frame, and both save formats round-trip them.
- `ref:` manifest fields (ADR-0009) are implemented as plain u32 (tag 5)
  on the wire; ref-ness lives in packer validation, generated-constant
  annotations, and the canonical schema-hash text (`name:ref<target>`).

## Consequences

- Stale references are detectable at any call site that holds an
  `blyt_entity_ref_t`. The pattern "check validity, then use slot" replaces
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
- The counter is maintained by `blyt_buffer_free_slot` — no cart code is
  required to increment it.

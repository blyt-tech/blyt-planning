# ADR-0101: Scene-scoped entity buffers and scene_id tagging

## Status
Accepted

## Context

ADR-0092 establishes scene lifecycle and stores scene identity in a 1-slot
world buffer, but it does not specify how individual entities are associated
with scenes. The current spec leaves this entirely to cart code: entity
buffers (ADR-0058) are cart-global; nothing in Stage answers questions like
"clear all gameplay entities when leaving the gameplay scene", "iterate only
the enemies in the active scene", or "keep the player across a scene
transition."

Three candidate designs were considered:

1. **Per-scene entity lists.** Each scene holds an array of entity refs,
   sized at pack time. Closest to Unity/Godot's scene-as-container model.
   Despawn becomes O(N) (must scan the list) or requires a back-pointer
   field on every entity. Adds a second capacity dimension per buffer per
   scene. Highest sync risk: refs in the list can go stale if anything
   frees a slot without updating the list.

2. **scene_id field on the entity.** Every scene-scoped row carries a `u8`
   tag. Despawn is free — the field disappears with the slot. One
   capacity dimension. No sync invariants. Iteration costs one byte load
   plus one branch per slot, which is negligible at blyt-scale entity
   counts and SOA-friendly.

3. **Join table.** A global `(scene_id, buffer_id, slot, generation)`
   tuple pool. Maximally flexible (multi-scene membership trivial) but
   most machinery and most sync risk; introduces a global capacity
   ceiling separate from any buffer's.

Design 2 is the natural fit for blyt: it composes with the existing pattern
of packer-assigned integer constants in POD fields (handlers, events, scene
ids), it has zero sync invariants, and despawn requires no extra work.
Multi-scene presence — the only thing Design 2 doesn't do natively — is not
a stated requirement and can be added later via a parallel mechanism if it
ever is.

## Decision

**Buffers opt into scene scoping with `scene_scoped: true` in the manifest.
The packer auto-injects a `scene_id: u8` field at a packer-managed location
and emits a stable handle for it. Stage uses the field to filter iteration,
drive cleanup, and tag spawns automatically.**

### Manifest opt-in

```yaml
state_buffers:
  enemies:    { type: Enemy,    count: 128, scene_scoped: true  }
  projectiles:{ type: Bullet,   count: 64,  scene_scoped: true  }
  particles:  { type: Particle, count: 256, scene_scoped: true  }
  player:     { type: Player,   count: 1,   scene_scoped: false }  # always SCENE_GLOBAL
  prefab_tbl: { type: PrefabRow,count: 32,  scene_scoped: false }  # pure data
```

The packer:

- Adds a `scene_id: u8` column to scene-scoped buffers — auto-injected, not
  declared in the type.
- Records the field handle so runtime code can read or write the field
  without knowing its position.
- Reserves the names `scene_id` and `SCENE_GLOBAL`. Cart-declared types may
  not shadow them.
- Validates that no `scene_scoped: false` buffer is named in any scene's
  `entities` list (see ADR-0102).

### Reserved values

`SCENE_GLOBAL` (numeric value 0) tags entities that survive scene
transitions: the player, persistent HUD elements, an audio listener,
anything explicitly cross-scene. Other scene IDs are 1..N matching the
constants in the packer-generated `cart_scenes.h` (ADR-0092).

### Storage and access

The injected field is an ordinary u8 column under SOA layout. Code accesses
it through a handle:

```c
blyt_field_h s = blyt_buffer_scene_field(S_ENEMIES);
uint8_t scene = blyt_buffer_get_u8(S_ENEMIES, slot, s);
```

A type-prefixed constant (`S_ENEMY_SCENE_ID`) is also generated for code that
prefers the explicit form. Field position is not part of the cart contract;
the handle is.

### Iteration helpers

Stage provides scene-aware iterators that filter on the field transparently:

```c
// Iterate slots in a specific scene; SCENE_GLOBAL slots are always visited.
blyt_iter_h it;
stage_buffer_iter_scene_begin(S_ENEMIES, SCENE_GAMEPLAY, &it);

// Iterate slots in the active iteration scope (= the running scene).
stage_buffer_iter_active_begin(S_ENEMIES, &it);

int32_t slot;
while (blyt_buffer_iter_next(it, &slot)) {
    // ...
}
```

Both iterators visit slots whose `scene_id` matches the requested scene OR
equals `SCENE_GLOBAL`.

When Stage calls a scene's `on_update`, it sets a thread-local iteration
scope to that scene's id. `stage_buffer_iter_active_begin` reads it. When
multiple stack frames update in the same frame (the top scene plus any
below it with `background_update: true`), each scene's update runs with
its own scope, so each scene's systems see only their own entities.

### Spawn integration

`stage_spawn` (ADR-0099) writes the `scene_id` field automatically using the
current iteration scope:

```c
// Inside scene_game_update, scope = SCENE_GAMEPLAY
int32_t slot = stage_spawn(S_ENEMIES, HANDLER_PREFAB_GRUNT, x, y);
// slot's scene_id is now SCENE_GAMEPLAY without the cart writing it.
```

Spawning into a different scope (e.g. `blyt_cart_init` creating the player as
`SCENE_GLOBAL`) uses the explicit form:

```c
int32_t slot = stage_spawn_into(S_PLAYER, SCENE_GLOBAL,
                                HANDLER_PREFAB_PLAYER, x, y);
```

For `scene_scoped: false` buffers, `stage_spawn` works unchanged; the field
simply doesn't exist on that buffer.

### Cleanup helpers

Scene-transition cleanup uses the same field:

```c
// Clear all slots in a single buffer that match this scene.
void stage_buffer_clear_scene(blyt_buffer_h buf, blyt_scene_h scene);

// Clear every scene-scoped buffer at once.
void stage_scene_clear(blyt_scene_h scene);
```

`stage_scene_clear` walks the packer's list of scene-scoped buffers and
frees every slot whose `scene_id` matches. The typical body of an `on_exit`
handler:

```c
void scene_game_exit(void) {
    stage_scene_clear(SCENE_GAMEPLAY);
}
```

`SCENE_GLOBAL` slots are never cleared by either helper.

### Lua API

```lua
local SCENE = require("cart_scenes")

-- Scene-scoped iteration
for slot in enemies:slots_in(SCENE.GAMEPLAY) do ... end
for slot in enemies:active_slots() do ... end

-- Spawn (scene_id auto-set from iteration scope)
local slot = stage.spawn(enemies, H.PREFAB_GRUNT, x, y)

-- Spawn into an explicit scope (e.g. for the player)
local slot = stage.spawn_into(player, SCENE.GLOBAL, H.PREFAB_PLAYER, 0, 0)

-- Cleanup
stage.scene.clear(SCENE.GAMEPLAY)
```

### Save state

The `scene_id` field is a POD u8 column and serializes with the buffer
(ADR-0058). A save state captures membership directly; on restore every
entity is in the scene it was in at capture time. No additional save/restore
handling required.

## Consequences

- Scene membership is one byte per entity. Lowest-overhead option of the
  three considered.
- Despawn is free — freeing a slot also frees the membership.
- Single source of truth (the field). No parallel scene-list to keep in
  sync; no class of bugs where the list and the buffer disagree.
- Field position is hidden behind a packer-emitted handle, so SOA/AOS
  layout choices remain free and blyt's existing pack-time-constants
  pattern is reused unchanged.
- Iteration costs one byte load and one branch per slot. Under SOA this is
  sequential memory access on a single column; cost is negligible at the
  entity counts blyt targets.
- An entity belongs to exactly one scene at a time, or `SCENE_GLOBAL`.
  Multi-scene presence is intentionally unsupported; carts that ever need
  it manage extra membership themselves.
- `stage_spawn` writing the tag automatically removes the most likely place
  a cart would forget to set it.
- `SCENE_GLOBAL` covers the persistent-entity case (player, HUD, audio
  listener) without a separate persistence mechanism.
- `scene_scoped: false` lets pure-data buffers (spawn points, prefab
  tables, runtime-internal scratchpads) skip the field cleanly.
- The packer reserves the names `scene_id` and `SCENE_GLOBAL`; cart code
  that tried to declare them previously must rename. This should be added
  to the reserved-identifier list in ADR-0090.

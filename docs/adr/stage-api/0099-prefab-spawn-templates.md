# ADR-0099: Prefab and entity spawn templates

## Status
Accepted

## Context

Spawning an entity from a buffer slot requires setting many field values to
their initial state: position, velocity, sprite ID, HP, AI state, and so on.
When this is done inline at every spawn site, the initialization logic is
duplicated and can drift. When a new field is added to the entity type, every
spawn site must be updated. A prefab is a named initialization recipe that
centralizes this.

Prefabs in Stage are implemented using named handler functions (ADR-0090):
a prefab is a handler that receives a freshly allocated slot and sets its
initial field values. The handler ID is the prefab's stable identity.

## Decision

**Prefabs are named handlers declared in `cart.config.yaml` that initialize
a freshly allocated entity slot. `stage_spawn` allocates a slot and calls the
prefab handler. Prefab handlers are pure: they receive a slot and set field
values; they do not post events or spawn additional entities.**

### Manifest

Prefabs are declared alongside other handlers:

```yaml
# cart.config.yaml
handlers:
  - prefab_grunt
  - prefab_archer
  - prefab_explosion
  - prefab_coin_pickup
```

The packer generates constants in the same `cart_handlers.h`:

```c
#define HANDLER_PREFAB_GRUNT        ((fc_handler_h)7)
#define HANDLER_PREFAB_ARCHER       ((fc_handler_h)8)
#define HANDLER_PREFAB_EXPLOSION    ((fc_handler_h)9)
#define HANDLER_PREFAB_COIN_PICKUP  ((fc_handler_h)10)
```

### C API

```c
// Allocate a slot and call the prefab handler.
// Returns the new slot index, or FC_INVALID_SLOT if the buffer is full.
int32_t stage_spawn(fc_buffer_h buf,
                    fc_handler_h prefab,
                    float x, float y);

// Prefab handler signature — called by stage_spawn:
// void my_prefab(int32_t slot, float x, float y);
stage_handler_register(HANDLER_PREFAB_GRUNT, prefab_grunt);

static void prefab_grunt(int32_t slot, float x, float y) {
    fc_buffer_set_f32(S_ENEMIES, slot, S_ENEMY_X,        x);
    fc_buffer_set_f32(S_ENEMIES, slot, S_ENEMY_Y,        y);
    fc_buffer_set_f32(S_ENEMIES, slot, S_ENEMY_VX,       0.f);
    fc_buffer_set_f32(S_ENEMIES, slot, S_ENEMY_VY,       0.f);
    fc_buffer_set_i32(S_ENEMIES, slot, S_ENEMY_HP,       30);
    fc_buffer_set_u8 (S_ENEMIES, slot, S_ENEMY_AI_STATE, AI_PATROL);
    fc_buffer_set_u32(S_ENEMIES, slot, S_ENEMY_SPRITE,   SPR_GRUNT_IDLE);
}
```

### Lua API

```lua
local H = require("cart_handlers")

-- Register
stage.handler.register(H.PREFAB_GRUNT, function(slot, x, y)
    local e = enemies[slot]
    e.x        = x
    e.y        = y
    e.vx       = 0
    e.vy       = 0
    e.hp       = 30
    e.ai_state = AI.PATROL
    e.sprite   = SPR.GRUNT_IDLE
end)

-- Spawn
local slot = stage.spawn(S_ENEMIES, H.PREFAB_GRUNT, spawn_x, spawn_y)
if slot then
    -- optional post-spawn customization
    enemies.hp[slot] = enemies.hp[slot] * difficulty_scale
end
```

`stage.spawn` returns the slot index on success, or `nil` if the buffer
is full. Callers check for nil before using the slot.

### Prefab purity constraint

Prefab handlers must only set field values on the provided slot. They must
not:

- Post events (the entity is not fully initialized; other systems may not
  be ready to process events about it)
- Spawn additional entities (the spawn stack could grow unboundedly)
- Read other entities' fields (initialization order would become implicit)

Post-spawn actions (spawning a particle, playing a sound, posting a
`EVT_ENTITY_SPAWNED` event) are the caller's responsibility after
`stage_spawn` returns. This keeps prefabs predictable and composable.

### Save state

Prefabs are static code (handler functions) and are never stored in buffers.
The initialized field values are stored in the entity's buffer slot and
serialize normally. A save state taken immediately after spawning an entity
captures its initialized state correctly; on restore, the entity is already
present in the buffer with its initialized values, and the prefab handler
does not run again.

## Consequences

- Entity initialization is centralized: adding a field to an entity type
  requires updating one prefab handler, not every spawn site.
- The prefab handler is a plain function that can be unit tested in
  isolation by allocating a slot and inspecting the fields it sets.
- Purity constraints (no events, no cascading spawns) keep prefab behavior
  predictable and save-state interaction simple.
- Post-spawn customization (scaling HP by difficulty) is done by the caller
  after spawn, not inside the prefab. This avoids parameterized prefabs
  that would complicate the handler signature.
- `stage.spawn` returning `nil` on buffer-full makes the failure visible
  at the call site. In dev builds, `stage_spawn` additionally logs a
  warning naming the buffer and the prefab, making buffer capacity issues
  easy to diagnose.

# ADR-0098: Lightweight FSM helper

## Status
Accepted

## Context

State machines are the natural structure for player controllers, AI
behaviours, and any entity with discrete modes (idle, patrol, chase, attack,
hurt, dead). The pattern recurs constantly: a current-state integer field
in the entity buffer, per-state update logic, and transitions between states.

Written by hand, this is readable but repetitive:

```lua
if e.ai_state == AI_PATROL then
    -- patrol logic
    if sees_player then e.ai_state = AI_CHASE end
elseif e.ai_state == AI_CHASE then
    -- chase logic
    if in_attack_range then e.ai_state = AI_ATTACK end
end
```

The structure is always the same. What varies is the number of states,
the names of the states, and the logic in each state. A helper that
provides named-state dispatch and separates on_enter/on_update/on_exit
from the transition test reduces repetition and makes the structure
explicit.

Critically, the current state must survive save states — it is an integer
buffer field. The FSM definition (which functions run in which state) is
static code. Only the current state integer is data.

Sequences (ADR-0094) handle linear logic; FSMs handle branching. They are
complementary, not alternatives.

## Decision

**Stage provides a lightweight FSM helper that dispatches named handler IDs
for each state's on_enter, on_update, and on_exit hooks. The current state
is an integer buffer field on the entity; the FSM definition is static code.**

### Manifest

State names are declared as handlers (ADR-0090):

```yaml
# cart.config.yaml
handlers:
  - ai_patrol_enter
  - ai_patrol_update
  - ai_chase_enter
  - ai_chase_update
  - ai_attack_enter
  - ai_attack_update
  - ai_attack_exit
```

State IDs are integer constants defined directly in cart code (not packer-
generated, since they are not stored in POD buffers by name):

```c
// cart_ai_states.h
#define AI_PATROL  0
#define AI_CHASE   1
#define AI_ATTACK  2
```

```lua
-- cart_ai_states.lua
return { PATROL = 0, CHASE = 1, ATTACK = 2 }
```

### FSM definition

The FSM is defined once, as static data:

```c
static const stage_fsm_state_t enemy_fsm[] = {
    [AI_PATROL] = {
        .on_enter  = HANDLER_AI_PATROL_ENTER,
        .on_update = HANDLER_AI_PATROL_UPDATE,
        .on_exit   = FC_HANDLER_NONE,
    },
    [AI_CHASE] = {
        .on_enter  = HANDLER_AI_CHASE_ENTER,
        .on_update = HANDLER_AI_CHASE_UPDATE,
        .on_exit   = FC_HANDLER_NONE,
    },
    [AI_ATTACK] = {
        .on_enter  = HANDLER_AI_ATTACK_ENTER,
        .on_update = HANDLER_AI_ATTACK_UPDATE,
        .on_exit   = HANDLER_AI_ATTACK_EXIT,
    },
};
static const stage_fsm_t ENEMY_FSM = {
    .states    = enemy_fsm,
    .num_states = 3,
};
```

### C API

```c
// Tick the FSM for one entity slot.
// Reads current state from buf[slot][state_field].
// Calls on_update for current state with (slot, 0).
void stage_fsm_update(const stage_fsm_t *fsm,
                       fc_buffer_h buf, int32_t slot,
                       fc_field_h state_field);

// Transition to a new state.
// Calls on_exit for current state, updates field, calls on_enter for new state.
void stage_fsm_transition(const stage_fsm_t *fsm,
                           fc_buffer_h buf, int32_t slot,
                           fc_field_h state_field,
                           uint8_t new_state);
```

Usage in an update loop:

```c
// In system_ai():
fc_iter_h it;
fc_buffer_iter_begin(S_ENEMIES, &it);
int32_t slot;
while (fc_buffer_iter_next(it, &slot)) {
    stage_fsm_update(&ENEMY_FSM, S_ENEMIES, slot, S_ENEMY_AI_STATE);
}

// Inside HANDLER_AI_PATROL_UPDATE, to transition:
if (can_see_player(slot)) {
    stage_fsm_transition(&ENEMY_FSM, S_ENEMIES, slot,
                          S_ENEMY_AI_STATE, AI_CHASE);
}
```

### Lua API

```lua
local AI = require("cart_ai_states")
local H  = require("cart_handlers")

local enemy_fsm = stage.fsm.define{
    [AI.PATROL] = {
        on_enter  = H.AI_PATROL_ENTER,
        on_update = H.AI_PATROL_UPDATE,
    },
    [AI.CHASE] = {
        on_enter  = H.AI_CHASE_ENTER,
        on_update = H.AI_CHASE_UPDATE,
    },
    [AI.ATTACK] = {
        on_enter  = H.AI_ATTACK_ENTER,
        on_update = H.AI_ATTACK_UPDATE,
        on_exit   = H.AI_ATTACK_EXIT,
    },
}

-- In the AI system update loop:
for slot in enemies:slots() do
    stage.fsm.update(enemy_fsm, enemies, slot, "ai_state")
end

-- Inside a handler, to transition:
stage.fsm.transition(enemy_fsm, enemies, slot, "ai_state", AI.CHASE)
```

### Save state

The current state integer is an entity buffer field and serializes
automatically. The FSM definition is static code; it is never stored in
a buffer. On save-state restore, the FSM resumes from the saved state
without re-running on_enter. If the cart's `fc_cart_on_load` or scene
`on_resume` needs to synchronize visual state to the restored FSM state,
it can call `on_enter` explicitly.

## Consequences

- State transitions are explicit: `stage_fsm_transition` calls on_exit and
  on_enter atomically from the caller's perspective. There is no way to
  accidentally skip an exit or enter hook.
- The FSM definition is static, cart-readable data — easier to audit than
  a nested if/elseif chain.
- All handler IDs are integers; the FSM definition contains no closures and
  is hot-reload safe.
- on_enter is not called on save-state restore — the FSM resumes from the
  saved state as-is. This is correct for most cases; carts that need to
  rebuild visual state have `on_resume` for this purpose.
- The helper is intentionally minimal: no guard conditions, no automatic
  transition logic, no timer-driven transitions. Complex AI that needs
  these can implement them in the update handler and call
  `stage_fsm_transition` when appropriate.

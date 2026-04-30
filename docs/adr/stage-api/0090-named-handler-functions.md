# ADR-0090: Named handler functions with compile-time IDs

## Status
Accepted

## Context

Game logic frequently needs to store callbacks: which function handles an
entity's death, which function runs when a timer expires, which behavior a
given AI entity is executing. The natural Lua approach is to store a closure
reference. The problem is that closure references are not serializable — they
cannot survive save states, and they do not survive Lua hot-reload.

The naive fix — storing function names as strings and looking them up at
call time — works but requires a string lookup on every dispatch and has no
compile-time validation.

fc32's existing pattern (ADR-0057, ADR-0059) resolves the same tension for
buffer fields, resources, and preferences: declare names in the manifest,
have the packer assign compile-time integer constants. The same pattern
applies directly to handler functions.

## Decision

**Handler functions are declared in `cart.config.yaml` and assigned
compile-time integer IDs by the packer. The ID is what is stored in state
buffers; the function body is registered at module load time by name.**

### Manifest declaration

```yaml
# cart.config.yaml
handlers:
  - ai_patrol
  - ai_chase
  - ai_attack
  - ai_death
  - on_door_opened
  - on_wave_complete
```

### C API

The packer generates integer constants in `cart_handlers.h`:

```c
// cart_handlers.h (generated, gitignored)
#define HANDLER_AI_PATROL       ((fc_handler_h)1)
#define HANDLER_AI_CHASE        ((fc_handler_h)2)
#define HANDLER_AI_ATTACK       ((fc_handler_h)3)
#define HANDLER_AI_DEATH        ((fc_handler_h)4)
#define HANDLER_ON_DOOR_OPENED  ((fc_handler_h)5)
#define HANDLER_ON_WAVE_COMPLETE ((fc_handler_h)6)
```

`fc_handler_h` is a `uint32_t` typedef. `FC_HANDLER_NONE` (zero) is the
invalid sentinel.

Handlers are registered at cart init and called by Stage at dispatch time:

```c
// Registration (in fc_cart_init or scene on_enter)
stage_handler_register(HANDLER_AI_PATROL,  system_ai_patrol);
stage_handler_register(HANDLER_AI_DEATH,   system_ai_death);

// Storage (in entity buffer field)
fc_buffer_set_u32(S_ENEMIES, slot, S_ENEMY_BEHAVIOR, HANDLER_AI_PATROL);

// Dispatch (by Stage internals or directly)
fc_handler_h h = fc_buffer_get_u32(S_ENEMIES, slot, S_ENEMY_BEHAVIOR);
stage_handler_call(h, slot);
```

### Lua API

In Lua, the packer generates a module `cart_handlers.lua` with integer
constants. Stage additionally wraps each handler as a callable table so it
can be used directly where a function is expected, while remaining
serializable:

```lua
-- cart_handlers.lua (generated)
local H = {}
H.AI_PATROL       = 1
H.AI_CHASE        = 2
H.AI_ATTACK       = 3
H.AI_DEATH        = 4
H.ON_DOOR_OPENED  = 5
H.ON_WAVE_COMPLETE = 6
return H
```

Stage wraps each constant as a callable:

```lua
local H = require("cart_handlers")
local stage = require("stage")

-- Registration
stage.handler.register(H.AI_PATROL, function(slot)
    -- patrol logic
end)

-- Storage in entity buffer field
enemies.behavior[slot] = H.AI_PATROL

-- Direct call (Stage dispatches internally; carts rarely call this)
stage.handler.call(H.AI_PATROL, slot)

-- Callable wrapper: h(slot) dispatches through the registry
local h = stage.handler.wrap(H.AI_PATROL)
h(slot)  -- equivalent to stage.handler.call(H.AI_PATROL, slot)
```

The callable wrapper returned by `stage.handler.wrap` is a table with a
`__call` metamethod and an `id` field holding the integer constant. It can
be compared, stored, and passed around, and its `id` can be written to a
buffer field. It is not a closure and has no upvalues.

### What this replaces

| Before | After |
|---|---|
| `entity.on_death = function() ... end` | `entity.on_death = H.AI_DEATH` (integer) |
| `store closure, lose on save` | `store integer, serializes automatically` |
| `string lookup on dispatch` | `integer table lookup, O(1)` |
| `stale reference after hot-reload` | `ID stable, function body updated by reload` |

### HANDLER_NONE

`FC_HANDLER_NONE` / `H.NONE` (zero) is the unambiguous sentinel for
"no handler." Stage dispatch calls skip entries with zero handler IDs.

## Consequences

- Handler IDs stored in buffer fields survive save states automatically —
  they are just integers in POD storage.
- Hot reload replaces the registered function body; the integer ID in the
  buffer is unchanged. AI entities resume with updated behavior logic and
  unchanged state.
- Dispatch is an integer table lookup — zero overhead beyond the function
  call itself.
- The packer validates that all referenced handler names are declared in
  the manifest, catching missing registrations at build time.
- Carts must declare all handlers in the manifest before use — this is the
  same discipline required for all named resources in fc32.
- `FC_HANDLER_NONE` (zero) provides a safe default for uninitialized
  handler fields.

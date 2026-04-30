# ADR-0091: Event publish/subscribe system

## Status
Accepted

## Context

Systems that call each other directly create tight coupling: a collision
system that applies damage must know about the health system, the audio
system, and the animation system. Adding a new reaction to a collision
requires modifying the collision system. This is particularly awkward in
a C/Lua mixed codebase where a C collision system would need explicit
`lua_pcall` sites for each Lua reaction handler.

An event bus inverts this: the collision system posts an event; any
interested system subscribes to it. Systems know about event types, not
about each other. The bus is the shared contract.

The key design constraint is save-state compatibility: subscriber lists
cannot be stored as Lua closure references (not serializable). They must
use the named handler ID system (ADR-0090).

A secondary constraint is allocation: the event queue must be fixed-size,
pre-allocated, with no dynamic allocation during gameplay.

## Decision

**Stage provides a deferred-dispatch event bus. `stage_event_post` enqueues
an event; `stage_event_flush` drains the queue and dispatches to all
registered handlers for each event type.**

### Event types

Event types are declared in `cart.config.yaml` alongside handler
declarations. The packer generates integer constants:

```yaml
# cart.config.yaml
events:
  - evt_damage
  - evt_entity_died
  - evt_trigger_entered
  - evt_item_collected
  - evt_wave_complete
```

```c
// Generated: cart_events.h
#define EVT_DAMAGE           ((fc_event_type_h)1)
#define EVT_ENTITY_DIED      ((fc_event_type_h)2)
#define EVT_TRIGGER_ENTERED  ((fc_event_type_h)3)
#define EVT_ITEM_COLLECTED   ((fc_event_type_h)4)
#define EVT_WAVE_COMPLETE    ((fc_event_type_h)5)
```

### Event payload

Each event carries a fixed-size payload of two `uint32_t` fields —
`subject` and `data` — sufficient for entity slot references, enum values,
counts, and packed small values. Complex payloads are passed by storing
relevant data in buffer fields before posting.

```c
typedef struct {
    fc_event_type_h type;
    uint32_t        subject;  // typically a slot index
    uint32_t        data;     // event-specific value
} fc_event_t;
```

### Queue

The event queue is a fixed-size ring buffer declared in `cart.config.yaml`.
Overflow in dev builds asserts; in release builds the oldest unprocessed
event is dropped and a counter is incremented.

```yaml
# cart.config.yaml
stage:
  event_queue_size: 256  # default; override if needed
```

### Flush point

The queue is flushed once per frame at a fixed point in the system
execution order (ADR-0093): after collision resolution, before animation.
This ensures handlers see fully-settled physics and collision state, and
their writes to buffer fields are visible to animation and camera systems
in the same frame.

### Subscriptions

Subscriptions are registered at scene load and stored in a static table,
not in POD buffers. They are rebuilt on scene entry and on save-state
restore. Dynamic subscriptions created during gameplay (outside `on_enter`)
must be re-registered in `fc_cart_on_load`; this is discouraged in favour
of registering all subscriptions unconditionally in `on_enter`.

```c
// Subscribe (typically called in scene on_enter)
stage_event_subscribe(EVT_DAMAGE, HANDLER_APPLY_DAMAGE);
stage_event_subscribe(EVT_ENTITY_DIED, HANDLER_ON_DEATH);

// Post
stage_event_post(EVT_DAMAGE, enemy_slot, 10);

// Flush (called by Stage at the fixed flush point; carts rarely call directly)
stage_event_flush();
```

### Lua API

```lua
local E = require("cart_events")
local H = require("cart_handlers")

-- Subscribe (in scene on_enter)
stage.event.subscribe(E.EVT_DAMAGE, H.APPLY_DAMAGE)
stage.event.subscribe(E.EVT_ENTITY_DIED, H.ON_DEATH)

-- Post
stage.event.post(E.EVT_DAMAGE, enemy_slot, 10)

-- Handler receives subject and data
stage.handler.register(H.APPLY_DAMAGE, function(subject, data)
    local amount = data
    enemies.hp[subject] = enemies.hp[subject] - amount
    if enemies.hp[subject] <= 0 then
        stage.event.post(E.EVT_ENTITY_DIED, subject, 0)
    end
end)
```

### Lua error containment

Lua handlers are called via `lua_pcall`. A handler that throws an error is
logged and skipped; remaining handlers for the same event are still called.
This follows the general fc32 policy (ADR-0084) that Lua errors must not
crash the engine.

### Save state

Events are fully transient. The queue is empty at the start of every
`fc_cart_update`. No event data is stored in POD buffers; no save/restore
handling is required for the bus itself. Subscription lists are rebuilt
on restore via `on_enter` or `fc_cart_on_load`.

## Consequences

- Systems are decoupled: adding a new reaction to an event requires only
  a new subscription, not a change to the posting system.
- C systems can post events that Lua handlers receive, and vice versa,
  with no special bridging code.
- Deferred dispatch means no re-entrant system calls: a handler running
  during flush can post new events, which are queued for the next flush
  in the following frame.
- The fixed flush point in the system order (after collision, before
  animation) ensures predictable handler sequencing every frame.
- Dynamic subscriptions created during gameplay (outside `on_enter`) are
  fragile across save/restore — authors are strongly guided toward
  unconditional registration in `on_enter` to avoid this class of bug.
- The fixed queue size means event-heavy carts must tune the queue size
  in `cart.config.yaml`; the dev-build overflow assertion makes this
  visible immediately.

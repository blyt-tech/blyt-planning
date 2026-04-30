# ADR-0094: Sequence scripting with automatic serialization

## Status
Accepted

## Context

Many game scripts are fundamentally sequential: play a sound, wait for the
dialogue to finish, unlock a door, wait two seconds, resume patrol. The
natural way to write this in Lua is a coroutine: sequential code with
`coroutine.yield` at wait points. The problem is serialization — a suspended
coroutine's call-stack position cannot be saved to a POD buffer, so it does
not survive save states.

The full-serialization approach (Eris-style libraries) is heavyweight, tightly
version-locked to specific Lua releases, and doesn't handle C frames on the
stack. ADR-0012 rejected it for these reasons and provides
`console.coroutine.create{}` for cases that genuinely need persistent
coroutines, at the cost of explicit save/restore callbacks.

For purely sequential scripts — a straight-line sequence of actions and waits
with no branching — there is a much simpler observation: **the complete save
state is just two integers: which wait step has been reached, and how many
frames remain on the current timer**. No call-stack serialization required.

## Decision

**Stage provides a `sequence` abstraction for linear scripts. The step index
and frame timer are stored in a small fixed-size sequence slot; save/restore
is automatic.**

### What sequences can express

A sequence can:
- Wait a fixed number of frames: `S.wait(120)`
- Wait until a named event is received: `S.wait_event(E.DOOR_OPENED)`
- Post an event: `S.emit(E.CUTSCENE_STARTED, npc_slot, 0)`
- Call a named handler directly: `S.call(H.SPAWN_WAVE, wave_number)`
- Loop unconditionally: `S.loop()` (jumps back to step 0)

A sequence cannot branch based on runtime conditions while preserving
automatic serialization. Sequences with conditional logic must use
`console.coroutine.create{}` (ADR-0012) with explicit save/restore hooks.

### Sequence slot buffer

Sequences are backed by a fixed-size pool declared in `cart.config.yaml`:

```yaml
# cart.config.yaml
stage:
  sequence_slots: 16  # max concurrent sequences; default 8
```

Each slot stores:

```
step_index:       u16    which wait step the sequence is currently on
remaining_frames: u16    frames remaining if waiting on a timer (0 otherwise)
handler_id:       u8     which sequence handler to run (ADR-0090)
waiting_event:    u8     event type the sequence is blocked on (0 = not blocked)
active:           bool
```

This is a POD struct in the sequence pool buffer. It serializes automatically
as part of the buffer save (ADR-0058).

### Manifest

```yaml
# cart.config.yaml
handlers:
  - cutscene_intro
  - patrol_loop
  - boss_phase_two

stage:
  sequence_slots: 16
```

### Lua API

```lua
local E = require("cart_events")
local H = require("cart_handlers")

-- Define and register a sequence
stage.handler.register(H.CUTSCENE_INTRO, stage.sequence(function(S)
    S.emit(E.DIALOGUE_START, npc_guard, 0)
    S.wait_event(E.DIALOGUE_DONE)
    S.wait(60)
    S.emit(E.DOOR_UNLOCK, door_north, 0)
    S.emit(E.GUARD_RESUME_PATROL, npc_guard, 0)
end))

-- Run a sequence (allocates a slot from the pool)
stage.sequence.run(H.CUTSCENE_INTRO)

-- Run with a subject (passed to each step's emit/call as context)
stage.sequence.run(H.PATROL_LOOP, enemy_slot)
```

`stage.sequence(fn)` takes a function that describes the sequence steps and
returns a handler-compatible function. The steps are enumerated at
registration time — the function is called once with a recording `S` object
to capture the step list, not called on each tick.

### C API

```c
// Registration (sequence defined as array of steps)
static const stage_seq_step_t intro_steps[] = {
    STAGE_SEQ_EMIT(EVT_DIALOGUE_START, 0),
    STAGE_SEQ_WAIT_EVENT(EVT_DIALOGUE_DONE),
    STAGE_SEQ_WAIT(60),
    STAGE_SEQ_EMIT(EVT_DOOR_UNLOCK, 0),
    STAGE_SEQ_EMIT(EVT_GUARD_RESUME_PATROL, 0),
    STAGE_SEQ_END,
};

stage_handler_register_seq(HANDLER_CUTSCENE_INTRO,
                            intro_steps,
                            ARRAY_LEN(intro_steps));

// Run
stage_sequence_run(HANDLER_CUTSCENE_INTRO, /*subject=*/0);
```

### Save/restore

The sequence pool is a manifest-declared state buffer. All slot fields
(`step_index`, `remaining_frames`, `handler_id`, `waiting_event`, `active`)
are POD and serialize automatically. On restore, Stage resumes each active
sequence from its saved step index without re-running earlier steps.

### Limitation: closed-over Lua locals

Local variables inside a sequence function body that accumulate values
across wait points do NOT survive save states — they are Lua locals, not
buffer fields. The sequence mechanism only guarantees that the *step index*
and *timer* survive.

```lua
stage.sequence(function(S)
    local count = 0       -- this will reset to 0 on save/restore
    S.emit(E.WAVE_START, 0, 0)
    S.wait(120)
    count = count + 1     -- count is gone after restore
end)
```

Scripts that need to accumulate data across wait points must store that data
in buffer fields and pass slot indices as the sequence subject.

## Consequences

- Linear scripts are written as readable sequential code and serialize
  automatically — no explicit save/restore callbacks required.
- The step-index-plus-timer save state is minimal and correct for any
  purely sequential script.
- Sequences are backed by a fixed-size pool, consistent with fc32's
  no-dynamic-allocation discipline.
- The Lua recording model (`S` object captures step list once at
  registration) means sequences have no runtime overhead from the
  function body — step dispatch is a fixed-cost table lookup.
- Branching scripts that cannot be expressed as linear sequences require
  `console.coroutine.create{}` (ADR-0012) with explicit save/restore hooks.
  This boundary is intentional: keep the simple case simple, and make the
  complex case explicit about its complexity.

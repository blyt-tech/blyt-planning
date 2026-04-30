# Stage: A Game Logic Layer for Cart Development

## Purpose

Stage is an optional, cart-side game logic layer that sits between the fc32
runtime API and game-specific code. It is not part of the runtime — it ships
as an SDK library that carts opt into. A cart that wants full control over
its structure can ignore Stage entirely and call the runtime API directly.

Stage addresses a specific gap: the fc32 runtime provides the building blocks
(typed state buffers, input, drawing, audio), but gives no structure to game
logic itself — how entities are organized, how systems communicate, how scenes
transition, how sequential scripts are written. Stage provides that structure
in a way that respects the runtime's constraints and works naturally from both
C and Lua.

The name reflects the design: a stage has scenes, actors (entities), a
viewpoint (camera), a cue system (events), and a script (sequences). Systems
are the crew executing a fixed call order behind the curtain.

---

## Programming Models Supported

### Sequential scripting

Cutscenes, tutorials, dialogue triggers, and timed event sequences are
naturally written as sequential code: do A, wait for B, do C. Stage provides
a `sequence` abstraction that looks like sequential code and serializes
automatically — the complete save state for any sequence is a step index plus
a frame timer. No explicit save/restore hooks required.

### Event-driven logic

Systems communicating directly creates tight coupling and ordering
dependencies. Stage's event bus lets systems post named events and subscribe
handlers without knowing about each other. A collision system posts
`EVT_DAMAGE`; a health system handles it. The bus is the shared interface,
not a direct call chain. Handlers are identified by compile-time integer IDs
so subscriptions survive save states.

### State machine logic

Player controllers, AI behaviours, and anything with discrete modes are
naturally written as state machines. Stage's FSM helper stores the current
state as an integer field in the entity's buffer (automatically serialized)
and dispatches named handlers for each state's enter, update, and exit.
Complements sequences: sequences for linear logic, FSMs for branching.

### Scene-based flow

Games are organized as scenes — title screen, gameplay, pause menu, game
over. Stage provides a scene lifecycle (on_enter, on_exit, on_update,
on_draw) and a primary-plus-overlay stack for cases where two scenes are
active simultaneously (e.g. gameplay beneath a pause overlay). Scene
identity is stored as a field in the world buffer so it survives save states.

### Entity lifecycle

Entities are slots in manifest-declared typed buffers. Stage adds spawn
templates (prefabs) for initializing slots from named recipes, and
generation counters for detecting stale slot references after reuse.
The timer system handles delayed and repeating callbacks against named
handlers without requiring per-cart free-list implementations.

---

## Architecture Constraints

### Save state compatibility

Everything that must survive save states — player position, AI state,
scene identity, timer countdowns, sequence progress — must live in POD
typed state buffers declared in the manifest. Lua locals, Lua tables, and
Lua closures are invisible to the save mechanism. This is the single most
important constraint shaping Stage's design.

The discipline it imposes: **state in buffers, logic in named functions**.
Entity field values are buffer state. Handler function bodies are logic.
The boundary must be respected consistently.

### Fixed-size pre-declared storage

There is no dynamic allocation during gameplay. All buffers — entity pools,
timer pools, event queues — are declared in the manifest with fixed counts
and allocated at cart load. Stage follows the same discipline: every pool it
manages is declared in `cart.config.yaml` and sized at pack time.

### C and Lua interoperability

Stage is designed for use from both C and Lua. The underlying mechanism —
named handler functions with packer-generated integer IDs — works identically
in both languages. A C physics system and a Lua AI system both write to the
same entity buffers using the same field constants; they are interchangeable
from the perspective of any other system that reads those fields.

### No runtime camera

ADR-0048 established that the runtime has no global camera offset. World-to-
screen coordinate transforms are the cart's responsibility. Stage provides
camera state as a 1-slot buffer and math helpers for the transform, but does
not inject any offset into draw calls. Every draw call explicitly receives
screen coordinates.

### Fixed timestep

fc32's update loop runs at a fixed rate with no `dt` parameter. Stage systems
assume a fixed step size. This is what makes save states, rewind, and netplay
work as structural properties. Sequences express time in frames; cameras lerp
by a fixed amount per frame; timers count down in frames.

### Hot-reload compatibility

Lua modules can be reloaded at runtime without restarting the cart. Stage is
designed so that reloads are safe: entity state lives in buffers and is
unaffected by a reload; handler function bodies are in Lua and are replaced
by the reload; handler integer IDs are packer-assigned constants that are
stable across reloads.

---

## Core Design Principle: Explicit State

The root cause of most save-state and hot-reload problems is implicit state:
Lua locals that accumulate over time, closure-captured variables, coroutine
call-stack positions. Stage eliminates these as a concern by making the
principle explicit and providing alternatives for each case.

**Closure callbacks** are replaced by named handler IDs. Instead of storing
a Lua function reference (which is not serializable), Stage stores a packer-
assigned integer and dispatches to the corresponding registered function at
call time. The integer lives in a buffer field; the function body is
reloadable Lua.

**Arbitrary coroutines** are replaced by sequences for linear logic and FSMs
for branching. Both store their progress as integers in buffer fields rather
than as call-stack positions.

**Scene-local Lua variables** that accumulate over time must be moved to
buffer fields. Stage cannot enforce this, but the system execution order and
the scene lifecycle make the natural place for game-relevant state obvious:
it belongs in the world buffer, not in Lua locals.

---

## Tradeoffs

### What Stage trades away

- **Closure-based callbacks.** You cannot store a Lua function reference as
  a callback that survives save states. All persistent callbacks must be
  named handlers with packer-assigned IDs.

- **Arbitrary coroutine state.** Coroutines that branch based on Lua-local
  state and need to survive saves require explicit `console.coroutine.create{}`
  hooks. Stage's sequence abstraction covers the common linear case; complex
  branching requires the explicit form.

- **Dynamic component attachment.** Entity "types" are fixed at manifest
  declaration time. An enemy always has exactly the fields declared for the
  Enemy type. You cannot add a component to an entity at runtime as you would
  in a runtime-archetype ECS.

- **Arbitrary-depth scene stacks.** Stage provides primary scene plus one
  overlay. Games that need deeper nesting (unusual at this fidelity) must
  manage additional depth themselves.

### What Stage gains

- **Automatic save-state compatibility** for all structured patterns. Any
  game logic written using Stage's building blocks (sequences, FSMs, timers,
  handler IDs) serializes correctly without per-feature save/restore code.

- **Hot-reload safety.** Because logic lives in named Lua functions and state
  lives in buffers, a Lua reload replaces logic without disturbing state.
  The game continues with updated logic applied to unchanged entity positions,
  AI states, and timer countdowns.

- **C/Lua interoperability.** The handler ID system and buffer field access
  work identically from C and Lua. A Lua scene and a C scene can coexist and
  interact through the same event bus and buffer fields.

- **Predictable system ordering.** Stage's canonical system execution order
  eliminates a class of subtle bugs caused by systems reading state written by
  other systems in the wrong order.

---

## Layer Map

| Concern | Provided by |
|---|---|
| Typed entity storage, slot allocation | fc32 runtime (ADR-0058) |
| Packer-generated field constants | fc32 packer (ADR-0057) |
| SOA / row-proxy Lua sugar | fc32 packer (ADR-0011) |
| Input hardware polling | fc32 runtime |
| Drawing, audio, resource loading | fc32 runtime |
| Screen shake application | fc32 runtime (ADR-0051) |
| **Named handler IDs** | **Stage (ADR-0090)** |
| **Event bus** | **Stage (ADR-0091)** |
| **Scene lifecycle and stack** | **Stage (ADR-0092)** |
| **System execution order** | **Stage (ADR-0093)** |
| **Sequence scripting** | **Stage (ADR-0094)** |
| **Camera state and helpers** | **Stage (ADR-0095)** |
| **Generation counters** | **Stage (ADR-0096)** |
| **Timer / alarm system** | **Stage (ADR-0097)** |
| **FSM helper** | **Stage (ADR-0098)** |
| **Prefab spawn templates** | **Stage (ADR-0099)** |
| **Input intent translation** | **Stage (ADR-0100)** |
| Game rules, win/loss, dialogue | Cart code |
| AI behaviour trees (complex) | Cart code |

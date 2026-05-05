# Stage: A Game Logic Layer for Cart Development

## Purpose

Stage is an optional, cart-side game logic layer that sits between the blyt
runtime API and game-specific code. It ships as part of the SDK; carts opt
in by using it and out by ignoring it. The runtime is unchanged either way.

Stage addresses a specific gap. The blyt runtime provides building blocks —
typed state buffers, input, drawing, audio — but imposes no structure on
how game logic is organised. Every cart faces the same problems: how
entities are tracked across scenes, how systems communicate, how scenes
transition, how sequential scripts survive save states, how a frame is
assembled. Stage answers those problems once, in a way that respects the
runtime's constraints and works naturally from both C and Lua.

The name is the metaphor: a stage has scenes, actors (entities), a
viewpoint (camera), a cue system (events), and a script (sequences).
Systems are the crew executing a fixed call order behind the curtain.

---

## Two Authoring Modes

Stage supports two authoring modes for drawing and frame structure. They
are peers — neither is the default. They serve different kinds of scene,
and a cart picks per scene based on what fits.

### Imperative

The cart writes the draw body directly. A scene's `on_draw` handler calls
`blyt_image_blit`, `blyt_tilemap_draw`, `blyt_text`, and other runtime functions
in whatever order it wants. This is the pico-8 / SDL programming model:
immediate-mode, per-frame freedom, tight per-pixel control.

Imperative suits scenes whose visuals do not fit a uniform entity model:
title screens, menus, debug overlays, puzzle boards, text-adventure
layouts, custom rendering effects, ports of code already written against
immediate-mode APIs.

### Declarative

The cart describes the scene; Stage runs it. A declarative scene declares,
in `cart.config.yaml`:

- A clear colour.
- An ordered list of tilemap layers.
- The scene-scoped entity buffers that participate in this scene.
- An optional UI hook.

Stage's default draw walks that data each frame: clear, tilemaps,
world-space entity blits in z-sorted order, screen-space entity blits in
z-sorted order, UI hook. Per-row reserved fields (`x`, `y`, `sprite`, `z`)
define the default blit; per-buffer `draw_handler` overrides the default
for a specific buffer; the optional UI hook handles whatever doesn't fit
the entity model.

Declarative suits scenes with many similar entities and a uniform draw
shape: scrolling worlds, shoot-em-up playfields, platformer levels,
top-down RPG maps — anywhere the cart would otherwise write the same
iterate-and-blit loop every frame.

### Mixing modes

Mode is per-scene. A cart with an imperative title screen, a declarative
gameplay scene, and an imperative pause overlay is normal. The choice for
each scene is local to that scene; the rest of the cart is unaffected.

Within a single scene, the mode is one or the other — declarative scenes
do not declare `on_draw`, and imperative scenes do not declare contents
that would drive the default pipeline. Mid-grain control inside a
declarative scene comes through per-buffer `draw_handler` (custom per-
entity drawing) or the optional UI hook (custom screen-space pass).

---

## Authoring Vocabulary

These are the patterns the cart works in. They are independent of draw
mode and apply equally in declarative and imperative scenes. All are
correct by construction with respect to save state and hot reload.

### Sequential scripts
Cutscenes, tutorials, dialogue triggers, timed events. Stage's `sequence`
abstraction looks like sequential code and serializes automatically — the
complete save state for any sequence is a step index plus a frame timer.
(ADR-0094.)

### Events
Systems post named events; subscribers handle them without knowing about
each other. Handlers are compile-time integer IDs, so subscriptions
survive save states and hot reload. (ADR-0091.)

### State machines
FSMs for player controllers, AI, anything with discrete modes. Current
state is an integer field on the entity; transitions dispatch named
enter/update/exit handlers. Complements sequences: linear scripts as
sequences, branching logic as FSMs. (ADR-0098.)

### Entity lifecycle
Prefabs are named handlers that initialise fresh slots from named
recipes. Generation counters detect stale slot references after reuse.
Timers schedule delayed and repeating callbacks against named handlers.
(ADR-0099, ADR-0096, ADR-0097.)

### Camera
A 1-slot camera buffer with world-to-screen helpers, entity-follow,
clamping, and screen-shake trauma. The declarative draw pipeline applies
the transform automatically; imperative scenes call the helpers directly.
(ADR-0095.)

### Input intent
Raw hardware input is translated once per frame into a structured
`InputIntent` value (axes, edge-triggered booleans). Game logic reads
intent, never raw buttons. Manifest-driven mapping; custom translators
supported for analogue sticks, accessibility remapping, or AI-driven
input injection. (ADR-0100.)

### Named handlers
The unifying primitive across all of the above. Handler functions are
declared in the manifest and the packer assigns compile-time integer IDs.
The integer is what's stored in buffers and event queues; the function
body is registered at module load and replaced freely by hot reload.
(ADR-0090.)

---

## Scene-Scoped Entities

Both modes need a way to know which entities belong to which scene — the
imperative draw body must filter its iteration, and the declarative draw
body relies on the same information. Stage provides one mechanism shared
by both.

Every scene-scoped entity buffer carries a packer-injected `scene_id: u8`
field. The reserved value `SCENE_GLOBAL` covers persistent entities
(player, HUD, audio listener). Entities are tagged automatically when
`stage_spawn` allocates a slot, using the currently running scene as the
scope. `stage_scene_clear(SCENE_X)` walks every scene-scoped buffer and
frees slots tagged for that scene — the typical body of an `on_exit`
handler. (ADR-0101.)

Imperative scenes use `stage_buffer_iter_active_begin` to filter their
own iteration. Declarative scenes have the filter applied automatically
by the default draw pipeline. The same tagging supports update-side
filtering in both modes.

---

## Architecture Constraints

These constraints come from the runtime. Stage is shaped by them; both
modes inherit them equally.

### Save-state compatibility

Everything that must survive save states — player position, AI state,
scene identity, timer countdowns, sequence progress, entity-to-scene
membership — must live in POD typed state buffers declared in the
manifest. Lua locals, Lua tables, and Lua closures are invisible to the
save mechanism. This is the single most important constraint shaping
Stage's design.

The discipline it imposes: **state in buffers, logic in named functions**.
Entity field values are buffer state. Handler function bodies are logic.
The boundary must be respected consistently.

### Fixed-size pre-declared storage

There is no dynamic allocation during gameplay. All buffers — entity
pools, timer pools, event queues, sequence slot pools — are declared in
the manifest with fixed counts and allocated at cart load. Stage follows
the same discipline: every pool it manages is declared in
`cart.config.yaml` and sized at pack time.

### C and Lua interoperability

Stage is designed for use from both C and Lua. The underlying mechanism —
named handler functions with packer-generated integer IDs — works
identically in both languages. A C physics system and a Lua AI system
both write to the same entity buffers using the same field constants;
they are interchangeable from the perspective of any other system that
reads those fields. Both authoring modes are language-agnostic: the
language a buffer's `draw_handler` or scene's `on_draw` is written in is
invisible to the runtime.

### No runtime camera

ADR-0048 established that the runtime has no global camera offset. World-
to-screen coordinate transforms are the cart's responsibility. Stage
provides camera state as a 1-slot buffer and applies the transform
automatically in the declarative draw pipeline. Imperative scenes call
the helpers directly.

### Fixed timestep

blyt's update loop runs at a fixed rate with no `dt` parameter. Stage
systems assume a fixed step size. This is what makes save states, rewind,
and netplay work as structural properties. Sequences express time in
frames; cameras lerp by a fixed amount per frame; timers count down in
frames.

### Hot-reload compatibility

Lua modules can be reloaded at runtime without restarting the cart. Stage
is designed so that reloads are safe: entity state lives in buffers and
is unaffected by a reload; handler function bodies are in Lua and are
replaced by the reload; handler integer IDs are packer-assigned constants
that are stable across reloads.

---

## Core Design Principle: Explicit State

The root cause of most save-state and hot-reload problems is implicit
state: Lua locals that accumulate over time, closure-captured variables,
coroutine call-stack positions. Stage eliminates these as a concern by
making the principle explicit and providing alternatives for each case.

**Closure callbacks** are replaced by named handler IDs. Instead of
storing a Lua function reference (which is not serializable), Stage
stores a packer-assigned integer and dispatches to the corresponding
registered function at call time. The integer lives in a buffer field;
the function body is reloadable Lua.

**Arbitrary coroutines** are replaced by sequences for linear logic and
FSMs for branching. Both store their progress as integers in buffer
fields rather than as call-stack positions.

**Scene-local Lua variables** that accumulate over time must be moved to
buffer fields. Stage cannot enforce this, but the system execution order
and the scene lifecycle make the natural place for game-relevant state
obvious: it belongs in the world buffer, not in Lua locals.

---

## Tradeoffs

### What Stage trades away

- **Closure-based callbacks.** You cannot store a Lua function reference
  as a callback that survives save states. All persistent callbacks must
  be named handlers with packer-assigned IDs.

- **Arbitrary coroutine state.** Coroutines that branch based on Lua-
  local state and need to survive saves require explicit
  `blyt32.coroutine.create{}` hooks. Stage's sequence abstraction
  covers the common linear case; complex branching requires the explicit
  form.

- **Dynamic component attachment.** Entity "types" are fixed at manifest
  declaration time. An enemy always has exactly the fields declared for
  the Enemy type. You cannot add a component to an entity at runtime as
  you would in a runtime-archetype ECS.

- **Arbitrary-depth scene stacks.** Stage provides primary scene plus one
  overlay. Games that need deeper nesting (unusual at this fidelity)
  must manage additional depth themselves.

- **Multi-scene entity membership.** An entity belongs to exactly one
  scene at a time, or `SCENE_GLOBAL`. Carts that genuinely need an entity
  in two scenes simultaneously manage that themselves.

### What Stage gains

- **Two authoring modes for drawing**, chosen per scene. Imperative for
  scenes whose visuals don't fit a uniform entity model; declarative for
  scenes where they do. Mode is local to the scene, so the choice for one
  scene doesn't constrain the others.

- **Automatic save-state compatibility** for all structured patterns.
  Any game logic written using Stage's building blocks (sequences, FSMs,
  timers, handler IDs, scene-tagged buffers) serializes correctly without
  per-feature save/restore code.

- **Hot-reload safety.** Because logic lives in named Lua functions and
  state lives in buffers, a Lua reload replaces logic without disturbing
  state. The game continues with updated logic applied to unchanged
  entity positions, AI states, and timer countdowns.

- **C/Lua interoperability.** The handler ID system, buffer field
  access, and both draw modes work identically from C and Lua. A Lua
  scene and a C scene can coexist and interact through the same event
  bus and buffer fields.

- **Predictable system ordering.** Stage's canonical system execution
  order eliminates a class of subtle bugs caused by systems reading state
  written by other systems in the wrong order.

---

## Layer Map

| Concern | Provided by |
|---|---|
| Typed entity storage, slot allocation | blyt runtime (ADR-0058) |
| Packer-generated field constants | blyt packer (ADR-0057) |
| SOA / row-proxy Lua sugar | blyt packer (ADR-0011) |
| Input hardware polling | blyt runtime |
| Drawing primitives, audio, resource loading | blyt runtime |
| Screen shake application | blyt runtime (ADR-0051) |
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
| **Scene-scoped entity tagging** | **Stage (ADR-0101)** |
| **Declarative scene drawing** | **Stage (ADR-0102)** |
| Game rules, win/loss, dialogue | Cart code |
| Per-entity behaviour | Cart code (handler bodies) |
| Custom rendering effects | Cart code (`draw_handler` or `on_draw`) |
| AI behaviour trees (complex) | Cart code |

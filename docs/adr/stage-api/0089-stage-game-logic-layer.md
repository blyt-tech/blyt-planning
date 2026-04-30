# ADR-0089: Stage — a structured game logic layer for cart development

## Status
Accepted

## Context

The fc32 runtime provides building blocks — typed state buffers, input,
drawing, audio — but imposes no structure on how game logic is organized.
Every cart must independently solve the same recurring problems: how to
communicate between systems without tight coupling, how to write sequential
scripts that survive save states, how to manage scene transitions, how to
follow entities with a camera.

The runtime's constraints (POD-only save state, fixed timestep, no runtime
camera, C/Lua dual-language support) mean that naive approaches to these
problems — Lua closure callbacks, arbitrary coroutines, Lua-local scene
state — silently break at save/restore boundaries. A cart author who
doesn't already understand these constraints will encounter subtle bugs
that are hard to diagnose.

Stage is the SDK's answer: a cart-side library that solves these problems
once, in a way that is correct by construction, and that works naturally
from both C and Lua.

## Decision

**Stage is an optional, cart-side game logic layer shipped as part of the
SDK. It is not part of the runtime and is not required.**

Stage provides:

- Named handler functions with packer-generated compile-time IDs (ADR-0090)
- An event publish/subscribe system (ADR-0091)
- A scene lifecycle and stack (ADR-0092)
- A canonical system execution order (ADR-0093)
- A sequence scripting abstraction (ADR-0094)
- Camera state and world-to-screen helpers (ADR-0095)
- Entity slot generation counters (ADR-0096)
- A timer and alarm system (ADR-0097)
- A lightweight FSM helper (ADR-0098)
- Prefab and entity spawn templates (ADR-0099)
- Input intent translation (ADR-0100)

**Stage is the theatrical metaphor made concrete.** A stage has scenes, actors
(entities), a viewpoint (camera), a cue system (events), and a script
(sequences). The name is carried through the API surface: `stage_scene_push`,
`stage_event_post`, `stage_sequence_run`, `stage_camera_to_screen`.

In Lua, Stage is available as the `stage` global module: `stage.event.post`,
`stage.scene.push`, `stage.sequence`, `stage.camera`.

**Stage does not replace the runtime API.** Drawing, audio, resource loading,
and input hardware polling remain runtime calls. Stage organizes game logic
above those calls; it does not wrap them.

**Stage enforces no runtime overhead on carts that don't use it.** A cart
that calls the runtime API directly incurs no cost from Stage's existence.
Stage components that are used pull in only what they need.

## Consequences

- Cart authors get a consistent, correct-by-construction framework for the
  most common game logic patterns without needing to understand fc32's
  save-state constraints first.
- Advanced carts that want full control skip Stage entirely — the runtime API
  remains the primary contract.
- The theatrical naming (`stage`, `scene`, `sequence`) is intentionally
  distinct from generic engine terminology (`world`, `system`, `script`)
  to reduce confusion with fc32 runtime concepts.
- Stage's ADRs (0090–0100) document each component's design rationale,
  constraints, and API shape independently.

# ADR-0092: Scene lifecycle and stack

## Status
Accepted

## Context

Games are organized into discrete modes — title screen, gameplay, pause
menu, game over. Each mode has a lifecycle: it initializes when entered,
cleans up when exited, and drives update and draw each frame while active.

Two complications arise:

**Layered scenes.** A pause menu draws over the running game without
destroying the game's state. A save/load dialog can sit over an inventory
modal, which itself sits over the gameplay scene. The natural fit is a
stack: push to layer a scene on top, pop to remove it.

A flat "primary + overlay" pair handles two of those layers and forces
carts that need a third to invent their own depth tracking — and to do
it in a way that survives save states. The cost of providing a real
stack is small: a fixed-size u8 array plus a depth counter in the world
buffer. Complexity is bounded by the configured depth, and the common
case stays as cheap as the previous two-slot design.

**Save state compatibility.** "What scenes am I in" is game state and
must survive save states. The stack lives in POD buffer fields so it
serializes automatically.

The `blyt_cart_draw` constraint (ADR-0076) also applies: `on_draw` must
not modify any state. Scene logic that produces side effects belongs in
`on_update`, not `on_draw`.

## Decision

**Stage provides a fixed-depth scene stack stored in the world buffer.
The default depth is 2 (matching the common game + overlay case). Carts
that need more layers raise the limit in `cart.config.yaml`.**

### Scene definition

A scene is a named set of handler functions declared in `cart.config.yaml`:

```yaml
scenes:
  - name: title_screen
    on_enter:  scene_title_enter
    on_exit:   scene_title_exit
    on_update: scene_title_update
    on_draw:   scene_title_draw
  - name: gameplay
    on_enter:  scene_game_enter
    on_exit:   scene_game_exit
    on_update: scene_game_update
    on_draw:   scene_game_draw
    background_update: true   # ticks even when scenes sit on top
  - name: pause_menu
    on_enter:  scene_pause_enter
    on_exit:   scene_pause_exit
    on_update: scene_pause_update
    on_draw:   scene_pause_draw
```

All referenced names must also be declared as handlers (ADR-0090). The
packer generates scene ID constants:

```c
// Generated: cart_scenes.h
#define SCENE_TITLE_SCREEN  ((blyt_scene_h)1)
#define SCENE_GAMEPLAY      ((blyt_scene_h)2)
#define SCENE_PAUSE_MENU    ((blyt_scene_h)3)
```

### Stack depth

Stack depth is a manifest setting:

```yaml
scene_stack_depth: 2   # default; raise for carts that nest deeper
scenes:
  - ...
```

Valid range is 1..16. Most carts leave it at the default. Pushing when
the stack is already at `scene_stack_depth` is a runtime error: the
cart panics with a packer-traceable diagnostic rather than silently
dropping the push.

### World buffer

Stage stores stack state on the world buffer (1-slot buffer named
`world` in the manifest). The packer auto-injects two fields, similar
to `scene_id` injection in ADR-0101:

- `scene_stack: u8[scene_stack_depth]` — scene ids; index 0 is the
  bottom of the stack, `scene_depth - 1` is the top.
- `scene_depth: u8` — number of valid entries (1..scene_stack_depth
  after the cart's first `stage_scene_set`).

The cart declares the buffer; the packer adds the fields and emits
handles for runtime access. Cart-declared types may not shadow these
names (added to ADR-0090's reserved-identifier list alongside
`scene_id` and `SCENE_GLOBAL`).

```yaml
state_buffers:
  world: { type: World, count: 1 }
```

### Stack semantics

The **top** scene is the current scene at `scene_stack[scene_depth - 1]`.
The stack is never empty after the cart's initial `stage_scene_set` —
operations that would empty it are errors.

- **Push** appends a scene. Calls `on_enter` on the new top. Errors if
  the stack is full.
- **Pop** removes the top scene. Calls `on_exit` on the popped scene.
  Errors if it would empty the stack — use `stage_scene_set` to
  transition the bottom scene.
- **Set** replaces the entire stack with a single scene. Calls `on_exit`
  top-down on every existing entry, then `on_enter` on the new scene.
  This is the standard "go to a different top-level mode" operation
  (title → gameplay → game over).
- **Replace top** pops the top and pushes the replacement in one step,
  emitting paired `on_exit`/`on_enter` only for the swapped scenes;
  scenes below are untouched.

### Update and draw semantics

**Update.** The top scene's `on_update` always runs. Scenes below the
top run `on_update` only if declared with `background_update: true`.
Updates run bottom-to-top, so a frame's input intent and event
publications from background simulation are visible to the top scene
that frame.

**Draw.** Every scene in the stack runs `on_draw`, bottom to top. A
scene below draws the world; scenes above draw their layer on top.
`on_draw` is forbidden from modifying state (ADR-0076), so draw order
has no semantic effect beyond pixel layering.

### C API

```c
// Replace the stack with a single scene.
// (calls on_exit top-down on existing scenes, on_enter on the new one)
stage_scene_set(SCENE_GAMEPLAY);

// Push a scene on top (calls on_enter on the new top).
stage_scene_push(SCENE_PAUSE_MENU);
stage_scene_push(SCENE_SAVE_DIALOG);   // pause_menu pauses, save_dialog runs

// Pop the top (calls on_exit on the popped scene).
stage_scene_pop();

// Pop the top and push a replacement in one step.
stage_scene_replace_top(SCENE_GAME_OVER);

// Query
blyt_scene_h top   = stage_scene_top();
uint8_t      depth = stage_scene_depth();
blyt_scene_h s     = stage_scene_at(0);   // 0 = bottom of stack
```

### Lua API

```lua
local S = require("cart_scenes")

stage.scene.set(S.GAMEPLAY)
stage.scene.push(S.PAUSE_MENU)
stage.scene.push(S.SAVE_DIALOG)
stage.scene.pop()
stage.scene.replace_top(S.GAME_OVER)

local top   = stage.scene.top()
local depth = stage.scene.depth()
```

### on_enter and save-state restore

**`on_enter` must not run on save-state restore.** Save-state restore
returns the cart to the exact frame captured — entity buffer fields,
timer countdowns, and all other state are already correct. Running
`on_enter` again would reinitialize entities, reset timers, and spawn
duplicates.

Stage distinguishes two restore paths:

- **Save-state restore** (`blyt_cart_on_load` called by the runtime
  during a save-state load): buffer state is already restored; Stage
  skips `on_enter` and calls a separate `on_resume` hook if declared.
  Every scene currently on the stack receives `on_resume`, bottom to
  top, so each layer can rebuild its Lua-side caches.
- **Save-game load** (cart calls `blyt32.save.read` to load a save
  slot): a scene transition initiated by the cart; the cart calls
  `stage_scene_set` (or equivalent) and `on_enter` runs normally.

Carts that need to rebuild Lua-side caches or re-register event
subscriptions after a save-state restore do so in `on_resume` rather
than `on_enter`.

### Save state

`scene_stack` and `scene_depth` are POD fields in the world buffer and
serialize automatically as part of the buffer save (ADR-0058). No
additional save/restore handling is required.

## Consequences

- Carts can layer scenes naturally — gameplay → inventory → save dialog
  works without inventing per-cart depth tracking.
- The default depth of 2 keeps the world-buffer footprint identical to
  the previous two-slot design for carts that don't opt into deeper
  nesting.
- Stack overflow is a manifest-bounded condition: pushing past
  `scene_stack_depth` panics deterministically rather than corrupting
  state. Authors raise the limit when they hit it.
- Scene identity survives save states automatically because the stack
  is a buffer field, not a Lua variable.
- The on_enter/on_resume distinction prevents entity duplication and
  state reset on save-state restore. After a restore, every stack
  frame gets `on_resume` so each layer rebuilds its Lua-side state.
- `background_update` is per scene, not per pair: a gameplay scene
  that should keep simulating under any modal sets it once and never
  reasons about which thing is on top.
- Scene declarations in the manifest let the packer validate that all
  referenced handler names exist, catching scene configuration errors
  at build time.

### Non-goals

- **Per-pair update gating** ("when X is on top of Y, Y should/shouldn't
  update"). `background_update` is per scene. Carts that need
  conditional ticking implement it inside the scene's update handler.
- **Stack inspection or rewriting at arbitrary positions.** The stack
  is push/pop/set/replace_top only. Carts that need to remove a scene
  from the middle restructure their flow.
- **Obscure/reveal callbacks** when a push/pop covers or uncovers a
  lower scene. Carts that need this emit events themselves.

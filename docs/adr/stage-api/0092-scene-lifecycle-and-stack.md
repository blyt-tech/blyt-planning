# ADR-0092: Scene lifecycle and stack

## Status
Accepted

## Context

Games are organized into discrete modes — title screen, gameplay, pause
menu, game over. Each mode has a lifecycle: it initializes when entered,
cleans up when exited, and drives update and draw each frame while active.

Two complications arise:

**Overlaid scenes.** A pause menu needs to draw over the running game
without destroying the game scene's state. This argues for a scene stack:
push the pause menu, pop it to resume. The question is how deep the stack
needs to be. For games at this fidelity, the common cases are one scene or
two (game + overlay). An arbitrary-depth stack adds complexity — which
scenes get update calls? — without serving most carts.

**Save state compatibility.** "Which scene am I in" is game state and must
survive save states. This means scene identity cannot live only in Lua;
it must be in a POD buffer field.

The `blyt_cart_draw` constraint (ADR-0076) also applies: `on_draw` must not
modify any state. Scene logic that produces side effects belongs in
`on_update`, not `on_draw`.

## Decision

**Stage provides a two-slot scene model: a primary scene and an optional
overlay. Scene identity is stored in the world state buffer.**

### Scene definition

A scene is a named set of handler functions declared in `cart.config.yaml`:

```yaml
# cart.config.yaml
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

### World buffer

Scene identity is stored in the world buffer, which carts declare as a
1-slot buffer in `cart.config.yaml`:

```yaml
state_buffers:
  world: { type: World, count: 1 }
  # minimum World fields for Stage:
  # current_scene:  u8
  # overlay_scene:  u8   (0 = no overlay)
```

### Primary and overlay scenes

The primary scene drives the main game. The overlay is an optional second
scene rendered and updated on top. When an overlay is active:

- Both `on_draw` callbacks run (primary first, overlay second).
- By default only the overlay receives `on_update`. The primary scene
  can opt into background updates by setting `background_update: true`
  in its scene declaration — used for games where simulation continues
  under a pause menu.

### C API

```c
// Transition primary scene (calls on_exit on current, on_enter on next)
stage_scene_set(SCENE_GAMEPLAY);

// Push an overlay (calls on_enter on overlay; primary pauses by default)
stage_scene_overlay_push(SCENE_PAUSE_MENU);

// Pop the overlay (calls on_exit on overlay; primary resumes)
stage_scene_overlay_pop();

// Query
blyt_scene_h current = stage_scene_current();
bool       has_overlay = stage_scene_has_overlay();
```

### Lua API

```lua
local S = require("cart_scenes")

stage.scene.set(S.SCENE_GAMEPLAY)
stage.scene.overlay_push(S.SCENE_PAUSE_MENU)
stage.scene.overlay_pop()
```

### on_enter and save-state restore

**`on_enter` must not run on save-state restore.** Save-state restore
returns the cart to the exact frame captured — entity buffer fields,
timer countdowns, and all other state are already correct. Running
`on_enter` again would reinitialize entities, reset timers, and
spawn duplicates.

Stage distinguishes two restore paths:

- **Save-state restore** (`blyt_cart_on_load` called by the runtime during
  a save-state load): buffer state is already restored; Stage skips
  `on_enter` and calls a separate `on_resume` hook if declared.
- **Save-game load** (cart calls `blyt32.save.read` to load a save slot):
  this is a scene transition initiated by the cart; `on_enter` runs normally.

Carts that need to rebuild Lua-side caches or re-register event subscriptions
after a save-state restore do so in `on_resume` rather than `on_enter`.

### Save state

`current_scene` and `overlay_scene` are u8 fields in the world buffer and
serialize automatically as part of the buffer save (ADR-0058). No additional
save/restore handling is required for the scene identity.

## Consequences

- The two-slot model (primary + overlay) covers the common cases cleanly
  without the ambiguity of which scenes get update calls in an arbitrary-
  depth stack.
- Scene identity survives save states automatically because it is a buffer
  field, not a Lua variable.
- The on_enter/on_resume distinction prevents entity duplication and state
  reset on save-state restore, a subtle bug that would otherwise be common.
- Carts that genuinely need more than one overlay must manage the additional
  depth themselves — this is uncommon at this fidelity.
- Scene declarations in the manifest allow the packer to validate that all
  referenced handler names exist, catching scene configuration errors at
  build time.

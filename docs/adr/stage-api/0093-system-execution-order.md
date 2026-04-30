# ADR-0093: System execution order

## Status
Accepted

## Context

A game's update loop consists of many systems — input, player control, AI,
physics, collision, animation, camera, rendering. The order they run in
determines what state each system sees. Running them in the wrong order
produces one-frame lags, inconsistencies between rendering and simulation,
or stale data reaching draw calls.

ADR-0076 establishes that `fc_cart_draw` must not modify state — it is a
read-only rendering pass. This divides all systems into two groups:
systems that write state (update) and systems that read state to produce
pixels (draw). Stage formalizes the canonical order within each group.

fc32 uses a fixed timestep with no `dt` parameter. Systems express time
in frames (1/fps seconds per step).

## Decision

**Stage defines a canonical system execution order. Carts can omit systems
they don't use or insert cart-specific systems at defined extension points.**

### fc_cart_update — all state writes

```
1.  stage_input_translate     reads hardware input → writes InputIntent (ADR-0100)
2.  [player controller]       reads InputIntent → writes entity Velocity, StateMachine
3.  [ai controllers]          reads entity state → writes entity Velocity
4.  [physics]                 reads Velocity → writes Position; applies gravity
5.  [collision]               reads/writes Position (resolves overlaps);
                              writes contact flags, on_ground
6.  stage_event_flush         drains event queue; handlers run here
                              (damage, triggers, deaths, pickups)
7.  [animation]               reads StateMachine → writes current_frame
8.  stage_camera_update       reads target entity Position → writes camera x/y
9.  stage_trauma_decay        reads/writes camera.trauma (approaches zero)
```

Steps 2–5 and 7 are cart-provided. Stage provides 1, 6, 8, and 9. The
positions of Stage-provided steps are fixed; cart steps are inserted between
them.

### fc_cart_draw — read only, no state writes (ADR-0076)

```
10. [clear / background]      fc_framebuffer_acquire, sky fill or clear color
11. [tilemaps]                draw scrolling layers back to front
12. [world entities]          for each entity: camera.to_screen(pos) → blit sprite
13. [particles]               drawn above or interleaved with entities by depth
14. [ui / hud]                screen-space coordinates only, no camera transform
```

All draw steps are cart-provided. Stage provides `stage_camera_to_screen`
as a helper; it does not drive the draw loop.

### Rationale for key ordering decisions

**Input first.** InputIntent is computed fresh each frame from hardware
state. Everything downstream reads intent, not hardware. If input ran after
player control, the controller would use last frame's intent.

**Player and AI before physics.** Both write Velocity this frame. Physics
integrates Velocity into Position. If physics ran first, player and AI
input would be one frame late.

**Collision after physics.** Physics moves entities into potentially
overlapping positions; collision corrects them. `on_ground` flags are set
here and are available to the player controller next frame — not this
frame, because player control has already run.

**Event flush after collision.** By the flush point, positions are final,
contact flags are set, and all velocity writes are complete. Handlers
therefore see settled state. Handlers that post further events queue them
for the next frame's flush.

**Animation after event flush.** Handlers may transition entity state
machines (e.g. setting an enemy to its death animation state). Animation
reads the state machine after all transitions have occurred.

**Camera last in update.** Camera follows entity positions, which are
finalized only after physics and collision. If camera ran earlier it would
lag one frame. Camera position must be final before `fc_cart_draw` reads it.

**Trauma decay after camera.** Trauma is a camera buffer field that drives
the screen shake offset passed to `fc_screen_shake` each frame. It decays
toward zero in update so that the value passed to `fc_screen_shake` reflects
this frame's shake magnitude.

**UI after world in draw.** UI is always rendered on top of the world. It
uses screen-space coordinates with no camera transform applied.

### Extension points

Carts insert their own systems by calling them at the appropriate position
within `fc_cart_update` and `fc_cart_draw`. Stage does not provide a system
registration mechanism — the order is expressed directly in cart code, which
is explicit and requires no framework machinery.

```c
void fc_cart_update(void) {
    stage_input_translate();        // step 1
    system_player_control();        // step 2
    system_ai();                    // step 3
    system_physics();               // step 4
    system_collision();             // step 5
    stage_event_flush();            // step 6
    system_animation();             // step 7
    stage_camera_update();          // step 8
    stage_trauma_decay();           // step 9
}

void fc_cart_draw(void) {
    draw_background();              // step 10
    draw_tilemaps();                // step 11
    draw_entities();                // step 12
    draw_ui();                      // step 14
}
```

```lua
function fc_cart_update()
    stage.input.translate()
    system.player_control()
    system.ai()
    system.physics()
    system.collision()
    stage.event.flush()
    system.animation()
    stage.camera.update()
    stage.trauma_decay()
end

function fc_cart_draw()
    draw_background()
    draw_tilemaps()
    draw_entities()
    draw_ui()
end
```

## Consequences

- A fixed, documented order eliminates a class of subtle bugs caused by
  systems reading state written by others in the wrong order.
- The update/draw split enforces ADR-0076: all state writes are complete
  before rendering begins.
- Stage-provided steps (input, event flush, camera, trauma decay) have
  fixed positions that other steps are organized around.
- Carts express the order directly in `fc_cart_update` and `fc_cart_draw` —
  no registration framework, no hidden ordering. What the code says is what
  runs.
- The fixed event flush point (step 6) means events posted by physics or
  collision are processed before animation sees their effects in the same
  frame.

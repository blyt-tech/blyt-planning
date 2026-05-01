# ADR-0102: Scene draw modes — imperative and declarative

## Status
Accepted

## Context

ADR-0093 specifies that the draw section of the frame (steps 10–14) is
cart-provided: the cart writes a `fc_cart_draw` body that calls draw
functions in order. This matches a pico-8 / SDL programming model and is
the right floor for low-level carts. It is the wrong ceiling for carts that
have already populated scene-scoped entity buffers (ADR-0101) — those carts
re-implement the same iterate-and-blit loop in every scene.

The aim is two coherent dev modes:

**Imperative.** The cart writes the draw body directly, calling
`fc_image_blit`, `fc_tilemap_draw`, `fc_text`, and similar in whatever
order it wants. Existing fc32 mental model. No change.

**Declarative.** The cart populates the scene; the runtime draws it. The
cart declares what's in the scene (clear colour, tilemaps, participating
entity buffers, optional UI hook); Stage walks that data and produces the
frame.

Mode is per-scene. A cart can have an imperative title screen, a declarative
gameplay scene, and an imperative pause overlay, in the same cart, because
draw mode is decided in the manifest per scene.

The HUD case folds cleanly into the declarative model when entities can
opt into screen-space rendering, removing a category that would otherwise
need its own machinery.

## Decision

**Each scene chooses imperative or declarative drawing by which fields it
declares in `cart.config.yaml`. Imperative mode supplies an `on_draw`
handler; declarative mode supplies scene contents and Stage drives the draw
loop. Reserved row fields (`x`, `y`, `sprite`, `z`) define the default
per-entity blit; per-buffer `draw_handler` overrides the default; per-scene
`on_draw` overrides everything. Pack-time validation rejects any
configuration that would silently fail to draw.**

### Mode selection

```yaml
scenes:
  # Imperative — cart owns the draw body
  - name: title_screen
    on_draw: scene_title_draw

  # Declarative — Stage owns the draw body
  - name: gameplay
    clear: 0x000000
    tilemaps: [bg_sky, bg_clouds, level]
    entities: [enemies, projectiles, particles, hud]
    ui: scene_game_draw_ui          # optional

  # Imperative overlay over a declarative primary
  - name: pause_menu
    on_draw: scene_pause_draw
```

A scene that declares `on_draw` is imperative; Stage hands control to the
handler and does not touch the framebuffer otherwise. A scene that omits
`on_draw` is declarative; Stage runs its default draw using the manifest
fields. The two are not mixed in a single scene declaration — a cart that
wants partial control uses a per-buffer draw handler or splits the work
into a primary + overlay pair.

### Reserved drawable fields

Buffers that participate in declarative drawing must supply, by name:

| Field          | Type | Meaning                                            |
|----------------|------|----------------------------------------------------|
| `x`            | f32  | Position of the sprite anchor                      |
| `y`            | f32  | Position of the sprite anchor                      |
| `sprite`       | u32  | Image handle (`fc_image_h`); `0` skips the slot    |
| `z`            | i16  | Draw order; lower draws first                      |

`x` and `y` are world-space by default; if the buffer declares
`screen_space: true` they are framebuffer pixels (see below). The packer
reserves these names the same way it reserves `scene_id` (ADR-0101). A
buffer that supplies all four is drawable by Stage without further
configuration.

### Per-buffer override: draw_handler

Buffers that need custom rendering — animated frames, particle palettes,
sprite atlases, custom blends — declare a draw handler:

```yaml
state_buffers:
  particles: { type: Particle, count: 256, scene_scoped: true,
               draw_handler: particle_draw }
```

The handler is a named-handler (ADR-0090) entry. Stage iterates the buffer
in z-sorted order and calls the handler with `(slot)` for each visible
slot. The handler can call `stage_camera_to_screen` if it wants the default
transform, or compute its own.

A buffer with a `draw_handler` need not declare the reserved drawable
fields — the handler's contract is to put pixels on screen however it
wants. It still participates in z-ordering: buffers with a draw handler
must declare a `z` field so Stage can sort their slots against other
buffers' slots in the same pass.

### Screen-space rendering — HUD as entities

Buffers can opt into screen-space mode to skip the camera transform:

```yaml
state_buffers:
  hud: { type: HudElem, count: 16, scene_scoped: true, screen_space: true }
```

In screen-space mode `x` and `y` are framebuffer pixel coordinates. Stage
draws screen-space buffers in a second pass after world-space buffers,
with their own z-sort, so HUD is never buried by world entities regardless
of z. The mixing rule is: world-space pass first (z-sorted across all
world-space buffers in the scene), screen-space pass second (z-sorted
across all screen-space buffers in the scene), `ui:` handler last.

This makes the dedicated `ui:` hook optional. A cart whose HUD is fully
entity-driven omits it. The hook remains for HUDs that resist the entity
model — animated minimap, post-process overlay, debug panel.

### Default draw pipeline

For a declarative scene, Stage's default body is:

```
1. fc_clear(scene.clear)
2. for each tilemap layer in declared order:
       draw with camera offset
3. world-space pass:
       collect visible slots from each scene-scoped buffer in scene.entities
       (visible = slot in scene OR SCENE_GLOBAL, sprite != 0)
       sort by (z, buffer_id, slot)
       for each slot:
           if buffer has draw_handler: call it
           else: blit sprite at stage_camera_to_screen(x, y)
4. screen-space pass:
       same as world-space but no camera transform; separate z-sort
5. if scene.ui declared: stage_handler_call(scene.ui, 0)
```

Tie-breaking by `(buffer_id, slot)` keeps draw order deterministic across
save/restore and across hosts (relevant to ADR-0083's bit-identity
requirements).

### Pack-time validation

The packer enforces participation rules at build time. Silent failures at
draw time are not allowed:

- A buffer listed in a scene's `entities` must have `scene_scoped: true`.
  Otherwise: pack-time error.
- A buffer listed in a scene's `entities` must either:
  - supply all four reserved drawable fields (`x`, `y`, `sprite`, `z`), OR
  - declare a `draw_handler` and a `z` field, OR
  - declare `non_drawable: true`.

  Otherwise: pack-time error.
- A buffer with `non_drawable: true` may participate in a scene (so it is
  cleared by `stage_scene_clear`) but Stage skips it during the draw loop.

```yaml
state_buffers:
  spawn_points: { type: SpawnPoint, count: 32, scene_scoped: true,
                  non_drawable: true }
```

A cart either draws an entity, supplies a custom draw path, or explicitly
opts out. There is no fourth case.

### Imperative mode is unchanged

Scenes with `on_draw` behave exactly as ADR-0093 already specifies. The
cart writes the body, calls draw functions directly, and may use Stage
helpers like `stage_camera_to_screen` if it wants. None of the declarative
machinery interferes; none of it costs anything at runtime in an imperative
scene.

A cart can mix freely: imperative title screen, declarative gameplay scene,
imperative pause overlay. Mode is per-scene, decided in the manifest, not
a cart-wide global.

### C API

```c
// Stage's default draw body — invoked when the active scene has no on_draw.
// Carts rarely call this directly; the runtime wires it in for declarative
// scenes.
void stage_scene_draw_default(void);

// Manual control over the entity draw pass, for hybrid layouts in
// imperative scenes that still want declarative entity rendering for part
// of the frame.
void stage_scene_draw_entities(void);
```

### Lua API

```lua
stage.scene.draw_default()
stage.scene.draw_entities()
```

### Save state

The draw pipeline is stateless across frames; declarative drawing has no
effect on save state. Buffer contents (positions, sprites, z) serialize as
ordinary buffer fields. Manifest declarations are static cart data and are
not part of the save image.

## Consequences

- Carts get pico-8-style imperative drawing as the floor and declarative
  scene drawing as a no-iteration alternative. Mode is per-scene; gradual
  migration from one to the other is natural.
- HUD collapses into a screen-space buffer with the same drawable
  conventions. The "HUD vs entities" category disappears; the optional
  `ui:` hook handles only HUDs that genuinely don't fit the entity model.
- Three escape-hatch layers compose cleanly:
  1. Default per-row blit using reserved fields — zero handler code.
  2. Per-buffer `draw_handler` — Stage drives iteration, cart controls
     each blit.
  3. Per-scene `on_draw` — cart controls the whole frame.
- Pack-time validation eliminates a class of silent draw-time bugs (entity
  in a scene's `entities` list but invisible because no draw path applied).
  Every buffer either draws, supplies a handler, or declares
  `non_drawable: true`.
- Z-ordering is deterministic across hosts and across save/restore because
  tie-breaking is `(z, buffer_id, slot)` — bit-identity-friendly per
  ADR-0083.
- Screen-space is a buffer-level flag, not a per-slot flag. Carts that
  need to mix world-space and screen-space items in one buffer declare a
  `draw_handler` and decide per slot. This keeps the default pipeline
  cheap and predictable.
- `non_drawable: true` is required for any pure-data buffer that
  participates in scene tagging (spawn points, prefab tables, AI
  scratchpads). Without it the packer rejects the manifest.
- Updates to existing ADRs:
  - **ADR-0092**: `on_draw` is optional. Its presence selects imperative
    mode; absence selects declarative.
  - **ADR-0093**: the draw section is "imperative-or-declarative per
    scene", not "always cart-provided." The default declarative pipeline
    executes the same logical steps the cart would have written by hand.
- Cart authors writing their first cart can populate scenes and never
  write a draw loop. Authors who need full control still get it via
  `on_draw`. Both groups are first-class.

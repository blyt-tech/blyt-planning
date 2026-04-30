# ADR-0095: Camera state and world-to-screen transform

## Status
Accepted

## Context

ADR-0048 established that the fc32 runtime has no global camera offset:
all draw calls use screen-space pixel coordinates measured from the
top-left of the 320×240 framebuffer. Cart code is responsible for
translating world coordinates to screen coordinates before calling draw
functions.

Every game that scrolls — which is most games — needs a camera: a world
position that defines what part of the world is currently visible, a zoom
factor, and optionally a rotation. The camera must follow entities, lerp
smoothly, handle screen shake, and be readable from both draw and update
code.

Screen shake is handled by the runtime at present time (ADR-0051): the
cart passes a trauma value to `fc_screen_shake` each frame, and the
runtime applies the resulting offset when blitting the framebuffer to
the display. The cart does not compute shake offsets — it only maintains
the trauma value.

Camera state that must survive save states — position, zoom, rotation,
and trauma — must live in a POD buffer field.

## Decision

**Stage provides camera state as a 1-slot state buffer declared in the
manifest, plus C and Lua helpers for world-to-screen transforms, entity
following, and trauma management.**

### Manifest

```yaml
# cart.config.yaml
types:
  Camera:
    x:        f32   # world position of camera centre
    y:        f32
    zoom:     f32   # 1.0 = no zoom
    rotation: f32   # radians; 0.0 = no rotation
    trauma:   f32   # 0.0..1.0; drives screen shake magnitude

state_buffers:
  camera: { type: Camera, count: 1 }
```

`rotation` may be omitted for carts that do not use camera rotation.
`zoom` defaults to 1.0; `trauma` defaults to 0.0.

### World-to-screen transform

The standard transform (no rotation):

```
screen_x = (world_x - camera.x) * camera.zoom + viewport_width  / 2
screen_y = (world_y - camera.y) * camera.zoom + viewport_height / 2
```

The viewport is always 320×240.

### C API

```c
// World-to-screen (no rotation)
void stage_camera_to_screen(float wx, float wy,
                             float *out_sx, float *out_sy);

// World-to-screen with rotation support
void stage_camera_to_screen_rot(float wx, float wy,
                                 float *out_sx, float *out_sy);

// Follow an entity slot (lerp toward target position)
void stage_camera_follow(fc_buffer_h buf, int32_t slot,
                         fc_field_h x_field, fc_field_h y_field,
                         float lerp_speed);

// Clamp camera to world bounds
void stage_camera_clamp(float min_x, float min_y,
                         float max_x, float max_y);

// Trauma management (called once per update; step 9 of ADR-0093)
void stage_trauma_decay(float decay_rate);   // default: 0.05 per frame
void stage_trauma_add(float amount);         // clamps to 1.0
```

`stage_camera_update` (step 8 in ADR-0093) calls `stage_camera_follow`
if a follow target is set. `stage_trauma_decay` (step 9) decays trauma
toward zero and calls `fc_screen_shake(trauma * trauma)` — squaring trauma
gives a more natural shake curve at low values.

### Lua API

```lua
-- World-to-screen (most common: no rotation)
local sx, sy = stage.camera.to_screen(wx, wy)

-- Follow target entity
stage.camera.follow(enemies, slot, "x", "y", 0.1)  -- lerp_speed

-- Clamp to world bounds
stage.camera.clamp(0, 0, world_width - 320, world_height - 240)

-- Shake
stage.camera.trauma_add(0.6)   -- e.g., on player hit

-- Direct field access via generated camera buffer
camera.x[1] = 0
camera.zoom[1] = 2.0
```

### Typical draw usage

```lua
function fc_cart_draw()
    -- Tilemap with camera scroll offset
    local cx = math.floor(camera.x[1])
    local cy = math.floor(camera.y[1])
    fc_tilemap_draw(T_LEVEL, -cx, -cy)

    -- Entities with world-to-screen transform
    for slot in enemies:slots() do
        local sx, sy = stage.camera.to_screen(enemies.x[slot], enemies.y[slot])
        -- simple visibility cull
        if sx > -32 and sx < 352 and sy > -32 and sy < 272 then
            fc_image_blit(enemies.sprite[slot], math.floor(sx), math.floor(sy), 0)
        end
    end
end
```

### Screen shake integration

Trauma is a camera buffer field (serializes automatically). Each frame,
`stage_trauma_decay` reads the field, calls `fc_screen_shake(trauma²)`,
then decays the field value. The cart never interacts with
`fc_screen_shake` directly when using Stage — it calls `stage_trauma_add`
to trigger shake and Stage manages the rest.

## Consequences

- Camera state survives save states automatically as POD buffer fields.
- Screen shake is decoupled from the camera transform: the runtime applies
  the shake offset at present time (ADR-0051); cart draw code uses
  `stage_camera_to_screen` which returns the un-shaken transform.
- Flooring the screen coordinates before passing to draw functions prevents
  sub-pixel jitter when scrolling — this is a convention, not enforced.
- The `follow` and `clamp` helpers cover the common cases. Carts with
  unusual camera behaviour (split-screen, cutscene pans) write directly
  to the camera buffer fields.
- Rotation support is provided but adds a multiply to the transform. Carts
  that never rotate can use the cheaper no-rotation variant.

# ADR-0100: Input intent translation

## Status
Accepted

## Context

The fc32 runtime provides raw hardware input: `fc_input_held(player, button)`,
`fc_input_pressed(player, button)`, and pointer state. Every game translates
this raw state into higher-level intent before passing it to game logic:
horizontal movement is a -1..1 axis derived from left/right buttons; jump is
a boolean that is true only on the frame the button is pressed.

Without a translation step, game logic is littered with raw button names.
Changing the control scheme (remapping a button, adding analogue stick
support) requires changes throughout the codebase. More importantly, the
same raw-to-intent translation is repeated identically for every player slot,
every frame — it belongs in one place.

ADR-0043 establishes that carts declare which inputs they use in the manifest.
InputIntent is the per-frame, per-player output of that declaration applied
to the current hardware state.

## Decision

**Stage translates raw hardware input into a structured `InputIntent` value
once per frame, per player, at the start of `fc_cart_update` (step 1,
ADR-0093). InputIntent is a plain value — a C struct or Lua table — passed
directly to controller systems. It is not stored in a state buffer.**

InputIntent is transient: it is computed fresh each frame from hardware state
and does not need to survive save states. It is not a POD buffer field.

### InputIntent structure

```c
typedef struct {
    float move_x;       // -1.0 (left) .. 1.0 (right); 0 = neutral
    float move_y;       // -1.0 (up)   .. 1.0 (down);  0 = neutral
    bool  jump;         // true on the frame the jump button is first pressed
    bool  attack;       // true on the frame the attack button is first pressed
    bool  interact;     // true on the frame the interact button is first pressed
    bool  jump_held;    // true while jump button is held
    bool  attack_held;  // true while attack button is held
    bool  pause;        // true on the frame the pause button is first pressed
} stage_input_intent_t;
```

`jump`, `attack`, `interact`, and `pause` are edge-triggered (pressed this
frame only). `jump_held` and `attack_held` are level-triggered (held state),
used for variable-height jumps and held attacks.

### Manifest mapping

The intent fields are wired to hardware buttons via the manifest, extending
the `inputs_used` declaration from ADR-0043:

```yaml
# cart.config.yaml
inputs_used:
  dpad:     Move       # maps to move_x / move_y
  button_a: Jump       # maps to intent.jump / intent.jump_held
  button_b: Attack     # maps to intent.attack / intent.attack_held
  button_x: Interact   # maps to intent.interact
  start:    Pause      # maps to intent.pause
```

Stage uses this mapping to fill IntentIntent from the hardware state. Carts
that want custom mappings (e.g. analogue stick for move_x/move_y) register
a custom translate handler.

### C API

```c
// Called by Stage at step 1 of fc_cart_update for each active player
void stage_input_translate(void);

// Read the current frame's intent for a given player (0-indexed)
stage_input_intent_t stage_input_intent(int player);

// Register a custom translation handler (optional; overrides manifest mapping)
void stage_input_set_translator(fc_handler_h handler);
```

Usage in a player controller system:

```c
static void system_player_control(void) {
    stage_input_intent_t intent = stage_input_intent(0);

    if (intent.jump && player.on_ground) {
        fc_buffer_set_f32(S_PLAYERS, 0, S_PLAYER_VY, -JUMP_FORCE);
    }
    float vx = intent.move_x * WALK_SPEED;
    fc_buffer_set_f32(S_PLAYERS, 0, S_PLAYER_VX, vx);
}
```

### Lua API

```lua
-- Read intent (called after stage.input.translate() in fc_cart_update)
local intent = stage.input.intent(0)  -- player 0

if intent.jump and players.on_ground[0] then
    players.vy[0] = -JUMP_FORCE
end
players.vx[0] = intent.move_x * WALK_SPEED
```

In Lua, `stage.input.intent` returns a table with the same field names as
the C struct. The table is reused each frame (not reallocated) to avoid GC
pressure.

### Multi-player

`stage_input_translate` fills intent for all active players. Player count
is determined by `max_players` in `cart.info.yaml` (ADR-0073). Controllers
with no connected hardware produce zero-valued intent (no movement, all
booleans false).

### Custom translation handler

Carts can register a named handler (ADR-0090) to override the manifest-
driven translation. The custom handler receives a player index and a pointer
to the intent struct to fill:

```c
static void my_translator(int32_t player, uint32_t unused) {
    stage_input_intent_t *intent = stage_input_intent_ptr(player);
    intent->move_x = fc_input_axis_x(player, FC_AXIS_LEFT_STICK);
    intent->jump   = fc_input_pressed(player, FC_BTN_A);
    // ...
}
stage_input_set_translator(HANDLER_MY_TRANSLATOR);
```

This is the extension point for analogue input, accessibility remapping,
and AI-driven intent injection (useful for replays and testing).

## Consequences

- Game logic reads `intent.jump` instead of `fc_input_pressed(0, FC_BTN_A)`.
  Remapping a button requires one change in the manifest, not changes
  throughout game code.
- Edge-triggered booleans (`jump`, `attack`) are computed once per frame
  in one place, eliminating duplicated `fc_input_pressed` calls.
- The intent table is transient — it is not saved, not stored in a buffer,
  and not accessible after the frame that produced it.
- The custom translator handler is the clean extension point for analogue
  sticks, accessibility options, and replay systems that inject pre-recorded
  input.
- AI controllers can use the same interface as the player controller by
  producing an `InputIntent` from AI logic, making AI and player code
  interchangeable from the physics system's perspective.

# ADR-0019: Expose cosmetic device info to carts; withhold capability info

## Status
Accepted

## Context

Players expect games to show correct button prompts ("Press Cross to jump" on
PlayStation, "Press A" on Xbox, "Press Z" on keyboard). Implementing this
requires carts to know something about the connected input device. However,
exposing full device capabilities (extra buttons, analog sticks, gyro) would
tempt or require carts to branch on hardware, undermining the console's fixed
input spec.

## Decision

Carts can query attached input devices for **cosmetic purposes only**:

```lua
local info = input.device_info(1)
-- { kind, name, labels = { a, b, x, y, l, r, start, select, ... } }

local jump_key = input.button_label(1, "a")
draw_text("Press " .. jump_key .. " to jump", 10, 10)
```

**Exposed:** device kind (gamepad/keyboard/other), device name, per-button
display labels, connection/hot-plug events.

**Not exposed:** device family (xbox/playstation/nintendo/etc.), extra buttons
the spec doesn't use (L2/R2, stick clicks, home/share), analog stick values,
touchpad, gyro, LEDs, rumble (deferred; if added, as opt-in with graceful
degradation).

**No `family` field.** Carts have no reason to branch on manufacturer.
For text prompts, `button_label` already gives the right glyyph-letter for
the connected device. For icon prompts, the runtime owns rendering: the
abstract prompt API in the bundled `font_icons` (ADR-0042) resolves
placeholders like `{prompt_action_a}` to the appropriate device-family
glyph automatically. The runtime needs to know the family internally to
drive that resolution, but the cart does not — and exposing it would only
invite carts to ship parallel per-family icon sets that the runtime already
provides.

The abstract button spec (D-pad + 4 face + 2 shoulders + Start/Select) is
fixed and unconditional. Carts cannot meaningfully branch on "this controller
has more buttons."

**Nintendo button-layout handling:** abstract button IDs follow positional
convention (SNES-style). The runtime maps physical device conventions so the
button in the correct position is pressed regardless of manufacturer labeling.

## Consequences

- Correct on-screen prompts are achievable at low implementation cost (SDL's
  gamepad database provides most data).
- The console's input spec stays fixed — carts branch on label, not
  capability.
- Family-appropriate icons are rendered automatically by the runtime's
  prompt API (ADR-0042); carts neither query nor ship per-family icon sets.
- Libretro's input descriptor mechanism (core tells frontend "button A means
  Jump") can be populated from cart metadata for RetroArch's remapping UI.

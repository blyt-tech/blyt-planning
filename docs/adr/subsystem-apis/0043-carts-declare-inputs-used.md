# ADR-0043: Carts declare which inputs they use

## Status
Accepted

## Context

Touch control overlays on mobile show all 10 abstract buttons by default.
A puzzle game using only D-pad and one face button gets a cluttered overlay
with 7 unused buttons. Hiding unused controls requires the runtime to know
which buttons the cart actually reads. Inferring this at runtime (watching
which input APIs are called) is unreliable; declarative manifest is cleaner.

## Decision

Carts optionally declare the set of inputs they use in the manifest:

```yaml
# cart.config.yaml
inputs_used: [dpad, button_a, button_b, start]
```

Or with labels for tooltips / prompts:

```yaml
# cart.config.yaml
inputs_used:
  dpad:     Move
  button_a: Jump
  button_b: Attack
  start:    Pause
```

**Group keywords:** `dpad`, `face_buttons`, `shoulders`, `system` expand to
their members. Individual button names also work.

**Runtime behavior:**
- **Touch scheme:** hides unused controls. Remaining buttons render larger
  and better-placed in the freed screen real estate.
- **Gamepad / keyboard:** declaration is informational. Prompt icons, input
  tutorials, and dev-mode overlays filter to the declared set.
- **Labels** (if provided) appear in touch button labels and prompt
  substitutions (`{action_a}` → "Jump").

**Declaration is a contract:** the cart receives events only for buttons in
its declared set; buttons outside the set are filtered by the runtime before
reaching the cart.

If `inputs_used` is **not declared**, the cart receives the full 10-button set
and the full virtual gamepad overlay on touch (safe default).

**Dev mode warning:** cart code reading a button not in its effective input
set triggers a warning — the declaration and code are out of sync.

## Consequences

- Touch overlay adapts to the cart's actual needs: a 2-button puzzle game
  gets a 2-button touch UI.
- The declaration is optional but encouraged; authors who see unused buttons
  in the touch overlay are naturally prompted to declare.
- `start` special handling: if a cart doesn't declare `start`, the runtime
  captures it for the pause menu. If the cart declares `start`, the runtime
  uses an alternative gesture for its system overlay.
- A cart that sets `interactive: false` (ADR-0031) and also declares
  `inputs_used` is a packer error. The two are contradictory: declaring
  inputs is the cart's way of saying it consumes them, while
  `interactive: false` declares the opposite. Failing the build avoids
  shipping a cart whose two manifest fields disagree.
- Pointer input declared via `inputs_used = ["pointer", ...]`; pointer-only
  carts can suppress the virtual gamepad entirely.

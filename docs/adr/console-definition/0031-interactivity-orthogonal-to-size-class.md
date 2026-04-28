# ADR-0031: Interactivity is orthogonal to size class

## Status
Accepted

## Context

Non-interactive carts (demoscene intros, ambient experiences, generative art)
and interactive carts (games) have different runtime behaviors: non-interactive
carts don't need touch overlays, don't need input handling, and benefit from
loop/duration declarations. If non-interactivity were coupled to the Demo size
class, non-interactive pieces with more content (an ambient music player, a
longer generative art piece) would be arbitrarily constrained.

## Decision

**Carts separately declare whether they are interactive.** Combined with size
class, this captures the cart's nature on two orthogonal axes.

```yaml
# cart.info.yaml
size_class:  demo
interactive: false

# when interactive: false
duration_hint: 2m30s   # optional: approximate duration
loops:         true    # default true; false means defined ending
```

- `interactive = true` (default): cart receives input per its `inputs_used`
  declaration. Runtime displays appropriate control UI for the declared
  inputs.
- `interactive = false`: cart receives no input. Runtime captures Start for
  pause/unpause only. Touch platforms display no control overlay — full screen
  for the cart.

`loops = true` (default): runtime restarts the cart when it completes.
`loops = false`: runtime displays a "complete" screen with option to restart.

Screensaver plugins, kiosk attract modes, and curated ambient-display
playlists filter on `interactive = false` regardless of size class.

## Consequences

- Demo + non-interactive = classic demoscene intro.
- Demo + interactive = tiny arcade game or micro-experience.
- Mini + non-interactive = ambient music player with more content than Demo.
- Standard + non-interactive = generative art piece, longer visual experience.
- Forcing non-interactive content into the Demo class (256 KB) would be
  artificially restrictive for ambient pieces that naturally want Mini scope.
- Screensaver / kiosk integration is format-agnostic across size classes.

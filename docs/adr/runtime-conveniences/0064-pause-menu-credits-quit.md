# ADR-0064: Pause menu, credits, and quit — runtime-provided with manifest declaration

## Status
Accepted

## Context

Every cart needs a pause menu (at minimum), a quit path, and optionally
credits. Implementing these from scratch in every cart is redundant; they also
require access to runtime internals (save state, screenshot, platform quit
signal). A runtime-provided UI is consistent across all carts and reduces
the minimum viable cart size.

## Decision

**The runtime provides a standard pause menu, credits screen, and quit
confirmation dialog. Carts declare their structure in the manifest.**

### Pause menu

The runtime captures the Start button (or equivalent gesture on touch) when
the cart declares `start` is not in its `inputs_used` (ADR-0043). The runtime
overlays a standard pause menu:

```yaml
# cart.config.yaml
pause_items:
  - { id: resume,   label_key: pause.resume }
  - { id: settings, label_key: pause.settings }
  - { id: credits,  label_key: pause.credits }
  - { id: quit,     label_key: pause.quit }
```

`label_key` values are locale keys (ADR-0062); the packer resolves each to
the corresponding `L_*` constant when generating `PAUSE_ITEM_*`.

The packer generates `PAUSE_ITEM_RESUME`, `PAUSE_ITEM_SETTINGS`, etc.
The cart receives a callback for each item selection:

```c
void blyt_cart_on_pause_item(blyt_pause_item_h item);
```

Standard items (`PAUSE_ITEM_QUIT`, `PAUSE_ITEM_CREDITS`) are handled
entirely by the runtime if the cart does not override `blyt_cart_on_pause_item`.

### Credits

```yaml
# cart.config.yaml
credits_file: credits.txt  # plain text; runtime formats and scrolls
```

Alternatively, the cart implements `blyt_cart_on_credits()` to render its own
credits sequence.

### Quit confirmation

`blyt_quit_request()` (callable by cart code or triggered by the pause menu)
shows a runtime-rendered confirmation dialog. On confirmation the runtime
calls the frontend's quit callback. The dialog is non-skippable on the first
call (prevents accidental quits); subsequent calls in the same session may
skip the dialog via flag.

## Consequences

- All carts have a working pause/quit flow from day one with zero cart code.
- The runtime-provided UI is styled consistently with the console brand;
  carts can override per item if they want custom behavior.
- Credits are trivial (a text file), removing a common excuse for shipping
  without credits.
- The runtime-provided quit dialog handles platform-specific quit
  confirmation requirements (some platforms require explicit confirmation
  before exit).

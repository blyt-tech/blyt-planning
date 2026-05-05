# ADR-0097: Timer and alarm system

## Status
Accepted

## Context

Delayed and repeating callbacks are needed throughout game logic: spawn the
next enemy wave in 120 frames, flash the screen for 10 frames, trigger
ambient dialogue after 5 seconds of silence. Per-entity timers are naturally
expressed as buffer fields on the entity itself — no additional infrastructure
needed. The problem is scene-level or game-global timers: these have no
natural entity to live on, and a Lua local (`local spawn_timer = 120`) will
not survive save states.

A managed timer pool solves this: timers are stored in a manifest-declared
POD buffer (so they serialize automatically), callbacks are named handler IDs
(so they survive save states and hot reload), and Stage ticks the pool each
frame.

## Decision

**Stage provides a fixed-size timer pool declared in the manifest. Each
timer stores a countdown, a handler ID to call on expiry, a payload value,
and a repeat flag. The pool is ticked automatically at the start of
`stage_event_flush` (step 6, ADR-0093): expired timers post their handler as
an event, then are freed or rescheduled.**

### Manifest

```yaml
# cart.config.yaml
stage:
  timer_slots: 32   # max concurrent timers; default 16
```

### Timer slot storage (POD, in buffer)

```
remaining_frames: u16    frames until expiry; 0 = inactive
handler_id:       u8     called on expiry (blyt_handler_h)
repeat_frames:    u16    if non-zero, reschedule with this count on expiry
subject:          u16    passed to handler as first argument
data:             u32    passed to handler as second argument
```

All fields are POD; the slot pool is a manifest-declared state buffer and
serializes automatically.

### C API

```c
// Schedule a one-shot timer
blyt_timer_h stage_timer_after(uint16_t frames,
                              blyt_handler_h handler,
                              uint16_t subject,
                              uint32_t data);

// Schedule a repeating timer
blyt_timer_h stage_timer_every(uint16_t frames,
                              blyt_handler_h handler,
                              uint16_t subject,
                              uint32_t data);

// Cancel a timer
void stage_timer_cancel(blyt_timer_h timer);

// BLYT_TIMER_NONE = 0 (invalid / already expired handle)
```

`blyt_timer_h` is a `uint16_t` slot index. It is valid only until the timer
expires (one-shot) or is cancelled. Carts that need to cancel a timer before
expiry must store the handle in a buffer field.

### Lua API

```lua
local H = require("cart_handlers")

-- One-shot: call HANDLER_SPAWN_WAVE after 180 frames, data = wave number
local t = stage.timer.after(180, H.SPAWN_WAVE, 0, wave_number)

-- Repeating: call HANDLER_AMBIENT_TICK every 300 frames
stage.timer.every(300, H.AMBIENT_TICK, 0, 0)

-- Cancel
stage.timer.cancel(t)
```

### Expiry dispatch

When a timer expires, Stage calls the registered handler with `(subject, data)`
directly — it does not post an event to the bus. This avoids consuming event
queue capacity for every timer tick and makes the handler call synchronous
with the flush step, ensuring timer handlers run at the same ordering position
as event handlers.

Repeating timers reschedule themselves automatically: on expiry, if
`repeat_frames > 0`, Stage sets `remaining_frames = repeat_frames` and does
not free the slot.

### Save state

The timer pool is a POD state buffer. All fields — including `remaining_frames`
— are serialized with the buffer. A save state taken mid-countdown restores
with the countdown intact; a save game loaded via `blyt32.save.read` also
restores countdown state. Timers continue from exactly where they were.

### Pool overflow

If `stage_timer_after` or `stage_timer_every` is called when the pool is
full, the call fails and returns `BLYT_TIMER_NONE`. In dev builds this
additionally asserts. Carts that overflow their pool should increase
`timer_slots` in the manifest.

## Consequences

- Scene-level and game-global timers survive save states automatically
  because the timer pool is a POD buffer.
- Repeating timers (ambient events, periodic spawns) require no per-frame
  cart code — they fire and reschedule themselves.
- Timer handles (`blyt_timer_h`) are valid only until expiry or cancellation.
  Carts that need to cancel timers store the handle in a buffer field (u16
  or u32).
- The per-slot memory cost is 11 bytes; 32 slots = 352 bytes. A generous
  pool is cheap.
- Expiry calls the handler directly rather than posting to the event bus,
  keeping the timer mechanism simple and avoiding indirect dependencies on
  bus queue capacity.

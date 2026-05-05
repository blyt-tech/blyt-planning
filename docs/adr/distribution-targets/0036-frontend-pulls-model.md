# ADR-0036: Frontend-pulls model — frontend owns the fixed-timestep accumulator

## Status
Accepted

## Context

The initial design (ADR-0037) stated that the runtime owns and drives the
fixed-timestep accumulator, calling into the frontend when it needs a frame
rendered. This puts timing control inside the runtime library, which
constrains how frontends can integrate frame pacing, V-sync, and audio
synchronisation.

During API design the control flow was reconsidered. The runtime is a
library (`libblyt`); it has no event loop of its own. Frontends
(SDL2, libretro, Emscripten, hardware) each have their own frame-pacing
requirements and cannot all be served by a single runtime-driven loop.
Moving accumulator ownership to the frontend gives each frontend full
control over timing while keeping the runtime stateless between calls.

## Decision

**The frontend owns the fixed-timestep accumulator and drives the runtime
via pull calls.** The runtime exposes two primary cart-execution entry
points:

```c
// Advance simulation by exactly one logical tick (1/60 s by default,
// or the rate declared by blyt_cart_fps()).
blyt_result_t blyt_runtime_update(blyt_runtime_h rt);

// Render the current frame into the runtime-owned framebuffer.
blyt_result_t blyt_runtime_draw(blyt_runtime_h rt);
```

The frontend is responsible for:
1. Measuring wall-clock elapsed time each frame.
2. Accumulating it and calling `blyt_runtime_update()` once per logical tick,
   up to a maximum of 3 catch-up ticks per render to avoid spiral-of-death.
3. Calling `blyt_runtime_draw()` once per render frame.
4. Reading the framebuffer via `blyt_runtime_get_framebuffer()` and blitting
   it to the display.

The cart declares its preferred tick rate via `blyt_cart_fps()` (read from
`cart.config`); the default is 60. The frontend is not required to honour
rates other than 60, but may do so.

The runtime never calls back into the frontend for frame pacing. Callbacks
from runtime to frontend are limited to: audio buffer fill, input poll, and
event notifications (cart save written, session started/ended, quit
requested, etc.). Achievement-unlock events join this set when the
achievement system ships (ADR-0014).

## Consequences

- Each frontend implements its own accumulator loop appropriate to its
  platform (SDL2 event loop, libretro retro_run, Emscripten
  emscripten_set_main_loop, hardware bare-metal timer).
- The runtime has no hidden thread or event loop; it is fully re-entrant
  within a single thread.
- Frontends can implement frame interpolation, variable-rate rendering, and
  audio-driven timing without fighting the runtime.
- ADR-0037 (fixed-timestep main loop) is updated to reflect that
  accumulator ownership sits in the frontend; the runtime only specifies
  the rules (1/60 tick, 3-tick cap, watchdog).
- ADR-0033 (runtime as library) is updated to list the pull API as the
  canonical integration pattern.

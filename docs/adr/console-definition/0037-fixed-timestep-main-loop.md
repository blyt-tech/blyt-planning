# ADR-0037: Fixed-timestep main loop with accumulator

## Status
Accepted

## Context

Game loops can use variable timestep (dt varies per frame based on actual
elapsed time) or fixed timestep (dt is always 1/60, independent of wall-clock
speed). Variable timestep is easier to implement but makes determinism
(ADR-0007) very difficult — two runs with different frame times produce
different simulation results.

Fixed timestep with a naive sleep loop wastes CPU and causes jerky rendering
when the host's display refresh doesn't align with the game's logical rate.
The accumulator pattern is the standard solution for clean fixed-timestep
with smooth presentation.

## Decision

**Fixed-timestep with accumulator, owned by the frontend (ADR-0036).**

- `update(dt)` always receives `dt = 1/60` (logical 60 Hz, or the rate
  declared by `fc_cart_fps()` in `cart.config`).
- The **frontend** maintains the logical-time accumulator and drives
  `fc_runtime_update()` / `fc_runtime_draw()` (see ADR-0036). The runtime
  does not own or advance the accumulator.
- Accumulator rules (enforced by convention and documented for all frontends):
  - Add elapsed real time each iteration, capped at 250 ms to prevent death
    spiral.
  - While accumulator ≥ tick interval **and** updates-this-frame < 3, call
    `fc_runtime_update()`.
  - Call `fc_runtime_draw()` once per render frame.
- Cap of 3 catch-up updates per render: mild overrun absorbed invisibly;
  severe overrun degrades gracefully to slow-motion gameplay rather than
  spiraling.
- **Watchdog:** a single `update` call exceeding a hard time limit (e.g.,
  2 seconds) terminates the cart with a diagnostic. Enforced via
  emulator-level instruction budget for all carts on emulated platforms;
  OS timer (`SIGALRM` or equivalent) for all carts on real RISC-V
  hardware. A Lua instruction-count hook is not used: because Lua runs as
  a RISC-V library inside the emulator (ADR-0025), Lua carts are
  indistinguishable from native carts at the emulator level, and a
  hook-based abort would travel through Lua's error system where `pcall`
  could intercept it (ADR-0084).

The libretro frontend delegates frame pacing to RetroArch via `retro_run`;
the fixed-timestep logic is its own accumulator loop for non-libretro
frontends.

## Consequences

- Determinism (ADR-0007) is guaranteed: `dt` is always the declared tick
  interval, input is snapshotted once per logical tick, and the simulation
  produces identical results regardless of host render rate.
- Save states, rewind, replay, and netplay all rely on the fixed logical
  timestep.
- Cart authors write `update(dt)` and can treat dt as a constant or use
  `console.time.frame()` for frame-count-based logic — both are correct.
- The 250 ms accumulator cap means a brief freeze (garbage collection on
  the host, context switch) is absorbed without a burst of many catch-up
  ticks.
- The 3-update cap means a cart that consistently takes too long to update
  will slow to perceived slow-motion rather than panicking.
- Carts with pathologically slow `update` implementations eventually hit the
  watchdog, which terminates them with a diagnostic rather than hanging the
  runtime.
- Each frontend implements its own accumulator loop; shared documentation
  specifies the cap and watchdog rules so all frontends behave consistently.

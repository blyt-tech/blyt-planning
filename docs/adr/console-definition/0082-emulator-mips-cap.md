# ADR-0082: Emulator MIPS cap fixed to minimum emulation host throughput

## Status
Accepted

## Context

The fixed-timestep model (ADR-0037) ensures that a cart's logical simulation
is identical across all platforms — `dt` is always constant, and the same
sequence of inputs produces bit-identical state. However, it says nothing
about how much work a cart may do within a single tick. A developer working
on a fast desktop can write an `update()` that executes hundreds of millions
of guest instructions and completes in 2 ms locally, while the same tick
would take 20 ms through the interpreter on a Pi Zero 2 W — missing the
frame budget entirely.

Without a cap, the only signal a developer gets is the watchdog (which fires
at pathological levels, not at floor-hardware levels) and the experience of
actually running on minimum hardware. This means carts can be accidentally
authored that work everywhere the developer tests but fail on the declared
minimum emulation host.

The minimum emulation host is a Pi Zero 2 W class device (Cortex-A53 @ 1 GHz,
ARMv8). The effective MIPS throughput of the RV32IMFC interpreter on this
hardware is the real performance ceiling for any cart running under emulation.
That ceiling applies whether the emulator is running on a Pi, a desktop, or
in a browser — the cart cannot know or benefit from the faster host.

## Decision

**All emulator runs cap guest instruction throughput to the measured effective
MIPS of the minimum emulation host.** This applies unconditionally: desktop
development builds, the libretro core, WASM, and any other emulator frontend.
It does not apply to native execution on RISC-V hardware, where carts run
directly on the CPU with no interpreter.

The cap is implemented as a throttle on the emulator's main step loop. After
each batch of emulated cycles (`rv_step()` in rv32emu, or equivalent in any
other interpreter), the emulator computes how much wall-clock time those
cycles would have taken at the target MIPS rate and sleeps the difference if
it is running ahead.

```
after each step():
  emulated_ns = cycles_this_step * 1_000_000_000 / (target_mips * 1_000_000)
  wall_elapsed = clock_gettime() - step_start
  if emulated_ns > wall_elapsed:
    nanosleep(emulated_ns - wall_elapsed)
```

**The target MIPS value is determined empirically by Spike A** of the early
validation programme (see `docs/design/early-validation-spikes.md`): run
CoreMark and Embench on a Pi Zero 2 W through the interpreter, measure
effective throughput, and use the result as the cap. The value is baked into
the runtime at build time and not user-configurable in release builds.

Until Spike A produces a measured value, the cap is left unset (emulator runs
at full speed). This is the only period in which fast-host drift is accepted.

## Consequences

- A cart that runs correctly under emulation on any host is guaranteed to fit
  the floor hardware's performance budget. Developers do not need to own a
  Pi Zero 2 W to validate performance.
- The emulated experience is consistent across all hosts: desktop, CI, WASM,
  and libretro all behave identically in terms of how much work a cart can do
  per tick.
- Fast hosts (desktop, CI runners) spend a portion of each frame budget
  sleeping. This is the correct trade-off: the emulator's job is to faithfully
  represent the target platform, not to run as fast as possible.
- The watchdog threshold (ADR-0037) becomes meaningful relative to the cap:
  a cart that hits the watchdog under emulation would also have hit it on real
  hardware.
- On WASM targets, `nanosleep` is not available; the equivalent is to skip
  `requestAnimationFrame` callbacks when the emulator is running ahead, or to
  use `setTimeout` for sub-frame delays. The same cap value applies; only the
  sleep mechanism differs.
- JIT-compiled execution paths (if a JIT is added in future) have approximate
  cycle costs rather than exact ones. The MIPS cap remains correct in
  aggregate even if individual block costs are approximate.
- The cap does not apply on native RISC-V hardware. A Milk-V Duo runs carts
  directly; its CPU speed is what it is, and no emulator layer is present to
  throttle.

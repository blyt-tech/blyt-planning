# ADR-0007: Full structural determinism

## Status
Accepted

## Context

Save states, rewind, deterministic replay, and netplay all require that the
same cart, given the same starting state and the same sequence of inputs,
produces bit-identical results on every platform and run. This is not free:
floating-point transcendentals, RNG, wall-clock time, GC timing, and input
event timing are all natural sources of non-determinism that must be
explicitly controlled.

The alternative — non-deterministic by default, with optional determinism
for individual features — was rejected. It produces fragile features that
work on some hardware configurations but not others, and generates hard-to-
reproduce bugs.

## Decision

**The console is fully deterministic given identical inputs and starting
state.** A cart running on any platform (native RISC-V, interpreted desktop,
WASM browser, libretro) with identical input sequences produces bit-identical
frame-by-frame results.

Sources of variation and their controls:

- **Floating-point:** basic ops are IEEE 754 bit-identical in strict mode.
  Compiler flags enforced: `-ffp-contract=off`, `-fno-fast-math`,
  `-fno-unsafe-math-optimizations`, `-fwrapv`, `-frounding-math`,
  `-fsignaling-nans`. No `-Ofast`. Runtime sets host FP state (MXCSR/FPCR)
  on emulator entry.
- **Transcendentals:** provided by the console API using a deterministic
  implementation (musl libm as reference). Carts do not use the host's libm.
- **NaN:** canonicalized on write to state buffers.
- **RISC-V interpreter:** uses SoftFloat for guest FP, or host FP with
  careful state management.
- **Integer overflow:** wraparound semantics enforced by `-fwrapv` in native
  carts and by Lua spec.
- **RNG:** runtime-owned, seedable streams; state lives in tracked regions
  (save state preserves it). No uninitialized sources.
- **Time:** only deterministic time exposed (`blyt.time.frame()`). No
  wall-clock API. `dt` in update is always 1/60.
- **Input:** snapshotted once per logical update; frozen within update.
  Events recorded as `(frame_n, button_state)` tuples, not timestamps.
- **Audio:** one-way data flow (cart → mixer) for determinism purposes.
  Exception: `blyt_voice_is_playing()` queries actual mixer state (see ADR-0006).
  The exception is bounded and acceptable; see ADR-0006 for rationale.
- **Resource loading:** synchronous. Observable behavior ("I called load, got
  a result") is identical regardless of decompression latency.
- **Coroutines:** cooperative only; runtime never preempts.
- **GC:** incremental, triggered by allocation pressure. Two identical runs
  make identical allocations, triggering GC identically. No GC state is
  cart-observable.

Acknowledged non-determinism:
- Wall-clock time to reach frame N differs across hosts (feature, not bug).
- Watchdog kill frame depends on host speed (only affects buggy carts).
- Memory-pressure eviction causes latency spikes but not state divergence.

## Consequences

- Save states, rewind, replay, and netplay all work as structural properties
  of the system, not as optional bolt-on features.
- CI validates determinism via cross-platform bit-identity tests (same cart
  workload, compared frame-by-frame across platforms).
- Cart authors cannot use `math.sin` from Lua's standard library; they use
  `blyt32.math.sin` (the deterministic version). This is enforced by the
  Lua sandbox (ADR-0038).
- Transcendental determinism requires the console to own its math library,
  which is a maintenance responsibility.
- The RISC-V interpreter must be validated against the `riscv-tests`
  conformance suite to guarantee it matches hardware behavior.

# ADR-0076: draw() is read-only over tracked state

## Status
Accepted

## Context

The runtime's fixed-timestep loop (ADR-0037) calls `update()` then `draw()`
each logical frame. The separation exists to keep simulation and presentation
distinct: `update()` advances the world; `draw()` renders it.

The canonical save state snapshot is taken after `update()`, before
`draw()`. If `draw()` writes to tracked state, that snapshot is incomplete
— restoring it skips the mutations draw would have applied. The save point
would have to move to after `draw()`, which makes draw part of the
simulation tick rather than a view over it, and complicates every place
that reasons about "the state at frame N."

### Cosmetic RNG is not an exception

An initial position was that a separate untracked "cosmetic" RNG stream
could advance freely in `draw()`, since visual noise (particle spread,
sparkle positions) does not affect gameplay outcomes. On examination this
is the wrong boundary:

- **Netplay**: each client runs `draw()` independently after syncing tracked
  state. Untracked cosmetic RNG diverges between clients, so spectators
  and players see different visual effects.
- **Replays and demos**: a replay re-runs `update()` with recorded inputs;
  if cosmetic RNG only advances in `draw()`, replay visuals differ from the
  original run.
- **Save/restore**: cosmetic effects in flight (particles, ambient sparkles)
  vanish on load because their state was never captured.

These are avoidable inconsistencies. The "cosmetic" label describes the
*semantics* of a RNG stream — it influences visuals but not gameplay
outcomes — not whether the stream lives outside tracked state.

## Decision

**`draw()` must not write to tracked state.** This includes all named RNG
streams (ADR-0041): cosmetic RNG streams advance in `update()` and are
read in `draw()` like any other tracked state. A cart calling
`console.rng.next("sparks")` in `draw()` gets the same value on all
clients, in replays, and after save/restore, because the sparks stream
already advanced in the preceding `update()` tick.

Lua-local temporaries computed inside `draw()` — interpolated camera
positions, scratch display values, variables that do not persist past the
function — are unaffected. The restriction is on writes to tracked state
buffers and globals that influence subsequent `update()` calls.

**The same principle applies to anything that transparently advances
simulation state.** Cases that matter in practice:

- **RNG consumption**: calling `console.rng.next()` in `draw()` advances
  the RNG stream, changing the sequence every subsequent call will see.
  RNG streams are tracked state; consuming them in `draw()` is a state
  write by another name. All RNG calls belong in `update()`.

- **Audio triggering**: calling `console.audio.play()` in `draw()` is
  an instruction that lands in simulation state — the frame on which a
  sound fires is how `is_playing()` queries are answered (ADR-0006).
  Triggering in `draw()` also risks double-firing if the frontend ever
  calls `draw()` more than once per tick (e.g., when rendering a save
  state thumbnail). Audio triggers belong in `update()`.

- **Palette modifications**: programmatic palette changes (colour
  substitutions, fade steps, manual palette cycling beyond the
  auto-advance in ADR-0061) alter the palette table, which is tracked
  state. A palette write in `draw()` would be lost on save/restore and
  would diverge across netplay clients. Palette updates belong in
  `update()`; `draw()` reads the current palette as set by the preceding
  tick.

The general rule: if a cart call has any effect that persists beyond the
current `draw()` invocation or that other systems observe, it belongs in
`update()`.

**Enforcement is soft: dev-mode detection, not a runtime lock.**

Making state buffers physically read-only during `draw()` would impose
runtime overhead in release builds and complicate legitimate patterns such
as writing to a draw-local scratch region declared in the manifest. A
detection approach is sufficient:

- In dev mode, the runtime tracks writes to tracked state during the
  `draw()` phase.
- Any write triggers a warning identifying the buffer, field, and source
  location.
- The warning is promoted to a hard error when the `--strict` dev flag is
  set, for authors who want to enforce the rule as a compile-time
  discipline.
- No detection overhead in release builds.

## Consequences

- Save state semantics are clean: the snapshot after `update()` fully
  captures cart state; `draw()` is a deterministic view over that snapshot.
- Visual output is deterministic across netplay clients, replays, spectator
  mode, and save/restore cycles — including cosmetic effects.
- Cosmetic RNG streams (named streams used only for visual variation) are
  slightly larger save states than if they were untracked. The cost is
  negligible: a few bytes per stream.
- Authors who accidentally mutate state in `draw()` are caught in dev mode
  rather than discovering the bug as save/restore corruption or netplay
  visual divergence.
- The mental model is simple and has no exceptions: `update()` writes,
  `draw()` reads.

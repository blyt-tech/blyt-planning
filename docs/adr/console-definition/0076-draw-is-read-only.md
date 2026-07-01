# ADR-0076: draw() is read-only over tracked state

## Status
Accepted — amended 2026-07-01 (#205, draw()-only extended to surface access)

> **Forward note (2026-06-29):** spec **#195** extends this simulation/
> presentation boundary to the framebuffer surface itself — the `screen` is
> read *and* write only within `draw()` (enforced), generalizing ADR-0122's
> prev-frame phase restriction. This is now **implemented (#205)** — see the
> amendment at the end of this ADR.

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

### Cosmetic RNG is the one carve-out

`BLYT_RNG_COSMETIC` (ADR-0041) is the single named stream readable
from `draw()`, and it is also the **only** stream the cart may not
read from `update()`. Forcing carts to compute draw-time noise
(per-frame sparkle offsets, particle jitter, screen-flicker) in
`update()` and stash it in state buffers just to satisfy the
read-only-draw rule is awkward for randomness that has no gameplay
significance and no inter-frame lifetime.

The runtime captures the cosmetic stream's post-draw state as a
per-frame recorded input (the same shape as ADR-0106's voice-end
events) and applies it as the stream's value at the start of the
next `update()`. Save state captures the post-draw value rather than
a post-update one for this stream specifically.

This dissolves the three concerns that originally argued against the
exception:

- **Netplay**: each peer runs `draw()` deterministically from
  identical state, so the post-draw cosmetic state is identical on
  every peer locally — no wire traffic. ADR-0106 covers the same
  rule in general for per-frame recorded inputs.
- **Replays and demos**: the recorded post-draw state is canonical;
  replay reproduces the visuals exactly.
- **Save/restore**: the post-draw value is what survives; the next
  frame's `draw()` resumes from it.

The split rule — `next(RNG.COSMETIC)` only in `draw()`,
`next(RNG.<anything else>)` only in `update()` — turns the
"cosmetic" label from a convention into a physical invariant.
Nothing the cart computes from cosmetic RNG can ever feed back into
simulation, because `update()` cannot read it. ADR-0041 specifies
the stream's full contract (post-draw capture, draw-only `next`,
seeding allowed from either phase).

## Decision

**`draw()` must not write to tracked state, with one carve-out:
`BLYT_RNG_COSMETIC` (see above and ADR-0041).** All other named RNG
streams (ADR-0041) advance in `update()` and are read in `draw()`
like any other tracked state. A cart calling
`blyt32.rng.next("sparks")` (a cart-declared stream) in `draw()` is
a warning in dev mode; the cart should advance "sparks" in
`update()` and read the produced value during draw.

Lua-local temporaries computed inside `draw()` — interpolated camera
positions, scratch display values, variables that do not persist past the
function — are unaffected. The restriction is on writes to tracked state
buffers and globals that influence subsequent `update()` calls.

**The same principle applies to anything that transparently advances
simulation state.** Cases that matter in practice:

- **RNG consumption**: calling `blyt32.rng.next()` in `draw()` advances
  the RNG stream, changing the sequence every subsequent call will see.
  RNG streams are tracked state; consuming them in `draw()` is a state
  write by another name. All RNG calls belong in `update()`, except
  `BLYT_RNG_COSMETIC`, which is the inverse — `next(RNG.COSMETIC)`
  is only valid in `draw()`, and the runtime captures the post-draw
  state as a per-frame recorded input (ADR-0041).

- **Audio triggering**: calling `blyt32.audio.play()` in `draw()` is
  an instruction that lands in simulation state — the frame on which a
  sound fires is how `is_playing()` queries are answered (ADR-0106).
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
  location. Cosmetic-RNG advances in `draw()` are exempt; the inverse
  rule (`next(RNG.COSMETIC)` in `update()`) is also a warning.
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

## Amendment (#195/#205, 2026-07-01): surface access is draw()-only, enforced

#205 (PR-B surfaces) extends the `update()`/`draw()` boundary to **surface
access** — all surfaces, including the `screen` — and makes it a *hard* contract,
not the soft dev-detection this ADR specifies for tracked-state writes:

- **Mechanism.** The runtime tracks a lifecycle `phase` (NONE/INIT/UPDATE/DRAW).
  The host cannot bracket `draw()` on the emulated path (the guest's `blyt_main`
  drives the loop and the host only sees ECALLs), so `blyt_main` emits a phase
  signal (`BLYT_ECALL_PHASE`, `runtime/shared/blyt_phase.h`) around each callback;
  the native and WASM host-Lua loops mirror it. Every surface access op is
  permitted only while the phase is `DRAW`.
- **Enforcement is hard and determinism-bearing** (unlike the soft dev-mode
  warning for state-buffer writes above, which stays soft). Outside `draw()`:
  a **debug** cart hard-errors (a dev trap → run abort); a **release** cart gets
  a *defined, leg-identical* no-op — writes drop, and a tier-2 `acquire` reads as
  **cleared** (zeros, never the real surface). Never UB, never a content leak.
  The behaviour must be bit-identical across every execution leg, so it is part
  of the determinism contract rather than a dev aid.
- **Why stronger here.** A state-buffer write in `draw()` corrupts *save-state
  fidelity* (caught by detection); an out-of-phase surface read could leak
  presentation state into logic or diverge across legs, so it is closed with a
  defined runtime behaviour on every leg, not merely flagged.

This subsumes ADR-0122's per-accessor phase restriction: the same phase gate now
governs all surface access. The tracked-state rule in this ADR's Decision is
unchanged (still soft dev detection).

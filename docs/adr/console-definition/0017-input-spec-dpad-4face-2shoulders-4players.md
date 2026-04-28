# ADR-0017: Input spec — D-pad + 4 face + 2 shoulders + Start/Select, up to 4 players

## Status
Accepted

## Context

The input button set determines which game genres are reachable and how well
the console maps to existing physical controllers. Too few buttons limits
genre range; too many makes touch emulation impractical and keyboard mapping
awkward.

Analog sticks were considered. Most games at this fidelity and scope don't
need analog input; keyboard emulation of analog is poor; the WASM and mobile
touch paths have no natural analog input. The design opts for digital only in v1.

**Shoulder buttons** are necessary to reach the target genre range. D-pad +
4 face buttons alone (a SNES-minus-shoulders layout) leaves out a large
category of action and platformer verbs: dodge/roll, sprint, guard, shoulder-
swap, item cycle, camera pan. These verbs need a dedicated button that doesn't
interfere with the main action cluster. Shoulders also fill a natural
keyboard position — one key on each side symmetrically above the face group —
making keyboard play ergonomic for action games.

**Two shoulders (L, R) rather than four (L1/L2/R1/R2).** The main use cases
for four shoulder buttons are analog triggers (not applicable — this console
is digital-only) and thumbstick-click buttons (not applicable — no sticks).
As digital buttons, L2/R2 add a second cramped row to the touch overlay and
require a third dedicated key position on each side of the keyboard. The genre
space reachable with two digital shoulders covers virtually every target genre:
platformers, Metroidvanias, action-RPGs, beat-'em-ups, top-down shooters,
racing games. Four-shoulder designs principally serve FPS and TPS genres, which
are not the target at 320×240.

## Decision

**10 abstract buttons per player:**
- D-pad (4 directions)
- 4 face buttons (A, B, X, Y — positional convention, SNES-style)
- 2 shoulder buttons (L, R)
- Start, Select

**Up to 4 players.**

**Mapping is runtime preference, not cart-visible.** Carts see abstract
button IDs; runtime translates from physical input.

**Three keyboard presets (player 1):**
- Arrow-style (default): Arrows + ZXCV + A/S + Enter/Right-Shift
- Gamepad-style: WASD + HJKL + U/O + Enter/Right-Shift
- Arrow-ASDF variant: Arrows + ASDF + Q/W + Enter/Right-Shift

**One device, one player.** Keyboard: player 1 only. Each gamepad maps to
one player, in connection order.

**Analog sticks** excluded from v1; may be added in v2 if demand emerges.

## Consequences

- Genre coverage: couch multiplayer (4-player party games, beat-'em-ups,
  Bomberman-likes, Micro Machines-style racers) is fully supported.
- The face button positional convention (A=right, B=bottom, X=top, Y=left)
  maps to SNES/PlayStation/Xbox layouts; Nintendo Switch layout is handled
  by the runtime's cosmetic device info (ADR-0019) remapping button labels.
- Shoulder buttons being mapped above the action group (not split) suits
  keyboard authors who use same-hand modifier chords.
- Local 2–4 player requires each player to have their own gamepad. No
  keyboard splitting.
- Architectural cost of 4 players over 2 is minimal: negligible extra state,
  same function signatures with player index 1–4, controller assignment
  loops to 4 slots.
- Carts storing per-player state should use fixed 4-element arrays from v1,
  even for single-player carts, for ecosystem consistency.

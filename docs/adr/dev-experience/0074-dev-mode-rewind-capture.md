# ADR-0074: Dev-mode rewind capture for debugging

## Status
Accepted

## Context

Reproducing and diagnosing bugs requires access to recent cart history.
Traditional post-mortem inspection of a crash only shows state at the
moment of failure; interesting bugs are often the result of a sequence
of events that began tens of seconds or minutes earlier.

The runtime already has save/restore infrastructure (ADR-0007, ADR-0045)
and deterministic input recording (ADR-0015 — used for speedrun replay).
These provide the mechanical foundations for a history-capture facility
at very low marginal cost.

**Player-facing rewind is explicitly out of scope for the runtime.**
It is available via RetroArch (which uses `retro_serialize` /
`retro_unserialize` to maintain its own rewind buffer) or other libretro
frontends that choose to implement it. Carts distributed outside the
RetroArch ecosystem — standalone desktop builds, hardware images, browser
— have no expectation of player rewind, and the runtime does not provide
it. This ADR is specifically about a dev-mode debugging facility, not a
player feature.

## Decision

**Dev mode maintains a rolling 5-minute history buffer consisting of
periodic save-state anchor points plus a continuous input log.**

The history buffer is only active in dev mode (`--dev` flag or equivalent).
It does not exist in release builds; the memory and CPU cost do not affect
players.

### Buffer contents

- **Save-state anchors**: the runtime snapshots full cart state (using the
  same `save_state()` machinery as hot reload and save games) at regular
  intervals. These serve as seek points; replaying from an anchor forward
  through the input log reaches any desired frame.
- **Input log**: every player input event for the last 5 minutes, recorded
  as `(frame_number, player_index, button_state)` tuples. Identical in
  format to the speedrun replay input log (ADR-0015).

The anchor interval trades memory against seek granularity. A 1-second
anchor interval for a typical cart with a few hundred KB of state costs
roughly 300 save states × state_size over 5 minutes. The interval is a
runtime implementation detail; v1 targets a seek-forward time of at most
a few seconds from any anchor.

### Capture triggers

The full history buffer is written to disk as a **debug capture file** on
any of three triggers:

1. **Cart crash**: automatically on any unhandled trap or runtime error.
   The capture is written before the crash screen is displayed; the crash
   screen notes the capture path so the author can locate it.

2. **Dev hotkey**: a keyboard shortcut in dev mode (default: a key that
   is not mapped to any cart input — exact binding is dev-mode UI
   configuration). Useful for capturing the sequence before a
   not-yet-crashed but misbehaving state.

3. **Cart API call**:
   ```lua
   blyt32.dev.dump()          -- write history buffer to disk now
   blyt32.dev.dump("label")   -- include a label in the filename
   ```
   Only available in dev mode; a no-op in release builds (ADR-0065
   pattern). This is the most powerful trigger: the author instruments
   a condition in the cart code and the capture fires automatically
   when that condition is true, even if the bug is subtle or takes
   time to manifest.

   Example:
   ```lua
   if enemy_count < 0 then          -- should never happen
       blyt32.dev.dump("negative-enemy-count")
   end
   ```

### Debug capture file

A debug capture file contains:
- Cart binary hash (to verify the replay matches the cart being debugged).
- Runtime version.
- The history buffer: all anchor save states and the full input log.
- A timestamp and optional label.

The file is written to the dev-mode output directory (same location as
other dev artefacts). File size is dominated by the state snapshots and
varies with cart complexity; typical small Lua carts produce captures of
a few hundred MB for a 5-minute window, which is acceptable for
development.

### Replay

Loading a debug capture file in dev mode:

1. The runtime restores the oldest anchor save state in the buffer.
2. It replays the recorded input log frame-by-frame, exactly as the
   speedrun replay mechanism works (ADR-0015). There are no forks or
   time splits — replay is linear from buffer start to the trigger point.
3. The DAP debugger (for Lua carts) or GDB remote server (for native
   carts) can be attached during replay. Breakpoints set before replay
   starts fire at the correct frames; the author can step forward
   frame-by-frame from any breakpoint.

The combination of "seek to any recent second" (via anchor + input
re-simulation) and "attach debugger and step forward" gives a
**backwards-in-time debugging** experience: the author knows approximately
when a bug manifests, rewinds to just before that point, and steps forward
with full inspection capability.

Frame-by-frame stepping backward (as a dedicated UI gesture) is deferred
to v2 — it requires a denser snapshot strategy or real-time rewind
synthesis. The v1 flow (seek to anchor, replay forward to target frame)
is sufficient and reuses existing infrastructure.

## Consequences

- Dev mode has a higher memory footprint due to the rolling history buffer.
  This is only active in dev mode; players are unaffected.
- `blyt32.dev.dump()` follows the ADR-0065 pattern: the call is present
  in the API header; the release implementation is a no-op. Carts can ship
  with dev instrumentation in place without it having any effect.
- The debug capture format is compatible with the speedrun replay format
  (ADR-0015) — same cart hash, runtime version, and input log structure.
  The additional save-state anchors are the main extension; tooling for
  speedrun replays can read captures (treating the first anchor as the
  starting state).
- The 5-minute window is a dev-mode default and may be adjustable as a
  dev config option for particularly time-consuming bugs.
- Player rewind remains exclusively a frontend concern; this ADR makes
  no provision for exposing the history buffer to players or for surface-
  ing rewind controls in the custom frontend.

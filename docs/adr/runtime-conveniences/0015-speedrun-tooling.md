# ADR-0015: Speedrun tooling built on deterministic replay

## Status
Accepted

## Context

The determinism guarantee (ADR-0007) and save state infrastructure (ADR-0013)
naturally support speedrun tooling. Frame-accurate timing, input recording,
and replay verification are nearly free given what is already built. The
question is which speedrun features to build in v1 vs. defer.

## Decision

**V1 scope:**

**Input log recording / replay.** The runtime records all player inputs
during a session and exports a replay file. Another instance (same cart
binary, same runtime version) replays the file and reaches bit-identical
state. Replay format:

- Header: cart binary hash (BLAKE3-256, ADR-0021), runtime version,
  session root seed (ADR-0041).
- Per-frame: input state for every player slot. A few bytes per frame.
- **Periodic full state snapshot every N frames.** Each snapshot is the
  same payload as a `save_state()` call: all tracked state regions plus
  all cart state buffers. Snapshots are optional during *playback* but
  always written during *recording*.

The periodic snapshots are dead weight if a recording replays cleanly, but
they pay back hard in three cases:

1. **Verification.** During replay the runtime compares each recorded
   snapshot against the live state at the same frame. Any mismatch
   pinpoints divergence to a single snapshot interval — much cheaper to
   debug than a 30-minute desync caught only at the final state.
2. **Nondeterminism hunting.** Authors investigating "this replay desyncs
   after the level-3 boss" run the replay with verification on; the runtime
   reports the exact interval where state first diverged, narrowing the
   search to N frames of input and code.
3. **Random-access scrubbing.** Tooling can jump to any snapshot-aligned
   frame by loading the corresponding snapshot rather than re-simulating
   from frame zero — useful for replay viewers, attract-mode resume, and
   bisecting bug reports.

**Cadence:** the cart declares the snapshot interval in
`cart.config.yaml`:

```yaml
# cart.config.yaml
speedrun_snapshot_interval: 60   # frames; default = declared fps (≈1 second)
# 0 disables periodic snapshots entirely
```

Cadence is in frames rather than Hz because the runtime is
frame-deterministic (ADR-0007); a frame count is exact and independent of
the cart's declared `fps` (ADR-0047). The default tracks `fps` so a
typical 60 Hz cart snapshots once per second and a 30 Hz cart snapshots
every 30 frames (also once per second), giving consistent verification
granularity in wall-clock terms across rates without authors having to
think about it.

Snapshots compress well (most cart state is sparse or repetitive); even
once per second they remain a small fraction of total replay size for
typical carts. Carts with very large state buffers (200-entity pools,
big mutable tilemaps) can lengthen the interval to trade verification
precision for replay file size; carts where every byte matters can
disable snapshots entirely with `0`.

**Frame-accurate timing.** `blyt32.time.frame()` is the deterministic frame
counter. Carts can compute run times natively if they want to display a
timer in-game.

**Speedrun mode** (player-selectable runtime setting):
- Pre-decompresses all cart resources to disk or locked memory, eliminating
  mid-run decompression latency spikes.
- Disables save states and rewind (would invalidate runs).
- Shows a timer overlay.
- Records input log automatically (with periodic snapshots).
- Uses a fixed session root seed so every attempt starts from the same
  RNG state. The seed source is the manifest's `rng_seed` if set
  (ADR-0041); otherwise the runtime supplies a documented constant
  reserved for speedrun mode.

**Replay-as-demo.** The runtime supports loading "cart + input log" as a
playable unit, with inputs injected over the deterministic simulation.
Use cases: author-provided "watch the intended solution," community speedrun
replays, attract mode / screensaver playback.

**Deferred to v2:**
- Cart-declared split points and in-runtime split time display.
- Community leaderboards and verified run submission.
- LiveSplit integration.

## Consequences

- Replay infrastructure is essentially free given existing determinism and
  save state work.
- Periodic snapshots make replay verification a one-pass operation: a
  desync is reported with snapshot-interval precision rather than
  discovered silently at the end of a long run.
- Replay file size is dominated by inputs (a few bytes per frame) rather
  than snapshots for typical carts; large state buffers (e.g., 200-entity
  enemy pools) push snapshot cost up but compression keeps it bounded.
  Carts override the snapshot interval (in frames) per their needs.
- Speedrun mode's pre-decompression requires sufficient disk space; falls
  back to lazy decompression with a warning if disk space is insufficient.
- The replay-as-demo format doubles as an attract mode mechanism for kiosk
  deployments and for Demo-class non-interactive carts; the periodic
  snapshots also make attract-mode "skip to interesting bit" trivial,
  jumping to the nearest snapshot frame.
- Community can build external split tooling (LiveSplit integration) on top
  of v1 APIs before the runtime provides native split UI.

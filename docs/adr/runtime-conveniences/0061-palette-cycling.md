# ADR-0061: Palette cycling — manifest-declared and auto-advancing

## Status
Accepted

## Context

Palette cycling (rotating a contiguous range of palette entries each frame)
is a classic technique for animating water, lava, sky gradients, and other
effects without per-frame sprite updates. It requires per-frame palette
mutations that should be deterministic and survive save/restore.

## Decision

**Palette cycles are declared in the manifest; the runtime auto-advances them
each frame; cycle state is tracked in the serialized save state.**

```yaml
# cart.config.yaml
palette_cycles:
  water: { start: 32, count: 8, direction:  1, interval: 2 }
  lava:  { start: 40, count: 4, direction: -1, interval: 1 }
```

The packer generates constants:

```c
#define CYCLE_WATER ((fc_cycle_h)1)
#define CYCLE_LAVA  ((fc_cycle_h)2)
```

Each cycle advances automatically every `interval` frames in the given
`direction` (1 = forward, -1 = backward). The runtime updates cycle state
once per tick (not per draw), before `draw()` is called.

Carts can override cycle behavior at runtime:

```c
fc_result_t fc_cycle_set_speed(fc_cycle_h cycle, int32_t interval);
fc_result_t fc_cycle_set_direction(fc_cycle_h cycle, int32_t direction);
fc_result_t fc_cycle_pause(fc_cycle_h cycle);
fc_result_t fc_cycle_resume(fc_cycle_h cycle);
fc_result_t fc_cycle_set_offset(fc_cycle_h cycle, int32_t offset);
```

Cycle state (current offset, speed, direction, paused flag) is stored in
the runtime's own tracked save-state region.

## Consequences

- Water and lava animations are zero-CPU-cost per sprite: they are palette
  mutations applied once per frame globally.
- Cycle state is deterministic and serialized: save states taken mid-cycle
  restore correctly; replay is bit-identical.
- Declaring cycles in the manifest allows the packer to validate palette
  ranges (no overlapping cycles, no out-of-bounds range).
- Runtime overrides (speed, direction, pause) enable interactive effects
  (slow lava when player activates a freeze spell) without losing the
  manifest declaration as the base configuration.

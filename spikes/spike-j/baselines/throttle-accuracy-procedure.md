# Throttle accuracy regression — procedure

Stage 1 step 5 of the plan: re-run Spike G.3's accuracy bench with the
master hook in place at `MASTER_THROTTLE` (`cfg.throttle_enabled = true`,
all other flags off). The target accuracy is unchanged from Spike G.3 —
within ±0.4 % of the Pi target across the five-bench set.

## Benches

`doom_tick`, `entity_update`, `binarytrees`, `mandelbrot`, `spectral-norm`
— same as Spike G.3 Stage 4. The `.lua` sources are vendored under
`spikes/spike-f/benchmarks/` and are not packaged as rv32emu carts.

## Two ways to run

1. **Lua-direct host (matches Spike G.3's harness)**: build a host
   binary that links `lib/master_hook.c` against the host Lua VM, like
   `host/dap_lua_host.c` but without the DAP server thread. The same
   `cfg.throttle_*` state. This is the straightforward port of
   `spikes/spike-g.3/throttle_host.c`.

2. **Repackage as carts**: each `.lua` becomes a Spike I case_c-style
   cart whose `main.lua` is the bench. Run under rv32emu with
   `MASTER_THROTTLE` linked into libconsolelua.so. Higher overhead; only
   needed if the Lua-direct measurement is suspect.

## Calibration

Reuse Spike G.3's `ns_per_line` table — the master hook adds one branch
and one struct load per fire compared to the standalone throttle. Spike
J does not re-derive the calibration; it re-measures accuracy under the
new dispatcher. If accuracy drifts beyond ±0.4 %, the dispatcher
overhead at line granularity is non-negligible and the table needs
adjustment (or the production model becomes "throttle off when DAP is
stepping" — a design implication the plan calls out under risks).

## Comparison vs Spike G.3

Tabulate per-bench:

| bench           | spike-g.3 measured ns/frame | spike-j MASTER_THROTTLE ns/frame | drift |
|-----------------|-----------------------------|----------------------------------|-------|
| doom_tick       | TBD                         | TBD                              | TBD   |
| entity_update   | TBD                         | TBD                              | TBD   |
| binarytrees     | TBD                         | TBD                              | TBD   |
| mandelbrot      | TBD                         | TBD                              | TBD   |
| spectral-norm   | TBD                         | TBD                              | TBD   |

Drift > ±0.4 % triggers a design-implications note in the result doc.

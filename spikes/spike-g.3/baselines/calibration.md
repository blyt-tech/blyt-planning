# Spike G.3 — calibration constants

The accumulated-debt hook in Spike G.3 inherits its `ns_per_line` constants
from Spike G.2; they are properties of the workload and the Pi target, not
of the hook mechanism. Reproduced here for convenience — see
`spikes/spike-g.2/baselines/calibration.md` for the derivation.

```
ns_per_line_bench = (pi_target_mid_ns − wasm_nohook_mean_ns) / lines_per_frame
```

| bench         | Pi target @6× (ms) | nohook mean (ms) | lines/frame | ns_per_line @6× |
|---------------|--------------------|------------------|-------------|-----------------|
| doom_tick     | 328.93             | 1.59             | 109,173     | **3,000**       |
| entity_update | 86.94              | 0.74             | 39,868      | 2,162           |
| binarytrees   | 380.39             | 2.80             | 49,151      | 7,682           |
| mandelbrot    | 555.93             | 3.73             | 729,888     | 756             |
| spectral-norm | 669.17             | 5.11             | 168,031     | 3,952           |

`doom_tick` is the calibration target. The Stage 3 sweep uses
D=2,250 / 3,000 / 3,750 — the same 4× / 6× / 8× Pi-band points G.2 used:

| variant | D (ns/line) | source                              |
|---------|-------------|-------------------------------------|
| D_LOW   | 2,250       | 0.75 × D_MID — calibrated to Pi @4× |
| D_MID   | 3,000       | doom_tick @ Pi @ 6× midpoint        |
| D_HIGH  | 3,750       | 1.25 × D_MID — calibrated to Pi @8× |
| D0      | 0           | hook installed, debt never grows    |

Per-bench line counts (reused from `spikes/spike-g.2/baselines/linecount/`):

| bench         | LINE_TOTAL  | frames | lines/frame |
|---------------|-------------|--------|-------------|
| doom_tick     | 3,275,190   | 30     | 109,173     |
| doom_tick_gc  | 3,278,220   | 30     | 109,274     |
| entity_update | 1,993,400   | 50     | 39,868      |
| binarytrees   | 983,020     | 20     | 49,151      |
| mandelbrot    | 14,597,760  | 20     | 729,888     |
| spectral-norm | 3,360,620   | 20     | 168,031     |
| fannkuch      | 4,764,400   | 20     | 238,220     |
| fasta         | 406,220     | 20     | 20,311      |
| nbody         | 173,160     | 20     | 8,658       |

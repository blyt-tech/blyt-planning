# Spike G.2 — calibration table

Computed `ns_per_line` constants for each bench, derived from Spike B Docker
means (Pi-projection midpoint at 6×) and Spike G.2 chrome-nohook means.

```
ns_per_line_bench = (pi_target_mid_ns − wasm_nohook_mean_ns) / lines_per_frame
```

| bench         | Docker mean (µs) | Pi @4× (ms) | Pi @6× (ms) | Pi @8× (ms) | WASM nohook mean (ms) | lines/frame | ns_per_line @6× |
|---------------|------------------|-------------|-------------|-------------|-----------------------|-------------|-----------------|
| doom_tick     | 54,822           | 219.29      | 328.93      | 438.58      | 1.59                  | 109,173     | **3,000**       |
| entity_update | 14,490           | 57.96       | 86.94       | 115.92      | 0.74                  | 39,868      | 2,162           |
| binarytrees   | 63,398           | 253.59      | 380.39      | 506.78      | 2.80                  | 49,151      | 7,682           |
| mandelbrot    | 92,655           | 370.62      | 555.93      | 741.24      | 3.73                  | 729,888     | 756             |
| spectral-norm | 111,528          | 446.11      | 669.17      | 892.22      | 5.11                  | 168,031     | 3,952           |

`doom_tick` is the primary calibration target — its 3,000 ns/line constant is
the `D_MID` value used to build the Stage 4 sweep:

| variant | D (ns/line) | source                            |
|---------|-------------|-----------------------------------|
| D_LOW   | 2,250       | 0.75 × D_MID — calibrated to Pi @ 4× |
| D_MID   | 3,000       | doom_tick @ Pi @ 6× midpoint      |
| D_HIGH  | 3,750       | 1.25 × D_MID — calibrated to Pi @ 8× |
| D0      | 0           | hook installed, no busy-wait      |

## Linecount source data

`LINE_TOTAL` from `baselines/linecount/<bench>.txt` divided by outer-frame
count (30 for `doom_tick`/`doom_tick_gc`, 50 for `entity_update`, 20 for
others):

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

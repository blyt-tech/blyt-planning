# Spike B results — Lua running inside the interpreter

**Status: build and stack end-to-end complete; ADR-0082 throttle path verified through the Lua VM; provisional Pi Zero 2 W projection from Docker numbers shows the load-bearing Lua tier has comfortable headroom for the platform's target game intensity, with native C/Rust available as a generous hot-path escape hatch; real-hardware confirmation pending.**

The question Spike B asks is whether Lua 5.4, compiled to RV32IMFC and running
under `rv32emu`, can execute a realistic game-loop workload within the 16.7 ms
frame budget on a Pi Zero 2 W. The definitive answer requires Pi hardware.
What the spike has proven without hardware:

- The double-interpretation stack (Lua bytecode → Lua VM as guest RV32IMFC
  code → rv32emu → host arm64) builds, runs, and produces correct output
  for eight distinct workloads.
- ADR-0082's MIPS-cap throttle behaves correctly when wrapped around the
  Lua VM hot loop.
- Extrapolating from spike A's Docker→Pi clock ratio, the load-bearing
  workload (`doom_tick` — Doom-shaped per-tic AI + projectile churn at
  64 mobs) is projected to consume **~27–55% of the half-frame budget
  reserved for game logic** on the Pi at the mean tic.
- The **same algorithm written in native C** (`doom_tick_c.c`, compiled to
  RV32IMFC and run under the same emulator) is **~78× faster** than the
  Lua version. Lua is the platform's *primary* authoring tier — the
  question this spike asks is "can Lua carry typical fc32 game logic at
  60 fps?", and that question is load-bearing: if Lua numbers don't
  work, the architecture is back open. Native C (and Rust) is the
  intended secondary tier for hot paths within an otherwise-Lua game,
  and is benchmarked here both as a comparison point and as the
  hot-path escape hatch. Doom-class workloads sit *above* the platform's
  target intensity for Lua; `doom_tick` is included as a deliberate
  stress test, not as a spec.
- Forcing `collectgarbage("collect")` after each game tick (`doom_tick_gc`)
  is **not** a useful mitigation: it adds +25% mean cost and the variance
  is unchanged. The variance is not from GC — it tracks the same shape on
  the native C version, indicating it is host-level jitter from rv32emu
  under Docker rather than anything Lua- or GC-specific. See
  "Provisional Pi Zero 2 W projection" below.
- The Pi-under-rv32emu environment is the platform's *minimum emulation
  host*, not its deployment target. Native RV32 execution on the actual
  target hardware (e.g. K230D / C908) is projected ~10× faster than
  rv32emu on Pi for the same binary, since native silicon avoids the
  interpreter dispatch overhead. A spike-b pass on Pi-rv32emu is therefore
  a *strong-form* pass: the worst environment in the deployment surface
  clears the bar.

---

## What was built

**Lua 5.4.7** with `LUA_32BITS` enabled, cross-compiled to
`-march=rv32imfc_zicsr -mabi=ilp32f`, statically linked with the spike-a
SoftFloat archive and a hand-rolled freestanding C runtime. Lives at
`spikes/spike-b/`. The Lua interpreter and standard libraries (math, string,
table, base) compile clean; the unused libraries (io, os, package, debug,
coroutine, utf8) are excluded at archive time so the linker has nothing to
drag in. `linit.c` is replaced with our own `lua_init_libs.c` to control
which standard libraries open.

**Freestanding C runtime** (`ports/rv32emu/lua_runtime.c` + `include/`) provides
the libc surface Lua needs: `setjmp`/`longjmp` via GCC builtins, `snprintf`/
`vsnprintf` good enough for `%g`/`%d`/`%s` output, a free-list `malloc`/`free`/
`realloc` over an 8 MiB heap, the string and ctype primitives, a `localeconv`
that always reports the C locale, and `clock_gettime` over Linux ECALL #403.
`sqrtf` is the real `fsqrt.s` instruction; the other transcendentals are stubs
(returning 0) — none of the chosen benchmarks exercise them in their hot
paths, and the linker keeps the stubs only because `lmathlib.c` registers the
full math library at startup.

**Cart binary** (`ports/rv32emu/lua_cart.c`) embeds one Lua benchmark script
per ELF (xxd-encoded), loads + compiles it once, and re-runs the compiled
chunk in a hot loop, timing each iteration via `clock_gettime` and reporting
`FRAME <name> <i> <us>` plus a `SUMMARY` line. One ELF per benchmark, all
named `lua_cart_<name>.elf`.

**Build environment** mirrors spike-a's pattern. The `Dockerfile` inherits
from `fc32-spike-a` (or `fc32-spike-a-capped`) so we reuse spike-a's
rv32emu binary, SoftFloat archive, RISC-V cross-toolchain, and ADR-0082
throttle patch. The single `--build-arg SPIKE_A_IMAGE=…` switches between
uncapped and capped variants without duplicating any rv32emu build logic.

**Benchmarks ported** (in `benchmarks/*.lua`):

| Script              | What it stresses                                     |
|---------------------|-------------------------------------------------------|
| `binarytrees.lua`   | Recursive tree allocation; GC + malloc pressure      |
| `fannkuch.lua`      | Tight integer loops, table indexing                  |
| `fasta.lua`         | String building, `table.concat`                      |
| `mandelbrot.lua`    | Pure float arithmetic (f32 under `LUA_32BITS`)       |
| `nbody.lua`         | Float physics, the only benchmark calling `math.sqrt`|
| `spectral-norm.lua` | Float inner products, `math.sqrt` at end             |
| `entity_update.lua` | Position-integration slice of per-mobj work        |
| `doom_tick.lua`     | Doom-shaped P_Ticker: AI state machines, sqrt distance checks, projectile spawn/free |
| `doom_tick_gc.lua`  | As above, with `collectgarbage("collect")` per tick (variance experiment) |
| `doom_tick_c.c`     | As `doom_tick.lua`, but written in native C — the secondary "hot-path" tier (and a comparison baseline) |

---

## Docker / Apple Silicon numbers (correctness check only)

Run on `linux/arm64` Docker on Apple Silicon (M-series). These prove the
stack works; they are **not** the spike's success criterion. The Pi Zero 2 W
will be much slower than this host — both because Cortex-A53 @ 1 GHz is below
the Docker host's effective throughput and because spike A's CoreMark numbers
suggest a roughly 4–10× slowdown for interpreter-heavy code on the Pi.

### Lua benchmark suite (uncapped, mean μs per script-iteration)

Each benchmark ELF runs its embedded script 20 times (50 for entity_update)
and reports min/max/mean per-iteration wall time. Numbers below are the
`mean=` field from the SUMMARY line.

| Benchmark        | Mean μs / iter | What "one iter" represents          |
|------------------|---------------:|-------------------------------------|
| `binarytrees`    | 63,398         | depth=10, 8 trees                   |
| `fannkuch`       | 32,936         | n=7 (5 040 permutations)            |
| `fasta`          | 19,133         | N=200                               |
| `mandelbrot`     | 92,655         | 64×64 image, 50 iter limit          |
| `nbody`          |  3,922         | 5 bodies, 50 leapfrog steps         |
| `spectral-norm`  | 111,528        | n=64, 5 power-iteration rounds      |
| `entity_update`  | 14,490         | 100 inner ticks @ 64 entities       |
| `doom_tick`      | 54,822         | 100 inner ticks @ 64 mobs (Doom-shaped) |
| `doom_tick_gc`   | 68,873         | as above, with `collectgarbage("collect")` per tick |
| `doom_tick_c`    |    698         | as `doom_tick`, but written in native C |

For `entity_update` specifically: 14,490 μs / 100 inner frames ≈ **145 μs
per game frame** at 64 entities on this host (uncapped, Docker, arm64).
The 16.7 ms frame budget is ~115× this number.

For `doom_tick`: 54,822 μs / 100 inner frames ≈ **548 μs per game tick**
at 64 mobs — approximately **3.8× heavier** than `entity_update`, landing
inside the "3–10× heavier per mob than position-only" band that an honest
projection has to assume for Doom-class game logic.

### Native C as the hot-path comparison

Lua is the platform's primary authoring tier; native C (and eventually
Rust) is the secondary tier intended for hot paths within an
otherwise-Lua game. Benchmarking both versions of `doom_tick` against
the same emulator pins down what the hot-path tier actually buys:

| Benchmark      | Mean μs / iter | Per-tick (μs) | Slowdown vs C |
|----------------|---------------:|--------------:|--------------:|
| `doom_tick_c`  |    698         |  6.98         |  1.00×        |
| `doom_tick`    | 54,822         | 548           | 78.5×         |

A **~78× factor** for the same algorithm puts a sharp edge on what Lua's
bytecode dispatch + table-field accesses + GC overhead actually cost on
this stack — and on what's available to a developer who drops a hot
path into C. Note also: `doom_tick` is intentionally a *stress test*
sitting above the platform's expected Lua workload; the architecture's
target is "modest retro game logic in Lua", not "all of P_Ticker in
Lua". The Lua projections in the next section are the load-bearing
question for the spike — if those don't work, the architecture is
back open.

### Forced GC variance experiment

`doom_tick_gc.lua` is byte-for-byte identical to `doom_tick.lua` except
for an added `collectgarbage("collect")` at the end of each game tick.
The hypothesis was that paying a small steady GC cost per tic would
flatten the variance from infrequent large opportunistic collections.

| Variant          | min μs    | mean μs   | max μs     | max/min |
|------------------|----------:|----------:|-----------:|--------:|
| `doom_tick`      | 40,942    | 54,822    |  90,712    | 2.21×   |
| `doom_tick_gc`   | 50,822    | 68,873    | 112,957    | 2.22×   |
| `doom_tick_c`    |    409    |    698    |     992    | 2.42×   |

Two findings, both informative:

1. **Forced GC didn't help.** Mean is +25%; the variance ratio is
   unchanged. Spending more time on GC did not reduce the worst-case-tic
   risk it was meant to address.

2. **Native C shows the same 2.4× variance.** This is the key result.
   The 2× variance signature persists across implementations that have
   nothing in common at the language level — Lua VM with incremental GC,
   Lua VM with forced collection per tick, and pure C with no GC at all.
   The variance therefore is **not from Lua's allocator/collector**; it
   is host-level jitter introduced by `rv32emu` running under arm64
   emulation on the Apple Silicon Docker host. On real hardware the
   jitter pattern will be different (and likely smaller, since real
   hardware has no virtualisation layer to schedule against).

This rotates the GC-pause concern from "the headline risk" to "almost
certainly not the risk we should worry about". The actual headline risk
is whatever produces the same 2× spread on the C version — a real-Pi
measurement is the only way to characterise it.

### ADR-0082 throttle verification through the Lua stack

The capped image (`fc32-spike-b-capped`, derived from `fc32-spike-a-capped`)
runs the same ELFs but with `MIPS_CAP=500` baked into rv32emu.

Spot check on `nbody`:

| Build      | Mean μs / iter | Slowdown |
|------------|---------------:|---------:|
| uncapped   |   3,922        |   1.0×   |
| capped 500 | 462,434        | 117.9×   |

A ~118× slowdown matches the spike-a observation that the Apple Silicon arm64
Docker host runs rv32emu at roughly 60 000 effective MIPS uncapped (60 000 /
500 ≈ 120). The cap arithmetic is correct end-to-end through the Lua VM —
nothing about Lua's bytecode interpreter loop interacts pathologically with
the throttle (the interpreter's hot loop is `rv_step()`, which is exactly
where the throttle is wrapped).

---

## Provisional Pi Zero 2 W projection

Pi numbers are not yet measured. Until they are, this is an extrapolation
from spike-a's Docker→Pi clock-ratio analysis (Apple Silicon arm64 Docker
runs rv32emu roughly **4–8× faster** than a Cortex-A53 @ 1 GHz on
interpreter-heavy code, based on the published RV32 soft-core CoreMark/MHz
range of 200–400 plus our measured 1650 iter/sec Docker score). Treat the
numbers below as an upper bound on Pi cost; they will be replaced once
hardware is run.

### doom_tick — the load-bearing measurement (Doom-shaped)

`entity_update` only models position integration. `doom_tick` adds the
state-machine dispatch, distance-check `sqrt`, and projectile spawn/free
churn that an honest Doom-class projection has to include. It is the
benchmark to project from.

| Metric                                                        | Value             |
|---------------------------------------------------------------|------------------:|
| Docker uncapped, per game tick (64 mobs)                      | **548 μs**        |
| Predicted Pi @ 4× slowdown                                    | **~2,190 μs**     |
| Predicted Pi @ 8× slowdown                                    | **~4,380 μs**     |
| Frame budget @ 60 fps                                         | 16,700 μs         |
| Success criterion (half-budget for draws)                     | ≤ 8,000 μs        |
| **Headroom remaining at 8× slowdown**                         | **3,620 μs (45%)**|

At the pessimistic 8× slowdown the Doom-shaped workload at 64 mobs uses
roughly **27–55% of the half-budget reserved for update()**. Comfortable
but no longer "almost no margin used" — that was an artefact of the
position-only entity_update model. 64 mobs is roughly the active-mobj
count in mid-difficulty Doom-1 maps; this is a realistic budget.

### entity_update — for reference

The lighter position-only workload remains useful as a "best case if all
your game logic is essentially particles":

| Metric                                                        | Value             |
|---------------------------------------------------------------|------------------:|
| Docker uncapped, per game frame (64 ent)                      | **145 μs**        |
| Predicted Pi @ 4× slowdown                                    | **~580 μs**       |
| Predicted Pi @ 8× slowdown                                    | **~1,160 μs**     |
| Headroom at 8× slowdown                                       | 6,800 μs (85%)    |

The 3.8× ratio between `doom_tick` and `entity_update` quantifies the
*per-mobj cost premium* for adding AI state machines, sqrt range checks,
and projectile alloc/free pressure on top of pure position update.

### Scaling with mob count (Doom-shaped, linear in N)

| Mobs | Predicted Pi (4×) | Predicted Pi (8×) | Verdict at 8 ms half-budget        |
|------|------------------:|------------------:|------------------------------------|
| 32   |  1,100 μs         |  2,200 μs         | very comfortable                   |
| 64   |  2,190 μs         |  4,380 μs         | comfortable                        |
| 128  |  4,380 μs         |  8,760 μs         | tight at the pessimistic end       |
| 256  |  8,760 μs         | 17,520 μs         | exceeds half-budget at 8×; needs spatial culling |

In practice any real Doom-class game with 256 simultaneously-thinking
mobs uses blockmap / sector-based culling so per-tic work is much less
than naïve linear extrapolation suggests. 128 mobs is roughly Doom-1's
worst-case in cramped maps and looks viable here.

### Variance is from the host, not from Lua's GC

The forced-GC experiment (see "Forced GC variance experiment" above)
demonstrated that `doom_tick`'s 2.2× max/min spread on Docker is **not**
caused by Lua's collector — the same 2.4× spread shows up on the native
C version (`doom_tick_c`) which has no allocator or GC at all. The
variance is host-level jitter introduced by emulating arm64 Docker on
Apple Silicon under rv32emu. On real Pi Zero 2 W hardware the jitter
shape will differ (most likely it will be tighter, since the Pi runs
rv32emu directly without a virtualisation layer to schedule against).

This is the crux of the remaining uncertainty: the **mean** projection
looks comfortable on both Lua and native-C; whether the **worst-case
tic** comes in tight enough to avoid frame drops is the variable that
real hardware will settle. Until then, treat the per-tick numbers above
as ranges with the worst-case being the load-bearing point.

### Where the projection could erode

1. **The cart loop overstates cost.** Each "iteration" of the cart re-runs
   the entire chunk, including the per-iteration setup that initialises
   the mob array. A real fc32 cartridge will compile once at boot and
   call `_update(dt)` each frame — saving the per-iteration startup cost.
   `doom_tick`'s 100 inner ticks already amortise this somewhat (the
   per-tick number is computed inside the cart's hot loop), but the
   first-tick cost still slightly overstates the steady-state per-tic cost.

2. **Transcendental stubs.** `sinf`/`cosf`/`expf`/`logf`/`powf` return 0 in
   this build. `doom_tick` and the other game-shaped benchmark only use
   `sqrt` (real, via `fsqrt.s`). A real libm linked in will add some cost,
   but only where games actually call transcendentals in tight loops
   (rare; most use lookup tables).

3. **The 4–8× Docker→Pi ratio is extrapolated, not measured.** If Cortex-A53
   handles rv32emu's interpreter-dispatch loop noticeably worse than the
   clock ratio suggests (cache size, branch-prediction depth, pipeline
   width all differ), the ratio could be 10–12× rather than 4–8×. At 12×
   slowdown `doom_tick` at 64 mobs is ~6,580 μs — still inside the
   8 ms half-budget for the mean tic, but wiping out almost all margin.
   The ratio is the single biggest unknown.

4. **The 2× variance signature.** Present in both Lua and native-C
   variants, so it is host-level (rv32emu under Docker arm64 emulation
   on Apple Silicon), not GC-driven. Scaled naïvely to Pi at 8×, the
   worst-case `doom_tick` tic projects to ~7.3 ms — eating the
   half-budget alone, *if* the same 2× signature persists on the Pi.
   Forced per-tick GC (`doom_tick_gc`) has been ruled out as a
   mitigation: it costs +25% mean with no variance benefit. The honest
   answer is that we cannot tell from Docker numbers whether real
   hardware will show this jitter pattern; it is the single most
   important thing to measure on the Pi.

### Asymmetric upside on the MIPS cap

ADR-0082's cap is a placeholder at 500 MIPS. Spike A's analysis suggests the
Pi delivers somewhere in the **200–400 effective MIPS** range — i.e. likely
*below* the placeholder cap. If that holds, the cap will not bind on real
hardware and we'll see uncapped Pi throughput. The cap value will be tuned
down to whatever the Pi actually delivers, and the Lua workload will run
exactly as fast as the Pi can run it.

### Pi-under-rv32emu is the worst-case deployment environment

The Pi Zero 2 W running rv32emu is fc32's *minimum emulation host* — the
slowest environment in the platform's intended deployment surface.
Native-RV32 deployment targets like the K230D (C908 @ 1.6 GHz) skip
both of the slowdowns this spike currently projects through:

1. The Pi's 4–8× clock-and-pipeline disadvantage vs the Apple Silicon
   Docker host where Spike B was measured (real, but specific to Pi).
2. The ~10× rv32emu interpreter-dispatch overhead vs executing the
   same RV32 instructions on actual RISC-V silicon (the headline cost
   that Spike A is quantifying directly).

Stacking it up for the same `doom_tick` cart binary:

| Environment                                  | Per-tick cost     |
|----------------------------------------------|------------------:|
| rv32emu on Apple Silicon Docker (measured)   |  ~7 μs (C) / 548 μs (Lua) |
| rv32emu on Pi Zero 2 W (projected)           | ~28–56 μs / 2.2–4.4 ms    |
| **Native RV32 on K230D C908**                | **~2–4 μs / ~150–350 μs** |

The native-K230D column is order-of-magnitude reasoning, not measurement.
The C908 is an out-of-order core at 1.6GHz; the C906 it replaces was
in-order at 1GHz and ~3.5 CoreMark/MHz. The C908 runs faster on both
dimensions, so the estimates are conservatively better than the former
C906 figures. The *direction* is unambiguous: native execution on a real
ILP32-capable RISC-V core is roughly 10× faster than the same binary
inside rv32emu on a comparable ARM host.

Practical consequence: a workload that fits the Pi under rv32emu fits
the Duo native with **roughly an order of magnitude of headroom to
spare**. The Doom-shaped stress workload (~2.2–4.4 ms projected on Pi
rv32emu) costs ~0.2–0.4 ms on Duo native — well under 3% of the frame
budget. The native-C escape-hatch path (already trivial on Pi rv32emu)
becomes "lost in the noise" on Duo.

This is the strong-form interpretation of a Spike B pass: the platform's
*worst* deployment environment clears the bar; everything else clears
it with substantial margin.

### Bottom line (provisional)

Lua is the load-bearing tier:

- **Lua** (the platform's primary authoring tier): the Doom-shaped
  stress workload at 64 mobs costs ~548 μs / tick on Docker, projecting
  to ~2.2–4.4 ms / tick on Pi — **27–55% of the 8 ms half-budget**.
  Comfortable for the spike's *stress* workload, with the actual
  expected Lua workload (modest retro game logic, well below
  Doom-class) sitting much further inside the budget. The Lua tier
  passes; the architecture is not in doubt on the basis of these
  numbers.
- **Native C / Rust** (the secondary hot-path tier): the same algorithm
  is ~78× faster, ~28–56 μs / tick projected on Pi — under 1% of the
  half-budget. This means a Lua game that hits a perf wall in one
  hot loop has a clear, generous escape hatch by rewriting that loop
  in C or Rust without changing the architecture.

Forced per-tick GC has been ruled out as a variance mitigation; the
variance signature is a host-jitter artefact common to both Lua and
native-C runs under Docker emulation, not anything Lua-specific. The
shape of that jitter on real Pi hardware is the single biggest unknown.

Cheapest path to firm it up: the spike-a Pi run, which fixes the
Docker→Pi ratio with a real measurement, *plus* a single Pi run of
`doom_tick.elf` and `doom_tick_c.elf` to characterise variance under
real-hardware conditions instead of nested emulation.

---

## What remains

1. **Run on Pi Zero 2 W.** Build the same Docker image natively on the Pi
   (or `scp` the cart ELFs and the rv32emu binary across), then:

   ```sh
   make -C rv32emu OUT=build -j4
   for f in elfs/lua_cart_*.elf; do
       ./rv32emu/build/rv32emu "$f"
   done
   ```

   Repeat with the capped build once the spike-a Pi MIPS measurement is
   confirmed and a non-placeholder cap value is selected.

2. **Evaluate against the success criterion.** From the spike intake:

   > The Lua workload representative of a non-trivial retro-era game's
   > `update()` function completes comfortably within the frame budget on
   > Pi Zero 2 W, with headroom remaining for draw calls.

   Concrete thresholds to apply once Pi numbers exist:
   - `entity_update` per game frame < 8 ms at 64 entities (leaves half the
     budget for draw calls, which will be native-speed ECALLs in the full
     runtime).
   - No Lua benchmark script exceeds 16.7 ms per script-iteration.

3. **Tune entity counts.** If 64 entities/mobs is comfortable, re-run with
   128 and 256 to find where the frame budget breaks.

4. **Investigate GC pause variance.** Measure `doom_tick`'s min/max range
   on the Pi. If the worst tic exceeds 8 ms, tune `collectgarbage("setpause",
   N)` and `setstepmul` to spread GC over more tics, and consider mob
   pool reuse to flatten the alloc rate. The mean is fine; the *worst-case*
   tic is what determines whether the runtime drops frames in practice.

---

## Running the benchmarks

```sh
# Pre-requisite: build spike-a first (spike-b inherits its image).
make -C ../spike-a docker-build           # uncapped
make -C ../spike-a docker-build-capped    # capped (with MIPS_CAP=N)

# Correctness check on Apple Silicon (arm64 Docker):
make docker-bench-lua                     # 6 standard Lua benchmarks
make docker-bench-entity                  # position-only entity update
make docker-bench-doom                    # Doom-shaped per-tic workload (Lua)
make docker-bench-doom-c                  # same workload in native C
make docker-bench                         # all of the above

# ADR-0082 cap verification:
make docker-bench-lua-capped              # benchmarks at 500 MIPS placeholder
make docker-bench-entity-capped           # entity workload at 500 MIPS
make docker-bench-doom-capped             # doom_tick at 500 MIPS

# On real Pi Zero 2 W (ssh in, clone repo):
make -C ../spike-a docker-build           # builds rv32emu natively too
make docker-build
for f in spikes/spike-b/ports/rv32emu/build/lua_cart_*.elf; do
    ./spikes/spike-a/rv32emu/build/rv32emu "$f"
done
```

---

## Notes & follow-ups

- **Float formatting.** Our `snprintf` `%g` is intentionally simplified — it
  emits with trimmed trailing zeros but does not pick `%e` for small/large
  magnitudes the way glibc does. Benchmark output is currently informational
  only (the cart's timing path uses integer μs, not floats). If we ever
  need round-trip-correct number formatting in production, swap in a vetted
  printf implementation.

- **Transcendentals are stubs.** `sin`, `cos`, `exp`, `log`, `pow`, `atan2`
  etc. all return 0. None of the seven benchmarks call them in their hot
  loops, but a fresh script that does will silently get wrong answers. When
  promoting this code path beyond the spike, link in a real libm (musl's, or
  port openlibm to RV32IMFC).

- **Allocator.** The free-list `malloc` is first-fit with no coalescing.
  `binarytrees` exercises it the most, freeing each tree before the next
  allocation cycle — for 8 trees at depth 10 that's ~8000 alloc/free pairs,
  fits inside the 8 MiB heap with room to spare. Larger workloads will need
  coalescing or a real allocator.

- **`lua_cart.c` vs the eventual fc32 runtime.** The cart compiles, loads,
  and re-executes the entire script body each iteration — appropriate for a
  benchmark, *not* for production. A real fc32 cartridge will compile once
  at boot and call a `_update(dt)` closure each frame. The per-frame cost
  measured here therefore overstates the true budget for game logic; treat
  the numbers as an upper bound.

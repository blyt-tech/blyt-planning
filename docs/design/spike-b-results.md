# Spike B results — Lua running inside the interpreter on minimum hardware

**Status: load-bearing risks measured on real Pi Zero 2 W hardware — the
double-interpretation Lua stack is viable for a bounded 60 Hz game loop.**

> **Supersedes an earlier projection.** A previous version of this document
> answered Spike B by *extrapolating* Docker/arm64 numbers to the Pi (a
> `doom_tick` workload, `rv32imfc`/`ilp32f`, a placeholder MIPS cap; real
> hardware "pending"). This rewrite reflects the **hardware-measured** probe
> built off the Spike A harness (`bench/spike-a/` in the implementation repo),
> which runs the **blyt Lua VM the runtime actually ships** (`BLYT_LUA_I32_F64`:
> int32/float64, the runtime's fixed hash seed) at the **real cart ISA**
> (`rv32imafdc`/`ilp32d`, post-Spike-U) through the shipped rv32emu fork, on an
> actual Pi Zero 2 W. Full numbers in `bench/spike-a/RESULTS.md`.

## The question

Can the Lua VM — Lua bytecode dispatched by the Lua VM, itself compiled to RV32
and dispatched by rv32emu (double interpretation) — execute a realistic game-loop
`update()` within the 16.67 ms frame budget at 60 fps on a Pi Zero 2 W? The
founding risk was that this stack could be an *order of magnitude* too slow.

## What was built

A Lua-VM probe in the Spike A harness (`bench/spike-a/guest/lua-port/`), run
through the same runner and interpreter core (interpreter-only, Berkeley
SoftFloat for F/D). Effective MIPS is derived from rv32emu's retired-instruction
counter (`rv->csr_cycle`); a per-frame marker records the per-frame instruction
distribution, so GC pauses surface in the percentiles.

- `lua-bench.elf <frames> [steady|alloc]` — a steady-state entity `update()`
  (256 entities; position/velocity integration; `sqrt`/`sin` per entity), plus
  an allocation-heavy variant that stresses the GC.
- `lua-suite.elf <name>` — standard lua.org / Benchmarks-Game micro-benchmarks
  (`fib`, `binarytrees`, `nbody`, `mandelbrot`, `strings`, `tables`) covering
  distinct Lua VM operation classes.

Hardware: **Pi Zero 2 W Rev 1.0, Cortex-A53 @ 1000 MHz, aarch64, Debian 13
(Trixie)** — the ADR-0082 minimum emulation host. All runs are deterministic
(identical digests + instruction counts Mac↔Pi).

## Result 1 — Lua-VM throughput

At the recommended `-O2 -fno-strict-aliasing` interpreter core:

| effective MIPS | Pi | Mac | Lua ÷ native-C (CoreMark) |
|---|---:|---:|---:|
| Lua VM (entity `update()`) | **20.5** | 438 | 64 % (Pi) |
| Lua VM, operation suite range | **17–23** | 211–566 | — |

- The Lua VM sustains **≈ 20 MIPS on the Pi**, and **17–23 MIPS across all
  operation classes** — `strings` the worst (17; string hashing/interning +
  pattern matching), `fib` the best (23). The band is **tight on hardware
  (1.36×)** — the slow in-order A53 amortises rv32emu's near-constant
  per-instruction cost across operation types, so a single Lua number is
  representative. (On the fast Mac the same suite spreads 2.7×.)
- Lua is **~36 % slower than native-C throughput** (20 vs CoreMark's 32 MIPS on
  the Pi) — see Result 4 and the cap note.

## Result 2 — frame budget and game-logic capacity

The per-frame instruction budget at 20.5 MIPS is **~342K Lua-VM instructions per
16.67 ms frame**, or **~171K** to leave the spike's ≥ 8 ms headroom. The entity
`update()` costs ~2.9K Lua-VM instructions per entity, so:

| workload | ms/frame (Pi, -O2) | vs 16.67 ms |
|---|---:|---:|
| 256 entities, steady | 36.5 | 2.2× over |
| ~120 entities (full budget) | ~16.7 | at budget |
| ~60 entities (≥ 8 ms headroom) | ~8 | fits with headroom |

**Verdict: a non-trivial but bounded Lua game loop fits.** ~60–120 entities of
`sqrt`/`sin`-per-entity complexity, more with lighter per-entity logic. The
founding fear (order-of-magnitude infeasible) is **not** realized — a
deliberately heavy 256-entity workload is only ~2× over budget and tunes down
cleanly. Draw calls are native-speed ECALLs and are not counted here.

## Result 3 — GC jitter

The steady workload is rock-stable (frame-time p99 ≈ mean). The allocation-heavy
variant (a temporary table + string per entity per frame) both runs slower *and*
shows **p99 ≈ 28 % above mean** (70 ms mean / 90 ms p99 at 256 entities) — Lua's
mark/sweep pauses. **Implication:** carts must budget to the **p99**, not the
mean, and minimise per-frame allocation to hold a steady 60 Hz. (Forcing GC per
tic is not a fix — it trades the pause for constant overhead.)

## Result 4 — native C/Rust as the hot-path escape hatch

The same harness runs native-C carts (CoreMark/Embench) at **≈ 32 MIPS on the
Pi** — and, more importantly, native code needs *far fewer instructions* to do
the same task than Lua bytecode does. Lua is the platform's **primary authoring
tier**; native C/Rust remains the intended secondary tier for hot paths within
an otherwise-Lua game. A cart that outgrows the Lua per-frame budget can move its
hot loop to a native module without leaving the cart model.

## Consequence for the ADR-0082 cap

Effective MIPS is workload-dependent, and the Lua VM (the primary authoring path)
sits **~1.6× below** native CoreMark on the Pi. A single CoreMark-anchored cap
(≈ 32 MIPS) is therefore optimistic for Lua carts. The cap should either be set
conservatively at **≈ 17–20 MIPS** (safe for every cart type and Lua operation
class — `strings` worst case 17) or be **per-execution-model** (native ≈ 32,
Lua ≈ 20). See `spike-a-results.md` for the cap decision, which also hinges on
the shipped interpreter optimization level (~4×: `-O0` ≈ 8 MIPS vs `-O2` ≈ 32).

## What remains

- **Real cart-path timing.** The probe static-links Lua into a bare ELF —
  faithful for VM throughput, but not the exact cart path (Lua bytecode loaded
  via `libblyt32lua.so`, ECALL-bridged draws). Spike I validated that path's
  *correctness*; confirming its *timing* is dominated by the VM cost (very
  likely) rather than load/bridge overhead is a small follow-up.
- **Above-target stress workload.** A Doom-class per-tic AI workload (the old
  projection's `doom_tick`) would characterise deliberately-above-target
  intensity; it is a stress test, not a spec, and the bounded-workload verdict
  above already answers the load-bearing question.

## Reproducing

```sh
cd bench/spike-a
scripts/build-lua.sh                                  # lua-bench.elf + lua-suite.elf
scripts/build-pi.sh                                   # aarch64 static runner
scp -r artifacts/pi/runner artifacts/guest pi@<pi>:spike-a/
# on the Pi:
./runner lua-bench.elf 400 steady        # frame-budget distribution
./runner lua-bench.elf 400 alloc         # GC jitter
for b in fib binarytrees nbody mandelbrot strings tables; do
  ./runner lua-suite.elf $b --json
done
```

**Dependency:** Spike A (harness, interpreter, MIPS cap).

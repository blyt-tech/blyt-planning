# Spike A results — Interpreter throughput on minimum emulation hardware

**Status: DONE — measured on real Pi Zero 2 W hardware.**

> **Supersedes an earlier attempt.** A previous Spike A harness (in the planning
> repo's `spikes/spike-a/` submodule) built CoreMark/Embench against
> *upstream* `sysprog21/rv32emu` on the **`rv32imfc_zicsr` / `ilp32f`** ISA with
> a custom crt0, and only ever ran in arm64 Docker (a 500 MIPS placeholder cap,
> hardware numbers pending). That predates Spike U (hardware doubles) and does
> not use the interpreter the runtime actually ships, so its numbers are not a
> valid cap. This document reflects the current, hardware-measured harness in
> the **implementation repo at `bench/spike-a/`**, which uses the shipped
> interpreter and the real cart ISA. `bench/spike-a/RESULTS.md` is the
> authoritative machine-readable record; this is the narrative.

## The question

Can the RV32 interpreter the runtime ships execute a realistic cart workload
within the 16.7 ms frame budget at 60 fps on a Pi Zero 2 W (Cortex-A53 @ 1 GHz)?
And — the secondary output that ADR-0082 depends on — **what is the effective
guest MIPS on that hardware**, which becomes the baked-in emulator MIPS cap?

## What was built

A standalone measurement harness (`bench/spike-a/`) that is deliberately faithful
to production:

- **Interpreter:** the **`blyt-tech/rv32emu` fork the runtime actually ships**
  (`g244cfb3-blyt-v0-p7`), built the same way `libblytemu` is — interpreter only
  (JIT/T2C/SYSTEM off), Berkeley SoftFloat for the F/D extensions. The runner
  links this core and drives it exactly as `cart_run.c` does. This is the point:
  the cap must reflect the interpreter that ships, not a different build.
- **Guest ISA/ABI:** **`rv32imafdc` / `ilp32d`** — the real cart target since
  Spike U added hardware doubles. Benchmarks are cross-compiled with the blyt
  clang toolchain against **static musl** (built from the pinned
  `blyt-tech/musl`), so libm/FP behaviour matches real carts.
- **Metric:** effective guest MIPS = retired guest instructions / host
  wall-clock seconds / 1e6, where the instruction count is rv32emu's own
  per-instruction counter `rv->csr_cycle`. The runner loads a bare ELF, provides
  a minimal RV32-Linux syscall shim (write/brk/mmap/clock_gettime/… + a proper
  SysV auxv stack so full static musl starts), runs the guest to `exit`, and
  reports the number. Nothing under `runtime/` is modified; this is measurement
  infrastructure only.
- **Benchmarks:** **CoreMark** (EEMBC, Apache-2.0) and **Embench-IoT** (19
  kernels). CoreMark uses the barebones port + static musl and reproduces the
  canonical validation CRCs; 18/19 Embench kernels build, run, and verify PASS.
- **Portability:** guest ELFs are host-independent; the runner cross-builds to
  aarch64 (static) from the Mac via a `linux/arm64` container, so the Pi needs
  no toolchain.

## Result — Raspberry Pi Zero 2 W (the ADR-0082 floor)

Hardware: **Pi Zero 2 W Rev 1.0, Cortex-A53 @ 1000 MHz, aarch64, Debian 13
(Trixie), kernel 6.18.34+rpt-rpi-v8.** Emulation verified correct on hardware:
CoreMark's own ≥10 s run (18.8 s at 2000 iterations) reports *"Correct operation
validated"* with the canonical CRCs; 18/19 Embench kernels verify PASS with zero
unhandled syscalls.

**The measured floor depends ~4× on how the interpreter core is optimized:**

| interpreter opt | CoreMark MIPS | CoreMark iters/sec | Embench cluster | guest insns / 16.67 ms frame |
|---|---:|---:|---:|---:|
| **`-O0`** — what `cmake -B build` builds **today** | **7.9** | 41 | ~7–9 | ~133K |
| **`-O2`** — a sane optimized release | **32.1** | 106 | ~30–40 | ~536K |
| `-O3` | 32.9 | 109 | — | ~549K |

Full Embench-per-kernel numbers at both opt levels are in
`bench/spike-a/RESULTS.md`. The Embench suite at `-O2` spans ~13 MIPS
(control-flow-heavy `nsichneu`) to ~57 MIPS (tight-integer `crc32`), clustered
~30–40, with CoreMark at 32.1 — a representative single cap number. The dev-Mac
baseline (Apple Silicon) is ≈ 550 MIPS CoreMark at `-O2`, so the Pi is ~17×
slower.

## Cross-check: the Lua bytecode interpreter (a Spike B probe)

CoreMark/Embench are native C. Lua carts run a *second* interpretation layer —
the Lua VM (compiled to RV32) dispatched by rv32emu — which is the primary
authoring path, so its throughput is what actually bounds Lua carts. The harness
includes `bench/spike-a/guest/lua-port/`: the **blyt Lua 5.4 VM** (int32/float64
`BLYT_LUA_I32_F64`, the runtime's fixed hash seed, + the guest quad soft-float
builtins) built to the cart ISA, running a steady-state entity `update()` (256
entities; position/velocity integration; `sqrt`/`sin` per entity). Deterministic
— identical digest and instruction count on Mac and Pi.

| effective MIPS | CoreMark | Lua-VM | Lua ÷ CoreMark |
|---|---:|---:|---:|
| Pi, interp **-O2** | 32.1 | **20.5** | **64 %** |
| Pi, interp -O0 | 7.9 | 6.7 | 85 % |
| Mac, interp -O2 | 545 | 438 | 80 % |

The Lua VM sustains only **~20 MIPS on the Pi at -O2** — 36 % below CoreMark, and
the gap is *widest* at the opt level you'd ship (the in-order A53 punishes the
VM's indirect-dispatch + softfloat-f64 mix harder than Apple Silicon does). So a
CoreMark-anchored cap is **~1.57× optimistic for Lua carts**: a Lua cart tuned to
a 32-MIPS dev throttle (~536K guest insns/frame) only gets ~342K/frame on
hardware → dropped frames. This directly informs Spike B and the cap; the probe
quantifies the worst-case authoring path so the cap decision is not made on
native C alone.

## The cap value, and the decision it forces

**The ADR-0082 cap cannot be a single number until the release interpreter's
optimization level is pinned**, because that choice moves the cap 4×:

- Ship the core **`-O2 -fno-strict-aliasing`** → cap **≈ 32 effective guest
  MIPS** (~536K guest instructions per frame). Draw calls are native-speed
  ECALLs, not counted here, so this is a comfortable retro-era budget and meets
  the spike's success criterion (a non-trivial game loop leaving ≥ half the
  frame for subsystem overhead).
- Keep the core at **`-O0`** (today's `cmake -B build`, no `CMAKE_BUILD_TYPE`) →
  cap **≈ 8 MIPS** (~133K/frame). That is tight enough that the floor-hardware
  performance story needs re-examination.

**Recommendation:** ship the interpreter core built `-O2 -fno-strict-aliasing`
(the `-O0` the repo builds today would set the cap ~4× too low). Then decide
whether the cap is one number or per-execution-model: native-C carts sit at
**≈ 32 MIPS**, but Lua carts (the primary authoring path) only sustain **≈ 20
MIPS** (see the Lua cross-check above), so a single 32-MIPS cap is ~1.57×
optimistic for Lua. Either bake the cap conservatively at **≈ 20 MIPS** (safe for
every cart type) or make it **per-execution-model** (native ≈ 32, Lua ≈ 20).

## Secondary findings

1. **Any optimized rv32emu build needs `-fno-strict-aliasing`.** At `-O2` *and*
   `-O3` the interpreter core starts the guest at `PC=0` ("failed to allocate or
   translate block") unless the flag is set — rv32emu reads/writes guest memory
   through incompatible pointer types, which the type-based alias analysis
   enabled at `-O2`+ breaks; the flag makes it well-defined (same reason the
   Linux kernel uses it). `-O3` is otherwise correct but buys nothing over `-O2`
   for an interpreter dispatch loop (occasionally marginally slower), which is
   why `-O2` is the recommendation. Latent risk the moment the runtime build is
   optimized; the harness sets the flag.
2. **`aha-mont64` deterministically aborts rv32emu.** It builds and runs, then
   trips an assertion in rv32emu's block/constant optimizer
   (`assert(rv->X[0] == 0)`, `optimize_constant`, `emulate.c:1837`). This is
   inside the vendored core, so a real cart with a similar instruction stream
   could hit it too — worth an engineering follow-up against the emulator. The
   other 18 Embench kernels and CoreMark run clean.
3. **Instruction counts are deterministic.** For a fixed ELF the retired-
   instruction count is bit-identical across hosts (Mac vs Pi vs arm64
   container); only wall-clock — hence MIPS — varies. (CoreMark's count wobbles
   by ~300 because its printed elapsed-time string has host-dependent digits;
   Embench prints no timing and is exactly deterministic.)

## Reproducing

In the implementation repo:

```sh
cd bench/spike-a
scripts/build-guest.sh && scripts/build-embench.sh   # host-independent RV32 ELFs
scripts/build-host.sh   && scripts/run.sh            # dev-Mac baseline + table
scripts/run-matrix.sh                                # MIPS across interpreter -O0/-O2/-O3

# Pi Zero 2 W (authoritative floor):
scripts/build-pi.sh                                  # aarch64 static runner (Docker)
scp -r artifacts/pi/runner artifacts/guest scripts pi@<pi>:spike-a/
ssh pi@<pi> 'cd spike-a && ./scripts/run.sh --runner ./runner'
```

See `bench/spike-a/README.md` for the full method and `RESULTS.md` for all
measured numbers.

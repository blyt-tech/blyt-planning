# Spike Y — Lua per-pixel plotting throughput (tier-2 surface lock)

**Status: DONE — measured on real Pi Zero 2 W hardware, emulated *and* native.**

A spin-out of the #195 Stage 2 (Lua fast-pixel) work (impl issue #208), building on
Spike A (interpreter throughput), Spike B (Lua-VM throughput) and Spike X
(rasterizer / surface model). The Stage-2 tier-2 surface lock lets a Lua cart
sample and plot individual pixels (`blyt32.surface.acquire` → per-pixel access);
this spike asks **how fast that per-pixel path actually is on the floor hardware,
what optimization strategies move it, and how big the emulated-vs-native gap is.**

## The question

For a Lua cart doing arbitrary per-pixel work (particles, plasma, a raycaster
column writer), **how many pixels can it plot per frame within the 16.67 ms /
60 Hz budget** — on the ADR-0082 floor device (Pi Zero 2 W)? And how does that
change across (a) the API/binding shape, (b) targeted Lua-VM fast paths, and
(c) emulated RV32 vs native Lua execution?

The success bar (use-case-anchored, not full-screen): a realistic per-pixel
workload of ~1 k–10 k px/frame at 60 Hz within ≤ ~50 % of the frame budget,
leaving the rest for game logic. Full-screen (76 800 px) per-pixel is explicitly
not the bar — tier-1 serviced primitives (clear/rect/line/blit) cover bulk.

## What was built

A measurement harness (kept in the session scratchpad; promotable to
`blyt:bench/spike-y/`), deliberately faithful to production:

- **Workload carts** — a plasma-style loop writing a W×H region of the screen
  each frame, `c = (x + y + t) & 0xFF` per pixel, in each candidate API form
  (method `lk:set`, plain function, implicit `set_pixel`, index `lk[i]`), at
  1 k (32×32), 4 k (64×64) and 10 k (100×100) pixels/frame. A C tier-2 cart
  (raw `lk.pixels[]` pointer) is the ceiling reference. All bit-identical
  (`[blyt:fbhash]` frame hash matches the `common::gfx` golden on every leg).
- **Emulated legs** — an aarch64 `blytplay` (the SDL player) cross-built for
  Debian trixie in a `linux/arm64` container, running the RV32 carts through the
  shipped `rv32emu` interpreter core + the RV32 guest `libblyt32lua`. The VM
  fast paths live in the `blyt-tech/lua` fork's `lvm.c`, gated by
  `-DBLYT_LUA_FASTPIXEL` and built only into the thick guest lib (so the
  host-Lua WASM leg stays unpatched and coherence is preserved). A baseline
  (`-DBLYT_LUA_FASTPIXEL=OFF`) and a fast lib swap in via `BLYT_LIB_DIR`.
- **Native leg** — a ~150-line standalone runner linking the *same patched Lua
  fork* compiled native aarch64 (`onelua.c`, `-O2 -fno-strict-aliasing`,
  `BLYT_LUA_I32_F64`), with the per-pixel API bound to native C writing a
  buffer. Runs the identical cart Lua with **no rv32emu layer** — i.e. what
  blyt's host-Lua fast path does.
- **Hardware** — Raspberry Pi Zero 2 W (Cortex-A53 @ 1 GHz, aarch64, Debian 13
  Trixie), the same device as Spike A/B. `blytplay` links SDL2 but runs headless.

**Method note that bit everyone (see Secondary findings):** `cmake -B build`
sets no `CMAKE_BUILD_TYPE`, so the interpreter core builds `-O0` and every number
is ~3–4× too slow. All results below use an **`-O2 -fno-strict-aliasing`** (or
`-O3`, equivalent) emulator + `-O3` guest libs, per Spike A's recommendation.

## Results — Pi Zero 2 W

### Emulated (rv32emu, `-O2`≡`-O3` core + `-O3` guest libs)

ms/frame, lower is better; "px in budget" = pixels/frame fitting the full
16.67 ms:

| strategy | 1 k px | 4 k px | 10 k px | ~px in 16.67 ms | VM patch |
|---|---:|---:|---:|---:|---|
| method `lk:set(x,y,c)` (shipped 2a) | 57.6 | 228 | 552 | ~266 | none |
| plain fn `pset(lk,x,y,c)` | 50.8 | — | — | ~300 | none |
| implicit `set_pixel(x,y,c)` | 25.3 | 91.7 | — | ~640 | none |
| method + fast `OP_SELF` + `OP_CALL`-inline | 19.4 | 70.9 | — | ~860 | 2 tiny |
| implicit `set_pixel` **inline** | 14.1 | 52.1 | — | ~1 150 | 1 |
| index `lk[y*w+x]` (realistic) | 13.8 | 46.2 | 90 | ~1 300 | table op |
| index `lk[row+x]` (row hoisted) | 12.0 | 39.4 | — | ~1 400 | table op |
| C tier-2 ceiling (raw ptr) | — | 2.9 | — | ~170 000 | n/a |

So on the emulated floor the fastest Lua path reaches **~1.2–1.4 k px/frame** in
budget — it clears the *low* end of the 1 k–10 k bar (at a full frame's worth of
pixels; ~700 within the ≤50 % bar), up from ~266 for the shipped method. Bulk
(4 k–10 k) stays out of reach for every Lua form; C tier-2 (~0.7 µs/px) clears it
by ~130×.

### Native (patched Lua VM compiled aarch64, **no emulation**)

ms/frame:

| strategy | 1 k px | 4 k px | µs/px | ~px in 16.67 ms |
|---|---:|---:|---:|---:|
| method `lk:set` | 0.333 | 1.313 | ~0.32 | ~52 000 |
| `set_pixel` | 0.296 | 1.163 | ~0.28 | ~59 000 |
| index `lk[y*w+x]` | 0.270 | 1.058 | ~0.26 | ~65 000 |

Native Lua reaches **~52–65 k px/frame — ~68–85 % of the full 76 800-px screen,
i.e. PICO-8-class full-screen per-pixel** — and the VM fast paths barely move it
(method 0.32 vs index 0.26 µs/px, ~22 %).

### The gap

Native is **~44–58× faster than emulated on the same device** (method 1 k:
0.333 ms native vs 19.4 ms emulated). That is far above Spike A's ~15× CoreMark
tax, because the **Lua VM is emulation-hostile** — branch-heavy, pointer-chasing,
function-dispatch code is rv32emu's interpreter worst case, versus CoreMark's
tight integer arithmetic.

## The optimization strategies

Each removes a specific per-pixel overhead. Baseline `lk:set(x,y,c)` pays three:
`OP_SELF` (metatable `__index` lookup for `"set"`), `OP_CALL` (the C-call frame,
`precallC`/`poscall`), and the C function's `luaL_check*` re-validation.

1. **Plain function `pset(lk,x,y,c)`** *(no VM patch).* Same C function, called
   as a localized plain function → **no `OP_SELF`**. ~13 % over the method.
2. **Implicit-screen `set_pixel(x,y,c)`** *(no VM patch).* No lock argument —
   uses a C-global screen lock stashed at `acquire(SCREEN)` — so **no
   `luaL_checkudata`** and no userdata deref: three int args + a bare C store.
   ~2.4× over the method, ≈ the `OP_CALL`-inline patch, with zero fork.
   `set_pixel_surface(lk,x,y,c)` (= `pset`) covers arbitrary (off-screen)
   acquired surfaces; measured coherent, ~0.4 ms slower than the screen form.
3. **`OP_CALL`-inline** *(VM patch).* Recognize the get/set/`set_pixel`
   light-C-function at `OP_CALL` by pointer identity, read the args off the
   stack, execute inline — **eliding the call frame and the `luaL_check*`**. Any
   unusual case (foreign self, non-int args, OOB, stale lock) falls through to
   the real C function, so semantics and coherence are preserved.
4. **Fast `OP_SELF`** *(VM patch).* Recognize `lock:set`/`lock:get` at `OP_SELF`
   (lock metatable + 3-char key match) and drop the known function in directly,
   **skipping the metatable `__index` lookup**. With `OP_CALL`-inline it brings
   the *method* form to ≈ plain-function speed (19.4 ms vs 57.6 ms at 1 k) —
   accelerating the existing, natural `lk:set` API with **no new surface**.
5. **Index-opcode `lk[y*w+x]`** *(VM patch on `OP_GETTABLE`/`OP_SETTABLE`).*
   Recognize the lock userdata + integer key and store/load inline — a single
   table opcode, no call at all. Fastest, but the cart computes the linear
   offset (poor DX; a realistic non-hoisted `y*w+x` costs only ~12 % over a
   hoisted `row+x`), and it needs an `__index`/`__newindex` metamethod fallback
   to stay coherent on the unpatched host-Lua leg.
6. **Inline `set_pixel`** *(the recommended (x,y) design).* `OP_CALL`-inline
   applied to the implicit `set_pixel`: index-opcode speed, the natural `(x,y)`
   API, coherent with **no metamethod fallback** (it is a plain function —
   unpatched builds just call it, identically), and the **simplest patch**
   (recognize one function pointer). Dominates the index-opcode for (x,y) work.
7. **2-axis `lk[x,y]`** *(hypothetical, not built).* New opcodes + a Lua
   **parser fork**. ≈ index speed, best DX, but the heaviest fork and a
   non-standard-Lua dialect (Lua has no `t[x,y]` syntax). The (x,y)→offset
   arithmetic it would move into C is only ~12 % of the cost, so it is *not*
   meaningfully faster than the 1-axis index — its only gain is ergonomics.

All are **bit-identical** to the slow path (frame-hash verified across
native/libretro/wasm + the QEMU gate). The VM patches are **guest-only pure-speed
optimizations** — the host-Lua WASM leg stays on the slow path, so cross-leg
output is unchanged and the determinism contract holds.

## Findings and the decision they force

1. **The per-pixel VM patches are a modest win, and only for the *emulated*
   path.** On the floor they deliver ~2–5× (method → index), reaching
   ~1.2 k px/frame — enough for the low end of the use-case bar, not for bulk.
2. **The order-of-magnitude lever is running Lua natively, not patching the
   emulated VM.** Native Lua on the same Pi is ~50× faster and full-screen-class,
   and there the patches barely matter — they were compensating for emulation.
3. **Line up how Lua runs per target:**
   - **Real RISC-V hardware (K230D):** carts run **native** (RV32 on RV32) →
     per-pixel is already ~full-screen-class. No patch needed; not a problem.
   - **aarch64 handhelds (Pi, R36S) via `blytplay`:** carts run **emulated**
     (rv32emu) → the slow path. This is the *only* place the per-pixel-speed
     problem exists.
   - **host-Lua fast path** (pure-Lua carts, native aarch64 Lua — today
     WASM-only): **native** → ~50× the emulated path, already deterministic
     (fixed hash seed + state buffers).
4. **Therefore:** the strategic answer to "make Lua per-pixel fast on aarch64
   handhelds" is to **extend the host-Lua fast path to the native player**
   (`blytplay`/native) so pure-Lua carts run native there too — a ~50× win that
   needs *no* VM patches — rather than to invest in the per-pixel VM fork.
5. **The per-pixel patches remain worthwhile for *hybrid* carts** (Lua + native
   code), which must use rv32 emulation for their native half and so cannot take
   the host-Lua path. For those, the recommended additions are the plain
   `set_pixel`/`set_pixel_surface` bindings (zero fork, ~2.4×) and/or the
   `set_pixel`-inline + fast-`OP_SELF` patches (index speed, accelerating the
   existing `lk:set`), in preference to the index-opcode (faster but worse DX
   and needs the metamethod fallback).

**Recommendation:** ship the plain `set_pixel`/`set_pixel_surface` binding as the
Stage-2 per-pixel API (coherent, no fork, natural DX, ~2.4× the shipped method);
open a separate, higher-leverage engineering item to **bring the host-Lua fast
path to the native player**; keep the VM fast-path patches parked (on branch
`spike/208-lua-vm-fastpixel`) for the hybrid-on-handheld case or a future revisit.

## Secondary findings

1. **The shipped build is `-O0`.** `cmake -B build` sets no `CMAKE_BUILD_TYPE`,
   and `rv32emu`/`libblytemu` carry no per-target `-O`, so the interpreter core
   ships unoptimized — ~3–4× slower (this masked the true floor in the first
   measurement pass). Release builds must set **`-O2 -fno-strict-aliasing`**
   (Spike A's cap recommendation). Independently, the devtool compiles **C carts
   at `-O0`** too (no `-O`/`cflags` option), so the C tier-2 ceiling here is
   conservative — a real `-O2` cart would be faster still. Both are worth an
   engineering follow-up.
2. **`-O3` silently miscompiles `rv32emu` without `-fno-strict-aliasing`** —
   the guest starts at `PC=0` ("failed to allocate or translate block"). Same
   root cause Spike A flagged; the flag is mandatory for any optimized build.
3. **`-O3` ≈ `-O2` for this workload** (within ~3 %, emulated and native), and
   `-O3` on the guest libs did not help either — matching Spike A's
   CoreMark `-O2` 32.1 vs `-O3` 32.9 MIPS.
4. **The Lua-VM emulation tax (~44–58×) is much larger than the CoreMark tax
   (~15×).** rv32emu's per-instruction overhead is worst for branchy /
   pointer-heavy / dispatch-heavy code, which is exactly the Lua interpreter —
   a useful data point for any "how slow is Lua under emulation" estimate
   (don't extrapolate from Spike A's compute-bound number).

## Corroboration — a Doom-shaped workload (logic + draw), real Pi Zero 2 W

Spike Y measures synthetic per-pixel plotting. To sanity-check the ~50× lever on
a realistic game shape, two Doom-shaped benchmarks were run host-Lua-native vs
emulated on the same Pi Zero 2 W: the Spike B `doom_tick` workload (Doom's
`P_Ticker` slice — IDLE→CHASE→ATTACK→DEAD state machines, a `math.sqrt` range
query per tic, projectile `table.insert`/`table.remove` GC churn) and a Doom
`R_DrawColumn` analog (affine paletted texture-mapped vertical columns + a
colormap light lookup, 320×240). Same blyt Lua fork (`BLYT_LUA_I32_F64`, fixed
seed), same bytecode — only native-aarch64 vs RV32-under-`rv32emu` differs.

| tier | native host-Lua | emulated rv32emu | speedup |
|---|---|---|---|
| logic — `doom_tick` (P_Ticker) | 15.2 ms/sim | 811 ms/sim (≈19 MIPS) | **53×** |
| draw — `R_DrawColumn` in Lua (320×240) | 33.9 ms/frame (~30 fps) | 1638 ms/frame (~0.6 fps) | **48×** |

(Startup-cancelled slope, best-of-N. On an M-series Mac desktop the same
benchmarks show ~185× / ~180× — the fast out-of-order core widens the gap that
the in-order A53 narrows to the ~50× floor.) Both legs are **bit-identical**
(logic checksum 117/sim; framebuffer FNV `1688251611`), so determinism holds
through both game logic *and* a software renderer on real hardware.

**Reading:** both tiers land in the same ~50× band because both are pure Lua-VM
work — a Lua per-pixel texture mapper has no native primitive to offload to, so
it pays the same emulation tax as the game logic (confirming finding #4). The
consequence for a Doom-class cart:

- **All-Lua renderer** (texture mapping in Lua): the *whole frame* runs at ~50× —
  ~30 fps native vs ~0.6 fps emulated for one full-screen textured pass.
  Emulated is unplayable; host-Lua drags a pure-Lua software renderer into
  borderline-playable range.
- **Native renderer module** (the idiomatic Doom-scale choice — C/Rust does
  `R_DrawColumn`): draw is native on both legs (leg-neutral), and host-Lua's 53×
  applies to the logic tier — a ~50× larger Lua game-logic budget, enough to
  author full Doom-scale AI/simulation in Lua instead of dropping it to native.

Either way the lever is the one Spike Y identifies: run Lua native. Harness +
ports live in `bench/spike-a` (`lua_doom.c`/`doom_bench.lua`,
`lua_draw.c`/`draw_bench.lua`); built static for aarch64 in a `debian:trixie`
`linux/arm64` container and run on the Pi.

## Reproducing

Emulated (kit): cross-build `blytplay` for the device OS in a `linux/arm64`
container (`-DCMAKE_C_FLAGS="-O2 -fno-strict-aliasing"`), pair it with the RV32
guest libs (`-DBLYT_LUA_FASTPIXEL=ON`/`OFF` for fast/baseline; RV32 libs are
host-independent), scp to the Pi, and time each cart with `BLYT_LIB_DIR` pointing
at the fast/baseline lib dir. Native: build the standalone runner against the
patched fork's `onelua.c` (`-O2 -fno-strict-aliasing -DBLYT_LUA_FASTPIXEL
-DBLYT_LUA_I32_F64`) for aarch64 and run the cart Lua directly on the Pi.

Cross-refs: impl issue #208 (#195 Stage 2), Spike A (interpreter throughput),
Spike B (Lua-VM throughput), Spike X (rasterizer / surface model).

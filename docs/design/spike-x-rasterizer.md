# Spike X — Host-side rasterizer: unified framebuffer mechanism + integer determinism across compile targets

**Status:** done — validated 2026-06-28 (results below; both load-bearing
assumptions hold). Implementation on branch `188-rasterizer-framebuffer-determinism`
in `../blyt` (commits `c7511d8` → `cc57f11` → `f54379d` → `d0e65fb`; Q3 is a
measurement, no commit).
**Depends on:** Spikes D / K / U (cross-platform + cross-host FP determinism,
hardware doubles) — all `done`. No new external blockers.
**Hardware gate:** none (QEMU native leg suffices; real-silicon confirmation
optional, deferrable like Spike U Stage 6).

> **Follow-on (2026-06-29):** this spike proved the framebuffer mechanism +
> integer determinism. The design then generalized to runtime-managed
> **surfaces** with a two-tier (serviced-ops / `acquire`-`release` lock) access
> model and the Lua fast-pixel path — captured in spec **#195**, which subsumes
> #193. For the *current* graphics-buffer design, read #195; this document is
> the originating spike, not the latest word.

---

## Self-contained context (read first)

This spike precedes building the Blyt32 graphics subsystem (≈50 designed API
entry points). A clean session does **not** need our chat history — everything
load-bearing is below or cited.

### Where graphics sits today

Nothing draws yet. Confirmed state of the implementation repo (`../blyt`):

- The host runtime owns an 8-bit **paletted framebuffer**, `uint8_t
  pixels[320*240]`, in host memory: `runtime/host/src/libblyt/cart_run.c:241`
  (dims `BLYT_FRAME_W=320` / `BLYT_FRAME_H=240`,
  `runtime/host/include/blyt_runtime.h:296`).
- A `cart_has_drawn` flag (`cart_run.c:244`, init false at `:2800`) is **never
  set true** — so the host always falls back to a PM5544-style test card
  (`runtime/host/src/libblyt/testcard.c`).
- Frontends read the frame back via `blyt_session_get_pixels()` (paletted),
  `blyt_session_get_palette()` (XRGB8888), and the convenience
  `blyt_session_expand_frame()` (`blyt_runtime.h:311–315`).
- The ECALL space **100–199 is reserved for graphics** and is entirely empty:
  `runtime/host/src/libblyt/ecall.h:15`. No `blyt_gfx_*` / `blyt_image_*`
  symbols exist anywhere in guest or host.

### The decided architecture (the *what*, settled in ADRs)

- **Memory model is API-based, not memory-mapped** (ADR-0008). The runtime owns
  the back buffer. The flat-map benefits are recovered through targeted APIs —
  crucially, `acquire_framebuffer()` returns *a direct writable pointer to the
  back buffer* as a hot-loop escape hatch ("no per-pixel API overhead"), while
  palette / primitives / blit are ordinary API calls.
- **Drawing primitives are host-side, reached by ECALL** (ADR-0052). Batch
  variants exist *because* per-primitive ECALL overhead is real (~1 µs); the
  batch wrappers "loop over the command array in native code." So a primitive
  like `blyt_gfx_rect_fill` is a call into runtime C, **not** guest-side library
  code.
- **Three execution models run that same drawing code:**
  1. **Emulated** (desktop `blytplay`, libretro, WASM-emulated): cart in
     rv32emu → ECALL stub in `libblyt32.so` → host C drawing.
  2. **Host-Lua fast path** (WASM, pure-Lua carts only): Lua runs on the
     host-compiled Lua VM, calls host C drawing **directly** (no rv32emu). See
     `frontends/wasm/wasm_main.c`.
  3. **Native bare-metal** (RISC-V hardware / QEMU gate): no host to trap to —
     the native `libblyt32` variant (`frontends/native/`) implements the API,
     and the *same rasterizer source* is compiled for RV32 and runs natively.
- **Determinism contract** (high-level-design §5; line ~3385): per-frame
  **framebuffer hashes must be bit-identical across every target**. This is the
  same contract Spikes D/K already proved for cart digests — graphics extends it
  to the pixel buffer.

### The determinism decision this spike validates ("Option A")

The rasterizer body will be **integer / fixed-point only — no floating point**.
Rationale (from the design discussion that produced this spike):

- The v1 primitive surface has nothing that *needs* FP: lines → Bresenham,
  circles/ellipses → midpoint, blit scaling → nearest-neighbour with a
  fixed-point source step `(src<<16)/dst`, fill patterns → 4×4 integer masks.
  No rotation, no sub-pixel positioning (ADR-0048: no camera/clip; ADR-0050:
  fill patterns; ADR-0049: index-255 transparency).
- FP determinism across the four compile targets is a real hazard
  (FMA contraction, rounding mode, host-`double` vs RV32 hardware-`double`).
  Integer C semantics are fully pinned across compilers, so **an integer
  rasterizer cannot be miscompiled into divergence by a dropped `-ffp-contract`
  flag on one target.** Removing that silent-divergence surface is the prize —
  not avoiding float math per se.
- The handful of FP-shaped API params (`fade(t)`, `palette_lerp(t)`, `shake`
  intensity/falloff) are **quantized to fixed-point at the API boundary**; the
  blend math stays integer.
- All FP-rich *convenience* math (easing, noise, `ease_lerp`) is a separate,
  as-yet-unbuilt guest-side subsystem (`blyt_math_*`/`blyt_ease_*`, ADR-0071 /
  ADR-0086 / ADR-0039) that wraps the console's deterministic musl libm and runs
  in the abstract machine under §5. **It is out of scope here** and never enters
  the host rasterizer — the cart computes eased floats guest-side and hands the
  rasterizer integers.

---

## Motivation — why this is the gate before building graphics

The graphics surface is large and the intent (see
`implementation-sequence.md`, "After Phase 10") is to build it out, potentially
ahead of the cross-platform SDK-packaging work. Before writing ~50 entry
points, two assumptions must be proven true, because if either is false the
whole subsystem design shifts:

1. that a **single `acquire`/`present` contract** can be implemented across the
   three execution models with the *same cart code*; and
2. that an **integer rasterizer is bit-identical** across host-x86, host-arm,
   wasm, and RV32-native.

Everything else about graphics is well-understood engineering (the algorithms
and the API shape are decided). This spike is the minimum code that answers the
two open questions and leaves behind the framebuffer-hash regression harness the
subsystem needs anyway.

---

## The questions

**Q1 (mechanism — the genuine unknown).** Can the runtime-owned back buffer be
exposed as a guest-writable raw pointer via `acquire`, identically observable
across: (a) emulated rv32emu, (b) the host-Lua fast path, (c) native
bare-metal — such that the *same* cart, writing pixels through the acquired
pointer and calling `present`, produces the *same* framebuffer on all three?

**Q2 (determinism — prove the load-bearing assumption).** Compiling one integer
rasterizer source into host `libblyt`, the WASM module, and native RV32
`libblyt32`, does a torture frame exercising every primitive hash
bit-identically across all legs including the QEMU native gate?

**Q3 (measurement, stretch — not a gate).** What is the per-primitive ECALL cost
on the slowest targets (native Milk-V-Duo-class, wasm-emulated)? This decides
whether batch variants (ADR-0052) ship in v1 or are deferred. Batch is purely
additive, so a wrong guess is cheap.

---

## Why this is a risk

- **Q1** is the project's signature risk class — cross-target *execution*, not
  algorithms. The ECALL-primitive path is low risk (host writes its own
  `session->pixels[]`). The **`acquire` raw-pointer-into-guest-memory path is
  the real unknown**: it requires a host-reserved, guest-visible,
  host-readable region inside rv32emu's memory map that does not collide with
  the cart heap / dynamic loader, and an equivalent on the host-Lua and native
  paths. If a single contract can't span all three, the API may need a fallback
  (e.g. ECALL-only blit in v1, raw-pointer deferred).
- **Q2** is *almost* certainly fine, but it is the assumption the entire
  "Option A" decision rests on. Integer hazards exist (signed-overflow UB,
  signed right-shift, `int` width) and are controllable (`-fwrapv`, fixed-width
  types) — the spike proves they were controlled, and the hash harness catches
  any future regression.

---

## What to build

Minimal, mirroring prior spikes: `clear` + two or three integer primitives +
the `acquire` raw-write path, wired through all three execution models and
hashed on every leg. **No** text, blit scaling, fill patterns, tilemaps, or
effects — those are implementation once Q1/Q2 are green.

### Stage 0 — Shared integer rasterizer skeleton

- One dependency-free C translation unit, owned by the **Blyt32** layer — *not*
  `libblytcommon`. The paletted 2D model is Blyt32-specific (ADR-0086:
  `blyt32.gfx.*`, absent on BlyTTY/Blyt3D), so the rasterizer belongs with the
  `libblyt32` family, not the variant-agnostic common layer that future variants
  share. The *same source* must compile into **(a)** the host runtime's Blyt32
  graphics ECALL handlers — serving both the emulated and host-Lua paths — and
  **(b)** the native `libblyt32` variant (compiled RV32, runs natively); the
  wasm module builds the host side via emscripten. The emulated guest
  `libblyt32.so` carries only the thin ECALL stubs, **no** rasterizer.
  **Confirm** this host + native-RV32 (+ wasm host) single-source structure is
  buildable — if it can't be one source, that itself is a finding.
- Implement, integer-only: `clear(color)`, `pixel(x,y,color)`,
  `rect_fill(x,y,w,h,color)` (no pattern yet), `line` (Bresenham). Operate on a
  `uint8_t *fb, int stride` so the same code serves both host and guest.
- No libc beyond `memset`/`memcpy`. No FP. Build with `-fwrapv` and fixed-width
  types.

### Stage 1 — Emulated path: ECALL primitives + `acquire`/`present`

- **ECALL primitives (low risk, do first):** allocate ECALL numbers in 100–199
  (`ecall.h`), add guest stubs (model on
  `runtime/guest/src/libblytcommon/blytcommon_emu.c` and the resource ECALLs),
  and host handlers in the `cart_run.c` dispatch switch (the big `switch`
  from `:1266`; resource handlers at `:1311` are the closest existing model).
  Handlers call the Stage-0 rasterizer against `session->pixels[]` and set
  `cart_has_drawn = true` (replacing the test card).
- **`acquire`/`present` raw pointer (the Q1 crux):** evaluate candidate
  mechanisms and pick one that generalizes:
  - (a) **Runtime-reserved guest region** — runtime carves a stable 76,800-byte
    region in rv32emu guest RAM as the back buffer; `acquire` returns its guest
    VA; `present` (or readback) copies/points it into `session->pixels[]`.
    Investigate how to reserve/locate that region without colliding with the
    cart heap or `ld-blyt.so`.
  - (b) **Registered cart buffer** — cart owns the buffer in its own guest
    memory and registers it; `acquire` returns it back, `present` reads the
    registered guest address. Simpler reservation, mild tension with ADR-0008
    "runtime owns the back buffer."
  - Record which works and why; (a) is preferred if feasible.
- Determinism note for this stage: ECALL-primitive pixels are produced by
  *runtime* C (Option A integer); `acquire` raw-write pixels are produced by
  *cart* code (deterministic via the §5 abstract machine on this path).

### Stage 2 — Host-Lua fast path

- Wire the same Stage-0 rasterizer and the same `acquire`/`present` +
  ECALL-primitive API into `frontends/wasm/wasm_main.c`'s host-Lua path. Here
  the back buffer is plain host memory and `acquire` returns a host pointer —
  the mechanism differs from Stage 1 but the **API contract must be identical**.
- Determinism caveat to verify: on this path, `acquire` raw writes come from
  host-compiled Lua using host-computed values. ECALL-primitive draws come from
  the shared integer rasterizer (host compile). Both must match the emulated and
  native legs.

### Stage 3 — Native bare-metal (QEMU gate)

- Implement `acquire`/`present` + the ECALL-primitive entry points in the
  native `libblyt32` variant (`frontends/native/src`), with the Stage-0
  rasterizer compiled for RV32 and running natively (no ECALL).
- Expose the back buffer over whatever the launcher
  (`frontends/native/launcher.c`) reads for headless frame capture.
- This leg is the one that proves the **RV32-compiled** rasterizer matches the
  host/wasm compiles.

### Stage 4 — Framebuffer-hash determinism harness (the Q2 proof)

- Two probe carts: **(i)** draws the torture frame via **ECALL primitives**;
  **(ii)** draws an equivalent frame via **`acquire` raw writes**. The torture
  frame exercises every Stage-0 primitive plus edge cases (off-screen clipping,
  zero-size, max coords, negative coords).
- Run both through all legs using the existing three-leg cart harness
  (`run_cart_native` / `run_cart_wasm` / `run_cart_libretro`; helpers in
  `tests/integration/tests/common/mod.rs`; suites `core.rs`, `wasm.rs`,
  `libretro.rs`, `native_qemu.rs`).
- Hash `blyt_session_get_pixels()` (the 320×240 paletted buffer — the
  determinism target) per leg. Assert **cross-leg equality** (catches
  divergence) *and* **against a checked-in golden** (catches collective drift +
  pins the pixel-coverage spec). The QEMU native leg vs the host legs is the
  highest-value assertion.

### Stage 5 — Per-primitive ECALL cost (stretch, Q3)

- Microbench: a cart drawing N rect_fills/sprites per frame via single ECALLs;
  measure cost-per-primitive on wasm-emulated and (if available) a
  Milk-V-Duo-class native target. Report whether 60 Hz holds at a realistic
  primitive count, i.e. whether batch variants are needed in v1.

---

## Success criterion

- **Q1 met:** one `acquire`/`present` + ECALL-primitive API contract is
  implemented on all three execution models; a single probe cart draws
  identically through each. The chosen `acquire` mechanism (Stage 1 a/b) is
  documented.
- **Q2 met:** both probe carts (ECALL and raw-write) hash bit-identically
  across host-x86, host-arm, wasm, and the QEMU native gate, and match the
  checked-in golden. The integer-only rasterizer is confirmed divergence-free
  with no FP-flag dependence.
- **Q3 recorded** (not gating): per-primitive ECALL cost and a
  batch-in-v1 yes/no recommendation.

Mirror the prior spikes' rigor: many runs, both cross-host directions
(arm64 + amd64) as Spikes D/K/U did.

---

## Results — validated 2026-06-28

**Verdict: both load-bearing assumptions hold. Option A (integer-only
rasterizer) is confirmed. Graphics can be built on this foundation.** Q1 ✅,
Q2 ✅. Q3 is only *partially* recorded — preliminary fast-host timings yield a
durable *structural* finding (cost is per-call not per-pixel; `acquire` is for
per-pixel work), but the batch-in-v1 question is **explicitly deferred** to a
floor-representative measurement under the ADR-0082 MIPS cap (see Q3 below); it
is not decided here. Full `test-integration` (341 tests incl. the QEMU native
gate) green; lint clean.

### Q2 (determinism) — ✅ proven across all four compile targets

One integer rasterizer source — `runtime/shared/blyt_raster.c`
(`clear`/`pixel`/`rect_fill`/`line`) — compiles into **(a)** the host runtime
(`blytplay`, arm64 here), **(b)** the wasm module (emscripten), **(c)** the
embedded libretro core, and **(d)** the native `libblyt32` variant (RV32,
runs natively under the QEMU gate). A torture frame (every primitive plus
off-screen / zero-size / negative / i32-overflowing-extent edge cases), an
`acquire`-raw-write frame, and a pure-Lua torture frame all hash
**bit-identically across every leg** (FNV-1a 64 over the 320×240 paletted
buffer) and match a checked-in Rust reference golden
(`tests/integration/tests/common/gfx`).

- **The single-source host + native-RV32 + wasm structure is buildable** (a
  Stage-0 open question): the rasterizer lives in `runtime/shared` (the
  single-source-into-guest-libs layer) and is wired into each leg by *where it
  is compiled*, which is what enforces the ADR-0086 Blyt32 scoping. The native
  `libblyt32` (previously an empty placeholder) switched to a multi-source build
  pulling in `blyt_raster.c` + `blyt_frame_hash.c`.
- **Option A is divergence-free with no FP-flag dependence** — trivially, since
  the rasterizer contains no floating point at all. Notably, **`-fwrapv` turned
  out not to be required**: the code avoids signed-overflow UB by doing clip math
  in `int64_t` intermediates, so all four targets already agree with the project
  default flags (the header's `-fwrapv` note is belt-and-suspenders, not load-
  bearing). This is a stronger result than the brief anticipated — there was no
  integer hazard to control, only one to *not introduce*.

### Q1 (mechanism) — ✅ proven, with a scoping refinement

Two sub-contracts, validated separately:

- **ECALL-primitive contract — universal.** Implemented and hashing identically
  on **all three execution models** (emulated rv32emu, host-Lua fast path, native
  bare-metal) for **both** C carts and Lua carts. The Lua surface is
  `blyt32.gfx.*` (`clear`/`pixel`/`rect_fill`/`line`), bound in the guest
  `libblyt32lua` (emulated-Lua → ECALL) *and* the host-Lua fast path
  (`wasm_main.c`, direct host call). A pure-Lua cart drawing the torture frame
  hashes to the same golden as the C cart across emulated-Lua (blytplay,
  libretro) and host-Lua (wasm).
- **`acquire`/`present` raw-pointer path — mechanism (a) chosen.** The runtime
  reserves a stable 320×240 region at guest VA **`0x02000000`** — a 32 MiB free
  gap between the exit trampoline (`0x01000000`) and the cart-heap arena
  (`0x04000000`); the cart image is capped at 16 MiB and the guest stack sits
  near the 256 MiB top, so the region collides with nothing. `acquire` returns
  the VA; the cart writes palette indices directly (no per-pixel ECALL);
  `present` copies the region into `session->pixels[]` and sets `cart_has_drawn`.
  On native bare-metal the same entry points return the in-process back buffer
  directly (no copy). **Proven on the emulated and native legs.**
- **Scoping refinement (the one nuance worth recording):** the raw-pointer
  `acquire` is inherently a **C/native escape hatch, not a Lua-surface feature** —
  a guest VA does not map cleanly to Lua, and the host-Lua fast path only runs
  *pure*-Lua carts (which have no raw pointers; a hybrid cart's C runs through the
  emulator anyway). So the *primitive* contract is what is truly universal across
  all execution models; the raw-pointer path spans the C/native models, which is
  exactly its intended hot-loop use under ADR-0008. **No ADR-0008 amendment is
  needed** — `acquire` stays as designed for C/Rust carts; Lua's drawing surface
  is the primitives. (If a future Lua cart needs bulk pixel access, the natural
  shape is a `blyt32.gfx` image/buffer object wrapping the region, not a raw
  pointer — out of scope here.)

### Capture mechanism (built, reusable by the subsystem)

`BLYT_FRAME_HASH=1` makes the runtime emit one `[blyt:fbhash] <hex>` line per
frame (FNV-1a 64 of the paletted buffer) on stdout — the one channel every
leg's harness captures, so a single mechanism serves blytplay, libretro, wasm,
and the QEMU gate. The host runtime emits it from its `frame_done` hook. On
native bare-metal, `frame_done` lives in the *variant-agnostic* `libblytcommon`,
which cannot reference the Blyt32 framebuffer directly; the emit therefore goes
through a **weak `blyt_gfx_on_frame_boundary` hook** that the native `libblyt32`
graphics variant defines strongly (the same cross-lib weak-ref pattern the Lua
stubs already use for `blyt_console_debug`), keeping `libblytcommon`
graphics-agnostic. The host-Lua fast path emits from its own present step.

### Q3 (per-primitive ECALL cost) — preliminary; NOT a floor-representative measurement

Recorded as a single-host data point, but it does **not** answer the
batch-in-v1 question and must not be read as doing so. Two reasons it is not
floor-representative:

1. **Wrong host, and uncapped.** Measured on one fast arm64 host (this Mac),
   `blytplay`/rv32emu, headless and **unthrottled**. The emulated path's
   performance floor is the Pi Zero 2 W (Cortex-A53 @ 1 GHz; ADR-0002), and
   **ADR-0082 mandates a MIPS cap** that throttles every emulator run to that
   floor's throughput *precisely so dev-host speed cannot mislead*. That cap is
   "left unset until Spike A measures the Pi value" — so this run is exactly the
   "fast-host drift accepted" regime ADR-0082 exists to eliminate. The honest
   unit is per-ECALL cost in guest cycles against the Pi-derived budget, plus the
   host-side rasterize wall-clock on the floor's own A53 (native host code, which
   the cap does not even model) — not fast-host wall-clock.
2. **The two floors behave oppositely on this question.** Native execution
   (K230D floor, ADR-0002) runs carts directly — *no emulator, no ECALL, no cap* —
   so ECALL cost is irrelevant there. The ECALL question lives only on the
   *emulated* path, whose floor is the Pi Zero 2 W. A Milk-V/K230D-class
   measurement would characterise the native path (no ECALL), not this.

Also unaddressed vs. the brief's Stage 5 / success criterion: no wasm-emulated
run (only `blytplay`), no cross-host (arm64 + amd64) pass, no floor-target run.

**Raw fast-host / uncapped numbers** (8×8 `rect_fill`, draw cost isolated as
`(T(N) − T(0)) / frames`; `.claude/q3_bench.py`):

| path (fast-host, uncapped) | per-call | calls/frame @ 60 Hz |
|---|---:|---:|
| single-ECALL `blyt_gfx_rect_fill` | ~0.22 µs | ~75,000 |
| raw `acquire` + guest-write (64 px/rect) | ~0.37 µs | ~44,000 |

- **Structural finding — hardware-independent, keep.** The cost is **per *call*,
  not per *pixel***: the ECALL trap dominates and the host's pixel work rides
  along cheaply. A many-pixel primitive (`rect_fill`/`line`/blit) amortises the
  trap; a one-pixel `gfx_pixel` pays the full trap for one pixel. The crossover —
  **bulk via primitives, per-pixel / read-modify-write / full-screen procedural
  via `acquire`** — follows from the *shape of the work*, not the clock, so it
  holds on any host (on the floor it is *more* pronounced: a full-screen
  per-pixel ECALL repaint is already ≈ one whole frame on this fast host, so it
  is hopeless at the floor — reinforcing that `acquire` is mandatory for that
  regime). This is the empirical backing for ADR-0008's "no per-pixel API
  overhead" framing.
- **Batch-in-v1 verdict — NOT decided here; deferred.** Projecting the fast-host
  number down ~5–10× to a Cortex-A53 puts single-ECALL `rect_fill` at single-digit
  µs and ~7–15 k calls/frame — *probably* still above realistic scene counts, but
  that is a projection, not a measurement, and it ignores the host-side rasterize
  cost that runs on the floor's own A53. The batch question properly belongs to
  **Spike A** (which sets the ADR-0082 cap from CoreMark/Embench on the Pi) plus a
  Doom-class workload, as ADR-0082 itself states. **ADR-0052 is left as-is** — its
  "~1 µs" is a working figure, and nothing here is floor-grade enough to amend it.

### Implementer notes / gotchas surfaced

- **`acquire`-raw pixels are cart-produced** (deterministic via the §5 abstract
  machine), whereas ECALL-primitive pixels are runtime-produced (Option A
  integer). Both hash identically, as required.
- The cross-compiled QEMU launcher (`build-riscv64/blyt_native`, built via the
  Docker `blyt_native_riscv64` target) is **not reliably rebuilt by
  `test-integration` when only `cart_load.c`'s import allowlist changes** — a
  stale launcher rejects carts importing new symbols with "imported symbol not on
  allowlist". Rebuild it explicitly after touching the allowlist.
- A new `runtime/shared/*.c` must also be added to
  `frontends/wasm/CMakeLists.txt` (it recompiles the libblyt sources rather than
  linking the target); the native `libblyt32` needed musl `-isystem` paths for
  `blyt_raster.c`'s `<string.h>` (`memset` resolves as a dynamic undef over the
  DT_NEEDED chain, like the bundled zstd decode sources).
- When benching, an empty draw (no `clear`/`present`) leaves `cart_has_drawn`
  false → the runtime renders the (costly) PM5544 test card, which silently
  dominated a baseline measurement until the probe was made to mark drawn.

---

## What this spike does and does not decide

**Decides:**
- That host-side integer rasterization is bit-identical across all targets
  (validates Option A), or surfaces the specific corner that isn't.
- That a unified `acquire`/`present` framebuffer mechanism is feasible, and
  which concrete mechanism the emulated path uses.
- Whether batch variants are needed in v1.

**Does not decide (deliberately out of scope):**
- The full primitive set, text/BMFont (ADR-0069/0072), blit + scaling
  (ADR-0049/0052), tilemaps (ADR-0060), palette ops, fill patterns (ADR-0050),
  present-time effects (shake/fade/prev-frame, ADR-0051/0122). All become
  implementation once the two assumptions hold.
- The pixel-coverage *reference rules* (which pixels a filled circle includes,
  fill-rule, endpoint inclusion). These are a spec-writing + golden-hashing
  task, not an empirical risk — the spike only needs *a* consistent behavior to
  hash.
- Anything in the `blyt_math_*` / easing subsystem (separate, guest-side,
  unbuilt) and the WASM host-Lua libm-parity question it raises.

---

## Starting points (implementation repo `../blyt`)

- Framebuffer + draw gate: `runtime/host/src/libblyt/cart_run.c:241`
  (`pixels[]`), `:244`/`:2800` (`cart_has_drawn`), `:1266`+ (ECALL dispatch
  switch), `:1311` (resource ECALL handlers — closest model for new handlers).
- ECALL map + 100–199 graphics reservation: `runtime/host/src/libblyt/ecall.h`
  (`:15`).
- Readback / hash target: `blyt_session_get_pixels` / `_get_palette` /
  `_expand_frame`, `runtime/host/include/blyt_runtime.h:311–315`; dims `:296`.
- Test card to displace: `runtime/host/src/libblyt/testcard.c`.
- Guest ECALL stubs: `runtime/guest/src/libblytcommon/blytcommon_emu.c`
  (emulated), `runtime/guest/include/blyt.h` (cart-facing decls).
- Host-Lua fast path: `frontends/wasm/wasm_main.c`.
- Native variant: `frontends/native/` (`launcher.c`, `src/`).
- Cart test legs + helpers: `tests/integration/tests/common/mod.rs`;
  `core.rs`, `wasm.rs`, `libretro.rs`, `native_qemu.rs`. New cart-visible
  functionality needs all three legs (CLAUDE.md "three legs" rule).
- Build/test: `cmake --build build --target sdk`, then
  `test-integration` (includes the QEMU native gate; cross-build
  `blyt_native_riscv64` first).

## Relationship to the ADR log

This spike validates the implementability/determinism of decisions already
made — it does not propose new ones. Relevant ADRs: 0008 (API-based memory /
`acquire`), 0046 (handles/errors/two-header split — `blyt32.h` vs
`blyt_runtime.h`), 0048 (no clip/camera), 0049 (index-255 transparency), 0050
(fill patterns), 0052 (host-side primitives + batch), 0069/0072 (text/BMFont),
0076 (draw is read-only), 0086 (variant-scoping: paletted 2D is Blyt32-specific
→ rasterizer home is the `libblyt32` family). If Stage 1 finds the `acquire` raw-pointer path
infeasible to unify, that *would* feed back into ADR-0008 (an amendment scoping
`acquire` per execution model) — flag it rather than work around it.

## Dependencies

- Spike U (`done`) — `ilp32d` / hardware doubles, and the cross-path digest
  harness this spike's framebuffer-hash extends.
- Spike D / K (`done`) — cross-platform / cross-host determinism methodology to
  reuse.
- No external blockers. Native leg runs under the existing QEMU gate; real
  silicon confirmation is optional and deferrable (cf. Spike U Stage 6).

# ADR-0136: Host-Lua everywhere — retire the emulated RV32 Lua VM as a shipped exec path on non-RISC-V hosts

## Status

Accepted (2026-07-07). Decision made by the overseer after the two gating
spikes returned GO. Supersedes the "drop to native for performance" premise of
ADR-0039; gates the direction ADR-0135 was built for.

## Context

Pure-Lua carts have two execution models today:

- **Emulated RV32 Lua** — the Lua 5.4 VM compiled to RV32, running under rv32emu
  on non-RISC-V hosts (desktop, libretro, browser).
- **host-Lua fast path** — the same Lua fork compiled *natively for the host*
  (WASM today), running the cart's bytecode directly with no rv32emu layer.

The host-Lua fast path is dramatically faster but was WASM-only. The question
this ADR settles: **should host-Lua replace emulated-RV32-Lua as the shipped
execution path for pure-Lua carts on non-RISC-V hosts (native players too), not
just WASM?** Two spikes gated it:

- **Spike Y (throughput)** — native Lua is ~44–58× faster than emulated on the
  floor device (Pi Zero 2 W). Corroborated this session on a Doom-shaped
  workload: `doom_tick` logic 53×, an `R_DrawColumn` Lua rasterizer 48×, on real
  Pi hardware, bit-identical output across legs.
- **Spike Z / blyt#225 (determinism) — GO.** Native host-Lua reproduces the
  Berkeley-SoftFloat reference **bit-for-bit** on real arm64 + real x86-64 (CI) +
  wasm, including the Phase-B strtod/number-format seam (blyt#227). Determinism
  was the only hard blocker; it is removed.

## Decision

**Ship host-Lua everywhere.** Pure-Lua carts run on the native host-Lua fast path
on all non-RISC-V hosts (blytplay, libretro, browser). The RV32 *Lua VM* is
retired as a shipped execution path there.

**Scope — what is retired vs kept:**

- **Retired (shipped path):** the emulated RV32 Lua VM for pure-Lua carts on
  non-RISC-V hosts. Those carts run host-Lua.
- **Kept — rv32emu:** still runs **native cart code** (C/Rust/C++) and the
  **native half of hybrid carts** on non-RISC-V hosts (ADR-0130 bridge). Removing
  the Lua VM from the shipped path does not remove rv32emu.
- **Kept — the RV32 Lua build:** on **real RISC-V hardware** carts run native
  (RV32 on RV32), so the RV32 Lua VM build stays for that target — and is
  exercised in CI via the QEMU native gate (see below).
- **Retired — emulated RV32 Lua as a determinism oracle.** *This ADR does not
  keep the emulated-RV32-Lua-under-rv32emu leg as the determinism reference.*
  rv32emu-as-oracle proves host-Lua == rv32emu-softfloat == the IEEE/RISC-V
  *spec* reference — it does not prove parity with real silicon (a model, not the
  hardware), it is entangled with the interpreter path being retired, and the
  property it verifies is already guaranteed by construction (below). It is
  replaced by the determinism-verification model that follows.

### Determinism verification (replaces the emulated-Lua oracle)

Determinism is a property of the *shipped* legs agreeing with each other, plus a
by-construction FP guarantee — not of matching a separate emulated leg.

- **Non-FP** (GC/iteration/interning/integer-overflow): guaranteed by the same
  Lua bytecode + fixed hash seed on every leg. Reference = a pinned golden;
  regression caught by cross-leg agreement (host-Lua x86-64 / arm64 / wasm).
- **FP: by construction.** IEEE-754 correctly-rounded ops + `-ffp-contract=off`
  (no FMA) + RISC-V NaN canonicalization (ADR-0010) ⇒ native f64 == the softfloat
  reference. Proven on real arm64 + x86-64 + wasm (Spike Z). Guarded — not
  re-derived each run — by the contraction-torture test (catches FMA regressions)
  + cross-leg agreement + the golden. A "pure softfloat Lua" (Mode B) as a live
  oracle is *non-viable to build natively* (Spike Z: `-msoft-float` fails on
  x86-64 SSE-return / is ignored on arm64) for the transcendental kernels, and
  unnecessary given the by-construction guarantee.
- **RISC-V hardware parity** (a public target): validated by the **QEMU native
  gate** — an *independent* RISC-V implementation, running the *native* cart path
  (the same path real hardware runs — RV32 Lua executing natively), with its
  *own* softfloat — plus periodic real-hardware spot-checks (Spike U Stage 6).
  This is a stronger RISC-V check than rv32emu (native path, independent impl)
  and is where the RISC-V-public-target confidence actually comes from.

## Rationale

1. **Throughput (Spike Y + this session).** ~50× on the floor device; the
   order-of-magnitude lever is running Lua native, not per-pixel VM patches.
2. **Determinism (Spike Z, GO).** host-Lua == the softfloat reference bit-for-bit
   across arm64/x86-64/wasm, transcendentals + `pow` (ADR-0135 Phase A) and
   strtod/number-format (Phase B, blyt#227). Netplay/replay/rewind hold.
3. **Meterability is preserved.** The console's consistent, floor-representative
   CPU budget (ADR-0082 MIPS cap, the watchdog, Spike G `lua_sethook`) depends on
   *counting instructions*, which is a property of interpretation. host-Lua keeps
   it: Lua bytecode is still interpreted by the Lua VM (only the VM's host
   compiler changes to native), so `lua_sethook` still counts and the budget
   stays consistent and portable. This is why host-Lua is safe where a JIT is not
   (see Alternatives).
4. **The native tier's role is clarified, not diminished.** With host-Lua, the
   Lua tier is fast; the console's native API primitives own the genuinely hot
   paths (gfx/audio, host-C, fast on every target). Measured this session:
   cart-native-C over host-Lua is ~1.0× on game logic (a tie — native is even
   ~10% slower interpreted) and ~1.4× on a tight rasterizer. So the cart's native
   tier is **not a "drop to native for performance" escape hatch** — it is a
   "bring your preferred native toolbox / build in your language" surface. This
   supersedes ADR-0039's premise that Lua authors will drop to native *for speed*
   (see the ADR-0039 amendment).

## Consequences

- **Implementation** is tracked by an epic (blyt#TBD): extend the host-Lua fast
  path to the native players — VM lifecycle + S-proxy + state buffers, the
  gfx/surface fast path, resource-heap (`guest_heap_used`) accounting, the
  ADR-0130 ECALL bridge for hybrid carts, libretro, DAP/GDB, the non-FP parity
  matrix on native (Spike Z Q5), and the cart-run dispatch that routes pure-Lua
  carts to host-Lua. The pinned hash seed (`luai_makeseed()=0x424C5954`) ships in
  the native player.
- **Prerequisite:** blyt#227 (Phase B FP number-format seam) merged with the
  stable fork tag; then blyt#225 closes.
- **Execution-model change:** `cart_run.c` gains a third model — non-RISC-V host
  + pure-Lua → host-Lua native (alongside emulated-native-code and
  bare-metal-RISC-V).

## Alternatives considered

- **WASM as the cart ISA (instead of RV32).** Given the "native RISC-V hardware"
  story is weak, RV32's main justification (ADR-0024) weakens, and WASM-as-cart
  would dissolve the browser C-tier floor, unify the execution zoo, and give the
  native tier a real JIT. **Not pursued now**, on two counts: (a) it gives up
  *meterability* — a consistent, floor-representative CPU budget needs instruction
  metering (interpretation), and there is no portable deterministic meter across
  WASM engines (wasmtime has "fuel"; V8/browser does not), so the budget would be
  runtime-dependent; (b) it gives up "one controlled runtime everywhere" (the
  browser necessarily runs the cart on V8, forcing cross-engine determinism
  governance). WASM-as-cart-ISA was never actually evaluated in ADR-0024; it
  merits a real spike + ADR if revisited (a cross-engine determinism probe is the
  cheap first step). Recorded here so the option is on the books.
- **JIT the emulated path (rv32emu T1C, or a WASM-JIT runtime) for the native/C
  tier.** Measured this session: rv32emu's T1C JIT gives ~6.3× on an integer
  rasterizer on the Pi (23.6 → 3.73 ms/frame). **Deprioritized**, because (a) a
  JIT breaks instruction-metering (ADR-0082 already notes JIT cycle costs are
  approximate) and cannot be made budget-consistent across heterogeneous
  runtimes — the exact property host-Lua preserves; (b) with host-Lua, the native
  tier is not the performance surface, so the JIT is a niche turbo, not a core
  need; and (c) the blyt-tech rv32emu fork's JIT lacks D-extension support
  (captured separately). A JIT, if ever adopted, belongs as an opt-in "turbo"
  mode, never the budget-defining path.

## References

ADR-0135 (host-Lua FP seam), ADR-0130 (ECALL-bridged Lua C API), ADR-0082
(emulator MIPS cap), ADR-0039 (Lua performance strategy — premise amended by
this ADR), ADR-0066 (Lua 5.4), ADR-0024 (cart ELF format). Spikes Y
(`spike-y-lua-per-pixel.md`), Z (`spike-z-*`). Issues blyt#223, #225, #227.

# ADR-0001: ISA choice — RV32IMAFC, little-endian, no D extension

## Status
Accepted — amended 2026-05-09 to add A (atomics) extension; amended
2026-06-15 to add D (hardware doubles) extension (Spike U).

## Context

The console needs a native instruction set for cart binaries. The ISA choice
determines which hardware can run carts natively, which compiler toolchains
authors can use, and how the emulator layer behaves on non-native hosts.

Key constraints:
- Must run on cheap SBC-class hardware ($9–13 range).
- Must be license-clean for an open-source project and for parties building
  physical hardware products.
- Must have a tractable emulator for desktop/browser hosts.
- Must support the console's 32-bit numeric model (§4).

ARM was the obvious alternative but carries licensing ambiguity consequential
for an open-source project and for anyone building hardware products around
the console. x86 is 64-bit-default, license-encumbered, and not available in
cheap SBC form. MIPS is legacy with a declining ecosystem.

## Decision

Use **RISC-V** as the ISA, specifically the **RV32IMAFC** profile:

- **RV32I** — 32-bit integer base.
- **M** — integer multiply/divide.
- **A** — atomics (LR/SC and AMO instructions).
- **F** — single-precision floating point (f32).
- **C** — compressed instructions (reduces code size ~25–30%).
- **Little-endian** (matches every shipping RISC-V implementation, WASM, x86,
  and ARM).

*(Amended 2026-06-15: the D extension is now included — see below.)*

Explicitly excluded:
- **V** (vector) — not present in the reference hardware floor.

## Consequences

- RISC-V is license-clean (RISC-V International open standard); no
  royalties or restrictions for hardware or software use.
- RV32IMAFC is supported by GCC, Clang/LLVM, and the full GNU toolchain.
  Cart authors can use Lua, Rust, C, Zig, or any RV32IMAFC-targeting language.
- The correct Rust bare-metal target is `riscv32imafc-unknown-none-elf`;
  the A extension is mandatory in that target string. There is no
  `riscv32imfc` Rust target in upstream nightly.
- The A extension is present in every shipping RISC-V SBC-class CPU
  (including the K230D's C908 core) and is fully implemented by the
  rv32emu emulator. Its inclusion adds no interpreter complexity relative
  to what the emulator already handles.
- Carts are single-threaded, so LR/SC and AMO instructions always succeed on
  the first attempt with no contention. Atomics are used by Rust's standard
  concurrency primitives — `AtomicU32`, `Once`, and ecosystem crates that
  use atomics internally — even in single-threaded code. Excluding A would
  require patching these patterns out of common Rust idioms.
- The emulator implements RV32IMAFC — a small, auditable surface relative
  to full RV64GC.
- RISC-V conformance can be validated against the upstream `riscv-tests`
  suite.
- f32 (single-precision) covers every realistic use case at 320×240
  fidelity. *(Superseded 2026-06-15: D extension added, f64 is now
  hardware-supported — see amendment below.)*
- Cheap reference hardware (K230D, ~$29) is available now; the SBC
  ecosystem is growing.
- Cart binaries are standard RV32IMAFC ELF files, debuggable with GDB and
  LLDB without custom tooling.

## Amendment — D extension (Spike U, 2026-06-15)

The ISA is extended from **RV32IMAFC** to **RV32IMAFDC**:

- **D** — double-precision floating point (f64), `ilp32d` hard-float ABI
  (doubles in FP registers, `FLEN=64`).

**Rationale.** The metal target is rv64 silicon running RV32 in compat mode;
the hardware physically has D regardless. The original `ilp32f` choice was a
definition decision, not a hardware constraint — and `ilp32f` is the niche
float ABI while `rv32imafdc`/`ilp32d` is the standard rv32-Linux baseline.
Spike U validated the full stack (all three execution paths, cross-host
bit-determinism, Lua int32+float64) and produced a functional branch; D is
reachable, cheap, and correct (see `docs/design/spike-u-hardware-doubles.md`
and ADR-0132).

**Implementation.** The `blyt-tech/rv32emu` fork adds the D extension on a
`spike-u-rv32d` branch (64-bit FP register file + NaN-boxing of F results +
26 scalar D ops + 4 compressed D ops, wired to Berkeley SoftFloat `f64_*`).
All FP remains routed through Berkeley SoftFloat — determinism is unaffected.

**ISA / ABI change.** Cart ELF headers now report:
- `e_flags = EF_RISCV_RVC | EF_RISCV_FLOAT_ABI_DOUBLE`
- arch attributes: `rv32i…f2p2_d2p2_c…zcd…`

The host loader enforces `EF_RISCV_FLOAT_ABI_DOUBLE` at cart load time (see
ADR-0024 amendment).

**Rust target.** There is no upstream `riscv32imafdc` Rust target; the console
uses a custom target JSON `riscv32imafdc-blyt-none-elf` (see ADR-0108
amendment and ADR-0132).

**Numeric model.** f64 is promoted to a first-class numeric type alongside
f32 (see ADR-0005 amendment and ADR-0132). V (vector) remains excluded.

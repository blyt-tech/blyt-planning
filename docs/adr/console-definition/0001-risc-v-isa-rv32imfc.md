# ADR-0001: ISA choice — RV32IMAFC, little-endian, no D extension

## Status
Accepted — amended 2026-05-09 to add A (atomics) extension.

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

Explicitly excluded:
- **D** (double-precision FP) — f64 is not part of the console's numeric
  model; including D would add interpreter complexity for no benefit.
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
  fidelity; the decision to exclude D is consistent with the 32-bit
  everywhere numeric model (ADR-0005).
- Cheap reference hardware (K230D, ~$29) is available now; the SBC
  ecosystem is growing.
- Cart binaries are standard RV32IMAFC ELF files, debuggable with GDB and
  LLDB without custom tooling.

# ADR-0010: Persistent state in POD typed buffers

## Status
Accepted

## Context

Game state that must survive save/restore cycles (save games, save states,
hot reload, rewind) needs a serialization strategy. Options ranged from
untyped memory snapshots (fast, fragile) to full object graph serialization
(flexible, complex) to constrained typed layouts (predictable, portable).

The determinism requirement (ADR-0007) and the 32-bit numeric model
(ADR-0005) heavily constrain the design: save state must be bit-identical
across platforms, which rules out pointer-containing or layout-undefined
structures.

## Decision

**Persistent state lives in manifest-declared typed buffers with
packer-generated field constants.**

Buffer schemas are declared in `cart.config` (the manifest) — see ADR-0009
for the declaration syntax and packer-generated constant format. Layouts are
**POD (Plain Old Data)**: primitive fields only (i8/u8/i16/u16/i32/u32/f32/
bool), fixed-size arrays of primitives, no pointers, no dynamic structures.
References between buffers use integer indices, not pointers.

The runtime tracks all declared state buffers plus its own internal state
(RNG, timers, input buffer, palette cycling, screen shake, etc.). Save/restore
walks tracked regions and emits a self-describing binary snapshot with type
info and endianness markers. The snapshot format is cross-platform portable.

Layout schema is encoded in the `.cart.config` ELF section, readable by the
runtime at load time — enabling migration (ADR-0045 hot reload) and
cross-platform compatibility.

**NaN canonicalization on f32 field writes.** Writing a NaN value to an f32
state buffer field canonicalizes it to `0x7FC00000` — the RISC-V canonical
NaN (positive quiet NaN, zero mantissa payload), which x86 and ARM also
produce as their default NaN. This prevents cross-platform divergence from
NaN payload bits, which are architecturally meaningless but not bit-identical
across implementations. In dev mode, a NaN write additionally triggers a
warning with the source location ("NaN written to `enemies.x[3]`") — NaN
in game state is almost always a bug in upstream arithmetic, and silent
canonicalization should not mask it during development. In release builds
the canonicalization is silent.

## Consequences

- Save/load is essentially memcpy of tracked regions — simple, fast,
  cross-platform portable.
- POD discipline forces data-oriented design, which is appropriate for games
  at this fidelity and yields good cache behavior.
- Typed layouts (vs. untyped bytes) enable cross-platform save portability:
  a save written on desktop loads on RISC-V hardware.
- Rewind (ring buffer of snapshots), replay (snapshot + recorded inputs), and
  netplay (lockstep sim) all follow from the save/restore infrastructure
  without additional design.
- Authors cannot store Lua function references, raw pointers, or heap
  allocations in state buffers. This forces patterns (handler by name,
  computed caches rebuilt in `init`) that are necessary for correct save
  games regardless.
- The packer can statically compute the total state memory footprint at build
  time, enabling size-class budget enforcement before the cart runs.
- The runtime can allocate all state memory statically at cart load time —
  before any cart code runs. There is no dynamic state allocation during
  gameplay; the full working set is known and committed upfront.
- Field access uses compile-time integer constants (ADR-0009), not string
  lookups — zero overhead beyond the API call itself.

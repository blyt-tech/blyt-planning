# ADR-0005: Numeric model — i32 and f32 everywhere

## Status
Accepted

## Context

Fantasy consoles and game engines often have subtle numeric boundary problems:
Lua defaults to f64, C compilers default to f64 for floating-point literals,
and cross-platform save state formats can diverge at the boundary between
scripting language and native code. The console's save state and determinism
requirements (ADR-0007) make numeric consistency load-bearing.

The ISA is RV32IMAFC (ADR-0001), which makes i32 and f32 natural first-class
types. The question is whether to expose f64 and i64 as first-class or confine
them to escape hatches.

## Decision

**All first-class numerics in the console are i32 and f32.**

- **Native carts (RV32IMAFC):** i32 and f32 are first-class in the ISA.
- **Lua:** built with `LUA_32BITS` (equivalent: `LUA_INT_TYPE=LUA_INT_INT`,
  `LUA_FLOAT_TYPE=LUA_FLOAT_FLOAT`). Lua numbers are i32 and f32.
- **State buffers:** field types i8/u8/i16/u16/i32/u32/f32/bool plus
  fixed-size arrays of these primitives. No f64 or i64 fields.
- **i64/u64:** available via a userdata library (`i64.new()`, arithmetic via
  metamethods) for rare cases. Not first-class in language or state.

Trade-off accepted: time accumulation as f32 seconds loses precision after
hours of play. Mitigation: store time as i32 frames or milliseconds;
`blyt.time.frame()` provides this.

## Consequences

- Eliminates precision-loss conversion at the Lua ↔ state buffer boundary.
- Save state is bit-identical across Lua and native carts.
- Consistent mental model: "this is a 32-bit console."
- f32 integer-exact range (up to 2²⁴ ≈ 16 million) is sufficient for game
  values at this fidelity. i32 range (±2.1 billion) covers any realistic
  score, counter, or index.
- Precedent: Defold uses 32-bit Lua in a commercial engine without issue.
- Authors needing i64 arithmetic (e.g., for cryptographic or 64-bit ID
  operations) use the userdata library at the cost of slightly more verbose
  syntax.
- Defining the console as "32-bit" gives it a clean, communicable identity
  consistent with the era it references.

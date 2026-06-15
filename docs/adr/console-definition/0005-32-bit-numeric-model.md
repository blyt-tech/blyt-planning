# ADR-0005: Numeric model — i32 and f32 everywhere

## Status
Accepted — amended 2026-06-15 (Spike U): f64 promoted to first-class
alongside f32 (see amendment below).

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

## Amendment — f64 as first-class numeric type (Spike U, 2026-06-15)

The original decision excluded f64 from the first-class numeric set because
"f64 is not part of the console's numeric model." Spike U revisited this: the
original justification no longer load-bears (Berkeley SoftFloat already
guarantees bit-determinism; the metal target physically has D; the window to
change the ABI is open pre-cart / pre-1.0). See `docs/design/spike-u-hardware-doubles.md`
and ADR-0132 for the full rationale.

**f64 is now a first-class numeric type** alongside i32 and f32:

- **Native carts (RV32IMAFDC):** i32, f32, and f64 are all first-class in
  the ISA. Doubles pass in FP registers per the `ilp32d` ABI.
- **Lua:** built with `BLYT_LUA_I32_F64` (`LUA_INT_TYPE=LUA_INT_INT`,
  `LUA_FLOAT_TYPE=LUA_FLOAT_DOUBLE`). `lua_Integer` = i32 (unchanged);
  `lua_Number` = f64 (was f32). `LUA_32BITS` is no longer used.
- **State buffers:** field type `8=f64` added (see ADR-0010 amendment and
  ADR-0133). A dedicated 64-bit value path carries f64 fields over the
  existing 32-bit scalar bus (lo/hi register pair).

**What is unchanged.** i64 and u64 remain non-first-class (userdata library
only). The "integer" first-class type remains i32 — the 32-bit integer
identity is unaffected. The console's numeric identity is now i32 + f64 (plus
f32 for legacy / size-sensitive paths), not strictly "32-bit everywhere."

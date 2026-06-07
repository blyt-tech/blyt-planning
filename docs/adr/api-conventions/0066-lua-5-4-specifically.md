# ADR-0066: Lua 5.4 specifically

## Status
Accepted

## Context

The Lua cart scripting layer requires a specific Lua version to be embedded
in the runtime. Lua 5.1 through 5.4 have meaningful differences. The choice
affects available language features, integer type support, bit operations, and
the maintenance trajectory of the embedding.

## Decision

**Lua 5.4, compiled with `LUA_32BITS`.**

- **`LUA_32BITS`:** the `lua_Integer` type becomes `int32_t` and
  `lua_Number` becomes `float` (32-bit IEEE 754). This aligns exactly with
  the 32-bit numeric model (ADR-0005): no 64-bit values exist in Lua code,
  matching the constraint on state buffer field types.
- **Native bitwise operators:** `&`, `|`, `~`, `<<`, `>>` (added in 5.3)
  are used extensively in flag manipulation and palette operations. LuaJIT
  required a separate bit library; 5.4 has them natively.
- **`_ENV`:** the `_ENV` mechanism (introduced in 5.2) allows the sandbox
  (ADR-0038) to replace the global environment cleanly, without patching
  the Lua interpreter.
- **Integer/float distinction:** Lua 5.4 distinguishes integer and float
  subtypes within the `number` type, enabling the runtime to enforce that
  state buffer integer fields receive integer values (not floats).
- **Maintained version:** Lua 5.4 is the current stable release as of the
  design date and receives ongoing maintenance. Lua 5.3 is no longer
  maintained.

LuaJIT is explicitly not used. LuaJIT does not support WASM compilation,
which is required for the browser frontend. A single Lua build that runs
everywhere (native, emulated, WASM) is prioritized over JIT performance
(see ADR-0039).

## Amendment (ADR-0130, 2026-06-07)

**Vendored version is 5.5.1.** The implementation vendors Lua 5.5.1
(the "deliberate, tested migration" anticipated below has effectively
already happened in the codebase). The rationale of this ADR carries
over unchanged — `LUA_32BITS`, native bitwise operators, `_ENV`
sandboxing, integer/float distinction — and in 5.5 the string-hash seed
became a `lua_newstate` parameter, which the next point relies on.

**Fixed hash seed.** All Lua builds (guest `libblyt32lua.so`, host-side
WASM Lua, `blyt-luac`) define `luai_makeseed()` to a fixed constant.
The default seed (ASLR + time) makes `pairs()` iteration order,
`table.sort` pivot selection, and default `math.random` streams differ
per run and between the rv32 and WASM execution paths, violating the
determinism requirements (ADR-0007, Spike D/Q gates). Hash-DoS
randomisation has no threat model here: a cart can only degrade its own
hash tables. See ADR-0130.

## Consequences

- All Lua carts use 32-bit integers and floats natively — no accidental 64-bit
  values sneak in through number literals or arithmetic.
- `_ENV` sandbox replacement requires no interpreter patching.
- The runtime embeds and maintains one specific Lua version; upgrading to
  5.5+ (when released) is a deliberate, tested migration, not an accident.
- Lua 5.4's garbage collector (generational mode available) can be tuned for
  game-loop allocations; the runtime configures it for minimal pause variance.

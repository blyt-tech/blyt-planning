# ADR-0132: ilp32d / hardware doubles — RV32IMAFDC as the console ABI

## Status
Accepted — validated by Spike U (2026-06-15).

## Context

The console was originally defined as a 32-bit machine with single-precision
floating-point only (`rv32imafc` / `ilp32f`). This was a definition decision,
not a hardware constraint. Three things converged to put hardware doubles on
the table (documented fully in `docs/design/spike-u-hardware-doubles.md`):

1. **Swift language probe (Spike V).** Swift's defaults lean on `Double`. The
   friction it created revealed that the f32-only choice was an unexamined
   default rather than a reasoned decision — no existing cart language had
   pushed on the assumption.

2. **The original justification is gone.** The historical reason to avoid
   hardware FP was non-determinism (x87 80-bit excess precision). Berkeley
   SoftFloat already provides bit-identical IEEE results on any host. The D
   extension adds hardware paths for the same correctly-rounded results — it
   does not introduce a new non-determinism source.

3. **Pre-1.0 window.** The project is pre-cart and pre-1.0: no external cart
   binary or save state depends on the ABI yet, so this is a rebuild exercise,
   not a migration. The toolchain was already in `build-std` territory (PIE/PIC),
   so a custom `ilp32d` target costs near-zero.

Spike U ran the full stack end-to-end (Stages 0–5): custom Rust target,
`ilp32d` sysroot rebuilt, rv32emu D extension, Lua int32+float64, save-state
across the widened FP file, and determinism cross-check on all three execution
paths.

## Decision

Adopt **RV32IMAFDC / `ilp32d`** as the console ABI.

- **ISA:** `rv32imafdc` — adds D (double-precision) to the existing
  `rv32imafc` profile (see ADR-0001 amendment).
- **Float ABI:** `ilp32d` — doubles pass in FP registers (`FLEN=64`). This is
  the standard rv32-Linux baseline ABI; `ilp32f` was the niche choice.
- **Rust target:** custom `riscv32imafdc-blyt-none-elf` JSON target, since no
  upstream nightly target exists for `riscv32imafdc` (see ADR-0108 amendment).
- **Lua:** `BLYT_LUA_I32_F64` (`LUA_INT_TYPE=LUA_INT_INT`,
  `LUA_FLOAT_TYPE=LUA_FLOAT_DOUBLE`) — `lua_Integer` = i32 (unchanged);
  `lua_Number` = `double` (was `float`). `LUA_32BITS` is no longer used.
- **Sysroot:** all guest libraries (`libblyt32.so`, `libblyt32lua.so`,
  `libblyt32lua-bridge`, `libblytc.so`, `libblytcommon.so`, `libblytcommonlua.so`,
  `blyt-tech/musl`) rebuilt with `-march=rv32imafdc -mabi=ilp32d`.
- **ELF identity:** `e_flags = EF_RISCV_RVC | EF_RISCV_FLOAT_ABI_DOUBLE`
  (see ADR-0024 amendment).
- **rv32emu fork:** `spike-u-rv32d` branch adds the D extension — 64-bit FP
  register file, NaN-boxing of F results in the widened file, 26 scalar D ops
  + 4 compressed D ops wired to Berkeley SoftFloat `f64_*`. All FP execution
  remains through Berkeley SoftFloat; determinism is unaffected.

## Consequences

**Determinism.** Berkeley SoftFloat routes all FP execution on the emulated
and WASM paths; hardware D on metal executes the same correctly-rounded IEEE
ops. Spike U Stage 5 demonstrated byte-identical output on rv32-softfloat
(arm64) and WASM-Lua-direct for both basic ops and transcendentals — the two
musl libm lineages (emscripten bundled and blyt-tech) agree to full double
precision for the tested transcendentals. Formal amd64 cross-host
confirmation and riscv-arch-test D suite run are deferred to the Docker run
before merge.

**Lua C API width.** `lua_Number` changes from `float` to `double`; `lua_Integer`
stays `int` (i32). Any hybrid Lua+C code (cart-side or host glue) that
declares `lua_Number` must use the blyt-tech/lua fork's `luaconf.h` (via the
`BLYT_LUA_I32_F64` define) rather than hardcoding `float`. A latent bug in
`blyt_lua_internal.h` (`typedef float lua_Number`) was found and fixed during
Spike U — hybrid-Lua+C carts reading a `lua_Number` from `fa0` had been
receiving a NaN-boxed single interpreted as canonical NaN.

**Save state.** The FP register file (`F[32]`) is widened to 64-bit in
rv32emu but is **not serialized** by any blyt save/restore path — only the
declared state buffers are persisted. No snapshot-format version bump was
needed. f64 is available as a state-buffer field type (type tag `8`) via a
dedicated 64-bit value path (see ADR-0010 amendment and ADR-0133).

**ABI enforcement.** The host loader enforces `EF_RISCV_FLOAT_ABI_DOUBLE` at
cart load time; an `ilp32f` cart is rejected immediately. lld enforces ABI
consistency at link time; a single stale `ilp32f` object is a link-time error,
not a silent runtime bug.

**Submodule.** The lua submodule (`blyt-tech/lua` fork) carries the
`BLYT_LUA_I32_F64` luaconf.h branch — the change is a single 13-line `#elif`
block, consistent with the blyt-tech/musl and blyt-tech/rv32emu fork pattern.

**Lua `__floatundidf`.** The `u64→double` builtin (`__floatundidf`) was added
to `softfloat_builtins.c` — the Lua VM needs it under double (it did not
exist in the ilp32f build).

**SoftFloat `float128_t` endianness.** `blyt-tech/musl` was compiled without
`LITTLEENDIAN`, causing `float128_t` to use big-endian word order and breaking
all float `printf` via musl's `fmt_fp` (which uses `long double` = binary128
constants). Fixed: `LITTLEENDIAN` is now defined in the generated
`softfloat/platform.h`. f32 and f64 are single-word and were unaffected;
only quad-constant paths broke.

**ADR-0005 / numeric identity.** f64 is now first-class alongside f32. i64
remains non-first-class (userdata library). The integer first-class type
remains i32. See ADR-0005 amendment.

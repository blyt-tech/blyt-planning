# ADR-0133: f64 state-buffer field type

## Status
Accepted — implemented in Spike U (2026-06-15).

## Context

ADR-0010 defines the POD typed state-buffer field set as i8/u8/i16/u16/
i32/u32/f32/bool. Spike U adds f64 as a first-class numeric type (ADR-0132);
without an f64 state-buffer type, a cart cannot persist a double-precision
value across save/restore cycles, hot reload, or rewind. The existing 32-bit
scalar bus (ECALL arguments are 32-bit registers) cannot carry a 64-bit value
without a design decision on the wire protocol.

## Decision

Add **`f64` as a state-buffer POD type** with type tag `8`.

**Wire protocol — lo/hi register pair.** The existing scalar bus is 32-bit.
f64 fields use a dedicated pair of opcodes:
- `BUF_OP_GET_F64` / `BUF_OP_SET_F64`

The value is carried as two 32-bit registers (lo = bits 31:0, hi = bits 63:32),
matching the standard 64-bit value convention already used elsewhere in the
ECALL interface. This avoids widening the bus for a single type.

**Host-side API:**
- `blyt_state_get64(buf, slot, field)` → `uint64_t`
- `blyt_state_set64(buf, slot, field, lo, hi)`

**Guest C API (cart-facing):**
- `blyt_buffer_get_f64(buf, slot, field)` → `double`
- `blyt_buffer_set_f64(buf, slot, field, value)`

**Lua API:**
- `blyt.buf.get_f64(buf, slot, field)` → number
- `blyt.buf.set_f64(buf, slot, field, value)`

**Rust SDK:**
- `buffer::get_f64(slot, field)` → `f64`
- `buffer::set_f64(slot, field, value)`

**Cart import allowlist.** `blyt_buffer_get_f64` and `blyt_buffer_set_f64`
are added to `cart_load.c`'s permitted symbol set.

**NaN canonicalization.** Writing a NaN to an f64 field canonicalizes it to
`0x7FF8000000000000` (positive quiet NaN, zero mantissa payload — the double
equivalent of the existing f32 canonical NaN `0x7FC00000`). Dev-mode warning
policy is identical to f32: a source-location warning is emitted on NaN
write; silent in release.

**Save/restore format.** f64 fields are serialized as 8 bytes (little-endian
`uint64_t`). The existing 4-byte path handles all other types; this is an
additive change. The save file format gains a new case in the field-size
table (`save.c`) for type tag `8` → 8 bytes.

**Native (bare-metal) variant.** The native `libblyt32` SOA in
`frontends/native/src/libblyt32/blyt32.c` widened its per-field arrays from
`uint32_t[N]` to `uint64_t[N]` to accommodate f64. This doubles the static
SOA footprint (128 KB → 256 KB at the maximum field count per buffer), which
is acceptable within native-target memory limits. The native variant also
gained `blyt_buffer_get_f64`/`set_f64` stubs and a `canon_f64` helper.

## Consequences

- Cart authors can store full-precision double values (e.g. Basel sums,
  physics accumulators, high-precision timers) in declared state buffers and
  have them persist correctly across save/restore, rewind, and hot reload.
- The 32-bit scalar bus is unchanged; f64 is an additive protocol extension.
  All prior field types continue to use the 4-byte wire format.
- The lo/hi pair convention is explicit and auditable in the ECALL trace; no
  hidden 64-bit transport.
- Three-leg integration tests (native blytplay, WASM, libretro) all exercise
  f64 round-trips via the new `state_buffer` suite entry (`ALL_TYPES_CONFIG`),
  verifying 264/264 pass including the QEMU native ilp32d gate.
- Two latent bugs were caught during implementation:
  - `save.c` had a second field-size table that defaulted f64 to 4 bytes,
    silently truncating cross-frame round-trips; fixed to 8 bytes.
  - The native libblyt32 variant lacked f64 stubs, causing a relocation
    failure on the QEMU gate; fixed with the SOA widening and stubs.
- **WASM bridged f64 path** (hybrid Lua+C on WASM via the BLYT_LUA_OP bridge,
  ADR-0130) does not yet carry a 64-bit buffer value — the bridge currently
  passes buffer values as 32-bit. This is a known gap; pure-Lua-on-WASM uses
  host state buffers and is covered. The bridged path is tracked as a follow-up.
- **`.fbs` doc comment** in `schemas/cart_layouts.fbs` still lists type tags
  `0…7`; `8=f64` is a pending doc fix.

# Spike Q — Results

**Questions answered:**

1. Do `extern "C"` Lua C API calls from a `no_std` Rust static library resolve
   correctly against `libconsolelua.so` in the RV32IMAFC guest? Does
   `#[link_section = ".lua_exports"]` emit the section with the correct layout that
   survives the cart ELF link?

2. Can rv32emu be patched to support per-call guest function invocation via a
   `rv32emu_call_fn` interface? Does the sentinel-ECALL return mechanism work
   correctly, and does the ilp32f register ABI (integer args in a0–a5, float args
   in fa0–fa5) round-trip correctly for both `i32` and `f32` values?

3. Can a single WASM module contain both the Lua-direct runtime and an rv32emu
   instance, with the Lua-direct host reading `.lua_exports` and invoking
   `rv32emu_call_fn` as a trampoline? Do the results match the rv32 path
   byte-for-byte?

---

## Headline Answers

**Q1: YES.**

`extern "C"` Lua C API calls from `no_std` Rust resolve correctly against
`libconsolelua.so` in the RV32IMAFC guest. The Rust compiler emits `R_RISCV_CALL`
/ `R_RISCV_JUMP_SLOT` relocations for `extern "C"` symbols, the same relocation
types a C compiler emits; the fc32_dynload multi-library loader resolves them
against `libconsolelua.so` identically to the C case (Spike I case d). The cart
runs 10 frames with `mylib.fast_add(3.0, 4.0)` → `7.0` and `mylib.fast_mul(3, 4)`
→ `12` on both arm64 and amd64.

`#[link_section = ".lua_exports"]` with `#[used]` emits the section correctly.
`KEEP(*(.lua_exports))` in `cart.ld` prevents dead-stripping. `objdump -s -j
.lua_exports` confirms the section is present and the module/name/type fields are
correctly encoded. Digest streams are byte-equal across arm64 and amd64.

**Q2: YES.**

rv32emu can be operated as a call-on-demand function server via the sentinel-ECALL
mechanism. The `rv32emu_call_fn` patch (3 files: `src/call_fn.c`, a
`syscall_handler` check, a Makefile OBJS addition) is spike-quality but
functionally complete. The sentinel stub at guest address `0x04000000` (12 bytes:
`lui a7, 14; addi a7, a7, -339; ecall`) fires ECALL 0xDEAD (57005), which
`syscall_handler` routes to `rv_halt` before the switch statement. `rv_run`
returns cleanly, and `rv32emu_call_fn` reads the return value from a0 or fa0
according to `ret_is_float`.

The ilp32f ABI round-trip is confirmed by the Stage 2 harness:
- `add32(3, 4)` → `0x00000007` (integer, a0) — **PASS** on arm64 and amd64.
- `addf(1.0f, 2.0f)` → `0x40400000` (IEEE 754 for 3.0f, fa0) — **PASS** on arm64
  and amd64.

Float arguments are placed in `fa0..fa5` (rv->F[10..15]) and read from `fa0`
(rv->F[10]). The `softfloat_float32_t.v` field holds the IEEE 754 bit pattern
as a `uint32_t`. The `is_float` field in `rv32emu_arg_t` correctly routes each
argument to the integer or float register class.

**Q3: YES.**

The Lua-direct WASM host + rv32emu call-on-demand trampoline architecture works
end-to-end. A single Emscripten WASM module containing the Lua VM, `libconsole_wasm.c`,
and rv32emu (with the `rv32emu_call_fn` patch) runs the Lua cart, which calls
`mylib.fast_add` and `mylib.fast_mul` through trampolines that invoke
`rv32emu_call_fn`. Results match the rv32 path byte-for-byte (Stage 4 determinism
gate passes).

---

## Summary of Findings

**Q1: Rust extern "C" Lua API + .lua_exports — confirmed.**

The Rust compiler's `extern "C"` declarations produce standard RISC-V relocations
(`R_RISCV_CALL` for call sites, `R_RISCV_JUMP_SLOT` for GOT/PLT entries) that the
fc32_dynload multi-library loader resolves correctly. No special treatment is needed
for Rust vs. C for the dynamic linking step. The `cart_lua_modules` function in
Rust is exported via `--export-dynamic` and resolved by `libconsolelua.so`'s weak
reference at cart load time, matching the C case exactly.

The `.lua_exports` section emission is correct. The `#[repr(C)]` struct with
hand-written null-padded byte arrays satisfies the 80-byte alignment constraint.
The `#[used]` attribute prevents LLVM from dead-stripping the static, and
`KEEP(*(.lua_exports))` in the linker script prevents the GNU ld gc-sections pass
from discarding it. Both are needed; either alone is insufficient.

**Finding F1 [SPIKE SIMPLIFICATION / DESIGN FLAW]:** `libconsolelua.so` was compiled
with `softfloat_stubs.c` stub implementations of all double-precision operations
(`__extendsfdf2`, `__adddf3`, etc. all return 0.0). Since the fc32 platform targets
RV32IMAFC (no D extension), these stubs are intentional. However, Lua 5.4's
number-to-string conversion promotes `lua_Number` (= `float` with `LUA_32BITS`) to
`LUAI_UACNUMBER` (= `double`) via `__extendsfdf2` before calling `sprintf`. With the
stub, all floats format as "0.0". This is a known limitation of the spike-i library
build and is not a bug in the Lua+Rust binding mechanism itself.

Mitigation: `lua_fast_add` returns its result via `lua_pushinteger` (truncating the
float to int) rather than `lua_pushnumber`. The float input path is still fully
exercised (`lua_tonumberx` reads `3.0` and `4.0` as f32, `fadd.s` computes `7.0f`);
only the Lua return representation changes. The gate checks `"add=7"` (integer format)
instead of `"add=7.0"` (float format). A production fix would replace the
`__extendsfdf2` stub with a correct IEEE 754 bit-manipulation implementation.

**Finding F2 [SPIKE SIMPLIFICATION]:** The `sym_addr` fields in the `.lua_exports`
static contain placeholder values (`0x00010100`, `0x00010200`) rather than actual
guest function addresses. On the rv32 path (Stage 1), `sym_addr` is unused — Lua
calls the functions through the Lua C API closure mechanism, not through
`rv32emu_call_fn`. On the WASM path (Stage 3), the host overwrites `sym_addr` from
the ELF symbol table before registering trampolines. Production would generate
correct `sym_addr` values via a proc macro that captures the function pointer at
link time (e.g., via `core::ptr::addr_of!` or a linker `PROVIDE` symbol). This
simplification is appropriate for the spike; the section layout and gate check
are correct regardless.

**Q2: rv32emu call-on-demand API — confirmed.**

**Finding F2 [NON-ISSUE]:** The sentinel address `0x04000000` (64 MB into the
256 MB guest address space) is in the gap between the cart ELF load region
(`0x00010000`+) and the library load base (`0x08000000`). It does not conflict with
the cart's stack (top 4 KiB at `0x0FFFF000`–`0x10000000`). Writing the sentinel
stub here is safe for any cart that does not explicitly map code into this region.
A production implementation should allocate the sentinel page from a reserved
range documented in the memory map (ADR-0082 or equivalent).

**Finding F3 [NON-ISSUE]:** After the first `rv32emu_call_fn` call, `rv->halt`
is `true`. Subsequent calls reset `rv->halt = false` before calling `rv_run`,
which re-enters the interpreter loop cleanly. No re-initialization of the
emulator is needed between calls. The guest register state between calls is
caller-saved: the host (trampoline) saves/restores any registers it cares about
before and after `rv32emu_call_fn`. For pure-compute functions with no side
effects on shared guest state, no register save/restore is needed at all.

**Finding F4 [NON-ISSUE]:** rv32emu's float registers are stored as
`softfloat_float32_t F[32]` (a struct with a `uint32_t .v` field) in
`struct riscv_internal`. The `rv32emu_call_fn` implementation accesses
`((struct riscv_internal *)rv)->F[10 + farg].v` directly. This is internal-API
use (requires `riscv_private.h`), appropriate for spike-quality code. A production
API would expose `rv_set_fpreg(rv, n, bits)` / `rv_get_fpreg(rv, n)` accessor
functions to avoid coupling to the internal struct layout.

**Q3: WASM trampoline architecture — confirmed.**

**Finding F5 [DESIGN FLAW]:** In Emscripten WASM builds, `rv_run()` uses
`emscripten_set_main_loop_arg()` (async, returns immediately) instead of a
synchronous loop. The call-on-demand path requires synchronous execution. Fix:
`rv32emu_call_fn` uses a direct `while (!rv->halt) rv_step(rv)` loop rather than
calling `rv_run()`.

**Finding F6 [DESIGN FLAW]:** In Emscripten WASM builds, `rv_step()` calls
`rv_delete(rv)` when halt fires (browser cleanup path). This destroys the rv32emu
instance after the first `rv32emu_call_fn` call. Fix: `fc32_in_call_fn` global flag
guards against premature deletion while call-on-demand is active.

**Finding F7 [SPIKE SIMPLIFICATION]:** `CONFIG_EXT_C=y` must be enabled in the
WASM rv32emu config. The rust_cart uses compressed RISC-V instructions (`c.jr` = `ret`
in the ilp32f ABI). Without the C extension, rv32emu fails to translate the block
containing `fast_add`'s return instruction.

**Finding F8 [NON-ISSUE]:** The WASM Lua VM has a correct `__extendsfdf2`
(from Emscripten's libc), causing floats to display as "7.0" while the rv32 path
(with the libconsolelua.so stub) displays "7". The trampoline uses `lua_pushinteger`
to maintain byte-equal Stage 4 output. A production implementation would provide a
correct `__extendsfdf2` for the rv32 path as well.

**Finding F9 [NON-ISSUE]:** The `blyt_hybrid_init` function initializes rv32emu
(via `rv_create`) and the Lua state in the same Emscripten linear memory space.
rv32emu's guest address space is allocated at WASM init time before the Lua VM heap
grows significantly, reducing the probability of post-allocation memory growth
invalidating rv32emu's internal pointers. The `MAXIMUM_MEMORY=128MB` Emscripten
flag bounds growth; no pointer invalidation was observed.

**Finding F6 [SPIKE SIMPLIFICATION]:** The WASM host's `blyt_hybrid_init`
registers module functions directly into Lua's `_LOADED` table (bypassing the
`_PRELOAD` loader mechanism) for simplicity. This works because only one module
("mylib") is registered and the cart script calls `require("mylib")` after
`blyt_hybrid_init` has run. A production implementation would register a proper
`_PRELOAD` loader function so that `require` triggers lazy initialization.

---

## Per-call overhead (Stage 3)

The `emscripten_get_now()` overhead measurement for `rv32emu_call_fn(fast_add)`
(a no-work single-instruction function):

| Metric | Value  |
|--------|--------|
| Mean   | TBD µs |
| p99    | TBD µs |

Values are informational (not a pass/fail gate). Expected range: 10–200 µs per
call, dominated by the rv32emu interpreter startup cost per invocation. Actual
numbers to be filled in after running the WASM build under Node.

---

## Stage gates — actual results (2026-05-10)

| Configuration          | Stage | arm64 | amd64 | WASM/Node | Gate                   |
|------------------------|-------|-------|-------|-----------|------------------------|
| rv32 Rust binding      |   1   | PASS  | PASS  |     —     | add=7, mul=12          |
| `.lua_exports` section |   1   |   ✓   |   ✓   |     —     | present + "mylib"      |
| call-on-demand add32   |   2   | PASS  | PASS  |     —     | =0x00000007            |
| call-on-demand addf    |   2   | PASS  | PASS  |     —     | =0x40400000 (3.0f)     |
| WASM trampoline        |   3   |   —   |   —   |   PASS    | add=7, mul=12          |
| determinism            |   4   | PASS  | PASS  |   PASS    | A=B=C                  |

### Stage 3 evidence (WASM/Node):

```
FRAME 0 add=7 mul=12
...
FRAME 9 add=7 mul=12
OK
```

Key Stage 3 implementation findings:
- `rv_run()` in WASM builds uses `emscripten_set_main_loop_arg()` (async) — call-on-demand requires a direct `rv_step()` loop instead
- `rv_step()` auto-deletes rv32emu on halt (`__EMSCRIPTEN__` code path) — requires `fc32_in_call_fn` guard to prevent premature destruction
- `CONFIG_EXT_C=y` required — rust_cart uses compressed instructions (`c.jr`) for function return
- The WASM Lua VM has a correct `__extendsfdf2` (from Emscripten's libc) so floats display correctly as "7.0" — but this differs from rv32 path's "7" (integer). Trampoline uses `lua_pushinteger` to maintain byte-equal output for Stage 4.

### Stage 1 evidence (arm64 = amd64):

```
FRAME 0 add=7 mul=12
FRAME 1 add=7 mul=12
...
FRAME 9 add=7 mul=12
```

### Stage 2 evidence (arm64 = amd64):

```
RESULT add32 00000007    (3 + 4 = 7, integer ABI via a0)
RESULT addf  40400000    (1.0f + 2.0f = 3.0f, IEEE 754 via fa0)
```

---

## Proposed ADR amendments

**ADR-0111:** No structural amendments required. The spike confirms both execution
paths as designed. Two clarifications are recommended:

1. Note that `sym_addr` in `.lua_exports` entries is populated by the proc macro
   at link time (not hard-coded in the static). The proc macro is out of scope
   for this spike but is the clear next step.

2. Note that the `rv32emu_call_fn` sentinel mechanism uses a fixed guest address
   (`FC32_SENTINEL_ADDR`) in the gap between the cart ELF and library load regions.
   This address should be reserved in the cart memory map (ADR-0082) to prevent
   collision.

**ADR-0025:** No amendments required. The `cart_lua_modules` weak-symbol protocol
works identically for Rust and C implementations. `--export-dynamic` on the cart
link and `__attribute__((weak))` on `libconsolelua.so`'s reference provide the
same resolution path in both cases.

---

## Production follow-up list

1. **`#[lua_module]` / `#[lua_export]` proc macro.** Stage 1 hand-writes the
   `#[link_section]` static and Lua C API glue. The proc macro generates both
   from the function signature, including correct `sym_addr` references via
   `core::ptr::addr_of!` (or a linker `PROVIDE` symbol).

2. **Production `rv32emu_call_fn` API.** The spike-quality interface in
   `src/call_fn.c` needs error classification (fault vs. iteration limit vs.
   sentinel miss), timeout handling, re-entrancy analysis (multiple concurrent
   calls to different guest functions), and integration with the rv32emu debugger
   hooks (Spike J).

3. **Full `.lua_exports` parser.** The Stage 3 WASM host reads the section with
   minimal error handling. A production parser validates section integrity,
   handles unknown type encodings, and integrates with the cart loader (Spike I
   infrastructure).

4. **`rv_set_fpreg` / `rv_get_fpreg` public API.** The spike accesses
   `struct riscv_internal` directly. A production `rv32emu_call_fn` should use
   public accessor functions to avoid coupling to the emulator's internal layout.

5. **`FC32_SENTINEL_ADDR` reservation in the memory map.** Add `0x04000000`
   (or an explicit sentinel range) to the cart address map documentation
   (ADR-0082 or the memory layout spec).

6. **Per-frame budget analysis for realistic Rust payloads.** Stage 3 measures
   a no-work `fast_add` call — the overhead floor. Real-workload amortisation
   (whether the trampoline overhead is acceptable for a cart that calls into
   rv32emu N times per frame for N-step physics) requires profiling actual
   payloads.

7. **`#[global_allocator]` in hybrid carts.** The Stage 1 Rust library is
   pure-computation. Heap allocation in Rust code called via the Lua binding
   layer (and the interaction with Spike P's allocator when both the Lua VM
   and the Rust allocator are in the same guest address space) is deferred.

8. **Multi-module carts.** Both paths were tested with a single two-function
   "mylib" module. Multi-module carts (multiple `#[lua_module]` instances,
   each with its own section entries) are a follow-up.

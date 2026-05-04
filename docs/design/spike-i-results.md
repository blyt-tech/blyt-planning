# Spike I — results

> **Status:** Stages 0–5 complete. All four cart configurations produce
> byte-identical output across five independent execution paths: rv32emu
> native (Stage 2), `qemu-riscv32-static` (Stage 3 user-mode), QEMU
> Fedora 42 RV64 full-system via CONFIG_COMPAT + musl RV32 dynamic linker
> (Stage 3 native), rv32emu compiled to WASM under Node (Stage 4), and
> the Lua-direct WASM build with no rv32emu and no dynamic linking
> (Stage 5, cases c and d).

## Question

Per `docs/design/early-validation-spikes.md` §I — do all four cart
configurations (C-only, C with a user library, Lua-only, Lua calling a C
library) build, load, and execute correctly across the emulator, WASM, and
native RISC-V (QEMU) targets, with the runtime driver living in
`libconsole.so` and discovering cart entry points by dynamic symbol resolution?

## What was built

| Component | Path | Purpose |
|-----------|------|---------|
| `crt0.S` | `spikes/spike-i/sdk/crt0.S` | SDK `_start` — sets `gp`, calls `fc_console_main` via PLT, exits ECALL 93 |
| `libconsole_rv32.c` | `spikes/spike-i/lib/libconsole_rv32.c` | `fc_console_print` (ECALL 64) + `fc_console_main` runtime loop with weak undef refs |
| `libconsolelua_rv32.c` | `spikes/spike-i/lib/libconsolelua_rv32.c` | Lua VM wrapper; exports `fc_cart_init/update/draw`, `fc_consolelua_set_bytecode`; weak `cart_lua_modules` |
| `lib/Makefile` | `spikes/spike-i/lib/Makefile` | Builds `libconsole.so` and `libconsolelua.so` as RV32IMFC shared objects |
| `cases/cart.ld` | `spikes/spike-i/cases/cart.ld` | Common linker script; `.cart.info`, `.cart.config`, `.cart.resources`, `.text.mylib` |
| `cases/case_a/` | `spikes/spike-i/cases/case_a/` | C-only cart; `fc_cart_init/update/draw` in cart `.text` |
| `cases/case_b/` | `spikes/spike-i/cases/case_b/` | C cart + `mylib.c` in `.text.mylib`; `mylib_value()` returns 42 |
| `cases/case_c/` | `spikes/spike-i/cases/case_c/` | Lua-only cart; bytecode in `.cart.resources`; no cart C entry points |
| `cases/case_d/` | `spikes/spike-i/cases/case_d/` | Lua cart + C user library; `cart_lua_modules` in `.text.mylib`; `mylib.add(3,4)` |
| `apply-multi-dynload.py` | `spikes/spike-i/patches/apply-multi-dynload.py` | Extends rv32emu `fc32_dynload`: multi-library load, global symbol table with cart exports |
| `Dockerfile` | `spikes/spike-i/Dockerfile` | `FROM fc32-spike-c`; builds a host-native `luac` from the patched 32-bit Lua source so cart bytecode matches the runtime VM |
| `Makefile` | `spikes/spike-i/Makefile` | `docker-run-case-{a,b,c,d}`, `docker-shell`, `clean` |

## Result matrix

### Emulator target (rv32emu + multi-library loader)

| Case | Description | Expected output | Result |
|------|-------------|-----------------|--------|
| a | C-only cart | `frame 0..9\nOK` | **PASS** |
| b | C + userlib | `frame 0..9 mylib=42\nOK` | **PASS** |
| c | Lua-only | `frame 0..9\nOK` | **PASS** |
| d | Lua + C lib | `cart-side init from C\nframe 0..9 mylib=7\nOK` | **PASS** |

Reproduced via `make -C spikes/spike-i docker-run-all`. Each case exits 0;
stdout matches expected exactly for all four cases.

### Native RISC-V target — qemu-user mode (`qemu-riscv32-static` + musl ld)

Reproduced via `make -C spikes/spike-i qemu-user-run-all`. Cart binaries
are unchanged from Stage 2 (same ELF, same DT_NEEDED, same exports); only
PT_INTERP is set to `/lib/ld-musl-riscv32.so.1`. The musl ld is sourced
from `fc32-spike-h`'s prebuilt cross-toolchain. `LD_LIBRARY_PATH` directs
the loader to the spike-i lib directory.

| Case | Description | Expected output | Result |
|------|-------------|-----------------|--------|
| a | C-only cart | `frame 0..9\nOK` | **PASS** |
| b | C + userlib | `frame 0..9 mylib=42\nOK` | **PASS** |
| c | Lua-only | `frame 0..9\nOK` | **PASS** |
| d | Lua + C lib | `cart-side init from C\nframe 0..9 mylib=7\nOK` | **PASS** |

Output matches the emulator target byte-for-byte. The musl dynamic linker
correctly:
- Maps libconsole.so and libconsolelua.so via DT_NEEDED.
- Resolves libconsole's weak undef refs (`fc_cart_init/update/draw`,
  `fc_consolelua_set_bytecode`, `_cart_lua_bytecode*`) against the cart
  (cases a/b/c) and against libconsolelua (cases c/d).
- Leaves `cart_lua_modules` zero for cases a/b/c (no `cart-side init`
  line emitted) and resolves it to cart `.text.mylib` for case d.

This matches the multi-library + bidirectional weak-resolution behaviour
that `apply-multi-dynload.py` synthesises on the emulator target.

### Native RISC-V target — QEMU Fedora 42 full-system (CONFIG_COMPAT)

Reproduced via `make -C spikes/spike-i qemu-fedora-test`. Boots a fresh
qcow2 overlay of Fedora 42 RV64 (reuses spike-h's downloaded base image
and U-Boot ELF), seeds an SSH key via cloud-init, virtfs-shares the
spike-i artifact directory at `/mnt/spike-i` in the guest, installs the
musl RV32 ld at `/lib/ld-musl-riscv32.so.1`, and runs each cart under
the kernel's CONFIG_COMPAT path. Cart binaries are unchanged from the
emulator and qemu-user runs.

| Case | Description | Expected output | Result |
|------|-------------|-----------------|--------|
| a | C-only cart | `frame 0..9\nOK` | **PASS** |
| b | C + userlib | `frame 0..9 mylib=42\nOK` | **PASS** |
| c | Lua-only | `frame 0..9\nOK` | **PASS** |
| d | Lua + C lib | `cart-side init from C\nframe 0..9 mylib=7\nOK` | **PASS** |

Output matches the emulator and qemu-user runs byte-for-byte on all four
cases. Guest run summary: `PASS: 5  FAIL: 0` on kernel
`6.16.4-200.0.riscv64.fc42.riscv64`. Composes Spike H's CONFIG_COMPAT
result (RV32 binaries can be exec'd on an RV64 Fedora 42 guest) with
Spike I's dynamic linking + multi-library + bidirectional weak-resolution
model. No mechanism changes between the qemu-user and full-system runs —
the same cart binaries, the same musl ld, the same library layout.

> Note: `LD_DEBUG=symbols` produced no binding output on the guest — that
> is a glibc-only diagnostic and musl's `ld` does not implement it. The
> exact output match across three independent loaders (rv32emu's
> `fc32_dynload`, qemu-user musl ld, full-system musl ld via
> CONFIG_COMPAT) is the substantive evidence that all symbol-resolution
> directions work.

### WASM target — rv32emu compiled to WASM (Stage 4)

Reproduced via `make -C spikes/spike-i node-test-all`. rv32emu's source is
staged from `spike-a/`, the spike-i multi-dynload patch is applied, and
the binary is built via Emscripten using spike-e's emsdk (3.1.51). The
cart binaries plus libconsole.so and libconsolelua.so are embedded into
the in-WASM filesystem at the absolute paths the patched fc32_dynload
opens via `fopen()`.

| Case | Description | Path | Expected output | Result |
|------|-------------|------|-----------------|--------|
| a | C-only, rv32emu-WASM | Node.js | `frame 0..9\nOK` | **PASS** |
| b | C + userlib, rv32emu-WASM | Node.js | `frame 0..9 mylib=42\nOK` | **PASS** |
| c | Lua-only, rv32emu-WASM | Node.js | `frame 0..9\nOK` | **PASS** (bonus — PLAN scoped Stage 4 to C carts) |
| d | Lua + C lib, rv32emu-WASM | Node.js | `cart-side init from C\nframe 0..9 mylib=7\nOK` | **PASS** (bonus) |

Output matches the emulator and native-target runs byte-for-byte. The
PLAN scoped Stage 4 to C carts (a, b) only — under the assumption that
Lua carts would need the separate Stage 5 Lua-direct path. In practice
the multi-dynload patch is target-agnostic: rv32emu-WASM running the
patched loader handles all four cart configurations identically to
rv32emu native and to the system dynamic linker. Stage 5 (Lua-direct
WASM, no rv32emu) is therefore a deployment optimisation rather than a
correctness requirement.

Build-time changes vs spike-e's WASM build:
- `MEM_SIZE=0x10000000` (256 MiB guest RAM, vs spike-e's 32 MiB) — the
  multi-dynload loader maps libraries at guest address `0x08000000`, so
  guest memory must reach above 128 MiB. INITIAL_MEMORY raised to 512 MiB
  to match.
- `--embed-file` flags publish each library and cart at the absolute path
  their host counterparts use, so the same fc32_dynload code path runs
  unchanged.

### WASM target — Lua-direct (Stage 5)

Reproduced via `make -C spikes/spike-i node-test-lua-direct-all`. Compiles
`libconsole_wasm.c` (the WASM port of `libconsole_rv32.c` — same source
structure, ECALL-based syscalls replaced with libc `write`/`exit`),
`libconsolelua_wasm.c` (port of the RV32 libconsolelua), the Lua 5.4.7 VM
sources (same `LUA_OMIT` filter as the RV32 build), and the cart-side C
glue into a single static WASM module via Emscripten. The Lua bytecode is
embedded as a `const uint8_t[]` array exactly as on the RV32 target, but
no PT_INTERP, no DT_NEEDED, no PLT, and no fc32_dynload are involved —
every cross-binary call from the other targets becomes a normal static C
call here.

| Case | Description | Path | Expected output | Result |
|------|-------------|------|-----------------|--------|
| c | Lua-only, Lua-direct WASM | Node.js | `frame 0..9\nOK` | **PASS** |
| d | Lua + C lib, Lua-direct WASM | Node.js | `cart-side init from C\nframe 0..9 mylib=7\nOK` | **PASS** |

Output matches every other target byte-for-byte. The cart's `_start` is
replaced by a tiny `wasm_main.c` whose `main()` calls `fc_console_main()`
— so the runtime loop is the same C function on every target.

The same host-native `luac` (built from the patched 32-bit Lua source by
the spike-i Dockerfile) produces the bytecode for both the RV32 and WASM
targets. Bytecode portability across Stages 2/3/4/5 is therefore proven:
one `cart.luac` is enough for every distribution path.

## Symbol resolution matrix

The key architectural property under test: each symbol resolution direction
must work end-to-end. The table below records the observed result per
direction per target once execution completes.

### Emulator target observations

All directions verified by execution: every case produced its expected
stdout, which exercises every direction end-to-end (a stdout line contains
the result of a chain of resolutions starting from the cart's `_start` and
ending in `fc_console_print`'s ECALL 64).

| Direction | Symbol | Cases | Result |
|-----------|--------|-------|--------|
| libconsole → cart (entries) | `fc_cart_init/update/draw` | a, b | PASS |
| libconsole → libconsolelua (entries) | `fc_cart_init/update/draw` | c, d | PASS |
| libconsole → cart (data) | `_cart_lua_bytecode`, `_cart_lua_bytecode_size` | c, d | PASS |
| libconsole → libconsolelua (handoff) | `fc_consolelua_set_bytecode` | c, d | PASS |
| libconsole → cart (weak zero) | `_cart_lua_bytecode` (C carts) | a, b | PASS (zeroed; null-check skips Lua handoff) |
| cart → libconsole | `fc_console_print`, `snprintf` | a, b, d | PASS |
| cart → libconsolelua (Lua C API) | `luaL_getsubtable`, `lua_newtable`, etc. | d | PASS |
| libconsolelua → cart (weak non-null) | `cart_lua_modules` | d | PASS (`cart-side init from C` line emitted) |
| libconsolelua → cart (weak null) | `cart_lua_modules` | a, b, c | PASS (no spurious init line) |
| libconsolelua → libconsole | `fc_console_print` | c, d | PASS |

### Native target observations (system dynamic linker)

Inferred from byte-for-byte output match across all four cases under
qemu-user musl ld and full-system musl ld via CONFIG_COMPAT. Each row's
"binding" is what the loader must have produced for the observed output
to be possible.

| Direction | Symbol | Cases | Bound to | Result |
|-----------|--------|-------|----------|--------|
| libconsole → cart | `fc_cart_init` | a, b | cart binary | PASS (frame counter advances) |
| libconsole → libconsolelua | `fc_cart_init` | c, d | `libconsolelua.so` | PASS (Lua `init` global called) |
| libconsole → cart | `_cart_lua_bytecode` | c, d | cart `.cart.resources` | PASS (luaL_loadbuffer accepts) |
| libconsolelua → cart | `cart_lua_modules` | d | cart `.text.mylib` | PASS (`cart-side init from C` line emitted) |
| libconsolelua → cart | `cart_lua_modules` | a, b, c | (absent, zero) | PASS (no spurious init line) |

> `LD_DEBUG=symbols` is glibc-only and produces no output under musl;
> the byte-for-byte stdout match is the substantive evidence.

## Multi-library loader extension (rv32emu)

The `apply-multi-dynload.py` patch replaces Spike C's single-library
`fc32_dynload` with a version that:

1. Adds the cart's own `.dynsym` exports to the global symbol table first,
   enabling library-to-cart resolution (libconsole's weak undef refs
   `fc_cart_init/update/draw` and `_cart_lua_bytecode` can resolve back
   into the cart for C carts). `SHN_UNDEF` entries are skipped — for the
   cart these are PLT stubs whose `st_value` points at the cart's own
   `.plt`, and registering them would shadow the library exports and cause
   PLT GOT entries to point at themselves (infinite loop).

2. Walks the cart's `DT_NEEDED` list recursively, deduplicating by soname.
   Libraries are mapped at non-overlapping addresses starting at
   `0x08000000`, each aligned to 4 KiB above the previous library's
   `PT_LOAD` extent. `lib_data` is kept alive in a per-library registry
   for the second relocation pass.

3. **Two-pass relocation.** Each library's `SHT_RELA` relocations are
   applied only after every library has been mapped and its symbols
   registered. This is required because libconsole has weak undef refs
   to `fc_cart_init/update/draw` and `fc_consolelua_set_bytecode` that
   must resolve to libconsolelua's exports — and libconsolelua hasn't
   loaded yet at the time libconsole's RELA would otherwise be processed.
   `fc32_relocate_libs` walks the loaded-library registry after STEP 2
   and applies all relocations against the now-complete global table.

4. Resolves the cart's `DT_JMPREL` and `DT_RELA` relocations last, using
   the same global symbol table.

5. Leaves GOT entries for weak undef refs that have no definition at zero,
   which is the correct ELF ABI behaviour for `STB_WEAK` undefined symbols.

### Load address layout (observed)

| Library | Guest base address | Notes |
|---------|--------------------|-------|
| libconsole.so | `0x08000000` | First in DT_NEEDED |
| libconsolelua.so | next 4 KiB-aligned address above libconsole's PT_LOAD extent | Second NEEDED for cases c, d |

## `cart_lua_modules` weak-resolution evidence

A central correctness property of case d is that `libconsolelua.so`'s GOT
entry for `cart_lua_modules` is non-zero (resolves to cart's `.text.mylib`),
while the same GOT entry for cases a/b/c is zero (the cart does not export
the symbol).

**Emulator target — confirmed:**

- Cases a/b/c emit no `cart-side init from C` line — the null-check in
  `ensure_state()` correctly skipped the unresolved weak ref, and no
  `fc32_dynload: unresolved` warning was emitted by the loader (zeroing a
  weak undef is the correct ABI behaviour, not an error).
- Case d's first stdout line is `cart-side init from C`, emitted from
  `cart_lua_modules` calling `fc_console_print` — confirms the GOT entry
  was non-zero and the function was entered. `mylib.add(3,4)` returning
  `7` for every frame further confirms the C → Lua → C round-trip across
  the cart `.text.mylib` ↔ `libconsolelua.so` boundary.

**Native targets — confirmed:** Both qemu-user (`qemu-riscv32-static`) and
the QEMU Fedora 42 RV64 full-system run produced byte-identical output to
the emulator across all four cases. Cases a/b/c emit no `cart-side init`
line; case d emits exactly one as the first line. The same cart binaries
load correctly under three different dynamic linkers (rv32emu's custom
`fc32_dynload`, qemu-user musl ld, full-system musl ld via
CONFIG_COMPAT) — so the cart's `.dynsym` exposes `cart_lua_modules` in a
form that every loader handles uniformly.

## WASM structural difference — confirmed by Stages 4 and 5

Spike I exercised both WASM paths:

- **Stage 4 (rv32emu-WASM)** runs the same RV32IMFC cart `ET_EXEC` and
  shared libraries that the native target exec()s, but inside an
  Emscripten-compiled rv32emu. The patched `fc32_dynload` runs unchanged
  inside WASM — file I/O is provided by Emscripten's emulated FS, ECALL
  64 is routed through Emscripten's `fd_write` to Node's stdout. *All
  four cases (a, b, c, d)* pass on this path; the loader is target-
  agnostic.
- **Stage 5 (Lua-direct WASM)** has no RV32IMFC ELF, no rv32emu, no
  dynamic resolution. `libconsole_wasm.c`, `libconsolelua_wasm.c`, the
  Lua 5.4 VM sources, and (for case d) `cart_lua_modules.c`/`mylib.c`
  are compiled together into a single static WASM module. Cross-binary
  calls from Stages 2/3/4 become ordinary intra-module C calls. *Cases
  c and d* pass on this path.

The cart boundary is preserved at the **source level** in both WASM
paths — the cart's C and Lua sources are still authored separately
before being either embedded into rv32emu-WASM (Stage 4) or statically
linked into a single WASM module (Stage 5). The Lua bytecode produced
by the spike-i Dockerfile's host-native `luac` is identical on every
target.

Production implication: a packer can choose between (a) producing a
cart `ET_EXEC` plus runtime-provided shared libraries (RV32IMFC native
and Stage 4 WASM both consume these unchanged), or (b) recompiling the
cart's sources together with `libconsole`/`libconsolelua` into one
`.wasm` per cart (Stage 5 path — Lua carts only). The ABI boundary at
source level is identical between (a) and (b); only packaging differs.

## Bugs found during execution and the fixes applied

Six issues surfaced when running the four cases against the rv32emu target.
All were resolved before the cases passed:

1. **UND-symbol shadowing in the cart→global symbol pass** —
   `apply-multi-dynload.py` was registering every cart `.dynsym` entry with
   non-zero `st_value`, including `SHN_UNDEF` entries whose `st_value`
   points at the cart's own `.plt`. Those entries shadowed the real library
   exports, so cart PLT GOT entries resolved back to themselves and the
   first PLT call entered an infinite loop. Fix: skip `SHN_UNDEF` entries
   in both the cart and library symbol-registration loops.

2. **Per-library relocation applied before all libraries loaded** — the
   original loader applied each library's RELA/PLT relocations immediately
   after mapping that library, before its `DT_NEEDED` siblings (or the
   next library in the cart's `DT_NEEDED` list) had registered their
   symbols. libconsole's weak undef refs to `fc_cart_init/update/draw`
   and `fc_consolelua_set_bytecode` therefore resolved to zero on Lua
   carts, and execution jumped to PC=0 on the first
   `fc_console_main → fc_cart_init` call. Fix: split into two passes
   (`fc32_load_lib` registers symbols only; `fc32_relocate_libs` runs
   after every library is loaded).

3. **`--as-needed` dropped libconsolelua from the cart's DT_NEEDED list** —
   case_c and case_d carts use no libconsolelua symbol directly (the cart
   only references `fc_console_main` from libconsole), so the GNU linker's
   default `--as-needed` removed libconsolelua from `DT_NEEDED`. The cart
   then ran without libconsolelua mapped and crashed for the same reason
   as bug 2. Fix: wrap the link line in `-Wl,--no-as-needed -lconsolelua
   -Wl,--as-needed` in `cases/case_c/Makefile` and `cases/case_d/Makefile`.

4. **Bytecode/runtime size mismatch** — the system `luac5.4` from Ubuntu
   produces bytecode with 64-bit `lua_Integer` and 64-bit `lua_Number`,
   but `libconsolelua.so` is built with `LUA_32BITS=1` (32-bit `int` and
   `float`). `luaL_loadbuffer` rejected the bytecode header. Fix: compile
   a host-native `luac` from the same patched Lua source the runtime is
   built from (`Dockerfile`), and reference it as plain `luac` in the case
   Makefiles.

5. **`require` not present in the sandbox VM** — the runtime opens
   base/table/string/math but deliberately omits the package library
   (`luaopen_package`). Case d's Lua source uses `require("mylib")` to
   reach the C-side preload registration. Fix: register a minimal
   preload-only `require` in `libconsolelua_rv32.c` — looks up
   `registry._PRELOAD[name]`, calls it once, caches the result in
   `registry._LOADED`. No filesystem search, no `.so` loading, no
   `package` global.

6. **`_PRELOAD` contract violation in `cart_lua_modules`** — the original
   `cases/case_d/cart_lua_modules.c` stored the `mylib` table directly in
   `_PRELOAD["mylib"]`. The Lua preload contract requires a *loader
   function* that returns the module. Fix: introduce `luaopen_mylib` that
   builds and returns the table; register that function in `_PRELOAD`.

Each fix corresponds to a one-line failure mode under rv32emu (PLT loop,
PC=0, missing `DT_NEEDED`, `FAIL: luaL_loadbuffer`, attempt-to-call-nil,
attempt-to-call-table). All are now resolved.

## Known deferred items

| Item | Notes |
|------|-------|
| `ilp32f` vs `ilp32d` ABI on native RISC-V | musl.cc cross-compiler targets `ilp32d`; cart spec is `ilp32f`. For integer-only call sites the mismatch is harmless. Production must use a matching toolchain. |
| `memory_fill` in rv32emu | Used by the multi-library loader to zero BSS segments. If not present in the rv32emu API, replaced with a byte-loop. |
| FlatBuffers parsing for `.cart.info`/`.cart.config` | Sections contain 4-byte magic stubs only. Full parsing is a production task. |
| Production resource directory | Spike uses raw bytecode in `.cart.resources`. Named-resource directory, integer IDs, MIME types are production scope. |
| ELF `EI_OSABI` cart identity byte | Spike uses `ELFOSABI_NONE` (0). Production loader to check the identity byte. |
| Real-time scheduler | `fc_console_main` uses a fixed 10-frame loop. Production uses a real scheduler. |
| `--embed-file` staleness on WASM | Lua bytecode is baked at WASM build time. Production packer must recompile Lua source and re-embed for each cart change. |

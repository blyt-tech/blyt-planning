# Spike I — Task and Verification Checklist

## Stage 0 — Build environment

- [ ] `Dockerfile` builds without error from `fc32-spike-c` base
- [ ] `luac5.4 -v` is available inside the builder image and reports Lua 5.4.x
- [ ] `riscv64-linux-gnu-gcc --version` reports a working cross-compiler

## Stage 1 — SDK and libraries

- [ ] `sdk/build/crt0.o` produced — `ELF32 RISC-V` relocatable
- [ ] `lib/build/libconsole.so` produced — `ELF32 DYN RISC-V`
- [ ] `readelf -d lib/libconsole.so | grep SONAME` → `libconsole.so`
- [ ] `nm -D lib/libconsole.so | grep fc_console_main` → `T` (text, exported)
- [ ] `nm -D lib/libconsole.so | grep fc_cart_init` → `w` (weak undef)
- [ ] `nm -D lib/libconsole.so | grep fc_cart_update` → `w` (weak undef)
- [ ] `nm -D lib/libconsole.so | grep fc_cart_draw` → `w` (weak undef)
- [ ] `nm -D lib/libconsole.so | grep _cart_lua_bytecode` → `w` (weak undef)
- [ ] `nm -D lib/libconsole.so | grep fc_consolelua_set_bytecode` → `w` (weak undef)
- [ ] `lib/build/libconsolelua.so` produced — `ELF32 DYN RISC-V`
- [ ] `readelf -d lib/libconsolelua.so | grep SONAME` → `libconsolelua.so`
- [ ] `nm -D lib/libconsolelua.so | grep fc_cart_init` → `T` (exported)
- [ ] `nm -D lib/libconsolelua.so | grep fc_cart_update` → `T` (exported)
- [ ] `nm -D lib/libconsolelua.so | grep fc_cart_draw` → `T` (exported)
- [ ] `nm -D lib/libconsolelua.so | grep fc_consolelua_set_bytecode` → `T` (exported)
- [ ] `nm -D lib/libconsolelua.so | grep cart_lua_modules` → `w` (weak undef)

## Stage 2 — Emulator patch

- [ ] `patches/apply-multi-dynload.py` finds spike-c marker in `src/elf.c`
- [ ] Patch applies without error; script prints confirmation message
- [ ] `rv32emu` rebuilds successfully after patch
- [ ] `memory_fill` is available in rv32emu's `memory_t` API (used by loader)

## Stage 2 — Case a (C-only cart) — emulator target

- [ ] `cases/case_a/build/cart_a` produced — `ELF32 EXEC RISC-V`
- [ ] `readelf -d cases/case_a/build/cart_a | grep NEEDED` → `libconsole.so`
- [ ] `nm -D cases/case_a/build/cart_a | grep fc_cart_init` → `T` (exported via --export-dynamic)
- [ ] `nm -D cases/case_a/build/cart_a | grep fc_cart_update` → `T` (exported)
- [ ] `nm -D cases/case_a/build/cart_a | grep fc_cart_draw` → `T` (exported)
- [ ] `make docker-run-case-a` produces exactly:
  ```
  frame 0
  frame 1
  frame 2
  frame 3
  frame 4
  frame 5
  frame 6
  frame 7
  frame 8
  frame 9
  OK
  ```
- [ ] No `fc32_dynload: unresolved` warnings in stderr for case a

### Symbol resolution directions — case a, emulator

| Direction | Symbol | Expected result |
|---|---|---|
| libconsole → cart | `fc_cart_init` | resolved to cart `.text` |
| libconsole → cart | `fc_cart_update` | resolved to cart `.text` |
| libconsole → cart | `fc_cart_draw` | resolved to cart `.text` |
| libconsole → cart | `_cart_lua_bytecode` | zero (weak undef, no definition) |
| libconsole → cart | `fc_consolelua_set_bytecode` | zero (weak undef, no definition) |
| cart → libconsole | `fc_console_print` | resolved to libconsole |
| cart → libconsole | `snprintf` | resolved to libconsole runtime |

- [ ] libconsole → cart entries: PASS
- [ ] libconsole → cart data (zero check): PASS
- [ ] cart → libconsole: PASS

## Stage 2 — Case b (C cart + user library) — emulator target

- [ ] `cases/case_b/build/cart_b` produced
- [ ] `readelf -S cases/case_b/build/cart_b | grep '\.text\.mylib'` → section present
- [ ] `nm -D cases/case_b/build/cart_b | grep mylib_value` → `T` (exported)
- [ ] `make docker-run-case-b` produces exactly:
  ```
  frame 0 mylib=42
  frame 1 mylib=42
  ...
  frame 9 mylib=42
  OK
  ```
- [ ] No `fc32_dynload: unresolved` warnings for case b

### Symbol resolution directions — case b, emulator

| Direction | Symbol | Expected result |
|---|---|---|
| libconsole → cart | `fc_cart_init/update/draw` | resolved to cart `.text` |
| libconsole → cart | `_cart_lua_bytecode` | zero (weak undef) |
| cart `.text` → cart `.text.mylib` | `mylib_value` | static call (intra-binary) |
| cart → libconsole | `fc_console_print`, `snprintf` | resolved |

- [ ] `.text.mylib` section present in cart binary: PASS
- [ ] libconsole → cart entries: PASS
- [ ] Intra-cart `.text` → `.text.mylib` call: PASS
- [ ] cart → libconsole: PASS

## Stage 2 — Case c (Lua-only cart) — emulator target

- [ ] `cases/case_c/build/cart.luac` produced by `luac5.4`
- [ ] `cases/case_c/build/cart_lua_bytes.inc` produced by `xxd`
- [ ] `cases/case_c/build/cart_c` produced — `ELF32 EXEC RISC-V`
- [ ] `readelf -d cases/case_c/build/cart_c | grep NEEDED` shows both `libconsole.so` and `libconsolelua.so`
- [ ] `nm -D cases/case_c/build/cart_c | grep _cart_lua_bytecode` → `D` (data, exported)
- [ ] `nm -D cases/case_c/build/cart_c | grep _cart_lua_bytecode_size` → `D` (data, exported)
- [ ] `nm -D cases/case_c/build/cart_c | grep fc_cart_init` → absent or `U` (cart does not define it)
- [ ] `make docker-run-case-c` produces exactly:
  ```
  frame 0
  frame 1
  ...
  frame 9
  OK
  ```

### Symbol resolution directions — case c, emulator

| Direction | Symbol | Expected result |
|---|---|---|
| libconsole → libconsolelua | `fc_cart_init` | resolved to libconsolelua |
| libconsole → libconsolelua | `fc_cart_update` | resolved to libconsolelua |
| libconsole → libconsolelua | `fc_cart_draw` | resolved to libconsolelua |
| libconsole → cart | `_cart_lua_bytecode` | resolved to cart `.cart.resources` |
| libconsole → cart | `_cart_lua_bytecode_size` | resolved to cart data |
| libconsole → libconsolelua | `fc_consolelua_set_bytecode` | resolved to libconsolelua |
| libconsolelua → libconsole | `fc_console_print` | resolved (Lua `console_print` wrapper) |
| libconsolelua → cart | `cart_lua_modules` | zero (weak undef, case c has no C modules) |
| Lua VM → bytecode | `init`/`update`/`draw` | Lua global lookup |

- [ ] libconsole → libconsolelua entries: PASS
- [ ] libconsole → cart data symbols: PASS
- [ ] libconsole → libconsolelua handoff (`fc_consolelua_set_bytecode`): PASS
- [ ] libconsolelua → libconsole (`fc_console_print`): PASS
- [ ] libconsolelua → cart (`cart_lua_modules` = zero, skipped): PASS
- [ ] Lua bytecode loads and executes: PASS

## Stage 2 — Case d (Lua cart + C user library) — emulator target

- [ ] `cases/case_d/build/cart_d` produced
- [ ] `readelf -d cases/case_d/build/cart_d | grep NEEDED` shows both libraries
- [ ] `readelf -S cases/case_d/build/cart_d | grep '\.text\.mylib'` → section present
- [ ] `nm -D cases/case_d/build/cart_d | grep cart_lua_modules` → `T` (exported)
- [ ] `nm -D cases/case_d/build/cart_d | grep _cart_lua_bytecode` → `D` (exported)
- [ ] `make docker-run-case-d` produces exactly:
  ```
  cart-side init from C
  frame 0 mylib=7
  frame 1 mylib=7
  ...
  frame 9 mylib=7
  OK
  ```

### Symbol resolution directions — case d, emulator

| Direction | Symbol | Expected result |
|---|---|---|
| libconsole → libconsolelua | `fc_cart_init/update/draw` | resolved to libconsolelua |
| libconsole → cart | `_cart_lua_bytecode`, `_cart_lua_bytecode_size` | resolved to cart |
| libconsole → libconsolelua | `fc_consolelua_set_bytecode` | resolved |
| libconsolelua → cart | `cart_lua_modules` | resolved to cart `.text.mylib` (non-zero!) |
| libconsolelua → libconsole | `fc_console_print` | resolved |
| cart `.text.mylib` → libconsole | `fc_console_print` | resolved (direct cart C → libconsole) |
| cart `.text.mylib` → libconsolelua | Lua C API (`luaL_getsubtable`, `lua_newtable`, etc.) | resolved to libconsolelua |
| Lua → C module | `mylib.add(3,4)` | dispatched via Lua preload table |

- [ ] libconsolelua → cart (`cart_lua_modules` non-zero): PASS
- [ ] cart C → libconsole from `.text.mylib`: PASS
- [ ] cart C → libconsolelua (Lua C API): PASS
- [ ] Lua `require("mylib")` dispatches to `l_add`: PASS
- [ ] First stdout line is `cart-side init from C`: PASS
- [ ] Each frame shows `mylib=7`: PASS

## Stage 3 — Native RISC-V target (QEMU Fedora 42)

Prerequisites: Spike H QEMU image available, `make qemu-boot` working.

### Case a — native

- [ ] Cart binary copied to QEMU guest virtfs mount
- [ ] `LD_LIBRARY_PATH=/mnt/spike-i/lib ./cart_a` exits 0
- [ ] Output matches expected (frame 0..9 + OK)
- [ ] `strace ./cart_a` confirms ECALL 64 (write) for each `fc_console_print`

### Case b — native

- [ ] `LD_LIBRARY_PATH=/mnt/spike-i/lib ./cart_b` produces `frame N mylib=42` × 10 + OK
- [ ] System dynamic linker resolves `fc_cart_init/update/draw` from cart `.dynsym`

### Case c — native

- [ ] `LD_LIBRARY_PATH=/mnt/spike-i/lib ./cart_c` produces frame 0..9 + OK
- [ ] `LD_DEBUG=symbols ./cart_c 2>&1 | grep fc_cart_init` → binding to `libconsolelua.so`
- [ ] `LD_DEBUG=symbols ./cart_c 2>&1 | grep _cart_lua_bytecode` → binding to `cart_c`

### Case d — native

- [ ] `LD_LIBRARY_PATH=/mnt/spike-i/lib ./cart_d` produces correct output (cart-side init + frame 0..9 mylib=7 + OK)
- [ ] `LD_DEBUG=symbols ./cart_d 2>&1 | grep cart_lua_modules` → binding to `cart_d`

## Stage 4 — WASM target, C carts (rv32emu-WASM)

- [ ] rv32emu compiles to WASM with multi-library loader patch
- [ ] `make node-test-case-a` → frame 0..9 + OK in Node stdout
- [ ] `make node-test-case-b` → frame 0..9 mylib=42 + OK in Node stdout
- [ ] ECALL 64 routed correctly through Emscripten fd_write to Node stdout

## Stage 5 — WASM target, Lua carts (Lua-direct)

- [ ] `consolelua-c.wasm` builds for case c (libconsole + libconsolelua + Lua VM + bytecode)
- [ ] `consolelua-d.wasm` builds for case d (adds `cart_lua_modules.c`, `mylib.c`)
- [ ] `make node-test-case-c` → frame 0..9 + OK
- [ ] `make node-test-case-d` → cart-side init + frame 0..9 mylib=7 + OK
- [ ] Structural difference documented: WASM Lua-direct is one static module (no dynamic linking)

## Cross-cutting verification

- [ ] `--export-dynamic` present in every cart link command
- [ ] No cart binary has `PT_INTERP` pointing to a real dynamic linker (`/no/interp` is the stub)
- [ ] `readelf -S cart_X | grep '\.cart\.'` confirms `.cart.info`, `.cart.config` in every cart
- [ ] Lua bytecode magic bytes (`\x1bLua`) confirmed in `cart_c.luac` and `cart_d.luac`
- [ ] `ilp32f` vs `ilp32d` ABI mismatch on native target documented as deferred
- [ ] `memory_fill` availability in rv32emu confirmed (if not present, zero-fill loop added)

# Spike I — implementation plan

**Question (per `docs/design/early-validation-spikes.md` §I):** Do all four
cart configurations — C-only, C with a user library, Lua-only, and Lua
calling a C library — build, load, and execute correctly across the emulator,
WASM, and native RISC-V (QEMU) targets, with the runtime driver living in
`libconsole.so` and discovering cart entry points by dynamic symbol
resolution?

**Why this spike exists:** Every prior spike used a bespoke benchmark harness,
not the cart format specified in ADR-0024 and ADR-0025. Spike C validated that
a single pre-mapped RV32IMFC shared library works via PLT/GOT, but the cart
in Spike C had its own `main()` that drove the test sequence — the cart's
lifecycle entry points were not discovered, they were called directly from
cart code.

The production model (ADR-0087) is different: the *runtime* — code outside
the user's cart source — drives `fc_cart_init`/`update`/`draw` and discovers
them as well-known exported symbols. This spike places that runtime driver
in `libconsole.so` itself: cart's `_start` calls `fc_console_main()` (exported
by `libconsole.so`), and `fc_console_main` declares weak undef refs to the
cart entry points that the dynamic linker resolves at load time. This fits
ADR-0024's "only `libconsole.so` and `libconsolelua.so` are permitted
DT_NEEDED entries; everything else statically linked" — there is no separate
runtime binary or extra DT_NEEDED.

This is the first complete matrix test of the cart format with the runtime
driver outside the cart binary.

**Dependencies:**
- ADR-0024 (cart ELF format `ET_EXEC` RV32IMFC, `.cart.*` sections, controlled
  dynamic linking, only libconsole/libconsolelua DT_NEEDED)
- ADR-0025 (Lua execution model, `libconsolelua.so` exports
  `fc_cart_init/update/draw` for Lua carts, `cart_lua_modules` hook, Lua C
  API re-exported from `libconsolelua.so` for cart C binding code)
- ADR-0087 (entry point names; runtime discovers them as exported symbols)
- Spike C (rv32emu dynamic-loader patch — `fc32_dynload`, the PLT/GOT fix-up
  mechanism. Needs extension to load two libraries plus build a global
  symbol table that includes the cart's exports.)
- Spikes F and G (Lua-direct WASM path, `host.c` shim structure, Emscripten
  build pipeline, Chrome measurement harness — all reusable)
- Spike H (Fedora 42 QEMU full-system image with CONFIG_COMPAT confirmed;
  used for the native RISC-V target)

---

## Key design decisions

### Architectural model — the runtime driver lives in `libconsole.so`

The cart binary is a standalone `ET_EXEC` RV32IMFC Linux executable. It is
fork/exec'd on the native target and pre-mapped into guest memory by the
extended `fc32_dynload` on the emulator target. Either way:

- Cart's `e_entry` is `_start`, supplied by the SDK's `crt0.o` (statically
  linked into every cart, per ADR-0024's "stage SDK is statically linked").
- `_start` sets up `gp` per the RISC-V psABI, then calls `fc_console_main()`
  via PLT into `libconsole.so`.
- `fc_console_main()` is the runtime loop. It calls `fc_cart_init` once,
  then loops `fc_cart_update`/`fc_cart_draw` 10 times, then prints `OK`.
- `fc_console_main` declares `fc_cart_init/update/draw` (and the Lua-cart
  bytecode handoff symbols) as **weak undefined externs**. The dynamic
  linker resolves them at exec time from the global symbol namespace.

```c
// inside libconsole.so
__attribute__((weak)) extern void fc_cart_init(void);
__attribute__((weak)) extern void fc_cart_update(void);
__attribute__((weak)) extern void fc_cart_draw(void);

// Lua handoff — only present if the cart loads libconsolelua.so
__attribute__((weak)) extern const uint8_t  _cart_lua_bytecode[];
__attribute__((weak)) extern const uint32_t _cart_lua_bytecode_size;
__attribute__((weak)) extern void fc_consolelua_set_bytecode(
    const uint8_t *data, uint32_t size);

void fc_console_print(const char *s);   // libconsole's own export

void fc_console_main(void) {
    if (&_cart_lua_bytecode && fc_consolelua_set_bytecode) {
        fc_consolelua_set_bytecode(_cart_lua_bytecode, _cart_lua_bytecode_size);
    }
    fc_cart_init();
    for (int i = 0; i < 10; i++) {
        fc_cart_update();
        fc_cart_draw();
    }
    fc_console_print("OK\n");
}
```

For C carts, `fc_cart_init/update/draw` resolve to the cart binary's own
exported symbols (the cart is built with `-Wl,--export-dynamic` so cart-defined
functions appear in `.dynsym`). For Lua carts, they resolve to
`libconsolelua.so`'s exports (libconsolelua is in the cart's `DT_NEEDED`
list and its symbols enter the global namespace at load time).

The 10-frame fixed loop is a spike simplification — production uses a real
scheduler. Document only that delta.

### Symbol-resolution paths exercised

The cart format is correct only if every direction below succeeds. Spike C
exercised only "cart → library." This spike adds the production-essential
"library → cart" direction (libconsole's runtime discovering cart entries)
plus the cross-library directions in case d.

| Direction | Where the call is | Where the symbol lives | Cases | Mechanism |
|---|---|---|---|---|
| **libconsole → cart** (entries) | `libconsole.so` | cart `.text` | a, b | Weak undef in libconsole; cart `.dynsym` via `--export-dynamic` |
| **libconsole → libconsolelua** (entries) | `libconsole.so` | `libconsolelua.so` | c, d | Weak undef in libconsole; libconsolelua loaded via cart's `DT_NEEDED` |
| **libconsole → cart** (data) | `libconsole.so` data refs | cart `.cart.resources` | c, d | Weak data refs to `_cart_lua_bytecode[]` / `_cart_lua_bytecode_size` |
| **libconsole → libconsolelua** (handoff) | `libconsole.so` PLT | `libconsolelua.so` | c, d | Weak undef `fc_consolelua_set_bytecode` |
| **cart → libconsole** | cart PLT | `libconsole.so` | a, b, d | `fc_console_print` |
| **cart → libconsolelua** | cart PLT | `libconsolelua.so` | d | Lua C API (`luaL_getsubtable`, `lua_pushcfunction`, `lua_setfield`, `lua_pushinteger`, `lua_tointeger`, `lua_newtable`, `lua_pop`) |
| **libconsolelua → cart** (weak) | `libconsolelua.so` PLT | cart `.text.mylib` | d | Weak `cart_lua_modules` resolved against cart's `.dynsym` |
| **libconsolelua → libconsole** | `libconsolelua.so` PLT | `libconsole.so` | c, d | `console_print` Lua wrapper calls `fc_console_print` |
| **Lua → libconsolelua C** | Lua VM dispatch | `libconsolelua.so` C funcs | c, d | C function pointer in Lua state — *not* dynamic linking, listed for completeness |
| **libconsolelua → user Lua** | `lua_pcall` from `libconsolelua.so` | bytecode loaded into VM | c, d | Lua VM dispatch, *not* dynamic linking |

The result write-up summarises this matrix per case (pass/fail per direction
per target).

### Lua bytecode access

The runtime driver in `libconsole.so` reads two cart-exported data symbols:
`_cart_lua_bytecode[]` and `_cart_lua_bytecode_size`. They live in the
cart's `.cart.resources` section and are exposed as default-visibility
globals (so they enter `.dynsym` via `--export-dynamic`). For C carts the
weak-undef references in libconsole resolve to nothing (kept zero) and the
guard skips the handoff. For Lua carts they resolve to the cart's section
data; libconsole then calls `fc_consolelua_set_bytecode` (resolved against
libconsolelua) to hand the pointer over before `fc_cart_init`.

This avoids requiring `libconsolelua.so` itself to parse the cart ELF — it
only sees the bytecode pointer it was handed.

### `.cart.*` section stubs

`.cart.info` and `.cart.config` are present as named ELF sections but
contain only a 4-byte magic stub (`FC32` and `CF32` respectively). The
loader and libraries do not parse them in this spike — their presence
validates that the linker script and section conventions work correctly.

### Lua-direct WASM path is structurally different

On the Lua-direct WASM path there is no RV32IMFC ELF, no rv32emu, and no
dynamic resolution at all. `libconsole.so`'s loop, `libconsolelua.so`'s VM
wrapper, the cart's Lua bytecode, and (for case d) the cart's
`cart_lua_modules`/`mylib` C source are compiled together into a single
Emscripten WASM binary. `fc_console_main` becomes a regular C function
calling other regular C functions. The cart-format boundary is preserved
at the source level (the cart's C and Lua sources are still authored
separately) but packaging is one static unit. Document this as the
structural difference between WASM Lua-direct and the other targets.

### Named `.text.mylib` sections (cases b, d)

These are statically linked into the cart binary. The linker script places
library functions in `.text.mylib` sections and exports them as regular
symbols. Validation: the packer-convention naming works end-to-end with
the cross-toolchain, survives `--gc-sections`, is callable from cart `.text`
(case b), and is callable from cart code that is itself called back from
`libconsolelua.so` (case d).

---

## Inputs we already have

- **Spike C `fc32_dynload`**: the rv32emu dynamic loader handles one cart
  ELF (`ET_EXEC`) plus one `.so`. Stage 2 extends it to load **two**
  libraries (libconsole.so and libconsolelua.so) and to build a global
  symbol table that includes the cart's `.dynsym` exports — so that
  libconsole's weak undef refs to `fc_cart_init` etc. can be resolved
  back into the cart. `patches/apply-multi-dynload.py` is the extension
  point.

- **Spike C's `liblua54.so` build**: Lua 5.4.7 compiled to RV32IMFC with
  `LUA_32BITS`, freestanding stubs (`lua_lib_runtime.c`, softfloat stubs),
  `-fPIC -ffreestanding -nostdlib`. The `libconsolelua.so` build in
  Stage 1 reuses this recipe. The Lua C API symbols (`lua_*`, `luaL_*`)
  are *re-exported* from `libconsolelua.so` per ADR-0025, so cart C
  binding code calls them via PLT without `DT_NEEDED: liblua54.so`.

- **Spike B's Lua source files**: `spikes/spike-b/benchmarks/`. Cases c and d
  use new simple Lua scripts — but the Lua VM and build toolchain carry
  forward.

- **Spike H's Fedora 42 QEMU image**: confirmed to run RV32IMFC ELFs via
  CONFIG_COMPAT. Stage 3 uses the same image and Makefile patterns as
  Spike H's `qemu-test-fedora` target.

- **Spike F/G's host shim and Emscripten pipeline**: `host.c` shape, the
  `emsdk` symlink, the Lua-direct WASM build flags. Stage 5 adapts these
  for the libconsole + libconsolelua + cart-as-static-objects WASM build.

---

## What we are NOT building

- **Full FlatBuffers parsing** for `.cart.info` and `.cart.config`. Stubs
  suffice; FlatBuffers integration is a production implementation task.
- **Full console API surface**. Only `fc_console_print` plus the runtime
  loop's `fc_console_main` are needed.
- **Production resource directory** in `.cart.resources`. The spike uses
  a simple blob with the Lua bytecode directly — no named-resource
  directory, no integer IDs, no MIME types.
- **Real-time scheduler**. The 10-frame fixed loop in `fc_console_main`
  stands in for production's scheduler.
- **Process isolation** — seccomp, namespaces, cgroups are Spike H scope.
  Stage 3 runs the cart as a normal Linux process.
- **Cart packer or FlatBuffers compiler**. Cart binaries are assembled by
  hand-written linker scripts and Makefiles.
- **ADR-0083 `fc_time_frame()` API**. The frame counter is maintained as
  cart-local state, not via a console API call.
- **ELF `EI_OSABI` cart identity byte** (pending from `docs/pending-name.md`).
  Spike uses `ELFOSABI_NONE` (0); the loader does not check it.

---

## The four cart cases

All cases use the same test workload, driven by `fc_console_main` inside
`libconsole.so`: call `fc_cart_init`, loop 10 frames calling `fc_cart_update`
+ `fc_cart_draw`, print `OK`. Expected stdout for cases a/c:

```
frame 0
frame 1
...
frame 9
OK
```

Case b output:
```
frame 0 mylib=42
...
frame 9 mylib=42
OK
```

Case d output:
```
cart-side init from C
frame 0 mylib=7
...
frame 9 mylib=7
OK
```

The `cart-side init from C` line is emitted by `cart_lua_modules` calling
`fc_console_print` directly — explicit cart→libconsole validation in the
same translation unit that calls the Lua C API (cart→libconsolelua). See
case d below.

### Case a — C-only cart

Cart binary contains the SDK's `crt0.o` (statically linked) plus user
code defining `fc_cart_init/update/draw`. `DT_NEEDED: libconsole.so`. Static
int frame counter. No `main()` — the runtime loop lives in libconsole.

```c
// cart_a.c
extern void fc_console_print(const char *s);
extern int snprintf(char *, unsigned long, const char *, ...);

static int frame = 0;

void fc_cart_init(void)   { frame = 0; }
void fc_cart_update(void) { frame++; }
void fc_cart_draw(void) {
    char buf[32];
    snprintf(buf, sizeof(buf), "frame %d\n", frame - 1);
    fc_console_print(buf);
}
```

Build:
```
riscv32-linux-musl-gcc -no-pie -nostdlib \
    -march=rv32imfc_zicsr -mabi=ilp32f \
    -Wl,--export-dynamic \
    crt0.o cart_a.o -L. -lconsole \
    -o cart_a
```

`--export-dynamic` is the load-bearing flag: without it, `fc_cart_init`
etc. would be in `.symtab` only, and libconsole's weak undef refs would
remain unresolved. Verify:
```
readelf -d cart_a               # DT_NEEDED libconsole.so
nm -D cart_a | grep fc_cart_    # fc_cart_init/update/draw exported in .dynsym
```

### Case b — C cart with user library

Case a plus `mylib.c` compiled with `-ffunction-sections` so the linker
places it in a `.text.mylib` section, and linked with `--gc-sections` to
verify the named section survives.

```c
// mylib.c → .text.mylib
int mylib_value(void) { return 42; }
```

```c
// cart_b.c — case a but draw() includes mylib_value()
void fc_cart_draw(void) {
    char buf[48];
    snprintf(buf, sizeof(buf), "frame %d mylib=%d\n", frame - 1, mylib_value());
    fc_console_print(buf);
}
```

Verify with `readelf -S cart_b` that `.text.mylib` appears.

### Case c — Lua-only cart

Cart binary contains the SDK's `crt0.o` (statically linked) plus the Lua
bytecode placed in `.cart.resources` and exposed as the two data symbols
`_cart_lua_bytecode` / `_cart_lua_bytecode_size`. No user C code — the
cart binary's only `.text` is from `crt0.o`. `DT_NEEDED: libconsole.so
libconsolelua.so`.

```lua
-- main.lua (compiled to bytecode by luac before embedding)
local frame = 0
function init()   frame = 0 end
function update() frame = frame + 1 end
function draw()
    console_print("frame " .. (frame - 1) .. "\n")
end
```

`console_print` is registered by `libconsolelua.so` as a global Lua C
function backed by `fc_console_print` (a `libconsole.so` symbol resolved
through libconsolelua's PLT — see the matrix above).

The runtime in libconsole.so resolves:
- `fc_cart_init/update/draw` to `libconsolelua.so`'s exports (the cart
  doesn't define them; the dynamic linker finds them in libconsolelua,
  which entered the namespace via cart's `DT_NEEDED`)
- `_cart_lua_bytecode` / `_cart_lua_bytecode_size` to data in cart's
  `.cart.resources`
- `fc_consolelua_set_bytecode` to libconsolelua.so

All four resolutions are observable in `readelf -r cart_c` and the
loader's relocation trace.

### Case d — Lua cart with C user library

Case c plus cart-side native code: `mylib.c` in `.text.mylib`, and
`cart_lua_modules.c` exporting `cart_lua_modules(lua_State *L)`. The cart
now has both Lua bytecode in `.cart.resources` *and* RV32 code in
`.text.mylib`. Lua bytecode `require("mylib")`s and uses `mylib.add(3,4)`
in draw output.

```c
// cart_lua_modules.c
#include "lua.h"
#include "lauxlib.h"

extern void fc_console_print(const char *s);   // libconsole.so

static int l_add(lua_State *L) {
    int a = (int)lua_tointeger(L, 1);
    int b = (int)lua_tointeger(L, 2);
    lua_pushinteger(L, a + b);
    return 1;
}

void cart_lua_modules(lua_State *L) {
    /* Direct cart→libconsole call — explicit validation that cart C code
       can reach libconsole.so symbols even when the same translation unit
       is also reaching libconsolelua.so symbols. */
    fc_console_print("cart-side init from C\n");

    /* Cart→libconsolelua: every Lua C API symbol below is an
       R_RISCV_JUMP_SLOT in cart_d resolved against libconsolelua.so. */
    luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
    lua_newtable(L);
    lua_pushcfunction(L, l_add);
    lua_setfield(L, -2, "add");
    lua_setfield(L, -2, "mylib");
    lua_pop(L, 1);
}
```

```lua
-- main.lua
local mylib = require("mylib")
local frame = 0
function init()   frame = 0 end
function update() frame = frame + 1 end
function draw()
    console_print("frame " .. (frame - 1) .. " mylib=" .. mylib.add(3, 4) .. "\n")
end
```

`libconsolelua.so` declares `cart_lua_modules` as a weak extern and calls
it after Lua state creation and sandbox configuration, before bytecode
load:

```c
// In libconsolelua.so
__attribute__((weak)) void cart_lua_modules(lua_State *L);
// ...
if (cart_lua_modules) cart_lua_modules(L);
```

Cases a/b/c have no `cart_lua_modules` symbol; case d does. The dynamic
linker (or rv32emu's loader) determines whether the GOT entry resolves
to zero (skipped at the null-check) or to the cart's `.text.mylib` symbol
(called).

---

## Stage 0 — Build environment

1. Create `spikes/spike-i/Dockerfile`. Base on the spike-c Docker image
   (`FROM fc32-spike-c AS builder`) to inherit:
   - `riscv32-linux-musl` toolchain
   - rv32emu source tree (the Spike C `fc32_dynload` patch will be
     superseded by Stage 2's extended patch)
   - Lua 5.4.7 source tree

   Add:
   - `luac` (native, for compiling Lua source to bytecode)
   - `qemu-riscv32-static` (for host-side smoke tests; does NOT exercise
     CONFIG_COMPAT — use Spike H's QEMU full-system target for that)

2. Confirm `luac -v` is available in the Docker image and produces
   bytecode that matches the Lua 5.4.7 VM version.

3. The WASM stages reuse `spikes/spike-f/emsdk/` via symlink. Confirm
   Emscripten is available via `emcc --version`.

---

## Stage 1 — Stub libraries and SDK crt0

### `crt0.o` — SDK static object (every cart links it)

```asm
// crt0.S
.section .text._start, "ax"
.globl _start
_start:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    call fc_console_main
    /* fc_console_main returns; exit(0) via ECALL 93. */
    li a0, 0
    li a7, 93
    ecall
1:  j 1b
```

`fc_console_main` is in libconsole.so; the call goes via PLT. Build with
the same flags as the cart object files; ship as `crt0.o` for the cart
Makefiles to consume.

### `libconsole.so` — RV32IMFC console + runtime driver

```c
// libconsole_rv32.c
#include <stdint.h>

static unsigned strlen_local(const char *s) {
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

void fc_console_print(const char *s) {
    /* ECALL 64 = __NR_write; a0=1 (stdout), a1=s, a2=len */
    unsigned len = strlen_local(s);
    register long a0 __asm__("a0") = 1;
    register const char *a1 __asm__("a1") = s;
    register unsigned a2 __asm__("a2") = len;
    register long a7 __asm__("a7") = 64;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
}

/* Weak undef refs — resolved at exec time from cart or libconsolelua. */
__attribute__((weak)) extern void fc_cart_init(void);
__attribute__((weak)) extern void fc_cart_update(void);
__attribute__((weak)) extern void fc_cart_draw(void);
__attribute__((weak)) extern const uint8_t  _cart_lua_bytecode[];
__attribute__((weak)) extern const uint32_t _cart_lua_bytecode_size;
__attribute__((weak)) extern void fc_consolelua_set_bytecode(
    const uint8_t *data, uint32_t size);

void fc_console_main(void) {
    if (&_cart_lua_bytecode && fc_consolelua_set_bytecode) {
        fc_consolelua_set_bytecode(_cart_lua_bytecode, _cart_lua_bytecode_size);
    }
    fc_cart_init();
    for (int i = 0; i < 10; i++) {
        fc_cart_update();
        fc_cart_draw();
    }
    fc_console_print("OK\n");
}
```

Build:
```
riscv32-linux-musl-gcc -shared -fPIC -nostdlib \
    -march=rv32imfc_zicsr -mabi=ilp32f \
    -Wl,-soname,libconsole.so \
    -Wl,--unresolved-symbols=ignore-all \
    -o libconsole.so libconsole_rv32.c
```

`--unresolved-symbols=ignore-all` lets us link with weak undef refs that
have no providing library at build time. Verify:
```
readelf -h libconsole.so   # ELF32 DYN
readelf -d libconsole.so   # DT_SONAME = libconsole.so
nm -D libconsole.so        # fc_console_print, fc_console_main exported;
                           # fc_cart_init/update/draw etc. show as 'w' (weak undef)
```

### `libconsolelua.so` — RV32IMFC Lua VM wrapper

`libconsolelua.so` contains the Lua 5.4.7 VM (same build as Spike C's
`liblua54.so`) and exports the Lua-cart entry implementations. It carries
the same freestanding stubs as Spike C.

Exported symbols:
- `fc_cart_init/update/draw` — Lua-cart implementations: `lua_getglobal`
  + `lua_pcall` against the configured Lua state.
- `fc_consolelua_set_bytecode(const uint8_t *data, uint32_t size)` — stores
  the bytecode pointer in a static.
- The full Lua C API surface (`lua_*`, `luaL_*`) re-exported per ADR-0025.

Lua sandbox (per ADR-0079): open only `base`, `math`, `string`, `table`.
Strip `os`, `io`, `package`, `debug`, `coroutine`. Register `console_print`
as a global C function backed by `fc_console_print` (libconsole.so symbol —
exercises libconsolelua → libconsole resolution).

`cart_lua_modules` is declared weak; called unconditionally after sandbox
setup and before `luaL_loadbuffer`, with a null-check guard.

Build:
```
riscv32-linux-musl-gcc -shared -fPIC -nostdlib \
    -march=rv32imfc_zicsr -mabi=ilp32f \
    -Wl,-soname,libconsolelua.so \
    -o libconsolelua.so \
    libconsolelua_rv32.o lua_*.o lmathlib.o lstrlib.o ltablib.o \
    lua_lib_runtime.o lua_init_libs.o softfloat_stubs.o \
    -L. -lconsole
```

Verify:
```
readelf -d libconsolelua.so       # DT_SONAME, DT_NEEDED libconsole.so
nm -D libconsolelua.so | grep -E '(fc_cart_|fc_consolelua_set_bytecode|^.{8} . (lua_|luaL_))'
                                  # entry points + Lua C API re-exports
nm -D libconsolelua.so | grep cart_lua_modules  # expect: w (weak undef)
```

---

## Stage 2 — Emulator target (rv32emu), cases a–d

### Extend the rv32emu dynamic loader

Spike C's `fc32_dynload` loads one library at `0x08000000` and resolves
the cart's PLT against it. Two extensions are needed:

**(1) Multi-library load.** Walk the cart's `DT_NEEDED` list. For each
entry, locate in `-L libpath`, recurse (libconsolelua's `DT_NEEDED`
includes libconsole), assign non-overlapping load addresses (first
library at `0x08000000`; each subsequent at the previous library's load
address + its `PT_LOAD` extent rounded up to 4 KiB). Map each library's
`PT_LOAD` segments at the assigned address.

**(2) Global symbol table includes the cart's `.dynsym`.** Build a single
table by merging:
- The cart binary's `.dynsym` (the cart was built with `--export-dynamic`,
  so user-defined functions and the `_cart_lua_bytecode` data symbols
  are present).
- libconsole's `.dynsym`.
- libconsolelua's `.dynsym` (for cases c/d).

This is the change that lets libconsole's weak undef ref to
`fc_cart_init` resolve back to the cart for C carts. The merge rule is
"first definition wins; weak undefs collect all weak refs."

Apply each library's `SHT_RELA` relocations using the global table,
then the cart's `DT_JMPREL` and `DT_RELA` relocations. Weak undef refs
that never bind to a definition: write `0` to the GOT entry. This is
what produces `cart_lua_modules == NULL` for cases a/b/c.

Test the extension incrementally:
1. Cases a/b first (cart + libconsole only) — confirms libconsole→cart
   weak resolution works.
2. Cases c/d (cart + libconsole + libconsolelua) — confirms 3-binary
   loading, libconsole→libconsolelua resolution for entry points,
   libconsole→cart resolution for the bytecode data symbols, and
   libconsolelua→cart resolution for `cart_lua_modules` (case d).

### Cart linker script

Common `cart.ld` for all four cases:
- Standard `.text`, `.data`, `.bss`, `.rodata` at the Spike-C cart load
  address (`0x00010000`).
- Named additional sections:
  ```
  .text.mylib : { KEEP(*(.text.mylib)) }   /* cases b, d */
  .cart.info  : { KEEP(*(.cart.info))  }
  .cart.config: { KEEP(*(.cart.config)) }
  .cart.resources : {
      KEEP(*(.cart.resources))
  }
  ```

Stub `.cart.info` and `.cart.config` data:
```c
__attribute__((section(".cart.info")))
static const char cart_info_stub[] = "FC32";

__attribute__((section(".cart.config")))
static const char cart_config_stub[] = "CF32";
```

For cases a/b: `.cart.resources` is empty (the section exists but carries
no data). For cases c/d:
```c
// Generated by: luac -o cart.luac main.lua && xxd -i cart.luac > cart_lua_bytes.c
__attribute__((section(".cart.resources"), visibility("default")))
const uint8_t _cart_lua_bytecode[] = {
    #include "cart_lua_bytes.c"
};
__attribute__((visibility("default")))
const uint32_t _cart_lua_bytecode_size = sizeof(_cart_lua_bytecode);
```

Default visibility plus `--export-dynamic` ensures both data symbols
appear in the cart's `.dynsym` for libconsole's loader-time resolution.

### Build and run each case

4. **Case a** — C cart, libconsole only:
   ```
   make docker-run-case-a
   # expected: "frame 0\nframe 1\n...\nframe 9\nOK\n"
   ```

5. **Case b** — case a plus `mylib.c` in `.text.mylib`. Verify
   `.text.mylib` in `readelf -S cart_b`. Output adds `mylib=42`.

6. **Case c** — Lua cart. First run that confirms:
   - libconsole's runtime loop resolves `fc_cart_init/update/draw` to
     libconsolelua (not to cart, which doesn't define them).
   - libconsole resolves `_cart_lua_bytecode`/`_cart_lua_bytecode_size`
     to cart's `.cart.resources`.
   - libconsole calls `fc_consolelua_set_bytecode` (libconsolelua).
   - libconsolelua loads the bytecode, calls Lua `init`/`update`/`draw`,
     prints `frame 0` through `frame 9`.

7. **Case d** — case c plus `cart_lua_modules.c` (in `.text.mylib`) and
   `mylib.c`. Confirm:
   - Same libconsole→libconsolelua resolutions as case c.
   - libconsolelua's GOT entry for `cart_lua_modules` is non-zero
     (resolved to cart's `.text.mylib`).
   - First stdout line is `cart-side init from C` — direct cart→libconsole
     call from `cart_lua_modules` before the loop.
   - Each frame prints `mylib=7`.

   This is the first end-to-end test of *all* of:
   - libconsole → cart entries (case a/b path) AND libconsole →
     libconsolelua entries (case c/d path)
   - cart → libconsole (direct from cart C code)
   - cart → libconsolelua (Lua C API)
   - libconsolelua → cart (weak `cart_lua_modules`)
   - libconsolelua → libconsole (Lua `console_print` wrapper)

---

## Stage 3 — Native RISC-V target (QEMU), cases a–d

Use the Fedora 42 QEMU full-system image from Spike H. Run the cart binary
as a normal Linux process — `fork`/`exec`. The system dynamic linker
(`/lib/ld-musl-riscv32.so.1`) handles every cross-binary symbol resolution
that Stage 2's extended `fc32_dynload` handles in the emulator. No custom
loader code needed on this target.

8. Reuse Stage 1's `libconsole.so` and `libconsolelua.so` unchanged. The
   guest kernel handles ECALL 64 identically to a native `write` syscall.
   Confirm by `strace`.

9. Build cases a–d with `PT_INTERP` pointing to the guest's dynamic linker
   (`/lib/ld-musl-riscv32.so.1`) — i.e., drop `--dynamic-linker /no/interp`
   from the link flags. Keep `--export-dynamic` so cart symbols are in
   `.dynsym` for libconsole's weak refs to bind. The cart is `ET_EXEC`,
   not `ET_DYN`.

   The musl.cc `ilp32d` vs cart-spec `ilp32f` ABI mismatch (Spike H) still
   applies — document as deferred.

10. Copy binaries and libraries to the QEMU guest via virtfs (Spike H's
    `build/` share pattern). Run each case:

    ```sh
    # inside guest, LD_LIBRARY_PATH set to the mount point
    LD_LIBRARY_PATH=/mnt/spike-i/lib ./cart_a   # frame 0..9 OK
    LD_LIBRARY_PATH=/mnt/spike-i/lib ./cart_b   # frame 0..9 mylib=42 OK
    LD_LIBRARY_PATH=/mnt/spike-i/lib ./cart_c   # frame 0..9 OK
    LD_LIBRARY_PATH=/mnt/spike-i/lib ./cart_d   # cart-side init… frame 0..9 mylib=7 OK
    ```

    The kernel exec's `cart_X`. The dynamic linker loads `libconsole.so`
    and (for c/d) `libconsolelua.so`, builds the global symbol table,
    resolves all weak undef refs in libconsole back to cart `.dynsym`
    (a/b) or libconsolelua `.dynsym` (c/d), resolves `cart_lua_modules`
    weak ref in libconsolelua against cart `.dynsym` (d). Cart's `_start`
    calls `fc_console_main`; the loop runs.

    If all four cases produce the expected output identically to Stage 2's
    rv32emu runs, the spike has demonstrated that the cart format works
    end-to-end through standard Linux dynamic linking — which is the
    production native target's loading path.

---

## Stage 4 — WASM target, C carts (cases a and b, rv32emu-WASM)

11. Reuse the rv32emu-WASM build from Spike E. The cart `ET_EXEC` and
    `libconsole.so` are loaded into guest memory by rv32emu running
    inside WASM, exactly as in Stage 2.

    Compile rv32emu to WASM with the Stage 2 multi-library loader extension
    (the loader is the same C code; only the host platform changes). Feed
    `cart_a` and `libconsole.so` (and the case-b counterparts) as embedded
    files or via a JS `FS.writeFile` call before invoking `rv32emu_run`.

    The `fc_console_print` stub issues ECALL 64, which rv32emu intercepts
    and routes to `fd_write` in the WASM environment.

    Test target: Node.js (simpler for automated output capture than
    headless Chrome):
    ```
    make node-test-case-a
    make node-test-case-b
    ```
    Confirm output matches `frame 0..9 OK` (a) and `frame 0..9 mylib=42 OK` (b).

---

## Stage 5 — WASM target, Lua carts (cases c and d, Lua-direct)

12. Build `consolelua.wasm`: the `libconsole.so` source (including
    `fc_console_main`), the `libconsolelua.so` source, the Lua 5.4.7 VM,
    and (for case d) `cart_lua_modules.c`/`mylib.c` — all compiled
    together to one WASM via Emscripten. The Lua bytecode is embedded
    via `--embed-file`. There is *no* dynamic resolution at all: every
    cross-binary call from Stages 2/3 becomes a normal static C call.

    ```
    emcc -O2 -DLUA_32BITS \
        libconsole_wasm.c libconsolelua_wasm.c \
        lua_*.c lmathlib.c lstrlib.c ltablib.c \
        $(if case-d, cart_lua_modules.c mylib.c) \
        --embed-file main.lua \
        -sMODULARIZE=1 -sEXPORT_NAME=ConsoleLua \
        -sEXPORTED_FUNCTIONS=_fc_console_main \
        -o consolelua.js
    ```

    Run under Node:
    ```
    make node-test-case-c   # expect: frame 0..9 OK
    make node-test-case-d   # expect: cart-side init… frame 0..9 mylib=7 OK
    ```

13. **Result write-up note.** The WASM Lua-direct path is *structurally
    different* from the other targets: there is no separate runtime, no
    separate cart binary, no separate libconsolelua. All sources are
    linked into one WASM module. The cart boundary is preserved at the
    source level (the cart's C and Lua sources are still authored
    separately) but packaging is one static unit. Production implication:
    the WASM packer compiles the cart's sources together with libconsole
    and libconsolelua into one WASM module per cart. The RV32IMFC packer
    produces a cart `ET_EXEC` plus the runtime-provided `libconsole.so`/
    `libconsolelua.so`; the WASM packer produces a single `.wasm`. ABI
    boundary at source level is the same; packaging differs.

---

## Risk notes

- **Multi-library load-address layout (rv32emu).** Spike C hard-coded a
  single library at `0x08000000`. Two libraries require non-overlapping
  load addresses. Compute each from the previous library's `PT_LOAD`
  extent at load time. If extents are wrong, segments overlap and
  relocations silently resolve to the wrong addresses. Verify with
  `readelf -l` on each `.so`.

- **Global symbol-table merge must include the cart's exports (rv32emu).**
  Spike C's loader built its symbol table only from the loaded `.so`.
  This spike requires the cart's `.dynsym` (after `--export-dynamic`) to
  also be in the table — otherwise libconsole's weak undef ref to
  `fc_cart_init` cannot resolve back to the cart for C carts. The merge
  is the new direction this spike validates relative to Spike C.

- **`--export-dynamic` discipline.** Without it, cart-defined functions
  end up in `.symtab` only, libconsole's weak refs stay zero, and
  `fc_console_main` calls a NULL `fc_cart_init` and crashes. The Makefile
  must enforce this flag for every cart build.

- **Weak undef ABI for the runtime loop.** `fc_console_main`'s correctness
  depends on the dynamic linker (or rv32emu's loader) writing zero to
  unbound weak refs. If the loader writes garbage, the C-cart guard
  `if (&_cart_lua_bytecode && fc_consolelua_set_bytecode)` fires
  spuriously and crashes. Test cases a/b specifically confirm the
  zero-fill — they are the negative test for both that ref and for
  `cart_lua_modules`.

- **`cart_lua_modules` weak symbol on rv32emu path.** Same correctness
  requirement as above, in the libconsolelua → cart direction.

- **Cross-library resolution order (Lua carts).** For libconsole's weak
  ref `fc_cart_init` in cases c/d, the cart doesn't define it but
  libconsolelua does. The dynamic linker's search order across the
  global namespace must reach libconsolelua's definition. Standard ELF
  semantics: the runtime walks load order; libconsolelua entered the
  namespace via the cart's `DT_NEEDED`, so its symbols are visible.
  Verify with `LD_DEBUG=symbols` in Stage 3 to confirm the resolution
  binds to libconsolelua and not to a stub.

- **`ilp32f` vs `ilp32d` on the native RISC-V target.** musl.cc prebuilt
  cross-compiler targets `ilp32d`; cart spec is `ilp32f`. For the test
  workload (no float arguments crossing boundaries), the mismatch is
  harmless; document as deferred.

- **Lua bytecode compatibility across `luac` versions.** `luac` running
  natively on the host (arm64) produces bytecode for the Lua 5.4 VM
  regardless of host architecture. The format is platform-neutral when
  `LUA_32BITS` is set for both compilation and loading. Confirm
  `LUAC_VERSION`/`LUAC_DATA` magic bytes match between host `luac` and
  embedded VM.

- **WASM `fd_write` capturing.** Cases a/b WASM tests rely on Emscripten
  routing ECALL 64 (write to fd 1) through WASI / emulated stdio to
  Node's stdout. Check `Module.print` is wired to `process.stdout.write`
  in the Node harness, or use `FS.writeFile('/dev/stdout', ...)` as a
  fallback.

- **`--embed-file` for Lua bytecode on WASM.** Emscripten's `--embed-file`
  bakes the file content into the WASM binary at build time. If the Lua
  source changes after the build, the embedded bytecode is stale. Document
  as a production concern: the WASM packer must recompile the cart's Lua
  source each time and re-embed it.

---

## Deliverables

- `spikes/spike-i/sdk/` — `crt0.S` (the SDK's `_start` object linked
  statically into every cart), Makefile.
- `spikes/spike-i/lib/` — `libconsole.so` (now containing
  `fc_console_main` and the weak undef refs) and `libconsolelua.so` as
  RV32IMFC shared objects; Makefiles and sources.
- `spikes/spike-i/cases/case_{a,b,c,d}/` — cart sources (`cart_X.c`,
  optional `mylib.c`, optional `cart_lua_modules.c`, optional `main.lua`),
  linker script, Makefile producing `cart_X` (`ET_EXEC`).
- `spikes/spike-i/patches/apply-multi-dynload.py` — extension of Spike C's
  `fc32_dynload` to load two libraries, build a global symbol table that
  includes the cart's `.dynsym` exports, and resolve weak undef refs
  bidirectionally (libconsole→cart, libconsolelua→cart).
- `spikes/spike-i/Dockerfile` — build environment (inherits from spike-c).
- `spikes/spike-i/Makefile` — top-level orchestration with per-case ×
  per-target targets:
  - `make docker-run-case-{a,b,c,d}` — emulator target
  - `make qemu-test` — native RISC-V target (Fedora 42, all cases)
  - `make node-test-case-{a,b,c,d}` — WASM target under Node
- `spikes/spike-i/wasm/` — WASM build for cases a/b (rv32emu-WASM) and
  cases c/d (Lua-direct, single statically-linked module); JS test
  harness.
- `spikes/spike-i/TASKS.md` — per-case × per-target × per-resolution-direction
  pass/fail checklist (rows from the symbol-resolution table above).
- `docs/design/spike-i-results.md` — write-up: the 4×3 result matrix, the
  per-direction symbol-resolution observations, the `cart_lua_modules`
  weak-resolution evidence (cases a/b/c GOT entry zero; case d resolved),
  the Stage 5 structural-difference note, the `ilp32f`/`ilp32d` finding,
  any open items for the production packer/loader.

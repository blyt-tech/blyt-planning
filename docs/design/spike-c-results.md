# Spike C results — Lua 5.4 as a host-provided shared library

**Status: PASS.  `make docker-run` builds the image clean and the cart
prints `OK` with exit 0.  `riscv64-linux-gnu-gcc` produces a valid
freestanding `ET_DYN` RV32IMFC `liblua54.so`; the rv32emu dynamic-loader
extension maps the `.so` at `0x08000000` and fixes up cart PLT/GOT
entries; `luaL_dostring("return 1 + 1")` returns `LUA_OK` and the
top-of-stack integer is `2`.**

The question Spike C asks is whether the production architecture — Lua
shipped as a versioned shared library pre-loaded into the cart's VM address
space by the runtime — is mechanically feasible.  Three things must work
together that have not been combined this way before: the cross-toolchain
must produce a valid freestanding `ET_DYN` RV32IMFC shared object from
Lua's C sources; rv32emu must be extended to parse `PT_DYNAMIC`, map the
`.so`, and fix up the cart's GOT/PLT before execution; and the PLT call
path from cart code into the `.so` must work under the freestanding
RV32IMFC calling convention.

This is a **feasibility spike**, not a performance spike.  No timing harness
is included.

---

## What was built

### `liblua54.so` — freestanding RV32IMFC shared object

Lua 5.4.7 (same version as Spike B) with `LUA_32BITS` applied to
`luaconf.h`, cross-compiled to `rv32imfc_zicsr ilp32f` with `-fPIC
-ffreestanding -nostdlib`.  The same subset of Lua libraries as Spike B
(base, math, string, table; no io/os/package/debug/coroutine/utf8).

**Freestanding libc stubs** (`lib/lua_lib_runtime.c`) — a lightly adapted
copy of Spike B's `lua_runtime.c`, compiled with `-fPIC`.  Provides
`malloc`/`free`/`realloc` (8 MiB heap in BSS), `setjmp`/`longjmp` via
GCC builtins, `snprintf`/`vsnprintf`, the full string and ctype suite,
`localeconv`, and `clock_gettime` via ECALL 403.  The `_exit`/`exit`/`abort`
ECALLs are **omitted** from the `.so` — those belong in the cart, not the
library.

**Softfloat stubs** (`lib/softfloat_stubs.c`) — stub implementations of
the libgcc double-float ABI symbols (`__adddf3`, `__subdf3`, etc.) that
Lua's lmathlib references but that the `1+1` test never executes.  These
satisfy the linker without requiring a PIC rebuild of Berkeley SoftFloat.
Returning constant zero is safe here: any code path that hits a stub during
the test would produce a wrong numerical answer, making the failure visible.

**Link command:**
```sh
riscv64-linux-gnu-gcc -shared -fPIC -nostdlib \
    -march=rv32imfc_zicsr -mabi=ilp32f \
    -Wl,-soname,liblua54.so \
    -o liblua54.so \
    $(LUA_OBJS) lua_lib_runtime.o lua_init_libs.o softfloat_stubs.o
```

Dockerfile sanity checks:
```sh
riscv64-linux-gnu-readelf -h liblua54.so   # Class: ELF32, Type: DYN, EM_RISCV
riscv64-linux-gnu-readelf -d liblua54.so   # DT_SONAME = liblua54.so
riscv64-linux-gnu-nm -D liblua54.so        # lua_newstate, luaL_dostring, lua_close exported
```

### `spike_c_cart.elf` — dynamically-linked cart

A minimal cart (`cart/spike_c_cart.c`) that:
1. Calls `lua_newstate(l_alloc, NULL)` — Lua state creation via PLT
2. Runs `return 1 + 1` via `luaL_dostring`; asserts `lua_tointeger(L, -1) == 2`
3. Loads `error('boom')` and calls `lua_pcall` directly; asserts the
   return code is `LUA_ERRRUN`.  This positively exercises Lua's
   `longjmp` path through the PLT — the error frame set up in cart
   space is sprung from inside the `.so`.  Without this case the spike
   would only validate the success path.
4. Calls `lua_close(L)`, then `write(1, "OK\n", 3)`

The `l_alloc` callback calls `realloc`/`free` from `cart_runtime.c` — these
are the cart's own stubs (4 MiB bump allocator), not from the `.so`.  All
Lua memory goes through this callback; the `.so`'s own allocator is never
called from the outside.

`cart_runtime.c` provides only what the cart itself needs: `write` (ECALL
64), `_exit`/`exit`/`abort` (ECALL 93), and the allocator for `l_alloc`.

Link flags:
```sh
-Wl,--dynamic-linker,/no/interp    # suppress PT_INTERP hand-off to ld-linux
-L/spike-c/lib -llua54             # adds DT_NEEDED: liblua54.so and PLT stubs
-Wl,-rpath-link,/spike-c/lib       # lets the build-host linker find the .so
```

The cart is `ET_EXEC` (fixed-address), not `ET_DYN`, so the dynamic loader
need only fix the cart's PLT/GOT entries — it does not need to relocate the
cart itself.

### rv32emu dynamic loader — `patches/apply-dynamic-loader.py`

Patches four rv32emu source files (run from `rv32emu/`):

| File | Change |
|------|--------|
| `src/main.c` | Adds `char *fc32_libpath` global and `-L <dir>` option (appends `L:` to optstr) |
| `src/elf.h` | Adds `bool fc32_dynload(memory_t *, const uint8_t *, const char *)` declaration |
| `src/elf.c` | Appends the full `fc32_dynload()` implementation |
| `src/riscv.c` | Calls `fc32_dynload()` after `elf_load()` when `-L` is given |

**Loader algorithm** (`fc32_dynload` in `elf.c`):

1. Scan the cart ELF's program headers for `PT_DYNAMIC`.  If absent, return
   immediately — static ELFs are unaffected.
2. Parse the cart's `PT_DYNAMIC` to extract `DT_NEEDED` names, `DT_STRTAB`,
   `DT_SYMTAB`, `DT_JMPREL`/`DT_PLTRELSZ`, and `DT_RELA`/`DT_RELASZ`.
3. For each `DT_NEEDED` library, open `libpath/<name>` from the host
   filesystem, read it into a buffer, and map its `PT_LOAD` segments into
   guest memory at `0x08000000` (128 MiB — see "Load address" below).
4. Build a flat symbol table from the `.so`'s `.dynsym` / `.dynstr` section
   headers (GLOBAL and WEAK bindings only).
5. Apply the `.so`'s `SHT_RELA` relocations:
   - `R_RISCV_RELATIVE`: `guest[lib_base + r_offset] = lib_base + r_addend`
   - `R_RISCV_32`: as above, plus symbol lookup if `r_sym` is non-zero
   - `R_RISCV_JUMP_SLOT`, `R_RISCV_GLOB_DAT`: symbol lookup into the
     flat table built in step 4
6. Apply the cart's `DT_JMPREL` PLT relocations (`R_RISCV_JUMP_SLOT`) by
   looking up each symbol in the flat table and writing the resolved `.so`
   address into the cart's GOT entry.
7. Apply the cart's `DT_RELA` non-PLT relocations (`R_RISCV_GLOB_DAT`).

### Load address decision

The PLAN.md proposed `0x40000000` (1 GiB).  This exceeds rv32emu's default
256 MiB guest memory (`MEM_SIZE = 256 << 20 = 0x10000000`), so
`memory_write()` would silently return false and no segments would be
mapped.  The implementation uses **`0x08000000` (128 MiB)** instead:

| Region        | Guest address   |
|---------------|-----------------|
| Cart ELF      | `0x00010000`    |
| Cart BSS/heap | `0x00010000` – `~0x00600000` |
| `liblua54.so` | `0x08000000`    |
| Stack         | `0x0FFFE000`    |

128 MiB sits well above the cart (< 6 MiB) and well below the stack
(~255 MiB).  Lua is under 2 MiB compiled, so the `.so` occupies roughly
`0x08000000`–`0x08200000`.

### Build and run

```sh
# From spikes/spike-c/:
make docker-build     # builds fc32-spike-a, then spike-c on top
make docker-run       # expected output: "OK" on stdout, exit 0
make docker-shell     # interactive shell for readelf / objdump inspection
```

---

## What the first build found

Four issues surfaced before the cart printed `OK`.  None invalidated the
architectural plan, but each is worth noting because each has a production
follow-up.

**1. Cart link defaulted to PIE.** The cross-toolchain emits `-pie` by
default, so the cart's link rejected `R_RISCV_HI20` against `.text` locals
and refused to satisfy `R_RISCV_CALL_PLT` to Lua symbols.  Adding `-no-pie`
to the cart link line resolved both.  In production every cart will need
this flag — the build tooling must not let `-pie` slip in.

**2. Library order on the cart link line.** `-llua54` was listed before the
cart `.o` files.  GNU `ld` resolves left to right, so the linker scanned
`liblua54.so` before any reference existed and discarded it.  Moving
`-llua54` to the end of the command line fixed the unresolved-Lua-symbol
errors.  Standard linker hygiene; documented here only because it cost a
build cycle.

**3. `__udivdi3` (and friends).** Lua 5.4 emits 64-bit / 32-bit divides as
calls to libgcc helpers (`__udivdi3`, `__umoddi3`, `__divdi3`, `__moddi3`).
Debian's `gcc-13-riscv64-linux-gnu` ships only `elf64-littleriscv` libgcc
multilib — there is no rv32 build, and linking the rv64 archive fails with
`ABI is incompatible`.  Resolved by writing the four divmod helpers
directly in `lib/lua_lib_runtime.c` (shift-subtract long division).  This
is fine for the spike; production will need either a built-from-source
multilib toolchain, an LLVM `compiler-rt` rv32 build, or a dedicated
`libfcrt.rv32.so` shipping the same handful of routines.

**4. `R_RISCV_32` against `__global_pointer$` on the cart.** The first
successful run printed `OK` but also a warning: `fc32_dynload: unhandled
cart rela type 1`.  `readelf -r` showed a single cart-side `R_RISCV_32` at
`0x11ff4` writing the absolute value of `__global_pointer$` into a GOT
slot.  The cart's `crt0.S` loads `gp` directly via `la gp,
__global_pointer$`, so the slot is unread and the warning was cosmetic.
The loader now handles `R_RISCV_32` for cart relocations: it looks up the
referenced symbol (cart-internal symbols use `st_value`, library-supplied
ones go through `fc32_sym_find`) and writes `value + addend`.  Re-running
produces just `OK`.

Two `-Werror` issues in the patched `src/elf.c` (a set-but-unused variable
and an unchecked `fread` return) also surfaced and were fixed inline.

## Answers to the open questions in PLAN.md

**1. Global pointer (`$gp`).** Non-issue, as predicted.  `-fPIC` causes the
`.so` to access its own globals through the GOT; the cart's `gp` value is
never read by Lua.  No special handling required in the loader.

**2. `setjmp`/`longjmp` across the PLT.** Works.  GCC `__builtin_setjmp` /
`__builtin_longjmp` inline their register save/restore and do not go
through the PLT, so error handling inside the `.so` is self-contained.
The cart now positively exercises this path: it calls `lua_pcall` with
a script that runs `error('boom')`.  `pcall` returns `LUA_ERRRUN`, the
error frame is unwound inside the `.so`, and control returns cleanly to
cart code across the PLT.

**3. Relocation coverage.** Audit (`readelf -r` on both binaries):
- `liblua54.so`: `R_RISCV_RELATIVE`, `R_RISCV_32`, `R_RISCV_JUMP_SLOT`.
- `spike_c_cart.elf`: `R_RISCV_32` (one entry, `__global_pointer$`),
  `R_RISCV_JUMP_SLOT` (one per Lua symbol called).

No `R_RISCV_GLOB_DAT` and no `R_RISCV_COPY` (which would be a sign of
something wrong on RISC-V).  All types observed are handled by
`fc32_dynload`; running the cart produces no "unhandled rela type"
warnings on stderr.

**4. Freestanding libc scope.** The self-contained allocator works.  Lua
allocates only via the cart-supplied `l_alloc` callback, so no pointer
crosses the cart/library heap boundary.  The `.so`'s internal BSS heap is
unused for the test (Lua doesn't call back into its own `malloc` for state
storage).  Production still needs a shared `libfcrt.rv32` heap; this spike
sidesteps that intentionally.

---

## What this spike decides

- `riscv64-linux-gnu-gcc` does produce a valid freestanding `ET_DYN`
  RV32IMFC shared object from Lua's sources, given a small set of locally
  supplied libgcc helpers (the rv32 multilib gap).
- rv32emu's ELF loader can be extended via a Python patch to pre-map a
  `.so` and fix up cart PLT/GOT before execution, without modifying the
  submodule.  The patch covers four files (`main.c`, `elf.h`, `elf.c`,
  `riscv.c`) and adds roughly 250 lines of C.
- The PLT call path from cart code into the `.so` works end-to-end under
  the freestanding RV32IMFC `ilp32f` calling convention; calls into
  `lua_newstate`, `luaL_loadstring`, `lua_pcallk`, `lua_tointegerx`, and
  `lua_close` all return correctly with the cart's allocator callback
  invoked across the PLT boundary.
- GP-relative addressing in the `.so` is, as expected, a non-issue with
  `-fPIC`.

## What this spike does not decide

- **Performance.** No timing harness; no benchmarks.  Spike B already answers
  the throughput question.
- **Production allocator strategy.** The `.so` carries its own heap; cart and
  library have separate allocators.  Production will put both on a shared
  `libfcrt.rv32.so` heap.  Not a spike-C concern.
- **Symbol versioning** / `relro` / lazy vs. eager binding.  All production
  concerns; skipped here.
- **rv32 libgcc multilib.** The four divmod helpers added to
  `lua_lib_runtime.c` are a workaround, not a position.  Production needs
  a real rv32 multilib (built from source GCC, LLVM `compiler-rt`, or a
  dedicated `libfcrt.rv32.so`).  Spike C did not pick a strategy.

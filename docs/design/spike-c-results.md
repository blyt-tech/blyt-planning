# Spike C results — Lua 5.4 as a host-provided shared library

**Status: implementation complete; `make docker-build` not yet run.  The
build will confirm whether the toolchain produces a valid freestanding
ET_DYN RV32IMFC shared object and whether the rv32emu dynamic-loader
extension resolves PLT/GOT entries correctly enough to execute
`luaL_dostring("return 1 + 1")` end-to-end.**

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
2. Calls `luaL_dostring(L, "return 1 + 1")` — trivial script execution via PLT
3. Asserts the top-of-stack integer equals 2
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

## Key open questions going into the first build

The PLAN.md identified four open questions.  The first build will answer or
partially answer each:

**1. Global pointer (`$gp`) initialisation.** The cart's `crt0.S` sets `gp`
to the cart's own `__global_pointer$` before `main()`.  The `.so` is compiled
with `-fPIC`, which suppresses GP-relative relaxations in shared library code
— so the `.so` accesses its own globals via the GOT, not via GP.  On paper,
the cart's `gp` value is irrelevant to `.so` execution.  The first build will
confirm this: if GP were wrong for the `.so`, Lua's table/string internal
globals would be silently corrupted and `luaL_dostring` would crash or return
garbage before `OK` could be printed.

**2. `setjmp`/`longjmp` across the PLT.** The `.so` uses `__builtin_setjmp`
/ `__builtin_longjmp` (via `lib/include/setjmp.h`).  GCC builtins are always
position-independent and do not go through the PLT — they inline their
register save/restore.  Lua's error-handling paths (protected calls,
`lua_pcall`, `luaL_dostring`) depend on these working.  The test exercises
the error path only indirectly (a successful script returns `LUA_OK`), but a
crash inside the VM before returning would catch a completely broken setjmp.

**3. Relocation coverage.** The implemented types are `R_RISCV_RELATIVE`,
`R_RISCV_32`, `R_RISCV_JUMP_SLOT`, and `R_RISCV_GLOB_DAT`.  The first build
should audit `riscv64-linux-gnu-readelf -r liblua54.so` and `readelf -r
spike_c_cart.elf` against this list.  If any new relocation type appears in
the output, `fc32_dynload` will print an "unhandled rela type" warning to
stderr; any such warning is a follow-up to investigate.

**4. Freestanding libc scope in the `.so`.** The self-contained allocator and
libc stubs mean the `.so` carries its own 8 MiB BSS heap.  The cart also has
a 4 MiB BSS heap for `l_alloc`.  These are distinct; no pointer crosses the
boundary.  The correctness risk is not double-free (pointers don't cross) but
rather `.so` startup code calling `malloc` before the cart's `l_alloc` is
installed — which Lua does not do (`lua_newstate` is the first allocation, and
it calls `l_alloc` immediately).

---

## What this spike decides (pending first successful run)

- Whether `riscv64-linux-gnu-gcc` can produce a valid freestanding `ET_DYN`
  RV32IMFC shared object from Lua's sources.
- Whether rv32emu's ELF loader can be extended via a Python patch to pre-map a
  `.so` and fix up PLT/GOT entries without modifying the submodule.
- Whether the PLT call path from cart code into the `.so` works end-to-end
  under the freestanding RV32IMFC calling convention.
- Whether GP-relative addressing in the `.so` is compatible with rv32emu's
  register initialisation (expected: it is a non-issue with `-fPIC`).

## What this spike does not decide

- **Performance.** No timing harness; no benchmarks.  Spike B already answers
  the throughput question.
- **Production allocator strategy.** The `.so` carries its own heap; cart and
  library have separate allocators.  Production will put both on a shared
  `libfcrt.rv32.so` heap.  Not a spike-C concern.
- **Symbol versioning** / `relro` / lazy vs. eager binding.  All production
  concerns; skipped here.

---

## Fallback plan

If `fc32_dynload` proves too invasive to wire into rv32emu correctly (e.g. the
ELF loader's internal state is too tightly coupled to extend cleanly), the
fallback is a standalone Python pre-linker that merges `liblua54.so` and
`spike_c_cart.elf` into a single static ELF that rv32emu loads unchanged.
This would still prove the build toolchain and relocation mechanics; it just
moves the linking step to build time rather than runtime.  Document which
path was used once the first build runs.

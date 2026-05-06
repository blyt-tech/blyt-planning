# Spike C — Lua as a host-provided shared library

**Question:** Can Lua 5.4 be built as a versioned RV32IMFC shared library,
pre-loaded into a cart's VM address space, and called from cart code via
direct in-VM function calls (not ECALLs)?

**Dependency:** Spike A (rv32emu binary, RISC-V cross-toolchain, Docker build
environment, freestanding C runtime stubs).

**Success criterion:** A minimal cart ELF with `DT_NEEDED: liblua54.so` calls
`lua_newstate()`, executes a trivial script via `luaL_dostring()`, asserts the
correct return value, and exits cleanly. `liblua54.so` is a distinct ELF mapped
at a guest address range separate from the cart. No correctness beyond basic
function is required — this spike is about architectural feasibility, not
performance.

---

## Why this is a risk

ADR-0025 places the Lua interpreter inside the RISC-V sandbox as a versioned
shared library (`liblua.rv32.so`) rather than statically linking Lua into each
cart. This keeps the runtime in control of the Lua version and gives all carts
an identical execution model. Three things must work together that have not been
combined this way before:

1. **Build toolchain**: `riscv64-linux-gnu-gcc` must produce a valid RV32IMFC
   `ET_DYN` shared object from Lua's C sources in a freestanding environment
   (no glibc, no ld-linux).
2. **Dynamic loader in rv32emu**: the interpreter must be extended to parse
   `PT_DYNAMIC`, locate and map the .so into guest memory, and fix up the cart's
   GOT/PLT before execution begins. rv32emu currently only loads static ELFs.
3. **PLT call path**: the cart calls `lua_newstate` etc. via the PLT; the GOT
   entry must resolve to the correct address in the .so's mapped range. RISC-V's
   PC-relative and GP-relative addressing modes add subtleties absent on x86.

---

## Key open questions going in

1. **Global pointer (`$gp`) initialisation.** The RISC-V psABI uses a
   global pointer register for relaxed GOT access. rv32emu's ELF loader
   initialises `$gp` for the main executable; it is not clear whether it will
   do so correctly for a dynamically-linked executable and a separately-mapped
   .so. If `$gp` is wrong for the .so region, Lua's accesses to its own global
   variables will silently read garbage. This is the single most plausible
   invisible failure mode.

2. **`setjmp`/`longjmp` across the PLT.** Lua relies heavily on `setjmp` for
   error handling. In a shared-library build the `setjmp` call will resolve
   through the PLT; the implementation must correctly save and restore all
   callee-saved registers under the RV32IMFC calling convention. Our existing
   freestanding `setjmp` (GCC builtins) was tested only in a static context.

3. **Relocation coverage.** GCC on RV32IMFC emits a specific set of relocation
   types for shared objects: `R_RISCV_RELATIVE`, `R_RISCV_JUMP_SLOT`,
   `R_RISCV_GLOB_DAT`, and potentially `R_RISCV_32`. The dynamic loader must
   handle all types that actually appear — audit the output of `readelf -r` on
   the built .so before implementing.

4. **Freestanding libc scope in the .so.** Lua needs `malloc`/`free`/
   `realloc`, `setjmp`/`longjmp`, `snprintf`, string primitives, and
   `localeconv`. Spike B linked these stubs into each cart ELF. For the .so the
   simplest approach is to link the same stubs *into* the library, making it
   self-contained. See the decision below.

---

## Decisions

### Freestanding libc strategy: self-contained .so

Spike B's `lua_runtime.c` provides the malloc, setjmp, snprintf, and string
stubs that Lua needs. For this spike, compile those stubs with `-fPIC` and link
them directly into `liblua54.so`.

This means the .so carries its own allocator. In production that would be
wrong — cart and library would have separate heaps, and objects crossing the
boundary would be double-freed. For this spike the cart does not allocate
anything itself; the correctness goal is only that `lua_newstate`, `luaL_dostring`,
and `lua_close` work. The self-contained approach is the fastest path to
answering the architectural question without introducing shared-allocator
complexity as a confound.

Document the production follow-up: the runtime SDK will define the allocator
in a separate `libfcrt.rv32.so` (or bake it into a thin shim layer) so that
both the cart and all SDK libraries share one heap. Spike C does not need to
solve this.

### Dynamic loader strategy: extend rv32emu via patch

Spike A extended rv32emu via a Python patch script (`apply-mips-cap.py`) that
adds C code to the rv32emu source at Docker build time without modifying the
submodule. Spike C uses the same pattern: a `apply-dynamic-loader.py` patch
that adds a minimal dynamic loader to rv32emu.

An alternative — a standalone Python pre-linker that merges the .so into the
cart ELF and produces a static binary — would avoid rv32emu surgery but would
produce a result that doesn't reflect the production architecture (where the
runtime, not a build-time tool, does the mapping). The rv32emu extension is
the architecturally honest path.

The loader is invoked by a new `--libpath <dir>` flag. When rv32emu sees a
dynamic executable it reads `DT_NEEDED`, finds the named libraries in the
lib path, and loads them before starting execution.

### Load addresses

| Region            | Guest address   | Rationale                          |
|-------------------|-----------------|------------------------------------|
| Cart ELF          | `0x00010000`    | rv32emu default for main ELF       |
| `liblua54.so`     | `0x40000000`    | Above the top of any plausible cart |
| Stack             | `0x7ffff000`    | rv32emu default                    |

These are fixed for the spike. A production loader would use ASLR or at least
a configurable base; spike C does not need either.

---

## What to build

### 1. `liblua54.so` — RV32IMFC shared library

**Sources:** Lua 5.4.7 (same version as Spike B). Apply the same `LUA_32BITS`
patch to `luaconf.h`. Include the same subset of Lua libraries (base, math,
string, table) via a spike-c `lua_init_libs.c`.

**Compile flags:**
```
-march=rv32imfc_zicsr -mabi=ilp32f
-O2 -fPIC
-ffreestanding -nostdlib -fno-stack-protector -fno-common
```

**Freestanding stubs:** compile `lua_runtime.c` (from Spike B) with the same
flags plus `-fPIC`. This provides `malloc`, `free`, `realloc`, `setjmp`,
`longjmp`, `snprintf`, `vsnprintf`, `strlen`, `strcmp`, `memset`, `memcpy`,
`memmove`, `localeconv`, and `clock_gettime` via ECALL 403.

**Link:**
```
riscv64-linux-gnu-gcc -shared -fPIC -nostdlib \
    -Wl,-soname,liblua54.so \
    -o liblua54.so \
    lua_*.o lmathlib.o lstrlib.o ltablib.o lua_runtime.o lua_init_libs.o \
    softfloat.a
```

**Verify:**
```sh
riscv64-linux-gnu-readelf -h liblua54.so   # Type: DYN, Class: ELF32, Machine: RISC-V
riscv64-linux-gnu-readelf -d liblua54.so   # DT_SONAME = liblua54.so
riscv64-linux-gnu-readelf -s liblua54.so   # lua_newstate, luaL_dostring, lua_close exported
riscv64-linux-gnu-readelf -r liblua54.so   # audit relocation types actually present
```

Capture the `readelf -r` output and use it to drive the relocation type list
implemented in step 3.

### 2. `spike_c_cart.elf` — minimal dynamic cart

**`crt0.S`:** identical to Spike B's; the cart entry point calls `main()` and
exits via ECALL 93.

**`spike_c_cart.c`:**
```c
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

static void *l_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud; (void)osize;
    if (nsize == 0) { free(ptr); return NULL; }
    return realloc(ptr, nsize);
}

int main(void) {
    lua_State *L = lua_newstate(l_alloc, NULL);
    if (!L) { write(1, "FAIL: lua_newstate\n", 19); return 1; }

    int rc = luaL_dostring(L, "return 1 + 1");
    if (rc != LUA_OK) { write(1, "FAIL: luaL_dostring\n", 20); return 1; }

    lua_Integer result = lua_tointeger(L, -1);
    if (result != 2) { write(1, "FAIL: wrong result\n", 19); return 1; }

    lua_close(L);
    write(1, "OK\n", 3);
    return 0;
}
```

Note: `write`, `free`, `realloc` are the cart's own stubs (a minimal cart
runtime, *not* from `liblua54.so`). The cart has its own tiny allocator for
the `l_alloc` callback; Lua will call back into this via the function pointer,
so no PLT boundary is crossed for allocation.

**Compile:**
```sh
riscv64-linux-gnu-gcc -march=rv32imfc_zicsr -mabi=ilp32f \
    -O2 -ffreestanding -nostdlib -fno-stack-protector \
    -I/spike-b/lua/src \
    -o spike_c_cart.elf \
    crt0.o cart_runtime.o spike_c_cart.o \
    -L. -llua54 \
    -Wl,-rpath-link,. \
    -Wl,--dynamic-linker,/no/interp
```

`-Wl,--dynamic-linker,/no/interp` suppresses the default interpreter path so
rv32emu doesn't try to hand off to a non-existent `ld-linux-riscv32.so.1`.

**Verify:**
```sh
riscv64-linux-gnu-readelf -h spike_c_cart.elf   # Type: EXEC or DYN, EM_RISCV
riscv64-linux-gnu-readelf -d spike_c_cart.elf   # DT_NEEDED: liblua54.so
riscv64-linux-gnu-readelf -r spike_c_cart.elf   # PLT entries for lua_newstate etc.
```

### 3. rv32emu dynamic loader patch

A Python patch script `patches/apply-dynamic-loader.py` extends rv32emu's
`src/elf.c` (or equivalent ELF loader file in the rv32emu version checked out)
with a minimal `elf_load_dynamic()` function. The patch is applied at Docker
build time, mirroring the spike-a approach.

**Loader algorithm:**

```
elf_load_dynamic(rv_state, cart_elf_path, lib_dir):
  1. Load cart ELF as normal (rv32emu already does this for the segments).
  2. Walk cart's PT_DYNAMIC to extract:
     - DT_NEEDED names  → list of required libraries
     - DT_JMPREL        → cart's PLT relocation table
     - DT_PLTRELZ       → size of PLT relocation table
     - DT_RELA / DT_RELASZ → cart's general relocation table
     - DT_SYMTAB, DT_STRTAB → cart's dynamic symbol table
  3. For each DT_NEEDED name:
     a. Open lib_dir/<name> as an ELF shared object.
     b. Map PT_LOAD segments into guest memory at LIB_LOAD_ADDR (0x40000000),
        honouring p_align.
     c. Build a symbol table: {name → (load_base + st_value)} for all
        STB_GLOBAL entries in the .so's .dynsym.
  4. Apply relocations from the .so's .rela.dyn and .rela.plt:
     - R_RISCV_RELATIVE:  guest_mem[r_offset] = load_base + r_addend
     - R_RISCV_32:        guest_mem[r_offset] = load_base + sym.value + r_addend
  5. Apply relocations from the cart's PLT relocation table:
     - R_RISCV_JUMP_SLOT: guest_mem[r_offset] = sym_table[sym_name]
     - R_RISCV_GLOB_DAT:  guest_mem[r_offset] = sym_table[sym_name]
  6. Set GP ($x3) to the .so's __global_pointer$ symbol value (if present).
     If the cart also uses GP-relative addressing, initialise it to whichever
     region's GP the main ELF declares — most likely the loader will need to
     set GP twice (once for cart startup, once when control first enters the .so).
     In practice: if Lua's .so declares __global_pointer$, set $gp to that
     value before the first PLT call. Since the cart itself is tiny and doesn't
     use GP-relative globals, one GP value (the .so's) suffices for the spike.
```

**Relocation types to implement:** determined by the `readelf -r` audit in
step 1. Expect at minimum `R_RISCV_RELATIVE` and `R_RISCV_JUMP_SLOT`. If
`R_RISCV_COPY` appears, something is wrong — `R_RISCV_COPY` should not appear
in .so → executable relocations for RISC-V; investigate if seen.

**`--libpath` flag:** add a command-line option so the spike can be invoked as:
```sh
rv32emu --libpath /spike-c/lib spike_c_cart.elf
```

If the main ELF is `ET_EXEC` with no `PT_DYNAMIC`, the loader code path is
skipped entirely — existing static ELF behaviour is unchanged.

### 4. Test harness

A single Make target runs the end-to-end test:

```sh
make docker-run
# expected: "OK" on stdout, exit status 0
```

```makefile
docker-run: docker-build
	docker run --rm --platform linux/arm64 $(DOCKER_IMAGE) \
	    /spike-a/rv32emu/build/rv32emu --libpath /spike-c/lib \
	    /spike-c/cart/spike_c_cart.elf
```

No timing harness. This spike measures feasibility, not performance.

---

## Build environment

Same Docker pattern as Spikes A and B.

```
FROM fc32-spike-a AS builder

# Lua 5.4.7 (same as spike-b)
RUN wget lua-5.4.7.tar.gz && ...patch LUA_32BITS...

# Build liblua54.so
COPY lib/ lib/
RUN make -C lib all

# Build cart
COPY cart/ cart/
RUN make -C cart all

# Patch rv32emu with dynamic loader
COPY patches/ patches/
RUN python3 patches/apply-dynamic-loader.py /spike-a/rv32emu/src/
RUN make -C /spike-a/rv32emu OUT=/spike-a/rv32emu/build -j$(nproc)

FROM fc32-spike-a
COPY --from=builder /spike-c/lib/liblua54.so   /spike-c/lib/
COPY --from=builder /spike-c/cart/spike_c_cart.elf /spike-c/cart/
```

### Directory layout

```
spikes/spike-c/
├── PLAN.md                        (this file)
├── Dockerfile
├── Makefile
├── patches/
│   └── apply-dynamic-loader.py   # extends rv32emu ELF loader
├── lib/
│   ├── Makefile                  # builds liblua54.so
│   ├── lua_init_libs.c           # open only base/math/string/table
│   └── lua_lib_runtime.c         # freestanding stubs (-fPIC copy of spike-b's)
└── cart/
    ├── Makefile                  # builds spike_c_cart.elf
    ├── crt0.S                    # cart entry (minimal, from spike-b)
    ├── cart_runtime.c            # cart's own write/free/realloc stubs
    └── spike_c_cart.c            # lua_newstate / luaL_dostring / lua_close
```

---

## What this spike decides

- Whether `riscv64-linux-gnu-gcc` can produce a valid freestanding `ET_DYN`
  RV32IMFC shared object from Lua's sources.
- Whether rv32emu can be extended (via a host-side patch) to pre-map the .so
  and fix up PLT/GOT before cart execution begins.
- Whether the PLT call path from cart code into the .so works end-to-end under
  the freestanding RV32IMFC calling convention.
- Whether GP-relative addressing in the .so is compatible with rv32emu's
  register initialisation.

## What this spike does not decide

- **Performance.** Spike B already answers whether Lua-in-interpreter is fast
  enough; Spike C does not re-run any benchmarks.
- **Security model.** ADR-0025 and ADR-0038 describe the two-layer sandbox.
  Spike C's .so has unrestricted access to the freestanding runtime; the actual
  environment sandbox (`luaL_openlibs` stripping) is a production concern.
- **Production allocator strategy.** The self-contained allocator in
  `liblua54.so` is a spike shortcut. Production will need a shared `libfcrt.rv32`
  so cart and library share one heap.
- **Symbol versioning.** `liblua.rv32.so` will be versioned in production
  (allowing multiple Lua versions side-by-side). The spike uses a fixed
  SONAME with no GNU versioning.
- **`relro` / lazy vs. eager binding.** Use `-Wl,-z,now` in production to
  resolve all PLT entries at load time (required for deterministic startup);
  the spike may skip this.

---

## Fallback plan

If the rv32emu patch approach turns out to require too much surgery (e.g., the
ELF loader is tightly coupled to rv32emu's internal state), the fallback is a
standalone Python pre-linker:

1. The pre-linker reads `liblua54.so` and `spike_c_cart.elf`, applies all
   relocations into a merged memory image, and writes a flat binary (with a
   thin ELF header) that rv32emu treats as a static executable.
2. The combined binary is handed to the unmodified rv32emu.
3. The spike still proves that the build toolchain works and that the
   PLT/GOT mechanics are correct; it just moves the linker step from runtime
   to build time.

The fallback is explicitly less production-realistic (the runtime cannot do
build-time linking), but it answers the feasibility question and unblocks
further work. Document which approach was used in the results file.

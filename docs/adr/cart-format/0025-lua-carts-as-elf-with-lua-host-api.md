# ADR-0025: Lua carts use a runtime-provided variant Lua library

## Status
Accepted — performance assumption unvalidated (see Consequences)

## Context

Lua carts need to run Lua code. Three approaches were considered:

**Bundle a Lua VM in every cart.** Rejected: adds ~200 KB to every cart,
prevents the runtime from owning the interpreter version, and gives each
cart its own isolated VM with no shared state.

**Embed Lua natively in the runtime host process.** This was the original
decision. The `lua_State` would live in the runtime's native address space,
giving the host full access to Lua's C debug API (`lua_sethook`,
`lua_getinfo`, `lua_getlocal`, etc.) for a first-class DAP debugger. However,
it treats Lua as a special privileged implementation language with a different
execution and security model from native carts, and requires the host process
to embed and maintain a C library dependency.

**Provide a runtime library that owns Lua entirely.** The runtime ships
`libblyt32lua.so`, a versioned RV32IMFC shared library that exports the
standard cart entry points (`init`, `update`, `draw`, etc.) and that
delegates the Lua lifecycle — VM creation, environment configuration,
bytecode loading, callback dispatch — to the variant-portable Lua engine
core (`libblytcommonlua.so`; see "Engine core / variant binding split"
below). The Lua interpreter itself (`liblua54.so`) is a private
dependency of the engine core, not exposed to the cart. A Lua cart
binary contains no RISC-V code; it is a pure data container.

An earlier variant of this approach had the cart binary contain a small
generated boilerplate ELF (the "Lua shim") that called the Lua C API
directly, with the cart linking against the Lua library as `liblua.rv32.so`.
This was superseded: the boilerplate belongs in `libblyt32lua.so`, not in
the cart, because the cart has no reason to know about the Lua C API.

## Decision

**Lua is just another implementation language.** Each variant ships a
matching Lua library — `libblyt32lua.so` for Blyt32, `libblyttylua.so`
for BlyTTY, `libblyt3dlua.so` for Blyt3D — as a versioned RV32IMFC
shared library; everything runs inside the VM sandbox.

**Engine core / variant binding split.** Each variant Lua library is a
thin shim. The variant-portable engine core — VM lifecycle, sandbox,
bytecode loading, the Lua C API, and the cart entry-point dispatch —
lives in `libblytcommonlua.so`, compiled once against the shared
umbrella `blyt.h`. The variant Lua libraries (`libblyt32lua.so` etc.)
re-export the engine-core symbols and add the variant-specific binding
table that registers Lua names against C symbols (e.g. `libblyt32lua`
binds `blyt32.gfx.blit` against `blyt_gfx_blit` from `libblyt32.so`).
Cart-facing semantics are unchanged: a Lua cart still declares exactly
one variant Lua library in `DT_NEEDED`. Examples below use
`libblyt32lua.so` as the running case; the other variants follow the
same pattern with their own variant-specific Lua namespace surface
(see ADR-0105).

### Execution model

A Lua cart declares `DT_NEEDED: libblyt32lua.so` (in addition to
`libblyt32.so`; see ADR-0024). `libblyt32lua.so` exports the cart entry
point symbols (`init`, `update`, `draw`, and optionally `on_save`,
`on_load`, `cleanup`, `on_credits`, `on_quit`; see ADR-0087). The cart
binary itself contains no RISC-V code — it is an ELF carrying only the
console-format data sections (`.cart.info`, `.cart.config`, `.cart.lua`,
`.cart.resources`).

When the runtime calls `init()` on a Lua cart, `libblyt32lua.so`:

1. Creates a `lua_State` via `lua_newstate()` — an internal call to its own
   embedded Lua dependency.
2. Configures the environment sandbox (strips `os`, `io`, etc.; see ADR-0079).
3. Loads cart bytecode from the `.cart.resources` section via the console
   resource API.
4. Calls the Lua `init()` function via `lua_pcall`.

Subsequent `update()` and `draw()` calls are forwarded to the corresponding
Lua functions. The `lua_State` is owned by `libblyt32lua.so` for the
lifetime of the cart session.

All Lua C API calls are internal to the Lua engine core
(`libblytcommonlua.so`, re-exported by `libblyt32lua.so`) — no ECALL
boundary. Console API calls from Lua go through the normal
`libblyt32.so` path, identical to native carts.

### Debugging

The Lua engine core includes an optional DAP server component, active in
dev builds, that runs inside the VM with direct access to `lua_State`. It
ships in `libblytcommonlua.so` (the engine core is variant-portable, and
debugging Lua state has nothing to do with graphics or input). Carts
reach it transparently via `libblyt32lua.so`:

```
VS Code (DAP client) ←→ Runtime (TCP passthrough) ←→ [VM: engine-core DAP server]
```

The DAP server calls `lua_sethook`/`lua_getinfo`/`lua_getlocal` directly.
The runtime exposes two dev-mode console API functions as the passthrough
channel:

```c
blyt_result_t blyt_debug_read(uint8_t *buf, int32_t n, int32_t *out_read);
blyt_result_t blyt_debug_write(const uint8_t *buf, int32_t n);
```

The runtime is entirely agnostic to the DAP protocol — it forwards bytes.
The same functions are available to native carts for any debug protocol
they choose to implement (e.g., a gdb stub). In release builds the DAP
component is compiled out of the engine core;
`blyt_debug_read`/`blyt_debug_write` are no-ops.

### C bindings in hybrid carts

A cart can mix Lua scripting with compiled C code — for example,
computationally intensive routines in C called from Lua via the Lua C API.
The cart binary carries both:

- Named `.text.mylib` sections (or equivalent) for C library code, statically
  linked into the cart. Placing library code in named `.text.*` sections is a
  convention for incremental builds: library sections are stable across
  game-logic or Lua source changes and need not be relinked when only those
  change. The loader treats all `.text.*` sections as executable; the naming
  is the packer's and developer's concern.
- Lua bytecode in `.cart.resources`, as in a pure Lua cart.
- `DT_NEEDED: libblyt32.so libblyt32lua.so` — no additional dependencies.

**The `cart_lua_modules` hook.** The cart exports the symbol:

```c
void cart_lua_modules(lua_State *L);
```

`libblyt32lua.so` declares a weak reference to this symbol. After creating
and sandboxing the Lua state (step 2 above) and before loading Lua bytecode
(step 3), `libblyt32lua.so` calls `cart_lua_modules(L)` if the symbol is
non-NULL. The function registers C modules with the Lua state — typically
via `package.preload` for lazy loading:

```c
void cart_lua_modules(lua_State *L) {
    luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
    lua_pushcfunction(L, luaopen_mylib);
    lua_setfield(L, -2, "mylib");
    lua_pop(L, 1);
}
```

Lua code then does `local mylib = require("mylib")`. A pure Lua data cart
does not export `cart_lua_modules`; `libblyt32lua.so` skips the hook and
proceeds directly to bytecode loading.

**Lua C API availability.** Cart C binding code uses the Lua C API
(`luaL_newlib`, `lua_push*`, `luaL_check*`, etc.). These symbols
ultimately come from the engine core's private `liblua54.so` dependency,
re-exported up through `libblytcommonlua.so` and then through
`libblyt32lua.so`. From the cart's perspective, the symbols simply
resolve against `libblyt32lua.so` at link time; the cart sees one
provider. The cart's `DT_NEEDED` list therefore remains
`{libblyt32.so, libblyt32lua.so}` only — no separate
`DT_NEEDED: libblytcommonlua.so` or `DT_NEEDED: liblua54.so` is required
or permitted (ADR-0024's packer check rejects unexpected entries). The
engine core controls exactly which Lua API surface is visible to cart C
code; the variant Lua library only adds variant-specific Lua-name
bindings on top.

## Consequences

- Lua carts and native carts share the same execution model, the same
  security boundary, and the same console API path. There is no special Lua
  path in the runtime.
- The runtime has no embedded Lua dependency. Lua is a versioned toolchain
  artifact inside `libblyt32lua.so`, not a host C library.
- A Lua cart binary is a pure data container — no RISC-V code, only ELF
  data sections. The packer never needs to compile or link RISC-V code for
  Lua carts; it only assembles the data sections.
- Hybrid carts (C library + Lua) are supported via the `cart_lua_modules`
  hook; `libblyt32lua.so` re-exports the Lua C API symbols that cart C
  binding code requires, so the cart's `DT_NEEDED` list is unchanged.
- The Lua environment sandbox (stripping `os`, `io`, etc.) is enforced by
  `libblyt32lua.so` before Lua code runs. The host runtime has no
  Lua-specific security logic.
- The debug framework is entirely inside `libblyt32lua.so`. The host
  provides two generic byte-pipe functions; the protocol is the library's
  concern.
- The same passthrough functions serve any implementation language that
  wants a debugger — this is a general mechanism, not Lua-specific.
- **Open performance question:** running Lua inside the RISC-V emulator
  incurs double-interpretation overhead on desktop and WASM targets (on real
  RISC-V hardware there is no penalty). The target game complexity is
  retro-era; modern desktop and WASM hosts likely have sufficient headroom,
  but this assumption must be validated with a benchmark spike before v1
  implementation begins. See benchmarking note below.

### Benchmarking note

Before committing to this architecture, run a representative game-loop
benchmark (128-entity update, ~200 tilemap collision checks, console math
calls) through a RISC-V interpreter and measure against the 16.6 ms frame
budget on:
- Native desktop (baseline)
- RISC-V interpreter on desktop
- RISC-V interpreter in WASM (browser)

If the WASM case fails the budget, the fallback is the host-embedded Lua
architecture (original decision), accepting the asymmetric security and
debugging model it entails.

# ADR-0025: Lua carts use a runtime-provided Lua interpreter compiled to RISC-V

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

**Provide the Lua interpreter as a RISC-V library.** The runtime ships Lua
5.4 (compiled with `LUA_32BITS`) as a versioned RV32IMFC shared library,
pre-loaded in every Lua cart's VM address space. Lua code executes inside
the RISC-V sandbox, identically to native carts. Debugging uses an in-VM
DAP server with host passthrough.

## Decision

**Lua is just another implementation language.** The runtime provides the
Lua interpreter as a versioned RISC-V library; everything runs inside the
VM sandbox.

### Execution model

The runtime pre-loads `liblua.rv32.so` (Lua 5.4, `LUA_32BITS`) into the
cart's VM address space before execution begins. The Lua cart's RISC-V
entrypoint is small generated boilerplate (provided by the SDK):

1. Call `lua_newstate()` — a direct in-VM function call to the Lua library.
2. Configure the environment sandbox (strip `os`, `io`, etc.).
3. Load and execute cart bytecode from the resource section.
4. Forward `init`/`update`/`draw` callbacks into Lua via `lua_pcall`.

All Lua C API calls (`lua_*`) are direct function calls within the VM — no
ECALL boundary. Console API calls from Lua go through the normal ECALL
mechanism, identical to native carts.

### Debugging

The Lua cart links against a small SDK-provided debug framework library
(`libcartdebug.rv32`) that, in dev builds, runs a DAP server inside the VM:

```
VS Code (DAP client) ←→ Runtime (TCP passthrough) ←→ [VM: Lua + DAP server]
```

The in-VM DAP server has direct access to `lua_State` and calls
`lua_sethook`/`lua_getinfo`/`lua_getlocal` natively. The runtime exposes
two dev-mode ECALLs as the passthrough channel:

```c
fc_result_t fc_debug_read(uint8_t *buf, int32_t n, int32_t *out_read);
fc_result_t fc_debug_write(const uint8_t *buf, int32_t n);
```

The runtime is entirely agnostic to the DAP protocol — it forwards bytes.
The same ECALLs are available to native carts for any debug protocol they
choose to implement (e.g., a gdb stub).

In release builds, `libcartdebug.rv32` is omitted from the cart entirely;
`fc_debug_read`/`fc_debug_write` are no-ops.

## Consequences

- Lua carts and native carts share the same execution model, the same
  security boundary, and the same ECALL surface. There is no special Lua
  path in the runtime.
- The runtime has no embedded Lua dependency. Lua is a versioned toolchain
  artifact (like the C standard library), not a host C library.
- The Lua environment sandbox (stripping `os`, `io`, etc.) is enforced by
  the in-VM Lua interpreter, not by the host runtime.
- The debug framework (`libcartdebug.rv32`) is an implementation of the
  DAP protocol in Lua/C running inside the VM. The host provides two
  generic byte-pipe ECALLs; the protocol is the cart's concern.
- The same passthrough ECALLs serve any implementation language that wants
  a debugger — this is a general mechanism, not Lua-specific.
- **Open performance question:** running Lua inside the RISC-V emulator
  incurs interpretation overhead on desktop and WASM targets (on real
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

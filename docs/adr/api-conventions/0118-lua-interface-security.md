# ADR-0118: Lua interface security for hybrid carts

## Status

Accepted

## Context

ADR-0111 defines the Lua+Rust hybrid binding layer. The security
properties of this layer differ significantly between rv32emu targets
and the WASM target, and the relationship between the Layer 1 and Layer 2
sandboxes (ADR-0038) is altered for hybrid carts.

## Decision

### Layer 2 sandbox status for hybrid carts

On rv32emu targets, Lua and the Rust binding code both execute inside the
same RV32IMAFC guest address space. The proc-macro-generated binding glue
calls Lua C API functions — `lua_pushinteger`, `lua_tonumber`,
`luaL_newlib`, etc. — that are exported from `libblyt32lua.so` and
resolved via the guest PLT/GOT. An adversarial cart could also call these
symbols directly, bypassing the `#[lua_export]` mechanism, to manipulate
the Lua VM state arbitrarily.

This means that for the Rust (native) portions of a hybrid cart, the
Layer 2 Lua environment sandbox (ADR-0038) is not a security boundary.
A determined cart author can re-register stripped standard library
functions, load arbitrary Lua bytecode, or manipulate Lua metatables.

**This does not compromise host security.** All eventual host effects —
regardless of how they are triggered within the guest — must cross the
ECALL dispatch table (Layer 1, ADR-0038). Layer 1 is the complete
security perimeter for all cart code on emulated platforms.

**ADR-0038 annotation:** Layer 2 (Lua environment sandbox) applies as a
security boundary only for the scripted Lua portions of a cart. For
hybrid carts, Layer 2 is a correctness and ergonomics property for the
Lua portions, not a security boundary for the native portions. Layer 1
is the complete perimeter for all cart code regardless of language.

### Restricted export surface for `libblyt32lua.so`

Although Layer 1 remains intact even if Layer 2 is bypassed, restricting
the Lua C API symbols exported from `libblyt32lua.so` reduces the
potential for hybrid carts to destabilise the Lua VM in ways that produce
hard-to-diagnose crashes or incorrect behaviour.

**Export:** the minimal set needed by `#[lua_module]`-generated binding
glue:
- Stack manipulation: `lua_gettop`, `lua_settop`, `lua_type`, `lua_typename`
- Value push: `lua_pushinteger`, `lua_pushnumber`, `lua_pushboolean`,
  `lua_pushnil`, `lua_pushstring`, `lua_pushlstring`
- Value read: `lua_tointegerx`, `lua_tonumberx`, `lua_toboolean`,
  `lua_tolstring`
- Error: `lua_error`, `luaL_error`, `luaL_argerror`
- Argument helpers: `luaL_checkinteger`, `luaL_checknumber`, `luaL_checkstring`,
  `luaL_optinteger`, `luaL_optnumber`
- Module registration: `luaL_newlib`, `luaL_setfuncs`, `lua_setfield`,
  `lua_getfield`, `lua_createtable`, `lua_newtable`
- Stack: `lua_settop` (for cleanup)

**Do not export:**
- `luaL_openlibs` — re-enables all stripped standard libraries
- `lua_load`, `luaL_loadbuffer`, `luaL_loadstring`, `luaL_loadfile`,
  `luaL_dofile`, `luaL_dostring` — load or execute arbitrary Lua code
- `luaL_requiref` — opens a named library into the environment
- Any other symbol whose primary purpose is loading, compiling, or
  executing Lua source or bytecode

On the WASM target, `libblyt32lua.so` is a host-side library (not in
guest address space); this export restriction applies only to its
rv32-target build.

### `.lua_exports` host-side validation (WASM target)

On the WASM target the host reads `.lua_exports` at cart load time to
register Lua→Rust trampoline functions. This is adversarial host-side
parsing; it must satisfy the validation requirements specified in
ADR-0112 (ELF load-time security checks, `.lua_exports` section).

### `rv32emu_call_fn` step limit

The call-on-demand mechanism sets guest PC to a function address from
`.lua_exports` and runs until ECALL `0xDEAD`. An adversarial `sym_addr`
pointing at a loop or the sentinel itself must not hang the host. The step
limit and its enforcement are specified in ADR-0115.

### Type constraint enforced at compile time

ADR-0111 constrains `#[lua_export]` function signatures to primitive
types (`f32`, `i32`, `u32`, `bool`). The proc macro enforces this at
compile time and rejects any non-primitive argument or return type. This
constraint eliminates the class of bugs where a pointer into host address
space (e.g. `*mut lua_State`) crosses the host/guest boundary.

## Consequences

- The security claim for hybrid carts is clearly scoped: Layer 1 is the
  perimeter; Layer 2 is not. Cart authors and auditors understand that
  a hybrid cart's Rust code operates below the Lua sandbox.
- Restricting the `libblyt32lua.so` export surface limits the potential
  VM-destabilisation surface without affecting any legitimate use of the
  binding layer.
- The distinction between rv32 targets (Lua and Rust both in guest) and
  the WASM target (Lua in host, Rust in guest) is explicit in the
  security model. The host-side `.lua_exports` parsing on WASM is covered
  by ADR-0112 rather than duplicated here.

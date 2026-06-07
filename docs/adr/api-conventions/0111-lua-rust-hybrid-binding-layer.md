# ADR-0111: Lua–Rust hybrid binding layer and cross-target calling convention

## Status

Accepted

## Context

ADR-0108 makes Rust a first-class cart language alongside Lua. ADR-0039
Layer 2 anticipated that Lua authors would want to drop to native code for
performance-critical paths, and noted that the unified ELF format makes
this natural. With Rust first-class, the Lua+Rust hybrid is a v1 design
concern, not a v2 deferral.

The key complexity is that the Lua+Rust calling path differs between
execution targets:

**rv32emu targets (hardware, Pi, desktop-native):** Lua runs inside the
rv32emu guest alongside the Rust code (both are RV32IMAFC). A Lua→Rust
call is an in-guest function call via the standard Lua C API. The
`cart_lua_modules` mechanism from ADR-0025 applies directly.

**WASM target:** Lua is compiled directly to WASM and runs in the WASM
host process (the host-embedded Lua fallback per ADR-0025, confirmed
necessary by Spike E/F). The Rust code is compiled to RV32IMAFC and runs
inside rv32emu (which is itself compiled to WASM). Lua and Rust are in
different execution contexts: a `lua_State *` pointer is only valid in the
WASM host, not inside the rv32emu guest.

A naïve approach — compile the Lua-callable Rust code to WASM and link
it into the Lua WASM module — is rejected. It removes the RV32IMAFC
sandbox from Rust code on the WASM target entirely and creates a
permanent execution-model divergence between WASM and hardware targets.
The goal is Rust staying inside rv32emu on all targets.

The host already calls named symbols in the rv32emu guest for cart
lifecycle entry points (`blyt_cart_init`, `blyt_cart_update`,
`blyt_cart_draw`). The same host→guest call mechanism can serve
Lua→Rust calls, with argument and return-value marshaling over the
boundary.

This ADR also decides the API granularity question: whether Lua
visibility applies to an entire Rust module or to individually annotated
functions.

## Decision

### Individual function annotation

Lua exports are declared per-function with `#[lua_export]`, not
automatically applied to all public functions in a module.

```rust
#[lua_module]
mod fast_code {
    #[lua_export]
    pub fn fast_noise(x: f32, y: f32) -> f32 { … }

    #[lua_export]
    pub fn batch_update(count: u32) -> u32 { … }

    // internal helper — not exported to Lua
    fn noise_kernel(x: f32, y: f32, freq: f32) -> f32 { … }
}
```

`#[lua_module]` provides the Lua module name (defaults to the Rust module
name; overridable: `#[lua_module = "mylib"]`). `#[lua_export]` opts each
function into the Lua API surface.

**Why individual, not whole-module:**
- Not all `pub fn` signatures can cross the boundary (primitives-only
  constraint; see below). Whole-module export would either silently skip
  incompatible functions or fail loudly for each one, neither of which is
  the right default.
- `pub` in Rust denotes visibility to Rust callers. The Lua API surface
  is a distinct concern — a function may be `pub` for use by other Rust
  modules but not intended for Lua callers.
- The `.lua_exports` ELF section (see below) is an explicit, reviewable
  API surface. Auto-generation from all public functions would make it
  implicit and easy to accidentally expand.

### Type constraints

Arguments and return values must be primitives that can be marshaled
through CPU registers: `f32`, `i32`, `u32`, `bool`. Pointers into guest
memory (e.g. a state buffer address obtained from a console API handle,
passed as a `u32` guest address) are also supported — the guest receives
a 32-bit address valid within its own address space.

What cannot cross: any pointer into host address space — in particular,
`*mut lua_State`. Lua-callable Rust functions have no access to the Lua
VM and do not use the Lua C API. This is by design: the purpose of the
binding layer is pure-computation functions (noise, pathfinding, collision
math, bulk buffer operations). Functions that need to interact with the
Lua VM directly do not belong in the Rust binding layer.

The proc macro enforces these constraints at compile time: a `#[lua_export]`
function with a non-marshallable argument type is a compile error.

### The `.lua_exports` ELF section

The `#[lua_module]` / `#[lua_export]` proc macro emits a `.lua_exports`
named section in the cart ELF. Each entry records the Lua module name,
function name, guest symbol address, and the type signature (argument
types and return types as a compact encoding):

```
.lua_exports section:
  { module: "fast_code", name: "fast_noise",  sym: <addr>, args: [F32, F32], ret: F32 }
  { module: "fast_code", name: "batch_update", sym: <addr>, args: [U32],     ret: U32 }
```

This section is present in all cart builds. On rv32 targets the runtime
ignores it (the `cart_lua_modules` path is used instead); on the WASM
target the host-embedded Lua library reads it at cart load time. The
section is also available to dev-mode tooling for documentation and
binding introspection regardless of target.

### rv32emu target — same-address-space path

On hardware, Pi, and desktop-native targets, Lua and Rust share the rv32
guest address space. The proc macro generates:

1. `extern "C" fn cart_lua_modules(L: *mut lua_State)` — registered via
   weak symbol, called by `libblyt32lua.so` before Lua bytecode loads
   (ADR-0025).
2. Per-function `lua_CFunction` wrappers that call `lua_tonumber` /
   `lua_pushinteger` / etc. on the Lua stack and delegate to the Rust
   function.
3. Module table construction via `luaL_newlib` and registration via
   `package.preload`.

The Lua C API symbols (`lua_tonumber`, `luaL_newlib`, etc.) are available
from `libblyt32lua.so` (re-exported from the engine core per ADR-0025).
The cart's `DT_NEEDED` list is unchanged: `{libblyt32.so, libblyt32lua.so}`.

### WASM target — host-trampoline path

On the WASM target, Lua runs in the WASM host and Rust runs inside
rv32emu. The host-embedded Lua library executes a registration pass over
`.lua_exports` at cart load time. For each entry it registers a typed
trampoline as a Lua C function with the host `lua_State`.

The trampoline for a function with signature `(f32, f32) -> f32`:

1. `lua_tonumber(L, 1)` → `x` (host-side Lua stack read)
2. `lua_tonumber(L, 2)` → `y`
3. Place `x` in guest register `a0`, `y` in `a1`
4. Call the guest symbol via the emulator's host→guest call mechanism
   (the same path used for `blyt_cart_init` / `blyt_cart_update`)
5. Read return value from guest `a0`
6. `lua_pushnumber(L, result)`
7. Return `1`

The host→guest call mechanism already exists: the emulator resolves ELF
symbols at cart load time and exposes a function to call a guest address
with a register context. The per-frame lifecycle callbacks use this path;
Lua→Rust calls are additional uses of the same mechanism.

**Overhead:** one host→guest context switch per Lua→Rust call on the WASM
target. This is the same cost as the per-frame `update` and `draw`
callbacks. For the intended use cases (bulk operations, per-tick expensive
computation) this is acceptable. Per-element calls from inside a Lua loop
— the pattern ADR-0039 identifies as having prohibitive overhead even for
Lua C API calls without the WASM complication — remain inadvisable.

**The `cart_lua_modules` function is not called on the WASM target.**
`libblyt32lua.so` (on rv32 targets) has a weak reference to it; the
WASM-target host-embedded Lua library does not. The `.lua_exports`
section is the registration source of truth on WASM.

### SDK crate and proc macro

The `blyt32` SDK crate provides the `#[lua_module]` and `#[lua_export]`
proc macro attributes as part of its stable surface. The macros are not
target-conditional from the cart author's perspective — the same
annotation generates the correct output for both targets. The dual
output (Lua C API path for rv32, `.lua_exports` section for WASM) is
entirely inside the macro implementation.

### `cart.build.yaml` for Lua+Rust hybrid carts

```yaml
languages:
  lua:
    codegen: true    # packer generates Lua constant modules if needed
  rust:
    codegen: false   # Rust binding modules need no packer-generated constants
                     # unless they also access state buffers directly
```

If the Rust code also reads or writes state buffers by address and needs
packer-generated field constants:

```yaml
languages:
  lua:
    codegen: true
  rust:
    codegen: true    # emit resources.rs, state.rs etc. into OUT_DIR
```

The `DT_NEEDED` list in the resulting cart ELF must include
`libblyt32lua.so` (in addition to `libblyt32.so`) because the cart
exports `cart_lua_modules` for the rv32 target path. The packer validates
this as it does for any Lua cart.

## Amendment (ADR-0118, 2026-05-11)

**Layer 2 scope:** Rust code in a hybrid cart has access to Lua C API
symbols via `libblyt32lua.so` and can manipulate the Lua VM arbitrarily.
The Layer 2 Lua environment sandbox is not a security boundary for the
native portions of a hybrid cart. Layer 1 (ECALL dispatch) is the complete
security perimeter. See ADR-0118 for the full security analysis and the
restricted export surface defined for `libblyt32lua.so`.

**`.lua_exports` validation on WASM target:** The host-side parse of the
`.lua_exports` section must satisfy the structural security checks
specified in ADR-0112, including `sym_addr` validation against executable
LOAD segments and string-field bounds checking.

**`rv32emu_call_fn` step limit:** The call-on-demand mechanism must enforce
a maximum instruction count per invocation to prevent infinite-loop guest
code from hanging the WASM host. See ADR-0115.

## Amendment (ADR-0130, 2026-06-07)

**Type constraints superseded for bridged exports.** The "Type
constraints" section above — in particular the rationale that "functions
that need to interact with the Lua VM directly do not belong in the Rust
binding layer" — is superseded by ADR-0130. The WASM target gains an
ECALL-bridged Lua C API: per-export opt-in (`flags` bit in
`.lua_exports`) wrappers run in the guest against the restricted Lua C
API (ADR-0118 surface), with each operation serviced by the host against
the real `lua_State`. Strings, tables, arbitrary arity, multiple
returns, and `luaL_error` cross the boundary on both targets with the
same wrapper source. Host pointers (including `*mut lua_State`) still
never cross — the guest-side `lua_State *` is an opaque call token.

**Typed path demoted to fast path.** The host-trampoline path described
above (typed conversion, args in guest registers) remains as the fast
path for primitive ≤4-argument signatures and stays the recommendation
for hot per-element calls (ADR-0039). It is no longer the only path.

**`.lua_exports` layout.** `wrap_sym` — previously recorded but unused
by the host — is now resolved and validated (ADR-0112) for bridged
entries. The first padding byte becomes `flags` (bit 0 =
`BLYT_LUA_EXPORT_BRIDGED`); existing carts carry 0 and are unaffected.

## Consequences

- Rust code stays inside the rv32emu sandbox on all targets, including
  WASM. The RV32IMAFC execution model is not bypassed for the WASM target.
- The `#[lua_export]` annotation makes the Lua API surface explicit and
  reviewable. It is not auto-derived from Rust visibility modifiers.
- The type constraint (primitives only) is enforced at compile time by the
  proc macro, not discovered at runtime.
- The `.lua_exports` ELF section is a stable, inspectable record of the
  Lua API surface regardless of target. Dev-mode tooling can use it for
  documentation and binding introspection.
- On rv32 targets, Lua→Rust calls are zero-overhead in-guest function
  calls via the Lua C API. No new runtime mechanism is required.
- On the WASM target, Lua→Rust calls carry a host→guest context-switch
  cost. This is the same cost as the per-frame lifecycle callbacks. Bulk
  operations that amortise this cost over many elements remain the correct
  pattern (consistent with ADR-0039's Layer 1 API shape rules).
- ADR-0039 Layer 2 (inline native code in Lua carts) is promoted to v1
  capability for Rust, not deferred to v2. The mechanism is different from
  ADR-0039's original packer-centric C vision: binding generation is via
  the SDK proc macro, not the packer. The packer is uninvolved in the
  binding layer. (See ADR-0039 amendment.)
- ADR-0025's `cart_lua_modules` mechanism for C hybrid carts is unchanged.
  Rust hybrid carts generate `cart_lua_modules` via the proc macro on rv32
  targets and bypass it on the WASM target in favour of the `.lua_exports`
  registration pass.

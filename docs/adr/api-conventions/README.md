# Cart API Conventions

The shape and rules of the API surface — C conventions (handles, error
returns, naming, two-header split), the universal packer-generated constant
pattern, Lua-specific conventions (version, bytecode, handle objects, module
names), and the performance strategy that informs API shape decisions.

| # | Decision |
|---|----------|
| [0039](0039-lua-performance-strategy.md) | Lua performance strategy — rich native API primitives in v1, inline native deferred |
| [0046](0046-core-api-conventions.md) | Core API conventions — handles, errors, naming, headers, flags |
| [0047](0047-cart-declared-framerate.md) | Cart-declared framerate |
| [0059](0059-packer-generated-constants-general-principle.md) | Packer-generated compile-time constants — the universal name→ID bridge |
| [0066](0066-lua-5-4-specifically.md) | Lua 5.4 specifically (LUA_32BITS, native bitwise operators, _ENV) |
| [0067](0067-lua-release-builds-ship-bytecode.md) | Lua release builds ship bytecode |
| [0068](0068-lua-handles-are-objects.md) | Lua handles are objects with methods |
| [0077](0077-lua-gc-strategy.md) | Lua GC strategy — generational mode, per-tick minor collection, emergency fallback |
| [0079](0079-lua-standard-library-allowlist.md) | Lua standard library — allowlist, removals, and aliases |
| [0084](0084-lua-error-model.md) | Lua error model — three tiers, VM-external panic termination |
| [0085](0085-ecall-calling-convention.md) | ECALL calling convention and number space |
| [0086](0086-api-surface-shape.md) | API surface shape — getter/setter convention and complete module map |

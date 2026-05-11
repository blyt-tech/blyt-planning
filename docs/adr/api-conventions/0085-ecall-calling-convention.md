# ADR-0085: ECALL calling convention and number space

## Status
Accepted (scope revised — ECALL is now the internal mechanism of the
emulator-side `libblyt32.so`, not the cart-facing ABI; see ADR-0024)

## Context

Every cart — regardless of implementation language — communicates with the
runtime through `libblyt32.so` (ADR-0024). On emulated platforms,
`libblyt32.so`'s function bodies issue `ecall` instructions to the
emulator's dispatch table; on native RISC-V hardware they are direct
implementations. This ADR specifies the ECALL convention used internally
by the emulator-side `libblyt32.so`. It is not the cart-facing ABI.

The calling convention and number assignment for ECALLs must be specified
before the emulator-side `libblyt32.so` can be implemented.

Lua is the first and primary scripting language for carts. A scripting
language implementation has two pieces (see ADR-0105):

- An **engine core** — VM lifecycle, sandbox, bytecode loading. Touches
  only shared subsystems (resources, state, lifecycle, etc.) and is
  variant-portable. Compiled once across all variants. For Lua this is
  `libblytcommonlua.so`.
- A set of **bindings** — registers language-level names against C
  symbols. Variant-specific because the C symbols include variant-only
  ones (`blyt_gfx_blit`, `blyt_term_put`). For Lua, the per-variant
  bindings are `libblyt32lua.so`, `libblyttylua.so`, `libblyt3dlua.so`,
  each a thin shim re-exporting the engine core.

Future scripting languages (Python, JavaScript via QuickJS, etc.) follow
the same pattern: one shared engine core, per-variant binding shims. The
ECALL surface is language-neutral; the shim layers above it are
language-specific.

The interface for all callers — native carts, language engine cores, and
language bindings — is the SDK's C wrapper functions:

- **Variant-portable code** (engine cores, cross-variant libraries)
  includes `blyt.h` (the shared umbrella) and calls only `blyt_*`
  symbols guaranteed to exist on every variant.
- **Variant-specific code** (cart code, language bindings) includes the
  variant umbrella (`blyt32.h` / `blytty.h` / `blyt3d.h`) and may also
  call symbols exported only by that variant's library
  (e.g. `blyt_gfx_blit` lives in `libblyt32.so`).

These declarations are resolved at link time against the variant library.
On emulated platforms, the variant library's function bodies issue ECALLs
to the emulator's dispatch table, so the ECALL numbers below are the
emulator-internal contract. On native hardware they are direct
implementations. Neither cart code nor shim code ever handles ECALL
numbers directly; ECALL numbers are an implementation detail of the
emulator-side variant library, not of the shim authoring contract.

## Decision

### Calling convention

Follow the RISC-V Linux syscall ABI:

- `a7`: ECALL number
- `a0`–`a5`: arguments (up to 6 registers)
- `a0`: return value (`blyt_result_t`, a typedef'd `int32_t`; ADR-0046)

This matches RISC-V toolchain expectations and is immediately legible to
anyone familiar with RISC-V Linux development. SDK stubs are thin inline
functions that load `a7`, fill `a0`–`a5`, execute `ecall`, and read `a0`.
Neither cart code nor shim code ever handles the `ecall` instruction
directly; all callers go through the named SDK wrapper functions.

### Argument passing

**≤5 arguments**: passed directly in `a0`–`a4`. `a5` is left available for
an out-parameter pointer when needed.

**>5 arguments**: caller stack-allocates a params struct and passes a pointer
in `a5`. The SDK stub handles struct allocation and field assignment
invisibly; call sites look like ordinary function calls. This follows the
existing pattern for complex calls (ADR-0069: text rendering params struct).
The emulator reads from a fixed-layout struct rather than reaching into
variable-depth guest stack frames.

**Strings**: pointer + length as two adjacent registers, not null-terminated.
The emulator never scans for a null byte. SDK stubs call `strlen` once at
the call site when given a C string literal; the result is passed as the
length register.

**Out-parameters**: a pointer in one of the argument registers; the ECALL
handler writes the result through it. For calls returning `blyt_result_t` plus
one value, this fits comfortably in the register budget alongside the input
arguments.

**Handles**: `uint32_t`, single register (ADR-0046). No special treatment.

### Number space

Mirrors the error-code subsystem grouping (ADR-0046), so both systems are
navigable by the same mental model:

| Range | Subsystem |
|---|---|
| 0 | Reserved / invalid |
| 1–49 | Lifecycle (`quit_ready`, `credits_done`, `last_error`, `log`) |
| 100–199 | Graphics (gfx primitives, image, tilemap, palette, cycles, screen shake) |
| 200–299 | Audio (music, voices, groups, speech, volume) |
| 300–399 | Input (buttons, pointer, device info) |
| 400–499 | State and save (buffers, slots, persistence, preferences) |
| 500–599 | Resources (load, unload, ptr, size) |
| 600–699 | Text and localisation (draw, measure, fonts, locale) |
| 700–799 | Math and spatial (deterministic math, noise, collision, tween) |
| 800–899 | Time and RNG |
| 900–999 | Dev / instrumentation (debug read/write, dev assertions) |
| 1000+ | Reserved for v2 additions |

100 slots per subsystem. V1 will use 15–30 per group; remaining slots
accommodate minor-version additions without renumbering.

Specific number assignments within each range are made during implementation
and frozen at that point. They are not speculatively assigned in advance of
the implementation.

### Versioning

ECALL numbers, argument layouts, and semantics are frozen per major API
version (ADR-0078). The runtime maintains a separate dispatch table per
supported major version. A cart's declared `api_version` field (from
`cart.info`) selects its dispatch table for the session. V2 additions either
occupy numbers in the 1000+ range or fill headroom in existing subsystem
ranges.

## Amendment (ADR-0114, 2026-05-11)

ADR-0114 specifies the argument validation policy that every ECALL handler
must implement: range checks on all integers, overflow-safe combined-range
checks, unknown-flags rejection, enum allowlists with default-reject, a
single shared `copy_from_guest` helper for string arguments, and
out-parameter pointer validation. These rules apply uniformly to every
ECALL entry in the number space defined here.

## Consequences

- Any language that compiles to RV32IMAFC uses this convention without
  modification. The engine core for each language (`libblytcommonlua`,
  any future `libblytcommonpy`, `libblytcommonjs`) is variant-portable
  and includes `blyt.h`; per-variant binding shims (`libblyt32lua`,
  `libblyttylua`, etc.) include the variant umbrella and call named
  SDK wrapper functions. ECALL numbers are never hardcoded in shim
  code; they are an implementation detail of the SDK headers.
- The ECALL surface is auditable as a numbered table. ADR-0038's security
  boundary is implementable as a dispatch table lookup — every host-side
  effect maps to exactly one entry.
- Params-struct calling for complex calls slightly increases stub code size
  but keeps the emulator's argument-reading path uniform and safe.
- Specific ECALL number assignments are an implementation task, not a design
  task. The number space structure is what this ADR decides.

## Note on §17 / stale design item

The high-level design document §17 listed "Exact Lua-host API surface: the
~10–15 ECALLs that the cart-side Lua shim wraps" as a pending decision. That
item described the now-superseded host-embedded Lua architecture (the
original approach considered and rejected in ADR-0025), in which Lua C API
calls (`lua_pcall`, `lua_newstate`, etc.) crossed the ECALL boundary.

Under the accepted ADR-0025 architecture (Lua compiled as an RV32IMAFC
library), all Lua C API calls are in-VM function calls with no ECALL
boundary. The ECALL surface is the console API described in this ADR, shared
identically by Lua and native carts.

If Spike B (`docs/design/early-validation-spikes.md`) invalidates ADR-0025
and the fallback host-embedded Lua architecture is adopted, a separate ADR
would cover the ~10–15 Lua-machinery ECALLs (state creation, source loading,
function dispatch, value marshalling). That work is deferred pending spike
results.

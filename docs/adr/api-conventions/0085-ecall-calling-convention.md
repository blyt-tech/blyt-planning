# ADR-0085: ECALL calling convention and number space

## Status
Accepted

## Context

Every cart — regardless of implementation language — communicates with the
runtime exclusively through the RISC-V `ecall` instruction (ADR-0038). This
is the complete security boundary: the only way a cart can cause any
host-side effect. The calling convention and number assignment for ECALLs
must be specified before the runtime can be implemented.

Lua is the first and primary scripting language for carts, and
`libconsolelua` is the first shim library to consume this surface. It is not
the only one: any future scripting language (Python, JavaScript via QuickJS,
etc.) would provide its own shim library. The ECALL surface is
language-neutral; the shim layers above it are language-specific.

The interface for all callers — native carts, Lua shims, and any future
language shim — is the SDK's C wrapper functions in `fc_cart.h`. These are
thin inline stubs that compile directly to ECALL instructions; calling
`fc_image_blit(...)` and manually loading `a7` then executing `ecall` produce
identical machine code. Shim authors include `fc_cart.h` and call the named
functions; they do not work with ECALL numbers directly. ECALL numbers are an
implementation detail of the SDK headers, not part of the shim authoring
contract. This means versioning is automatic: a shim compiled against the v1
SDK header gets v1 ECALL numbers; recompiled against v2, it gets v2 numbers,
with no changes to the shim's own source.

## Decision

### Calling convention

Follow the RISC-V Linux syscall ABI:

- `a7`: ECALL number
- `a0`–`a5`: arguments (up to 6 registers)
- `a0`: return value (`fc_result_t`, a typedef'd `int32_t`; ADR-0046)

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
handler writes the result through it. For calls returning `fc_result_t` plus
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

## Consequences

- Any language that compiles to RV32IMFC uses this convention without
  modification. `libconsolelua` and any future `libconsolepy`,
  `libconsolejs`, or similar shim library each include `fc_cart.h` and
  call named SDK wrapper functions. ECALL numbers are never hardcoded in
  shim code; they are an implementation detail of the SDK headers.
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

Under the accepted ADR-0025 architecture (Lua compiled as an RV32IMFC
library), all Lua C API calls are in-VM function calls with no ECALL
boundary. The ECALL surface is the console API described in this ADR, shared
identically by Lua and native carts.

If Spike B (`docs/design/early-validation-spikes.md`) invalidates ADR-0025
and the fallback host-embedded Lua architecture is adopted, a separate ADR
would cover the ~10–15 Lua-machinery ECALLs (state creation, source loading,
function dispatch, value marshalling). That work is deferred pending spike
results.

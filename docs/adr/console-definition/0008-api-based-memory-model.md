# ADR-0008: Memory model — API-based, not memory-mapped

## Status
Accepted

## Context

Classic fantasy consoles (PICO-8, TIC-80) and classic real hardware expose a
flat memory map: framebuffer at a fixed address, palette at another, input
state at another. This gives a tactile, low-level feel and trivializes save
states (snapshot the whole address space). The alternative is API-based
access, where carts call functions to read/write each subsystem.

A flat memory map was seriously considered because it enables zero-copy
framebuffer access, palette swaps via direct writes, and a simple save state
implementation (memcpy the whole map). The cost is that the entire internal
layout — framebuffer address, palette offset, mixer state, entity storage —
becomes a permanent cart-visible ABI. Any future reorganisation of runtime
internals breaks existing carts. The layout also becomes the security boundary:
carts can read or corrupt any region they can address, not only what the API
permits.

The flat-map benefits are all recoverable through targeted API design without
publishing the layout:
- **Framebuffer throughput:** `acquire_framebuffer()` returns a direct pointer
  to the back buffer. Carts write pixels in hot loops at the same speed as
  a memory-mapped design; there is no per-pixel API overhead.
- **Palette and input:** these are set once per frame or less. Function-call
  overhead for bulk operations (`set_palette`, `get_input_state`) is
  immeasurable.
- **Save state:** walking declared typed regions is equally fast to memcpy'ing
  a fixed address space, and avoids snapshotting runtime-internal state that
  belongs to the runtime, not the cart.

## Decision

**The memory model is API-based, not memory-mapped.**

Budgets:
- Runtime memory: 32 MB total.
- Cart-visible working memory: 16 MB (other 16 MB is runtime overhead —
  decoded resources, framebuffer, mixer, VM state).
- Cart size (on disk): varies by size class (ADR-0030).

Key access patterns:
- **Framebuffer:** `acquire_framebuffer()` / `present_framebuffer()` — cart
  gets a direct pointer to the back buffer for the frame; direct pixel writes
  in hot loops with no per-pixel API overhead.
- **Palette, input, mixer state:** bulk API calls (`set_palette(table)`,
  `get_input_state()`). Infrequent enough that call overhead is irrelevant.
- **Typed buffers (entities, particles):** `alloc_state(layout, count)`
  returns a typed buffer with SOA storage and direct-index access from Lua.

Rejected: flat memory region at a fixed address.

## Consequences

- The runtime is free to evolve its internal memory layout without breaking
  existing carts — the cart-visible contract is the API surface, not a
  memory address.
- The benefits of a memory map (fast framebuffer access, cheap palette swaps,
  trivial save states) are all recoverable through targeted APIs without
  freezing layout details.
- `acquire_framebuffer()` gives carts the same raw write speed as a memory-
  mapped design for the most performance-critical access pattern.
- Save states are implemented by walking tracked typed regions (ADR-0008)
  rather than snapshotting a flat address space — more structured but
  equally fast for typical cart state sizes.
- Carts can query memory usage via `blyt32.mem.stats()`. Allocation failures
  at the cap return nil/null gracefully. Dev tools display "X of 16 MB used"
  continuously.

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

## Amendment (#158, 2026-06-28): the 16 MB budget is enforced

Until #158 the 16 MB cap was a documented intent with no enforcement (no
resident-byte total, no budget check). #158 (resource-memory epic #156, child 3)
makes it real, as the backend for ADR-0029 and the pressure trigger for
ADR-0027's eviction. The enforced model:

- **One unified 16 MB logical budget** spanning the guest heap **and** the
  decompressed resource cache — a deterministic logical counter, decoupled from
  real host/native RSS. An allocation (guest `malloc` **or** a resource
  decompress/`pin`) succeeds **iff**
  `guest_heap_used + non_evictable_footprint + incoming ≤ 16 MB`. Determinism is
  carried by that predicate alone; `resource_cache_used` / residency / LRU order
  are **advisory** and never serialized (ADR-0027 v2).
- **Passive bidirectional shared counters, not a callback.** A guest-visible
  block (host-stamped at fixed guest symbols, exactly like `blytc_arena_base`/
  `blytc_arena_size`) holds `non_evictable_footprint` (host-written, guest-read)
  and `guest_heap_used` (guest-written, host-read). Guest `malloc` decides
  locally; the resource ECALLs check the same predicate host-side. No reclaim
  ECALL. Counters change only at the single-threaded ECALL handoff, so each side
  always reads a current value.
- **Eviction is decoupled from the allocation decision** — `malloc` never
  evicts. Resident cache is bounded where it grows (inside the resource ECALLs,
  evict-before-decompress, LRU-incremental) plus a frame-boundary housekeeping
  sweep.
- **Cooperative limit, not a security boundary.** Guest-side counters can be
  tampered by a misbehaving cart, which only breaks its own determinism; the
  real ceilings are the rv32emu sandbox + physical arena cap (emulated) and the
  native launcher rlimit/seccomp (native). Host invariant: the host treats
  guest-published counters as advisory scalars in comparisons only — never as a
  pointer/length/offset — so a hostile value cannot cause an out-of-bounds host
  access.

### Invariant: the host-Lua fast path must stay 32-bit

`guest_heap_used` must be **byte-identical** across the WASM host-Lua fast path
(wasm32) and the emulated/native rv32 path for the same Lua workload — otherwise
a Lua cart would hit the cap at different points on different legs, breaking
determinism. This holds because the fast path runs as wasm32 (same
`BLYT_LUA_I32_F64` numeric model, same 32-bit pointer width as rv32, so Lua
object sizes match) **and** because #158 single-sources the arena allocator into
`runtime/shared` and runs the identical implementation on **every** leg, making
the accounting structural rather than a per-allocator match.

### The cart heap is one allocator on every leg

#158 routes cart-visible heap (`malloc`/`free`/`realloc`/`calloc`) through the
single `runtime/shared` arena on **all** legs:

- **emulated rv32** `libblytc` (already arena-based),
- **native** `libblytc` — replacing the previous musl-`malloc` delegation
  (`libblytc_native.c` was a stub forwarding to `ld-blyt.so.1`), so a native cart
  and a desktop cart fail an allocation at the identical logical point (required
  for save-state / replay / netplay between a native player and a desktop
  player), and
- the **WASM host-Lua fast path** `lua_Alloc`.

So `guest_heap_used` is identical by construction for **every cart language**, not
just Lua. The **resource cache** uses a *separate* raw allocator (host/musl/mmap)
so its bytes never enter `guest_heap_used` — it is accounted only in the advisory
`resource_cache_used` and, for resident loaded/pinned/persistent entries, in
`non_evictable_footprint`. If a 64-bit desktop host-Lua path is ever added it
must not be on the determinism-bearing path without re-establishing this parity.

# ADR-0008: Memory model — API-based, not memory-mapped

## Status
Accepted — amended 2026-06-28 (#158, budget enforced) and 2026-07-01 (#205,
`acquire` generalized to all surfaces)

> **Forward note (2026-06-29):** spec **#195** generalizes `acquire_framebuffer`
> from the framebuffer to *all* surfaces (the runtime owns every surface buffer,
> reached via `acquire`/`release`). This is now **implemented (#205)** — see the
> amendment at the end of this ADR; the framebuffer-only framing of `acquire`
> below is superseded by the surface model.

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

## Amendment (#195/#205, 2026-07-01): `acquire` generalized to all surfaces

The framebuffer-only `acquire_framebuffer()` / `present_framebuffer()` framing
above is superseded by the **runtime-managed surface model** (#195 design,
implemented in **#205** — PR-B). The API-based principle is unchanged; the
generalization is:

- **The runtime owns every surface buffer**, not just the framebuffer. A surface
  is a runtime-managed palette-index buffer referenced by an opaque handle
  (`blyt_surface_h`). The framebuffer is simply the built-in `screen` surface
  (`BLYT_SCREEN`); `present` is the screen's release.
- **Two access tiers.** *Tier-1* serviced ops (`blyt_surface_clear/pixel/
  rect_fill/line/blit(dst, …)`) rasterize host-side — one API crossing each, no
  lock, the default. *Tier-2* is the generalized `acquire`/`release`
  (`blyt_surface_acquire(s, &lock)` → `blyt_surface_release(&lock)`): the runtime
  materializes the surface's buffer where the caller can read/write it — a direct
  canonical pointer where the address space is shared (host-Lua, native bare
  metal), a copy-in/copy-out where it is not (emulated, the hybrid native half) —
  and `release` flushes it back. This is the same raw-write-speed access this ADR
  promised for the framebuffer, now available on any surface.
- **Off-screen surfaces are draw-scoped** (blank-create in `draw()`, auto-reaped
  at the frame boundary) and **count against this ADR's unified 16 MB budget**
  (the #158 amendment above) — they are non-evictable for the frame, charged
  against the same `guest_heap_used + non_evictable_footprint + incoming ≤ 16 MB`
  predicate. They are not tracked state and not serialized (ADR-0076/0122).
- The old fixed-region raw `acquire_framebuffer` (Spike X #188) still exists but
  is superseded by the tier-2 lock; the surface model is the API going forward.

Handle encoding is the console-wide tagged `u32` (ADR-0096/ADR-0134); the
draw()-only access rule is ADR-0076. Lua is tier-1 only (the per-pixel lock is
C/Rust + native).

## Amendment (#231, 2026-07-09): 64-bit native host-Lua re-establishes 32-bit parity via an accounting seam

The "Invariant: the host-Lua fast path must stay 32-bit" section above closes
with: *if a 64-bit desktop host-Lua path is ever added it must not be on the
determinism-bearing path without re-establishing this parity.* ADR-0136 adds
exactly that path — pure-Lua carts run on the native fork Lua VM on the desktop
players (64-bit) and, in the end state, on rv32 hardware. This amendment records
how #231 re-establishes the parity, because the 64-bit host breaks the
"same 32-bit pointer width" assumption the original invariant relied on.

- **The single-arena structural guarantee still holds** for word-size-agnostic
  allocations (the arena itself, `TValue`/`Node`/byte-array payloads — 16 B / same
  on both ABIs). What diverges on a 64-bit host is only the **pointer-bearing
  object headers** (`TString`, `Table`, `Proto`, closures, `lua_State`) and pure
  pointer arrays.

- **The #231 accounting seam** (`BLYT_HOSTLUA_HEAP_SEAM`) makes the 64-bit host
  count those at their **rv32 sizes**: the fork's `luaM` layer threads the
  rv32-equivalent request size (from a `sizeof` table generated by the real rv32
  toolchain, so it cannot rot on a Lua bump) to the runner, which drives a shadow
  arena at the 32-bit-canonical sizes. `LUAL_BUFFERSIZE` is pinned so the auxlib
  string-buffer box threshold matches (its default scales with `sizeof(void*)`).
  Direction: the 32-bit sizes are the **cart identity** (ilp32d, i32, 16 MB); the
  64-bit desktop is the outlier normalising down to them — the same shape as the
  ADR-0135 FP seam presenting SoftFloat values on native `f64`. rv32 hardware and
  wasm32 report their real (canonical) sizes; the seam is confined to the 64-bit
  desktop.

- **`guest_heap_used` is cart-attributable**, not raw arena occupancy: the per-leg
  **runtime-scaffolding baseline** (VM + stdlibs + API + cart bytecode, captured
  after a settling GC) and **VM execution scratch** (a thread's data stack +
  `CallInfo`) are excluded (`BLYT_HOSTLUA_HEAP_ACCT`), so the figure does not
  depend on call depth or on how the runtime drives the cart.

- **`guest_heap_used` is byte-exact on every leg** (see the ADR-0029 #231/#267
  amendment). The fail-outcome at the 16 MB cap coincides across legs, and so
  does the exact count: every host-Lua leg drives the cart through one shared
  coroutine driver and builds its scaffolding through one shared registration
  (#242/#267), so the allocation sequence is identical by construction; and the
  seam pins the host-width constants that reach that sequence — the auxlib buffer
  threshold and the GC's pacing constants — to the 32-bit canonical, not just the
  object sizes (#267). The budget remains the thing to *branch* on: the count is
  the same everywhere, but it is not promised to survive a runtime upgrade.

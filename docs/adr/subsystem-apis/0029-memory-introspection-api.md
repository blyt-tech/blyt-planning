# ADR-0029: Memory introspection API

## Status
Accepted

## Context

Carts operating near the 16 MB working memory budget need visibility into
their actual usage to make informed decisions about releasing resources. The
runtime has this information; the question is whether to expose it to carts.
Without it, authors must estimate and guess; with it, they can query and react.

## Decision

The runtime exposes a memory introspection API:

```lua
local mem = blyt32.mem.stats()
-- mem.resource_cache_used  -- bytes of decompressed resources in cache
-- mem.cart_allocations     -- bytes of cart-allocated memory (state buffers, etc.)
-- mem.total_used           -- total cart-visible memory in use
-- mem.budget_cap           -- 16 MB working memory cap
-- mem.resources_loaded     -- table of currently-loaded resources with sizes
```

This is exposed to carts so they can make informed decisions about releasing
resources under pressure. It is also displayed prominently in the dev-mode
debug overlay so authors see memory usage during development.

### Amendment (2026-06-28, issue #159): deterministic-vs-advisory tiers

The fields are **tiered**, and the API documents this loudly. Mixing the tiers
— branching deterministic game logic on an advisory field — is a usage bug that
desyncs netplay/replay across platforms.

- **Deterministic** — bit-identical across every peer/platform, safe to branch
  game logic on:
  - `budget_cap` (always 16 MB).
  - the **outcome** of an allocation: a `load`/alloc returning `nil`/`NULL` at
    the cap. This follows from the unified-budget predicate (ADR-0008, #158),
    which depends only on the non-evictable footprint, not on LRU history.
- **Advisory** — history-dependent (LRU/eviction order differs across
  platforms), must **not** feed deterministic game state; a *tuning* signal
  only (“should I proactively release something”, itself a memory decision):
  - `resource_cache_used`, `total_used`.
  - `resources_loaded` residency (which resources are resident, and the order).

**Mechanism (#159).** The scalar totals (`cart_allocations`,
`resource_cache_used`, `total_used`, `budget_cap`) are **published running sums**
in the guest-readable unified accounting block (#158) — the cart reads them with
no host round-trip, matching the “no traversal at call time” note above. Only the
variable-length `resources_loaded` list needs the host (it is host-owned table
data) and is resolved **on demand**, never published per mutation. The dev
overlay is a separate dev-mode aid (a display surface over these numbers), not a
determinism surface — because the advisory figures it shows can differ per
platform.

### Amendment (2026-07-09, issue #231): `cart_allocations` is a canonical diagnostic; the budget is the contract

The #159 amendment tiered `budget_cap` + the allocation **outcome** as
deterministic and left `cart_allocations` (guest_heap_used) untiered. #231
(native host-Lua fast path, ADR-0136) makes that placement explicit and
strengthens the number, while keeping the **contract on the budget, not the byte
count**.

- **The contract is the budget.** What a cart may branch game logic on is the
  16 MB cap and the *outcome* of an allocation against it (does malloc /
  `blyt_resource_load` succeed or return `nil`/`NULL`). That outcome is
  deterministic across every peer/platform — a cart can never exceed the budget,
  and the fail-point coincides across legs for any realistic workload.

- **`cart_allocations` is deterministic (#267).** The cart heap runs through one
  arena (ADR-0008/#158), counted at the **32-bit-canonical** object sizes on
  every leg: on a 64-bit host the #231/#267 accounting seam
  (`BLYT_HOSTLUA_HEAP_SEAM` / `BLYT_HOSTLUA_HEAP_ACCT`) models Lua object sizes
  down to their rv32 widths, so wider host pointers do not inflate the count. It
  is byte-identical on every leg — the native ones (64-bit desktop and rv32
  hardware) and wasm32 — and therefore safe to branch on. It remains a poor thing
  to branch on: it is stable across peers and replays at a given cart+runtime
  version, but a runtime upgrade or Lua bump legitimately moves it, so a cart that
  branches on the exact byte count pins itself to an implementation detail that is
  promised to be *the same everywhere*, not *the same forever*. Branch on the fail
  outcome to express "am I out of memory".

- **What is excluded from `cart_allocations` (why it is "cart-attributable").**
  Two classes of allocation are runtime overhead, not cart heap, and are excluded
  so the figure is comparable across legs: (a) the per-leg **runtime-scaffolding
  baseline** (the VM, stdlibs, blyt/blyt32 API, loaded cart bytecode), captured
  after a settling collection and subtracted; (b) **VM execution scratch** — a
  Lua thread's data stack and `CallInfo` — excluded at allocation time, so the
  count does not depend on call depth or on how the runtime drives the cart.

- **One execution model, one setup (#242/#267).** Every host-Lua leg drives the
  cart through the same shared coroutine driver, and builds its blyt/blyt32
  scaffolding through the same shared registration, so the allocation *sequence*
  is identical by construction rather than by two implementations agreeing. (The
  coroutine is forced by the single-threaded browser/Node event loop — a debugger
  breakpoint must `lua_yield` out; a native player instead pauses by blocking a
  thread. Sharing it does not force native to yield for debugging: native keeps
  thread-blocking pause, and the coroutine is only the shared execution container.)

- **Pin host-width constants, not just object sizes (#267).** Modelling object
  sizes down to the rv32 canonical is necessary but not sufficient.
  `cart_allocations` is read from a *first-fit* arena that charges a recycled
  block whole when the remainder cannot be split, so the count depends on
  allocation and free ORDER, not on sizes alone. Any host-width constant that can
  reach that order is therefore part of this contract and must be pinned to the
  32-bit canonical. Three did in practice: the auxlib buffer threshold
  (`LUAL_BUFFERSIZE`, `16 * sizeof(void*) * sizeof(lua_Number)`), the GC step size
  (`200 * sizeof(Table)`), and the GC's work-per-step divisor (`sizeof(void*)`).
  The latter two are pure *pacing*: left on host widths they make the 64-bit VM
  sweep at different points than the 32-bit legs, changing which blocks get
  recycled and so the count — a divergence with no per-object explanation at all,
  which is what made it expensive to find. The rule for any future fork bump: *a
  host-width constant reachable from a cart-visible number is part of this
  contract, even when it is not a size.*

**Deferred to v2:** memory pressure callbacks
(`on_memory_pressure(severity)`) — notifying the cart when budget usage
crosses warning/critical thresholds so it can proactively release non-
essential resources. Authors can poll `blyt32.mem.stats()` in their update
loop in v1.

## Consequences

- Authors can query usage at any time and make release decisions
  programmatically.
- Dev-mode overlay makes memory usage continuously visible during development,
  reducing the chance of shipping carts that exceed the budget on some
  platforms.
- No polling overhead concern: `blyt32.mem.stats()` is a fast introspection
  call (no allocation, no traversal at call time if the runtime maintains
  running totals).

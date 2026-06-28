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

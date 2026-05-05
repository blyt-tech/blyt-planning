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

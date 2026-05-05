# ADR-0027: Explicit resource release API

## Status
Accepted

## Context

Long-running carts with phase-specific resources (cutscenes, level intros,
boss fights with unique assets) can accumulate decompressed resource cache
beyond the 16 MB working memory budget if resources are never freed. The
runtime needs a way to reclaim this memory. Two approaches: automatic GC-only
eviction, or explicit release combined with GC as a fallback.

## Decision

**Carts can explicitly release loaded resources:**

```lua
-- R_HERO_SPRITES: packer-generated compile-time constant (ADR-0059)
local hero_sprites = blyt32.resource.load(R_HERO_SPRITES)
-- ... use it ...
blyt32.resource.release(hero_sprites)
-- handle is now invalid; re-load returns a new handle
```

- `load` is idempotent: loading an already-cached resource returns quickly
  without re-decompressing.
- `release` is advisory: tells the runtime "cart no longer needs this."
  The runtime may or may not immediately free the decompressed bytes —
  keeping recently released resources cached enables fast re-load.
- Actual eviction is the runtime's decision. v1: evict under memory pressure
  only. v2: consider LRU with explicit hints.

**Lua GC integration:** resource handles are userdata with `__gc` metamethods
that call `release` on collection. Carts that don't manage lifecycles
explicitly still get reasonable behavior; explicit release is recommended for
large resources like cutscene art.

**Handle stability across save/restore:** resource handles encode logical
identity (resource name + load generation), not physical address. Save state
preserves which resources were loaded; restore re-establishes the physical
mapping. Handles survive save/load, hot reload, and cross-platform transport.

**Persistent resources** (ADR-0028) are declared in the manifest and are
never evicted; `release` on a persistent resource is a no-op.

## Consequences

- Authors with phase-specific large resources can proactively free memory at
  scene transitions, preventing budget exhaustion.
- The API is consistent regardless of whether a resource is compressed —
  `release` on an uncompressed mmap'd resource is a no-op; the cart doesn't
  need to know which case applies.
- v1 ships without automatic LRU eviction; authors relying on the runtime to
  manage cache size automatically may hit budget limits. The memory
  introspection API (ADR-0029) helps them detect and respond to this.

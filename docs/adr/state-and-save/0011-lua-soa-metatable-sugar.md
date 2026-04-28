# ADR-0011: Lua state buffer ergonomics — packer-generated SOA and row proxy access

## Status
Accepted

## Context

Typed state buffers (ADR-0010) use SOA (Structure of Arrays) storage
internally for cache efficiency. Lua authors need to access them ergonomically.
The options originally considered were:

1. Raw index-based access (`enemies_x[i]`, `enemies_y[i]`) — verbose.
2. **SOA with field-named access** (`enemies.x[i]`, `enemies.y[i]`) via
   metatables — moderate ergonomics, ~300–400 lines of generic implementation.
3. Row proxies (`enemies[i].x`) via `ref<T>` machinery — most ergonomic,
   ~1500–2000 lines of generic implementation including generation counters
   to catch stale references.

Option 3 was deferred in the original decision due to implementation cost.
That cost estimate assumed a *generic* proxy system. The packer code generation
infrastructure (ADR-0059) changes the economics: the packer already generates
field handle constants for every buffer schema; extending it to also emit
type-specific metatables is a small step. A generated proxy for a specific
type is ~20–30 lines of straightforward Lua with field handles hardcoded —
no generic machinery, no generation counters, no `ref<T>`.

The stale reference concern also does not apply: manifest-declared buffers
(ADR-0009) are fixed-size and allocated at cart load. They never move or
resize during gameplay, so a proxy holding a buffer handle and an index is
always valid for the lifetime it is used.

## Decision

**The packer generates a Lua module for each buffer schema providing both
SOA field access and row proxy access.** Both forms are available; authors
choose based on context.

```lua
-- Generated module excerpt for Enemy schema (cart_state.lua)
-- Both access styles work on the same underlying SOA storage.

-- SOA: iterate a single field — cache-friendly, preferred for hot loops
for i = 1, enemies.count do
  enemies.x[i] = enemies.x[i] + enemies.vx[i] * dt
end

-- Row proxy: access multiple fields on one entity — ergonomic for
-- initialisation, one-off queries, and readable non-hot code
local e = enemies[5]
if e.active and e.hp <= 0 then
  e.active = false
  e.death_frame = console.time.frame()
end

-- Caching the proxy avoids repeated table allocation in a loop:
for i = 1, enemies.count do
  local e = enemies[i]
  if e.active then
    e.x = e.x + e.vx * dt
    e.y = e.y + e.vy * dt
  end
end
```

**Row proxy allocation.** Since buffer counts are manifest-declared and fixed
at cart load, the generated module pre-allocates a proxy pool at
initialisation — one proxy table per slot — and `enemies[i]` is a pure pool
lookup with zero allocation:

```lua
-- Generated initialisation (runs once at cart load)
local _pool = {}
for i = 1, enemies.count do
  _pool[i] = setmetatable({_i = i}, EnemyProxy)
end
-- enemies[i] thereafter: return _pool[i]
```

The proxy never caches field values — all reads and writes go live to the
SOA buffer through the metatable — so there is no invalidation concern and
no per-frame clearing. The pool is valid for the entire cart session.

Memory cost is one Lua table per slot. For entity-scale buffers (tens to
hundreds) this is negligible (~64 bytes × 128 enemies ≈ 8 KB). For very
large buffers (thousands of particles) the pool overhead is worth noting;
at that scale the SOA form is typically preferable for cache reasons anyway.

**Generation.** The buffer schema Lua module is generated alongside the
field handle constants (ADR-0059) as part of the packer's normal output.
Each type produces:
- A field array table (`enemies.x`, `enemies.y`, etc.) backed by the SOA
  buffer, implementing `__index` and `__newindex` per field via the compiled
  handle constants.
- A row proxy metatable with `__index` and `__newindex` dispatching to the
  same handle constants.
- A `count` property on the buffer.

All handle constants in the generated code are integer literals, not
string lookups — zero overhead beyond the API call itself.

## Consequences

- Authors get both ergonomic row access and cache-friendly SOA access from
  the same generated API, with no manual choice between them at design time.
- The implementation cost is proportional to the number of declared types,
  not a fixed infrastructure investment — small schemas have small output.
- No generic proxy machinery in the runtime; the packer owns the generation.
- The SOA layout is preserved internally; the row proxy is a purely syntactic
  layer over the same field-indexed storage.
- Row proxy access (`enemies[i].x`) is zero-allocation after cart load;
  the pre-allocated pool means no GC pressure from proxy usage in any loop.
- For very large buffers where pool memory is a concern, the SOA form
  (`enemies.x[i]`) remains available and is cache-friendlier regardless.
